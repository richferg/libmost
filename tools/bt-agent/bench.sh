#!/bin/bash
# Exercise the Bluetooth side with no car and no mostd.
#
#   bench.sh                      run bt_agent.py against a stand-in for mostd's
#                                 socket; type what mostd would send (see below)
#   bench.sh capture [secs] [wav] record the phone's A2DP stream to a WAV and
#                                 report its level; the phone must be playing
#
# Stand-in events, one per line, Ctrl-D to quit:
#   seek next 0 0 | seek prev 0 0 | tune <n> | source on | source off
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"

capture() {
	local secs="${1:-10}" wav="${2:-bt-capture.wav}" rc=0

	# A PCM has one client: bluealsa-aplay has to let go of the stream.
	sudo systemctl stop bluealsa-aplay
	trap 'sudo systemctl start bluealsa-aplay' EXIT
	bluealsa-cli list-pcms
	timeout "$((secs + 5))" arecord -q -D plug:bluealsa -f cd -d "$secs" "$wav" || rc=$?
	if [ "$rc" -ne 0 ]; then
		echo "capture failed (arecord exit $rc): is the phone connected and playing?" >&2
		return "$rc"
	fi
	python3 -I - "$wav" <<'EOF'
import array, math, sys, wave
with wave.open(sys.argv[1]) as w:
    pcm = array.array("h", w.readframes(w.getnframes()))
peak = max(map(abs, pcm), default=0)
print("%s: %.1f s, peak %s" % (sys.argv[1], len(pcm) / 88200,
      "%.1f dBFS" % (20 * math.log10(peak / 32768)) if peak else "none (silence)"))
EOF
}

bench() {
	# Not local: the EXIT trap runs after this function has returned.
	dir="$(mktemp -d)"
	sudo true
	# sudo runs as root: signalling it takes sudo too.
	trap 'sudo kill "$agent" 2>/dev/null; rm -rf "$dir"' EXIT
	# Listen first so the agent's first connect attempt finds the socket.
	( sleep 0.5; exec sudo python3 "$HERE/bt_agent.py" --socket "$dir/sdars.sock" -v ) &
	agent=$!
	echo "pair from the phone now; type stand-in events, Ctrl-D to quit" >&2
	socat "UNIX-LISTEN:$dir/sdars.sock,mode=666" - >/dev/null
}

case "${1:-}" in
capture) shift; capture "$@" ;;
"") bench ;;
*) sed -n '2,10p' "$0" >&2; exit 2 ;;
esac
