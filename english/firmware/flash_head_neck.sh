#!/bin/sh
# Program the closed head through the neck from the torso USB port, using its pont command.
#   sh firmware/flash_head_neck.sh                         (complete edition, automatically found torso port)
#   sh firmware/flash_head_neck.sh /dev/cu.usbmodem1101
#   sh firmware/flash_head_neck.sh instructable
# The head restarts in ROM download mode on the neck UART; the torso relays esptool at 460800 baud.
# Only the application is written, taking roughly fifteen seconds. esptool restarts the head at
# completion, and the torso restarts itself 15 s later. Close the torso serial monitor first.
# Restart the torso before opening the bridge: after hours of operation, fragmented memory may
# lack the contiguous 20 KB bridge buffer and report “pont : mémoire insuffisante”.
# -b 115200 applies to the Mac-side USB, which ignores baud rate; this avoids asking the ROM,
# already synchronised to the neck, to change its UART speed.
set -eu
# Conversation on the way with the robot connected to this Mac: its relay allows the torso port to be used during the script execution.
[ -e "$HOME/moodimos/port.verrou" ] && [ -z "${MOODIMOS_PORT_PRIS:-}" ] &&
  exec python3 "$(dirname "$0")/with_port.py" sh "$0" "$@"
cd "$(dirname "$0")/.."
edition=complete   # default to the home robot; use the Instructable edition only when explicitly selected
case "${1:-}" in
  instructable|complete) edition=$1; shift;;
  petite) echo "the petite edition became complete (2026-10-28)" >&2; shift;;
  ""|/dev/*|COM*) ;;
  *) echo "unknown edition: $1 (instructable or complete)" >&2; exit 2;;
esac
case "$edition" in instructable) id=2;; complete) id=3;; esac
port=${1:-$(arduino-cli board list | awk '/esp32:esp32/ && /usbmodem/ {print $1; exit}')}
[ -n "$port" ] || { echo "Torso not found on USB" >&2; exit 1; }
esptool=$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | tail -1)
[ -x "$esptool" ] || { echo "esptool from the Arduino ESP32 core not found" >&2; exit 1; }
options=PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-head-cou.XXXXXX")
echo "Head, edition $edition, through the torso on $port."
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options "$options" \
  --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
  --build-path "$build" firmware/moodimos-head

echo "Restarting the torso"
"$esptool" --chip esp32s3 -p "$port" --after hard-reset read-mac >/dev/null

# Torso restarted: opening bridge (waiting for "Head in place", "bridge", wait for "bridge ready")
python3 firmware/open_bridge.py "$port"

"$esptool" --chip esp32s3 -p "$port" -b 115200 --before no-reset --after watchdog-reset \
  write-flash -z 0x10000 "$build/moodimos-head.ino.bin"
echo "Head programmed. The torso restarts in 15 s."
