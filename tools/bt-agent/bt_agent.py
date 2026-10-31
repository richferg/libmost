#!/usr/bin/env python3
"""bt-agent: put a Bluetooth phone behind mostd's emulated SatRadio (FBlock 0x44).

Audio does not pass through here. The phone's A2DP stream is decoded by BlueZ +
bluez-alsa into the PiMost ALSA card (I2S, clocked by the OS8104), which is the
MOST source port mostd routes onto the synchronous channels. This agent carries
everything *around* the audio:

  BlueZ (D-Bus)                                   mostd (unix socket, protocol/control.h)
  ---------------------------------------------   ----------------------------------------
  paired devices (Device1.Paired/Alias)       ->  stations <n> / station ... / end
  connected device                            ->  current <n>, esn <alias>, [signal 0|1]
  MediaPlayer1.Track (Title/Artist/Album)     ->  text f0|f1|f2
  MediaPlayer1.Next/Previous                  <-  seek next|prev <list> <cat>
  Device1.Connect                             <-  tune <n>
  MediaPlayer1.Play / Pause                   <-  source on|off  (ASK built / tore down the source)

Pairing: registers a NoInputNoOutput agent that accepts everything, and makes
the adapter pairable+discoverable for --discoverable seconds at start-up (0 =
leave the adapter alone). Pair from the phone.

Needs python3-dbus and python3-gi (GLib). Tested against mostd in the car on
2026-10-31.
"""
import argparse
import logging
import os
import socket
import sys
import time
import unicodedata

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

BLUEZ = "org.bluez"
OM = "org.freedesktop.DBus.ObjectManager"
PROPS = "org.freedesktop.DBus.Properties"
ADAPTER = "org.bluez.Adapter1"
DEVICE = "org.bluez.Device1"
PLAYER = "org.bluez.MediaPlayer1"
AGENT_MGR = "org.bluez.AgentManager1"
AGENT_PATH = "/org/mostd/bt_agent"
A2DP_SOURCE_UUID = "0000110a-0000-1000-8000-00805f9b34fb"

SHORT_LEN, LONG_LEN, TEXT_LEN, ESN_LEN = 8, 20, 36, 12
CATEGORY_ID, CATEGORY_SHORT, CATEGORY_LONG = 1, "Phones", "Paired phones"

log = logging.getLogger("bt-agent")


def latin1(text, limit):
    """What the MMI can show: latin-1, no '|' (our separator), at most `limit` bytes."""
    if text is None:
        return ""
    text = unicodedata.normalize("NFKD", str(text))
    out = []
    for ch in text:
        if ch == "|":
            ch = "/"
        if ord(ch) < 0x20:
            continue
        try:
            ch.encode("latin-1")
        except UnicodeEncodeError:
            continue
        out.append(ch)
    return "".join(out).strip()[:limit].rstrip()


class Agent(dbus.service.Object):
    """BlueZ pairing agent that accepts every request (the Pi has no UI)."""

    @dbus.service.method("org.bluez.Agent1", in_signature="", out_signature="")
    def Release(self):
        log.info("agent released")

    @dbus.service.method("org.bluez.Agent1", in_signature="os", out_signature="")
    def AuthorizeService(self, device, uuid):
        log.info("authorize %s %s", device, uuid)

    @dbus.service.method("org.bluez.Agent1", in_signature="o", out_signature="s")
    def RequestPinCode(self, device):
        return "0000"

    @dbus.service.method("org.bluez.Agent1", in_signature="o", out_signature="u")
    def RequestPasskey(self, device):
        return dbus.UInt32(0)

    @dbus.service.method("org.bluez.Agent1", in_signature="ouq", out_signature="")
    def DisplayPasskey(self, device, passkey, entered):
        log.info("passkey %06d for %s", passkey, device)

    @dbus.service.method("org.bluez.Agent1", in_signature="os", out_signature="")
    def DisplayPinCode(self, device, pincode):
        log.info("pin %s for %s", pincode, device)

    @dbus.service.method("org.bluez.Agent1", in_signature="ou", out_signature="")
    def RequestConfirmation(self, device, passkey):
        log.info("confirm %06d for %s: yes", passkey, device)

    @dbus.service.method("org.bluez.Agent1", in_signature="o", out_signature="")
    def RequestAuthorization(self, device):
        log.info("authorize pairing %s: yes", device)

    @dbus.service.method("org.bluez.Agent1", in_signature="", out_signature="")
    def Cancel(self):
        log.info("agent request cancelled")


class MostdLink:
    """Line protocol to mostd's control socket; reconnects on its own."""

    def __init__(self, path, on_event):
        self.path = path
        self.on_event = on_event
        self.sock = None
        self.buf = b""
        self.watch = None
        self.on_connect = None
        GLib.timeout_add(1000, self._try_connect)

    def connected(self):
        return self.sock is not None

    def _try_connect(self):
        if self.sock is not None:
            return True
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.connect(self.path)
        except OSError:
            s.close()
            return True
        s.setblocking(False)
        self.sock = s
        self.buf = b""
        self.watch = GLib.io_add_watch(s.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR,
                                       self._readable)
        log.info("mostd: connected to %s", self.path)
        if self.on_connect:
            self.on_connect()
        return True

    def _drop(self):
        if self.watch:
            GLib.source_remove(self.watch)
            self.watch = None
        if self.sock:
            self.sock.close()
            self.sock = None
        log.warning("mostd: connection lost; retrying")

    def _readable(self, fd, cond):
        if cond & (GLib.IO_HUP | GLib.IO_ERR):
            self._drop()
            return False
        try:
            data = self.sock.recv(4096)
        except BlockingIOError:
            return True
        except OSError:
            self._drop()
            return False
        if not data:
            self._drop()
            return False
        self.buf += data
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            line = line.decode("latin-1").strip()
            if line.startswith(("ok ", "err ")) or line == "pong":
                (log.warning if line.startswith("err ") else log.debug)("mostd: %s", line)
            elif line:
                self.on_event(line.split())
        return True

    def send(self, line):
        if self.sock is None:
            return False
        try:
            self.sock.sendall((line + "\n").encode("latin-1", "replace"))
            log.debug("-> mostd: %s", line)
            return True
        except OSError:
            self._drop()
            return False


class BtAgent:
    def __init__(self, args):
        self.args = args
        self.bus = dbus.SystemBus()
        self.om = dbus.Interface(self.bus.get_object(BLUEZ, "/"), OM)
        self.adapter_path = None
        self.devices = {}        # path -> dict of Device1 props we care about
        self.players = {}        # path -> dict(Track, Status, device path)
        self.stations = []       # device paths in station order (1-based for mostd)
        self.current = None      # device path that is playing / connected
        self.last_text = None
        self.link = MostdLink(args.socket, self.on_mostd_event)
        self.link.on_connect = self.resync
        self.agent = Agent(self.bus, AGENT_PATH)

        self.bus.add_signal_receiver(self.on_interfaces_added, dbus_interface=OM,
                                     signal_name="InterfacesAdded")
        self.bus.add_signal_receiver(self.on_interfaces_removed, dbus_interface=OM,
                                     signal_name="InterfacesRemoved")
        self.bus.add_signal_receiver(self.on_properties_changed, dbus_interface=PROPS,
                                     signal_name="PropertiesChanged", path_keyword="path")
        self.scan_objects()
        self.register_agent()
        if args.discoverable:
            self.make_discoverable(args.discoverable)

    # ---- BlueZ object model ------------------------------------------------
    def scan_objects(self):
        for path, ifaces in self.om.GetManagedObjects().items():
            self.on_interfaces_added(path, ifaces)

    def on_interfaces_added(self, path, ifaces):
        path = str(path)
        if ADAPTER in ifaces and self.adapter_path is None:
            self.adapter_path = path
            log.info("adapter %s", path)
        if DEVICE in ifaces:
            self.devices[path] = dict(ifaces[DEVICE])
            self.rebuild_stations()
            self.update_current()
        if PLAYER in ifaces:
            self.players[path] = dict(ifaces[PLAYER])
            self.update_text()

    def on_interfaces_removed(self, path, ifaces):
        path = str(path)
        if DEVICE in ifaces and path in self.devices:
            del self.devices[path]
            self.rebuild_stations()
            self.update_current()
        if PLAYER in ifaces and path in self.players:
            del self.players[path]
            self.update_text()

    def on_properties_changed(self, iface, changed, invalidated, path=None):
        path = str(path)
        if iface == DEVICE and path in self.devices:
            self.devices[path].update(changed)
            if changed.get("Paired"):
                self.trust(path)
            if "Paired" in changed or "Alias" in changed or "Name" in changed:
                self.rebuild_stations()
            if "Connected" in changed or "Paired" in changed:
                self.update_current()
        elif iface == PLAYER:
            self.players.setdefault(path, {}).update(changed)
            if "Track" in changed or "Status" in changed:
                self.update_text()

    def trust(self, path):
        """So the phone can reconnect on its own without asking the agent."""
        try:
            dbus.Interface(self.bus.get_object(BLUEZ, path), PROPS).Set(
                DEVICE, "Trusted", dbus.Boolean(True))
            log.info("trusted %s", self.alias(path))
        except dbus.DBusException as e:
            log.warning("trust %s failed: %s", path, e)

    def register_agent(self):
        mgr = dbus.Interface(self.bus.get_object(BLUEZ, "/org/bluez"), AGENT_MGR)
        mgr.RegisterAgent(AGENT_PATH, "NoInputNoOutput")
        mgr.RequestDefaultAgent(AGENT_PATH)
        log.info("pairing agent registered (NoInputNoOutput)")

    def adapter_props(self):
        if not self.adapter_path:
            return None
        return dbus.Interface(self.bus.get_object(BLUEZ, self.adapter_path), PROPS)

    def make_discoverable(self, seconds):
        p = self.adapter_props()
        if p is None:
            log.warning("no adapter; cannot make discoverable")
            return
        p.Set(ADAPTER, "Powered", dbus.Boolean(True))
        p.Set(ADAPTER, "Pairable", dbus.Boolean(True))
        p.Set(ADAPTER, "DiscoverableTimeout", dbus.UInt32(seconds))
        p.Set(ADAPTER, "Discoverable", dbus.Boolean(True))
        log.info("adapter pairable + discoverable for %d s", seconds)

    # ---- model -> mostd ------------------------------------------------------
    def paired_devices(self):
        return sorted((p for p, d in self.devices.items() if d.get("Paired")),
                      key=lambda p: str(self.devices[p].get("Address", p)))

    def alias(self, path):
        d = self.devices.get(path, {})
        return str(d.get("Alias") or d.get("Name") or d.get("Address") or "Phone")

    def rebuild_stations(self):
        stations = self.paired_devices()
        if stations == self.stations and self.link.connected():
            return
        self.stations = stations
        self.send_stations()

    def send_stations(self):
        if not self.link.connected():
            return
        rows = []
        seen = set()
        for path in self.stations:
            short = latin1(self.alias(path), SHORT_LEN) or "Phone"
            # keep short names distinct; the MMI list shows only these
            base, n = short, 2
            while short in seen:
                short = (base[:SHORT_LEN - 1] + str(n))[:SHORT_LEN]
                n += 1
            seen.add(short)
            long_ = latin1(self.alias(path), LONG_LEN) or short
            rows.append("station %s|%s|%d" % (short, long_, CATEGORY_ID))
        if not rows:
            rows = ["station BT Audio|Bluetooth Audio|%d" % CATEGORY_ID]
        self.link.send("stations %d" % len(rows))
        self.link.send("category %d|%s|%s" % (CATEGORY_ID, CATEGORY_SHORT, CATEGORY_LONG))
        for r in rows:
            self.link.send(r)
        self.link.send("end")
        self.update_current(force=True)

    def connected_device(self):
        for path in self.stations:
            if self.devices.get(path, {}).get("Connected"):
                return path
        return None

    def update_current(self, force=False):
        cur = self.connected_device()
        if cur == self.current and not force:
            return
        self.current = cur
        if not self.link.connected():
            return
        if cur in self.stations:
            self.link.send("current %d" % (self.stations.index(cur) + 1))
            self.link.send("esn %s" % (latin1(self.alias(cur), ESN_LEN) or "Phone"))
        if self.args.signal:
            self.link.send("signal %d" % (1 if cur else 0))
        self.update_text(force=True)

    def active_player(self):
        """The MediaPlayer1 under the connected device, if the phone exposes one."""
        if not self.current:
            return None
        for path in sorted(self.players):
            if path.startswith(self.current + "/"):
                return path
        return None

    def update_text(self, force=False):
        player = self.active_player()
        track = self.players.get(player, {}).get("Track", {}) if player else {}
        text = (latin1(track.get("Title"), TEXT_LEN),
                latin1(track.get("Artist"), TEXT_LEN),
                latin1(track.get("Album"), TEXT_LEN))
        if not any(text):
            text = (latin1(self.alias(self.current), TEXT_LEN) if self.current else "", "", "")
        if text == self.last_text and not force:
            return
        self.last_text = text
        self.link.send("text %s|%s|%s" % text)

    def resync(self):
        """mostd (re)connected: it may have been restarted, so send everything."""
        self.send_stations()
        self.link.send("get")

    # ---- mostd -> phone ------------------------------------------------------
    def player_call(self, method):
        player = self.active_player()
        if not player:
            log.info("%s: no media player on the connected phone", method)
            return
        try:
            getattr(dbus.Interface(self.bus.get_object(BLUEZ, player), PLAYER), method)()
            log.info("player %s", method)
        except dbus.DBusException as e:
            log.warning("player %s failed: %s", method, e)

    def connect_station(self, n):
        if n < 1 or n > len(self.stations):
            log.warning("tune %d: no such station", n)
            return
        target = self.stations[n - 1]
        for path in self.stations:
            d = self.devices.get(path, {})
            if path != target and d.get("Connected") and self.args.exclusive:
                try:
                    dbus.Interface(self.bus.get_object(BLUEZ, path), DEVICE).Disconnect()
                except dbus.DBusException as e:
                    log.warning("disconnect %s failed: %s", path, e)
        if self.devices.get(target, {}).get("Connected"):
            return
        log.info("tune %d -> connect %s", n, self.alias(target))
        try:
            dbus.Interface(self.bus.get_object(BLUEZ, target), DEVICE).Connect()
        except dbus.DBusException as e:
            log.warning("connect %s failed: %s", target, e)

    def on_mostd_event(self, words):
        log.info("<- mostd: %s", " ".join(words))
        if not words:
            return
        if words[0] == "seek" and len(words) >= 2:
            self.player_call("Next" if words[1] == "next" else "Previous")
        elif words[0] == "tune" and len(words) >= 2:
            self.connect_station(int(words[1]))
        elif words[0] == "source" and len(words) >= 2:
            if words[1] == "on":
                self.player_call("Play")
            elif self.args.pause_on_source_off:
                self.player_call("Pause")
        elif words[0] == "current" and len(words) >= 2:
            pass            # state report; the phone side is authoritative here
        # ui open/closed, preset store: informational


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--socket", default=os.environ.get("MOSTD_CTL_SOCKET", "/run/mostd/sdars.sock"))
    ap.add_argument("--discoverable", type=int, default=120,
                    help="seconds pairable+discoverable after start (0 = leave adapter alone)")
    ap.add_argument("--signal", action="store_true",
                    help="map 'phone connected' onto the 0x400 reception flag (unverified on the MMI)")
    ap.add_argument("--exclusive", action="store_true",
                    help="disconnect other phones when the MMI selects one")
    ap.add_argument("--no-pause", dest="pause_on_source_off", action="store_false",
                    help="do not pause the phone when the car drops the source")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="bt-agent: %(message)s", stream=sys.stderr)

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    BtAgent(args)
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
