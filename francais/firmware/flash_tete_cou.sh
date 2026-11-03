#!/bin/sh
# Programme la tête fermée par le cou, depuis l'USB du torse (commande « pont » du torse).
#   sh firmware/flash_tete_cou.sh                         (édition complète, port du torse détecté tout seul)
#   sh firmware/flash_tete_cou.sh /dev/cu.usbmodem1101
#   sh firmware/flash_tete_cou.sh instructable
# La tête redémarre dans le mode téléchargement de sa ROM, qui écoute les fils du cou ; le torse relaie
# esptool au débit du cou, 460800 bauds (application seule, une quinzaine de secondes). esptool relance la
# tête à la fin, le torse redémarre de lui-même 15 s plus tard. Ferme d'abord tout moniteur série du torse.
# Le torse est redémarré avant le pont : après des heures de marche, sa mémoire morcelée ne lui laisse
# plus les 20 Ko d'un seul tenant du pont (« pont : mémoire insuffisante »).
# -b 115200 : côté Mac l'USB du torse ignore le débit, et esptool n'envoie ainsi aucun changement de débit
# à la ROM, déjà calée sur le cou.
set -eu
# Conversation en route avec le robot branché sur ce Mac : son relais rend le port du torse le temps du script.
[ -e "$HOME/moodimos/port.verrou" ] && [ -z "${MOODIMOS_PORT_PRIS:-}" ] &&
  exec python3 "$(dirname "$0")/avec_port.py" sh "$0" "$@"
cd "$(dirname "$0")/.."
edition=complete   # le robot de la maison ; l'instructable seulement si on la nomme
case "${1:-}" in
  instructable|complete) edition=$1; shift;;
  petite) echo "l'édition petite est devenue complete (2026-10-28)" >&2; shift;;
  ""|/dev/*|COM*) ;;
  *) echo "édition inconnue : $1 (instructable ou complete)" >&2; exit 2;;
esac
case "$edition" in instructable) id=2;; complete) id=3;; esac
port=${1:-$(arduino-cli board list | awk '/esp32:esp32/ && /usbmodem/ {print $1; exit}')}
[ -n "$port" ] || { echo "torse introuvable sur l'USB" >&2; exit 1; }
esptool=$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | tail -1)
[ -x "$esptool" ] || { echo "esptool du cœur ESP32 d'Arduino introuvable" >&2; exit 1; }
options=PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc
build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-tete-cou.XXXXXX")
echo "Tête, édition $edition, par le torse sur $port"
arduino-cli compile --fqbn esp32:esp32:esp32s3 --board-options "$options" \
  --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
  --build-path "$build" firmware/moodimos-tete

echo "Redémarrage du torse"
"$esptool" --chip esp32s3 -p "$port" --after hard-reset read-mac >/dev/null

# Torse redémarré : ouverture du pont (attente de « Tête en place », « pont », attente de « pont prêt »)
python3 firmware/ouvrir_pont.py "$port"

"$esptool" --chip esp32s3 -p "$port" -b 115200 --before no-reset --after watchdog-reset \
  write-flash -z 0x10000 "$build/moodimos-tete.ino.bin"
echo "Tête programmée. Le torse redémarre dans 15 s."
