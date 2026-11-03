#!/bin/sh
# Compilations reproductibles des deux cartes, pour les deux éditions ; aucune carte n'est programmée.
set -eu
cd "$(dirname "$0")/.."
firmware_build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-build.XXXXXX")
for edition in instructable complete; do
  case "$edition" in instructable) id=2;; complete) id=3;; esac
  for board in tete torse; do
    echo "— $board, édition $edition —"
    arduino-cli compile --fqbn esp32:esp32:esp32s3 \
      --board-options PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc \
      --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
      --build-path "$firmware_build/$board-$edition" "firmware/moodimos-$board"
  done
done
printf 'Binaires de vérification : %s\n' "$firmware_build"
