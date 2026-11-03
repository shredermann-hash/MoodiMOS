#!/bin/sh
# Reproducible compilations for both circuit boards, for both editions; no board is programmed.
set -eu
cd "$(dirname "$0")/.."
firmware_build=$(mktemp -d "${TMPDIR:-/tmp}/moodimos-build.XXXXXX")
for edition in instructable complete; do
  case "$edition" in instructable) id=2;; complete) id=3;; esac
  for board in head torso; do
    echo "— $board, Edition $edition —"
    arduino-cli compile --fqbn esp32:esp32:esp32s3 \
      --board-options PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc \
      --build-property "compiler.cpp.extra_flags=-DMOODIMOS_EDITION=$id" \
      --build-path "$firmware_build/$board-$edition" "firmware/moodimos-$board"
  done
done
printf 'Verification binaries: %s\n' "$firmware_build"
