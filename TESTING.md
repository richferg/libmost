# Testing

Three layers: unit tests that need no hardware, a Bluetooth bench test that
needs a phone but no car, and a checklist for a session in the car.

## Unit tests

```sh
make            # libmost first
make -C mostd test
```

Each test is a single C file that `#include`s the module under test and stubs
the transmit and INIC functions, so it exercises the real handlers and payload
builders against a captured TX log. A test prints nothing and exits 0 on
success; an `assert` failure aborts the run.

| Test | Covers |
|---|---|
| `tests/sdars_state_test.c` | the SatRadio responder: every subscribed property parses under a re-implementation of the MMI's receive grammar; array windows per peer, complete download, move modes, destroy; station select, step, presets, users, category drill-in; FktIDs list; source contract and events; station-table replacement, programme text, signal/ESN/current |
| `tests/control_test.c` | the control-socket line parser (every command, transaction errors) and a real socket round trip |
| `tests/tx_segment_test.c` | control frame segmentation: TelID/TelLen/counter for 0..289-byte payloads |
| `tests/diagnosis_test.c` | Diagnosis 0x06.54 FktIDs reply, request classification, job-name lookup, emitters |
| `tests/netblock_advertisement_test.c` | `FBlockIDs.Get` advertises `0x44.00` and `0x06.54` from our real instance |

To add a test: copy the stub preamble from the closest existing file, add a
compile-and-run stanza to the `test` target in `mostd/Makefile`.

## Bluetooth agent without the car

`tools/bt-agent/bench.sh` runs `bt_agent.py` against a stand-in for the
`mostd` socket so pairing, metadata, seek and source on/off can be checked
with a phone alone. See `tools/bt-agent/README.md` ("Off-car test") for the
steps and the expected lines.

## In the car

With the package installed and `mostd` and `mostd-bt-agent` enabled, follow
`/run/mostd.log`. Every line below is logged at the default level; run with
`--debug` to also see each transmit (`ctrl tx ...`). A good session shows, in
order:

```
link change seq=... old=0 new=2 ...
nodecfg: position=0x.., node addr 0x0110 (was 0x....), group 0x62
netblock: FBlockIDs.Get from 0x0100 -> advertise 0x44.00, 0x06.54
requesting CentralRegistry (advertised=1, tick=...)
registry: CentralRegistry decoded 60 entries ...
registry: SDARS 0x44 registered -> node=0x.. inst=0x00 flags=0x01
control: client connected
sdars: station list replaced: N stations, 1 categories, current=1
sdars: Notification.Set ctrl=0x01 dev=0x0102 fkts=[0x400]       (and more)
sdars: CreateArrayWindow peer=0x0103 handle=0x0C02 mother=0xC02 ... -> window=0xD10
sdars: served window 0xD10 to 0x0103: total=N absPos=0 size=20
```

When SAT is selected on the head unit:

```
sdars: 0x120 added user fblock=0x10 inst=0x00
sdars: SOURCE fn 0x101 op=0x2 source=1 allocated=0 ...
sdars: source bringup OK: granted 4 chan(s) label=0x.. free=0x.. chans=.. .. .. ..
sdars: SOURCE fn 0x103 op=0x2 ...
diagnosis: emitted DiagnosticsChannel(0x050) STATUS to 0x0102 (SAT-select replay)
```

Then phone audio should be audible. Steering-wheel seek shows
`sdars: 0x410 step dir=1 list=1 category=0x00` and the agent skips the track;
picking a station shows `sdars: tuned SRStation=0x.. requester=0x0102`.

Lines that mean trouble:

| Line | Meaning |
|---|---|
| `registry: SDARS 0x44 NOT present in CentralRegistry` | the NetworkMaster did not register us; check the FBlockIDs exchange above it |
| `sdars: source bringup: timing-master did not grant` | Resource Allocate failed; no audio |
| `sdars: allocate: source bringup failed` | same, reported to ASK |
| `ctrl tx fail seq=...` / `ctrl tx ... result=write_error` | transmit problems; check `hw error` lines and the driver log |
| `hw error seq=... bMSGS=... bXSR=...` | OS8104 error interrupt; repeated identical errors are rate-limited to one line per 5 s |
| `control: tx buffer full, dropped` | the agent is not reading its socket |
| `control: bind/listen ... (control plane disabled)` | socket path not creatable; the built-in station list is served instead |

To capture a full session for offline analysis, run with a trace file:

```sh
sudo MOSTD_TRACE_FILE=/var/tmp/mostd-trace.log /usr/sbin/mostd --debug
```

or add `Environment=MOSTD_TRACE_FILE=...` in a drop-in for `mostd.service`. The
trace has one `cmt` line per control message (RX and TX, segmented messages
reassembled) and one `evt` line per driver event, each with a wall-clock
timestamp and full payload hex.

Environment knobs for experiments (`SDARS_SEEK`, `SDARS_LIST_COUNT`) are
listed in `mostd/README.md`.

## Driver

`driver/run.sh` loads the overlay and module and writes kernel output to
`driver/dmesg.log` while it runs. `driver/tools/live_parse.py` colours the
register-access lines from `../dmesg.log`; those lines are `dev_dbg()` output
and only exist in a build made with `make -C driver debug`. It needs
`colorama` (`pip install colorama`).
