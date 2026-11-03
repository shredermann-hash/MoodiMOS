#!/bin/sh
# Programme le robot branché sur le hub de la UNO Q : le Mac compile, la UNO Q programme par son USB.
#   sh firmware/flash_uno_q.sh tete                  (tête fermée, par le cou ; édition complète)
#   sh firmware/flash_uno_q.sh torse
#   sh firmware/flash_uno_q.sh tete instructable
#   UNO_Q=arduino@adresse sh firmware/flash_uno_q.sh tete     (UNO Q, joignable en SSH par clé)
# Sur la UNO Q, une fois : python3 -m venv ~/moodimos/venv && ~/moodimos/venv/bin/pip install esptool==5.3.1
# (la version du cœur ESP32 du Mac : avec esptool 5.6.0, l'outil chargé dans la tête ne répondait plus à
# travers le cou, et la tête restait en mode téléchargement jusqu'à ce qu'on la soulève et la repose).
# Seule l'application est écrite (0x10000), comme pour la tête par le cou : la partition des phrases du
# torse n'est pas touchée. Pour la tête, mêmes étapes que flash_tete_cou.sh (redémarrage du torse, pont,
# esptool à 115200 bauds vers la ROM de la tête calée sur le cou).
set -eu
cd "$(dirname "$0")/.."
case "${1:-}" in
  tete|torse) board=$1; shift;;
  *) echo "usage : $0 tete|torse [instructable|complete]" >&2; exit 2;;
esac
edition=${1:-complete}   # le robot de la maison ; l'instructable seulement si on la nomme
case "$edition" in
  instructable) id=2;;
  complete) id=3;;
  *) echo "édition inconnue : $edition (instructable ou complete)" >&2; exit 2;;
esac
plist="$HOME/Library/LaunchAgents/fr.moodimos.conversation.plist"
host=${UNO_Q:-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
[ -n "$host" ] || { echo "quelle UNO Q ? UNO_Q=arduino@adresse sh firmware/flash_uno_q.sh $board" >&2; exit 2; }
options=PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-uno-q.XXXXXX")
echo "$board, édition $edition, par la UNO Q ($host)"
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options "$options" \
  --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
  --build-path "$build" "firmware/moodimos-$board"

ssh -o BatchMode=yes "$host" 'mkdir -p ~/moodimos'
scp -q "$build/moodimos-$board.ino.bin" firmware/ouvrir_pont.py "$host:moodimos/"
ssh -o BatchMode=yes "$host" sh -s "$board" <<'EOF'
set -eu
cd "$HOME/moodimos"
# Le relais de la conversation (service du Mac) tient le port du torse : il le rend à qui écrit son numéro dans
# port.pause et le reprend quand le verrou est libre, à la fin de ce script.
exec 9>>port.verrou
if ! flock -n 9; then
  echo $$ > port.pause
  flock -w 10 9 || { rm -f port.pause; echo "port du torse pris par un autre programme" >&2; exit 1; }
  rm -f port.pause
fi
esptool="$HOME/moodimos/venv/bin/esptool"
[ -x "$esptool" ] || { echo "esptool absent de la UNO Q (~/moodimos/venv)" >&2; exit 1; }
port=$(ls /dev/serial/by-id/*Espressif* 2>/dev/null | head -1)
[ -n "$port" ] || { echo "robot introuvable sur le hub de la UNO Q" >&2; exit 1; }
if [ "$1" = torse ]; then
  "$esptool" --chip esp32s3 -p "$port" --after hard-reset write-flash -z 0x10000 moodimos-torse.ino.bin
  echo "Torse programmé."
else
  echo "Redémarrage du torse"
  "$esptool" --chip esp32s3 -p "$port" --after hard-reset read-mac >/dev/null
  python3 ouvrir_pont.py "$port"
  "$esptool" --chip esp32s3 -p "$port" -b 115200 --before no-reset --after watchdog-reset \
    write-flash -z 0x10000 moodimos-tete.ino.bin
  echo "Tête programmée. Le torse redémarre dans 15 s."
fi
EOF
