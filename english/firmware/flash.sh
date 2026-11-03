#!/bin/sh
# Compile and program a board, unless instructed otherwise for the "instructable" edition:
# 
# ```
# sh firmware/flash.sh torso                  (complete edition, auto-detects port)
# sh firmware/flash.sh torso /dev/cu.usbmodem1101
# sh firmware/flash.sh head instructable
# ```
set -eu
# Conversation on the way with the robot connected to this Mac: its relay allows the torso port to be used during the script execution.
[ -e "$HOME/moodimos/port.verrou" ] && [ -z "${MOODIMOS_PORT_PRIS:-}" ] &&
  exec python3 "$(dirname "$0")/with_port.py" sh "$0" "$@"
cd "$(dirname "$0")/.."
case "${1:-}" in
  head) shift; set -- tete "$@";;
  torso) shift; set -- torse "$@";;
esac
board=${1:?board: head or torso}
case "$board" in tete|torse) ;; *) echo "unknown board: \`$board\`" >&2; exit 2;; esac
shift
case "$board" in tete) sketch_board=head;; torse) sketch_board=torso;; esac
edition=complete   # default to the home robot; use the Instructable edition only when explicitly selected
case "${1:-}" in
  instructable|complete) edition=$1; shift;;
  petite) echo "the petite edition became complete (2026-10-28)" >&2; shift;;
  ""|/dev/*|COM*) ;;
  *) echo "unknown edition: $1 (instructable or complete)" >&2; exit 2;;
esac
case "$edition" in instructable) id=2;; complete) id=3;; esac
port=${1:-$(arduino-cli board list | awk '/esp32:esp32/ && /usbmodem/ {print $1; exit}')}
[ -n "$port" ] || { echo "No ESP32 board found on USB" >&2; exit 1; }
options=PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-flash.XXXXXX")
echo "Board: \`$board\`, Edition: \`$edition\`, Port: \`$port\`"
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options "$options" \
  --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
  --build-path "$build" "firmware/moodimos-$sketch_board"
arduino-cli upload -p "$port" --fqbn esp32:esp32:esp32s3 --board-options "$options" --input-dir "$build"
