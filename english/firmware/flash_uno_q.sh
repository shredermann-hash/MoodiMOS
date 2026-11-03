#!/bin/sh
# Firmware the robot connected to the UNO Q hub: Mac compiles, UNO Q programs via USB.
# ```
# sh firmware/flash_uno_q.sh head
# ```
# (closed head via neck; complete edition)
# 
# ```
# sh firmware/flash_uno_q.sh torso
# ```
# 
# ```
# sh firmware/flash_uno_q.sh head instructable
# ```
# 
# ```
# UNO_Q=arduino@adresse sh firmware/flash_uno_q.sh head
# ```
# (UNO Q, SSH-accessible via key)
# 
# On UNO Q, once:
# ```
# python3 -m venv ~/moodimos/venv && ~/moodimos/venv/bin/pip install esptool==5.3.1
# ```
# (Mac ESP32 core version; with esptool 5.6.0, the head tool in the neck stopped responding, and the head remained in flash mode until lifted and reattached).
# 
# Only the app is written (0x10000), like the head via the neck: the torso phrase partition remains untouched. For the head, same steps as `flash_head_neck.sh` (torso reboot, bridge, `esptool` at 115200 bauds to head ROM fixed on the neck).
set -eu
cd "$(dirname "$0")/.."
case "${1:-}" in
  head) shift; set -- tete "$@";;
  torso) shift; set -- torse "$@";;
esac
case "${1:-}" in
  tete|torse) board=$1; shift;;
  *) echo "Usage: \`$0 head|torso [instructable|complete]\`" >&2; exit 2;;
esac
case "$board" in tete) board=head;; torse) board=torso;; esac
edition=${1:-complete}   # default to the home robot; use the Instructable edition only when explicitly selected
case "$edition" in
  instructable) id=2;;
  complete) id=3;;
  *) echo "unknown edition: \`$edition\` (instructable or complete)" >&2; exit 2;;
esac
plist="$HOME/Library/LaunchAgents/fr.moodimos.conversation.plist"
host=${UNO_Q:-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
[ -n "$host" ] || { echo "Which UNO Q? UNO_Q=arduino@address sh firmware/flash_uno_q.sh $board" >&2; exit 2; }
options=PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-uno-q.XXXXXX")
echo "$board, edition $edition, through the UNO Q ($host)"
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options "$options" \
  --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
  --build-path "$build" "firmware/moodimos-$board"

ssh -o BatchMode=yes "$host" 'mkdir -p ~/moodimos'
scp -q "$build/moodimos-$board.ino.bin" firmware/open_bridge.py "$host:moodimos/"
ssh -o BatchMode=yes "$host" sh -s "$board" <<'EOF'
set -eu
cd "$HOME/moodimos"
# The conversation relay (Mac service) manages the torso port: it releases it to whoever writes its number in `port.pause` and reclaims it when the lock is free, at the end of this script.
exec 9>>port.verrou
if ! flock -n 9; then
  echo $$ > port.pause
  flock -w 10 9 || { rm -f port.pause; echo "Another program is using the torso port" >&2; exit 1; }
  rm -f port.pause
fi
esptool="$HOME/moodimos/venv/bin/esptool"
[ -x "$esptool" ] || { echo "Missing ESPTool on UNO Q (\`~/moodimos/venv\`)" >&2; exit 1; }
port=$(ls /dev/serial/by-id/*Espressif* 2>/dev/null | head -1)
[ -n "$port" ] || { echo "Robot not found on the UNO Q hub." >&2; exit 1; }
if [ "$1" = torso ]; then
  "$esptool" --chip esp32s3 -p "$port" --after hard-reset write-flash -z 0x10000 moodimos-torso.ino.bin
  echo "Torso programmed."
else
  echo "Restarting the torso"
  "$esptool" --chip esp32s3 -p "$port" --after hard-reset read-mac >/dev/null
  python3 open_bridge.py "$port"
  "$esptool" --chip esp32s3 -p "$port" -b 115200 --before no-reset --after watchdog-reset \
    write-flash -z 0x10000 moodimos-head.ino.bin
  echo "Head programmed. The torso restarts in 15 s."
fi
EOF
