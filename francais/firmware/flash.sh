#!/bin/sh
# Compile et programme une carte, en édition complète sauf si on demande l'instructable.
#   sh firmware/flash.sh torse                  (édition complète, port détecté tout seul)
#   sh firmware/flash.sh torse /dev/cu.usbmodem1101
#   sh firmware/flash.sh tete instructable
set -eu
# Conversation en route avec le robot branché sur ce Mac : son relais rend le port du torse le temps du script.
[ -e "$HOME/moodimos/port.verrou" ] && [ -z "${MOODIMOS_PORT_PRIS:-}" ] &&
  exec python3 "$(dirname "$0")/avec_port.py" sh "$0" "$@"
cd "$(dirname "$0")/.."
board=${1:?carte : tete ou torse}
case "$board" in tete|torse) ;; *) echo "carte inconnue : $board" >&2; exit 2;; esac
shift
edition=complete   # le robot de la maison ; l'instructable seulement si on la nomme
case "${1:-}" in
  instructable|complete) edition=$1; shift;;
  petite) echo "l'édition petite est devenue complete (2026-10-28)" >&2; shift;;
  ""|/dev/*|COM*) ;;
  *) echo "édition inconnue : $1 (instructable ou complete)" >&2; exit 2;;
esac
case "$edition" in instructable) id=2;; complete) id=3;; esac
port=${1:-$(arduino-cli board list | awk '/esp32:esp32/ && /usbmodem/ {print $1; exit}')}
[ -n "$port" ] || { echo "aucune carte ESP32 trouvée sur l'USB" >&2; exit 1; }
options=PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-flash.XXXXXX")
echo "Carte $board, édition $edition, port $port"
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options "$options" \
  --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
  --build-path "$build" "firmware/moodimos-$board"
arduino-cli upload -p "$port" --fqbn esp32:esp32:esp32s3 --board-options "$options" --input-dir "$build"
