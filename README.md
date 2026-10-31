# libmost

MOST25 on a Raspberry Pi 5: a kernel driver for the OS8104 transceiver, a small
userspace library, and `mostd`, a daemon that puts an emulated SatRadio FBlock
on a BMW E65 ring so a Bluetooth phone can play through the car's audio system
and be controlled from the head unit.

Runs on a [PiMost](https://github.com/rhysmorgan134/SocketMost) board, Rhys
Morgan's Raspberry Pi HAT for the OS8104.

- `driver/`: `os8104` kernel module, device-tree overlays, load/run helper
- `libmost/`: userspace library for `/dev/mostX` (events, control frames,
  registers)
- `mostd/`: the daemon. `mostd/README.md` describes what it does on the ring.
- `tools/bt-agent/`: the Bluetooth bridge that feeds `mostd`. See its README.
- `tools/examples/`: bus probes and one-off utilities
- `package.sh`: builds the `libmost-runtime` Debian package

Supported target: Raspberry Pi 5 only. Other hardware is untested.

## Build

From the project root:

```sh
make                 # driver (needs kernel headers), libmost, mostd
make tools           # tools/examples, staged in dist/tools/bin/
make -C mostd test   # unit tests, no hardware needed
make clean
```

Outputs:

- `dist/libmost/libmost.a`, `dist/libmost/libmost.so`
- `dist/mostd/bin/mostd`
- `dist/tools/bin/*`
- `driver/src/os8104.ko`, `driver/overlay/*.dtbo`

Driver on its own:

```sh
make -C driver                 # module + overlays
make -C driver run             # load overlay + module, dmesg to driver/dmesg.log
./driver/run.sh --wait         # same, keep loaded until Ctrl-C
sudo make -C driver install    # module to /lib/modules/$(uname -r)/extra, both dtbo to /boot/overlays
make -C driver debug           # module with dev_dbg() enabled (KCFLAGS=-DDEBUG)
```

Prerequisites: kernel headers for the running kernel
(`/lib/modules/$(uname -r)/build`), `dtc`, `dtoverlay`, and `sudo` for module
and overlay operations.

## Install

### Package

```sh
./package.sh
```

builds everything and writes `dist/libmost-runtime_<VERSION>_<arch>.deb` for the
running kernel. The package installs:

| File | Path |
|---|---|
| `os8104.ko` | `/lib/modules/<kver>/extra/` |
| `os8104.dtbo`, `pimost.dtbo` | `/usr/lib/libmost/overlays/`, copied to `/boot/overlays/` by postinst |
| `libmost.so` | `/usr/lib/` |
| `mostd` | `/usr/sbin/mostd` + `mostd.service` |
| `bt_agent.py` | `/usr/sbin/mostd-bt-agent` + `mostd-bt-agent.service` |
| `bluealsa-aplay.service.d/pimost.conf` | `/etc/systemd/system/bluealsa-aplay.service.d/` |
| `wireplumber-no-bluez.conf` | `/etc/wireplumber/wireplumber.conf.d/90-mostd-no-bluez.conf` |

postinst runs `depmod`, `ldconfig` and `systemctl daemon-reload`. Services are
not enabled automatically:

```sh
sudo systemctl enable --now mostd
sudo systemctl enable --now mostd-bt-agent   # optional, see tools/bt-agent/README.md
```

### Manual

```sh
sudo make -C driver install
sudo install -m 0644 mostd/mostd.service /etc/systemd/system/mostd.service
sudo systemctl daemon-reload
sudo systemctl enable --now mostd.service
```

### Boot configuration

In `/boot/firmware/config.txt`:

```ini
dtparam=spi=on
dtparam=i2s=on
dtoverlay=os8104
dtoverlay=pimost
```

`pimost` (`driver/overlay/pimost.dts`) creates the ALSA card `PiMost` on the
OS8104's I2S port, which `bluealsa-aplay` plays into
(see `tools/bt-agent/bluealsa-aplay.service.d/pimost.conf`). A Pi set up before
the overlay was packaged may load the same card under another overlay name
(for example `piMost48KhzStereo`); either works, use one.

The `os8104` overlay can also be loaded at runtime:

```sh
sudo dtoverlay -d driver/overlay os8104
sudo dtoverlay -R os8104
```

## Running mostd

```sh
./dist/mostd/bin/mostd [-v|--debug] [--quiet] [--trace-file PATH]
```

`mostd` opens `/dev/most0` (fixed). Environment:

| Variable | Meaning |
|---|---|
| `MOSTD_LOG` | `error`, `info` (default) or `debug`; `0`/`1`/`2` also accepted |
| `MOSTD_TRACE_FILE` | append a structured CMT RX/TX trace to this file (off by default) |
| `MOSTD_CTL_SOCKET` | Unix socket for the Bluetooth agent (default `/run/mostd/sdars.sock`) |

The systemd unit waits up to 120 s for `/dev/most0`, runs `mostd` at the
default `info` level, and appends stdout and stderr to `/run/mostd.log` (not
the journal). `--debug` adds one line per transmit and per transmit
completion; enable it with a drop-in that overrides `ExecStart`. It sets
`MOSTD_CTL_SOCKET=/run/mostd/sdars.sock` and creates `/run/mostd`. It does not
set `MOSTD_TRACE_FILE`; add a drop-in if you want a persistent trace.

What `mostd` does once the ring is up, the SatRadio functions it serves, the
control-socket protocol and the diagnostic environment knobs are documented in
`mostd/README.md`.

## Tools

Built by `make tools` into `dist/tools/bin/`. Most take the device index as the
first argument (`0` for `/dev/most0`).

| Tool | Purpose |
|---|---|
| `most-send` | send one raw 21-byte control frame |
| `sniffer` | log and decode received control frames (appends to `./most-sniff.log`) |
| `segtest`, `segparse`, `parse` | offline segmentation / frame parsing (`tools/examples/frame.txt` is a sample input) |
| `os8104_ctl` | print link state and stats; note it also writes node 0x1234 / group 0x56 to the chip |
| `os8104_aint_probe` | fire one async packet via raw register ioctls and poll bPCTS, for watching AINT with `gpiomon` |
| `resource_alloc`, `deallocate`, `whose_channel` | synchronous channel allocation via the timing master |
| `audio_tx`, `audio_inject` | end-to-end "send our audio to the amplifier" bring-up |
| `amp_probe`, `source_probe` | interrogate a sink or source FBlock on the ring |

`driver/tools/most/fblock_*.h` and `most_error_enum.h` are FktID and error-code
name tables generated from the MOST 2.5 FBlock library, kept for reference.

## Testing

See `TESTING.md`.

## Versioning

Project version is in `VERSION` (SemVer: MAJOR for incompatible API/ABI/protocol
changes, MINOR for features, PATCH for fixes). Current: `0.5.0`.

The kernel module exports `MODULE_VERSION("0.4.1")` in
`driver/src/os8104_driver.c`. `libmost 0.4.x` pairs with `os8104 0.4.x`. If DKMS
is added later, use the same version string for `PACKAGE_VERSION`.
