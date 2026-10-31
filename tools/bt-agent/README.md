# bt-agent: a Bluetooth phone as the car's SAT source

Companion to `mostd` (`protocol/control.c`). The protocol is documented in
`mostd/README.md` and `mostd/protocol/control.h`.

```
phone ──A2DP──> BlueZ ──> bluealsa-aplay ──> ALSA "PiMost" ──I2S──> OS8104 ──> MOST sync channels
      ──AVRCP─> BlueZ D-Bus <──> bt_agent.py <──unix socket──> mostd ──FBlock 0x44──> MMI
```

## Pieces

| File | Installs to | Role |
|---|---|---|
| `bt_agent.py` | `/usr/sbin/mostd-bt-agent` | BlueZ ⇄ mostd bridge (this directory) |
| `mostd-bt-agent.service` | `/lib/systemd/system/` | runs it after bluetooth + mostd |
| `bluealsa-aplay.service.d/pimost.conf` | `/etc/systemd/system/bluealsa-aplay.service.d/` | points bluez-alsa's player at the PiMost card |
| `wireplumber-no-bluez.conf` | `/etc/wireplumber/wireplumber.conf.d/90-mostd-no-bluez.conf` | keeps PipeWire off the A2DP endpoints |
| `bench.sh` | not installed | off-car test, see below |

Audio never touches the agent: `bluealsa-aplay` decodes A2DP straight into the PiMost
ALSA card, which is the OS8104 source port. The agent only moves metadata and commands.

## Pi setup

```sh
sudo apt install bluez bluez-alsa-utils libasound2-plugin-bluez python3-dbus python3-gi socat
sudo rfkill unblock bluetooth                       # the agent exits if the radio is blocked
sudo sed -i -E '0,/^#?Class *=.*/s//Class = 0x200414/' /etc/bluetooth/main.conf   # show as audio
sudo systemctl restart bluetooth bluealsa bluealsa-aplay
systemctl --user restart wireplumber                # picks up wireplumber-no-bluez.conf
sudo systemctl enable --now bluealsa bluealsa-aplay
sudo systemctl enable --now mostd-bt-agent
```

`bluetoothctl show` should list Audio Sink with `Class: 0x..0414`, and `bluealsa-cli
status` should list `A2DP-sink`.

Pair from the phone while the agent is discoverable (first 120 s after start, see
`--discoverable`). The agent accepts every pairing (NoInputNoOutput) and marks the
phone trusted for reconnects. Check `aplay -l` shows `PiMost`. The OS8104 is the I2S
master but keeps clocking with the ring down, so writing to the card succeeding
says nothing about audio reaching the car. Off-ring that clock runs at about
23.4 kHz whatever rate is requested (measured 2026-10-31), so a phone playing into
the card on the bench fills the journal with bluealsa `PCM overrun` lines. That is
expected there; `bench.sh capture` bypasses the card and is unaffected.

## Off-car test

No mostd or ring needed; `bench.sh` puts a `socat` listener where mostd's socket
would be, so the agent runs for real against BlueZ and the phone.

```sh
tools/bt-agent/bench.sh              # pair within 120 s, then type events
tools/bt-agent/bench.sh capture 10   # second terminal, phone playing
```

| Check | Expect |
|---|---|
| Pair and connect | `trusted <phone>`, then `stations`/`current`/`esn` lines |
| Change track on the phone | `-> mostd: text <title>\|<artist>\|<album>` |
| Type `seek next` / `seek prev` | phone skips |
| Type `source off` / `source on` | phone pauses / plays |
| Type `tune 1` with the phone off, then on | phone reconnects |
| `capture` while playing | WAV with a peak well above silence |

## What the car sees

- Station list = paired phones (short name ≤8, long ≤20, category "Phones"). No
  phone paired → one row "BT Audio / Bluetooth Audio".
- Current station = the connected phone; ESN dialog shows its name.
- Programme-info dialog (press the playing station in the channel list) shows
  Title / Artist / Album from AVRCP; updates live on track change.
- Steering-wheel seek → mostd emits `seek` → AVRCP Next/Previous.
- Picking another phone in the list → `tune n` → BlueZ `Connect()` (add
  `--exclusive` to drop the other phone first).
- ASK building / tearing down the source → `source on|off` → AVRCP Play / Pause.

## Options

`--signal` maps "no phone connected" onto the 0x400 reception flag (the MMI
then reports no reception); off by default until checked in the emulator.
`--no-pause` leaves the phone playing when the car drops the source.
`-v` logs every line to and from mostd.

## Status

Written 2026-10-27 against the control protocol in `mostd/protocol/control.h`.
Run against BlueZ 5.82 and bluez-alsa 4.3.1 with a stand-in socket and a Pixel 7
Pro on 2026-10-31: pairing, trust, reconnect, station list, track text, seek,
source on/off, tune and A2DP capture work. Since tested against mostd in the car:
working.
