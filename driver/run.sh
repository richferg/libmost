#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOGFILE="$SCRIPT_DIR/dmesg.log"
MODULE="os8104"
MODULE_PATH="$SCRIPT_DIR/src/${MODULE}.ko"
OVERLAY_NAME="os8104"
OVERLAY_DIR="$SCRIPT_DIR/overlay"
DTBO_PATH="$OVERLAY_DIR/${OVERLAY_NAME}.dtbo"
DMESG_PID=""
OVERLAY_LOADED=0
MODULE_LOADED=0
RUN_UNTIL_INTERRUPT=0
DEVNODE=""

usage() {
	echo "Usage: $0 [--wait]"
	echo "  -w, --wait    Keep the module loaded until interrupted (Ctrl-C)"
}

cleanup() {
	if [ -n "$DMESG_PID" ] && kill -0 "$DMESG_PID" 2>/dev/null; then
		echo "Stopping dmesg logging"
		kill "$DMESG_PID"
		wait "$DMESG_PID" 2>/dev/null || true
	fi

	if [ "$MODULE_LOADED" -eq 1 ]; then
		echo "Removing module: $MODULE"
		sudo rmmod "$MODULE" >/dev/null 2>&1 || true
	fi

	if [ "$OVERLAY_LOADED" -eq 1 ]; then
		echo "Removing overlay: $OVERLAY_NAME"
		sudo dtoverlay -R "$OVERLAY_NAME" >/dev/null 2>&1 || true
	fi
}
trap cleanup EXIT

while [ $# -gt 0 ]; do
	case "$1" in
	-h|--help)
		usage
		exit 0
		;;
	-w|--wait)
		RUN_UNTIL_INTERRUPT=1
		shift
		;;
	*)
		echo "Unknown option: $1" >&2
		usage
		exit 1
		;;
	esac
done

if ! command -v dtoverlay >/dev/null 2>&1; then
	echo "dtoverlay not found; install device-tree-compiler tools." >&2
	exit 1
fi

if [ ! -f "$DTBO_PATH" ]; then
	echo "Overlay $DTBO_PATH not found. Run 'make overlay' first." >&2
	exit 1
fi

if [ ! -f "$MODULE_PATH" ]; then
	echo "Module $MODULE_PATH not found. Run 'make module' or 'make release' first." >&2
	exit 1
fi

echo "Loading overlay: $OVERLAY_NAME"
sudo dtoverlay -R "$OVERLAY_NAME" >/dev/null 2>&1 || true
sudo dtoverlay -d "$OVERLAY_DIR" "$OVERLAY_NAME"
OVERLAY_LOADED=1

echo "Starting dmesg logging to $LOGFILE"
dmesg -W > "$LOGFILE" &
DMESG_PID=$!

echo "Inserting module: $MODULE (from $MODULE_PATH)"
sudo insmod "$MODULE_PATH"
MODULE_LOADED=1

echo "Waiting for character device (/dev/mostN) to appear"
for attempt in $(seq 1 10); do
	DEVNODE=$(ls /dev/most* 2>/dev/null | head -n1 || true)
	if [ -n "$DEVNODE" ]; then
		echo "Found device node: $DEVNODE"
		break
	fi
	sleep 0.5
done

if [ -z "$DEVNODE" ]; then
	echo "Warning: no /dev/mostN device node appeared" >&2
else
	echo "Control frames: write() 21-byte frames to $DEVNODE; read() returns struct most_event records (see libmost)"
fi

if [ "$RUN_UNTIL_INTERRUPT" -eq 1 ]; then
	echo "Module will remain loaded. Press Ctrl-C to stop and clean up."
	while true; do
		sleep 1
	done
fi

echo "Waiting 5 seconds..."
sleep 5

echo "Removing module: $MODULE"
sudo rmmod "$MODULE"
MODULE_LOADED=0

echo "Removing overlay: $OVERLAY_NAME"
sudo dtoverlay -R "$OVERLAY_NAME" >/dev/null 2>&1 || true
OVERLAY_LOADED=0

echo "Done."
