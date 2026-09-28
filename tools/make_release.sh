#!/usr/bin/env bash
# Build MeshDeck and write a single merged flash image to release/.
#   tools/make_release.sh            (ESP-IDF environment must be exported)
# The image targets the chip revision family set in sdkconfig.defaults.
set -euo pipefail
cd "$(dirname "$0")/.."
idf.py build
rev=$(grep -E '^CONFIG_ESP32P4_REV_MIN_FULL=' sdkconfig | cut -d= -f2)
case "$rev" in
  0|1|100) tag="rev1" ;;
  *)       tag="rev3" ;;
esac
out="release/meshdeck-p4-4c-${tag}-merged.bin"
(cd build && python -m esptool --chip esp32p4 merge_bin -o "../${out}" @flash_args)
echo "wrote ${out} ($(wc -c < "${out}") bytes), ESP32-P4 min revision ${rev}"
