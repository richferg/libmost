# mostd

`mostd` is the MOST control-plane daemon. On the car ring it is a node that
hosts an emulated **SatRadio FBlock 0x44** so the head unit (MMI) shows a SAT
source, lists "stations", and has the ConnectionMaster route our audio to the
amplifier. The audio itself comes from a Bluetooth phone via `tools/bt-agent`,
which talks to `mostd` over a Unix socket and turns the car's seek, tune and
source on/off requests into AVRCP commands.

To look like a complete node, `mostd` also hosts the mandatory **NetBlock**
(FBlock 0x01) and a logging-only **Diagnosis** server (0x06.54), the pair the
real SDAR65 module advertises.

Everything received on the control channel is logged with its full payload, so
the daemon doubles as the bus recorder for reverse-engineering work.

## What happens on the ring

1. **Start.** Open `/dev/most0`, open the control socket, wait for events.
2. **Link up.** `protocol/nodecfg.c` programs our logical node address to the
   fixed value `0x0110` and group `0x62`. The productive ECUs keep the addresses
   they were assigned at the original ring configuration, so `0x0100|position`
   collides with whichever ECU already owns it (seen live); `0x0110` is the
   first free slot above the 15-node ring. The NetworkMaster can still move us
   with `NodeAddress.Set`.
3. **NetworkMaster scan.** `protocol/netblock_server.c` answers NetBlock
   requests addressed to our instance (ring position, read live from bNPR) or
   to the wildcards 0x00/0xFF:
   - `0x000 FBlockIDs.Get` -> `{0x44,0x00, 0x06,0x54}`
   - `0x001 DeviceInfo.Get <id>` -> the selected identity string (Visteon
     SDARS strings from the real module); unknown ids get a parameter error
   - `0x002 NodePositionAddress.Get` -> `0x04NN`
   - `0x003 NodeAddress` Get, and Set/SetGet which reprograms the chip and acks
   - `0x004 GroupAddress` Get, and Set/SetGet (`{0x03, group}`)
   - `0xC00` (Becker proprietary "unavailability") -> "nothing unavailable"
4. **CentralRegistry.** Once we have answered `FBlockIDs.Get` (or after 2 s if
   the NetworkMaster never asks), `protocol/runtime.c` sends
   `NetworkMaster.CentralRegistry.Get` to `0x0100`. `protocol/registry.c`
   decodes the reply and logs whether `0x44` made it into the registry.
5. **MMI bring-up.** The two MMI contexts (`0x0102` service, `0x0103` list
   client) subscribe with `Notification.Set`, register as users on `0x120`,
   open an array window on the station list, and read the properties below.
   `protocol/sdars.c` serves all of it. On the `0x120` select,
   `protocol/diagnosis.c` replays the `DiagnosticsChannel` (0x050) Status the
   real module emits.
6. **Audio source build.** The ConnectionMaster (ASK, `0x0100`) runs the
   GeneralFBlock source contract against us: `SourceInfo 0x100`,
   `Allocate 0x101`, `SourceActivity 0x103`, `Deallocate 0x102`. On the first
   `Allocate` we do a Resource Allocate against the timing master, route our
   I2S source port onto the granted channels in the OS8104 routing table, and
   report them. From then on whatever plays into the `PiMost` ALSA card is on
   the ring.
7. **Steady state.** Steering-wheel and screen actions arrive as SatRadio
   requests and are answered and/or forwarded to the Bluetooth agent as events.

## SatRadio FBlock (`protocol/sdars.c`)

Identity `0x44.00`, matching the real module and ASK's connection profile.
Payloads follow the MMI's parser grammar (`Single` = bare value, `Record` =
`00 00` + fields, `Array` = `00 00` + rows, `String` = `01 <chars> 00`). An
Error reply or an unparseable Status invalidates the function on the MMI, so
unsupported operations are logged and dropped rather than errored.

| FktID | Name | Served as |
|---|---|---|
| `0x000` | FktIDs | nibble-packed list of everything below |
| `0x001` / `0x002` | Notification | Set(Function)/Clear(Function) per FktID; a subscribe pushes the current value at once |
| `0x090` / `0x091` / `0x092` | Create/Destroy/MoveArrayWindow | window per (peer, handle) on its own FktID `0x0D10+slot`; mother array `0xC02` is never published directly |
| `0x100` / `0x101` / `0x102` / `0x103` / `0x116` | SourceInfo, Allocate, Deallocate, SourceActivity, SyncDataInfo | audio source contract (step 6 above) |
| `0x120` | users | list of `{FBlock, Inst, x}` triples; register/unregister; empty <-> non-empty is the `ui open|closed` event |
| `0x200` | SRStationSelect | current station; Set/SetGet/Increment/Decrement/Get |
| `0x205` | ESN | string up to 12 |
| `0x206` | programme text | 13 strings up to 36; title, artist, album from the agent, station names otherwise |
| `0x400` | service status | `{receptionOK, 1}`; `receptionOK` is the agent's `signal` |
| `0x401` | presets | 6 slots; select (value 2) tunes, store (value 3) saves the current station |
| `0x403` | unknown enum | constant 0 |
| `0x410` | step | next/previous on channel/preset/category list; see below |
| `0x411` | service enabled | constant 1 (0 opens the MMI's "service disabled" dialog) |
| `0xC04` | category info | `{id, short name}` rows for categories in use |
| `0xC05` | GetCategoryStations | method; ResultAck with the category's stations |
| `0xC10` | current station info | `{station, short, long, subscribed, category}` |

Station numbers are 1-based positions in the list. The built-in list has 8
stations in 6 categories and is served until the agent replaces it. `0x212`
and `0x094` requests are logged only.

**Steps.** Every `0x410` step is treated as a track skip on the phone: it goes
to the agent as `seek next|prev <list> <category>` and the station stays put
(the steering wheel sends list 1 even for the channel list). Set `SDARS_SEEK=station`
to make `mostd` step through its own list instead, like a real tuner.

## Control socket (`protocol/control.c`)

One client (normally `tools/bt-agent/bt_agent.py`) on the Unix stream socket
`MOSTD_CTL_SOCKET`, default `/run/mostd/sdars.sock`. Line protocol, `\n`
terminated, fields separated by `|`. Non-blocking throughout; output that does
not fit the buffer is dropped with a log line. Without a client `mostd` serves
its built-in station list.

Agent -> mostd:

| Line | Effect |
|---|---|
| `text <title>|<artist>|<album>` | 0x206 programme text (pushed on change) |
| `stations <n>` ... `end` | replace the station table: `category <id>|<short8>|<long20>` and exactly n `station <short8>|<long20>|<category>[|<subscribed 0/1>]` lines; `abort` cancels |
| `current <n>` | the station that is playing (no `tune` event back) |
| `signal 0|1` | 0x400 reception flag |
| `esn <text up to 12>` | 0x205 |
| `get` | emit a state report (`current`, `source`, `ui`) |
| `ping` | `pong` |

Replies are `ok <command>`, `err <command> <reason>` or `pong`.

mostd -> agent (events):

| Line | Meaning |
|---|---|
| `seek next|prev <list> <category>` | 0x410 step |
| `tune <n>` | the MMI selected a station |
| `source on|off` | ASK SourceActivity / Deallocate edge |
| `ui open|closed` | first MMI registered / last unregistered on 0x120 |
| `preset store <slot> <station>` | the MMI stored a preset |
| `current <n>` | state report |

## Audio path

The OS8104 is the I2S master; the Pi's I2S peripheral is a slave on it, exposed
as the ALSA card `PiMost` by a separate overlay (see the root README).
`bluealsa-aplay` decodes the phone's A2DP stream straight into that card. On
`Allocate`, `sdars.c` requests 4 synchronous bytes (16-bit stereo) from the
timing master at `0x0400`, writes the granted channel numbers into the routing
table with the source-port address references `0x49 0x59 0x69 0x79`, clears
source bypass (SBY) while keeping the node active (ABY), and sets the serial
port to I2S at 32Fs. `Deallocate` reverses it. `SourceInfo` reports `FF` while
unallocated, as the real module does.

## Diagnosis 0x06.54 (`protocol/diagnosis.c`)

Answers `FktIDs.Get` with the real module's directory (`0x001, 0x050, 0x051`)
and classifies `DiagnosticsChannel.Set` requests (immediate-leaf form vs
tunneled KWP2000 records, named via `diagnosis_jobmap.h`) without replying,
because the real module's replies come from a backend that is not modelled. The
only proactive emission is the 0x050 Status replayed on SAT select.

## Observation modules

Active on every received message:

- `protocol/addrbook.c` learns `fblock+inst -> logical address` from the source
  of every response.
- `protocol/registry.c` decodes the CentralRegistry.
- `protocol/catalog.c` + `protocol/decode.c` decode a handful of known
  telegrams (KombiMisc odometer, FbNavigation status) into named fields on the
  log line.
- `runtime.c` observes NetBlock `ShutDown` and de-duplicates log lines from
  high-rate broadcasters (handlers still run on every message; only the log
  line is suppressed).

The ring-enumeration modules from the earlier bus-mapping work (FktIDs scan of
every FBlock, NetBlock and DeviceInfo probes of every node, outbound
`Notification.Set` subscriptions) were removed in October 2026 after being
disabled since June; they are in git history if that work resumes.

## Environment knobs

| Variable | Read | Effect |
|---|---|---|
| `MOSTD_LOG` | start | log level (`error`/`info`/`debug`) |
| `MOSTD_TRACE_FILE` | start | structured CMT RX/TX + driver-event trace file |
| `MOSTD_CTL_SOCKET` | start | control socket path |
| `SDARS_SEEK=station` | start | 0x410 steps the station list instead of emitting `seek` |
| `SDARS_LIST_COUNT=n` | start | clamp the built-in station list to n (1..8) |

## Logging

Console lines are `mostd: [<monotonic s>] ...` at the selected level. Every
received control message appears as
`ctrl msg src=... fb=... inst=... fkt=... op=... len=... payload=<hex>`; sends
appear at debug level as `ctrl tx ...`. The trace file (opt-in) holds one
`cmt ts=... dir=rx|tx ... payload=...` line per message and one `evt ...` line
per driver event, with reassembled segmented payloads.

## Tree layout

- `main.c`: options, environment, event loop (device fd + control socket, 100 ms tick)
- `log.c` / `log.h`: console logging, trace file
- `mostd.service`: systemd unit
- `protocol/transport.c`: `libmost` open/poll/read wrapper
- `protocol/rx.c`: reassembly of segmented control frames into `mostd_rx_msg`
- `protocol/tx.c`: control frame transmit, segmentation, busy retry
- `protocol/cmt.h`, `frame_layout.h`: OpType enum and CMT packing, frame byte offsets
- `protocol/runtime.c`: event dispatch, link-up sequence, registry request, log dedup
- `protocol/nodecfg.c`: node address / group programming on link-up
- `protocol/netblock_server.c`: NetBlock 0x01 responder
- `protocol/sdars.c` / `sdars.h`: SatRadio 0x44 responder and audio source
- `protocol/control.c` / `control.h`: control socket and line protocol
- `protocol/diagnosis.c` / `diagnosis_jobmap.h`: Diagnosis 0x06.54
- `protocol/registry.c`, `addrbook.c`: CentralRegistry decoder, learned addresses
- `protocol/catalog.c`, `decode.c`: telegram field layouts and decoder
- `tests/`: unit tests, see `TESTING.md`

## Known gaps

- The Diagnosis backend is not modelled; KWP requests get no reply.
- `0x0110` is assumed free. Adjust `MOSTD_DIAG_NODE_ADDR` in `nodecfg.c` if a
  ring populates it.
- The audio-enable GPIO the real module toggles on `SourceActivity` has no Pi
  equivalent; the source is live from `Allocate` onwards.
- The catalog decoder handles FbNavigation `add*` telegrams only when every
  field is present; partial presence subsets are logged raw.
