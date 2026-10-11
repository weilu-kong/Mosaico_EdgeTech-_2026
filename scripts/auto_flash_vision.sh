#!/usr/bin/env bash
# Auto-detect S31 board in ROM download mode and flash Stage 4b vision firmware
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXPECT_MAC="1c:29:04:d0:a3:36"

echo "=================================================================="
echo "Waiting for ESP32-S31 (MAC $EXPECT_MAC) in ROM download mode..."
echo "Instructions: Press & hold BOOT button -> click RESET button -> release BOOT button."
echo "=================================================================="

source ~/esp/esp-idf-mosaico-7b9cc1a/export.sh >/dev/null 2>&1 || true

for i in {1..300}; do
  for dev in /dev/cu.usbmodem*; do
    [[ -e "$dev" ]] || continue
    [[ "$dev" == *11201* ]] && continue # Always skip the other project's S3

    echo "Found candidate serial port: $dev, probing MAC..."
    # Probe chip and MAC
    OUT="$(esptool --chip esp32s31 -p "$dev" --before no-reset --after no-reset chip-id 2>&1 || true)"
    if echo "$OUT" | grep -qi "$EXPECT_MAC"; then
      echo ">> Confirmed target ESP32-S31 on $dev! <<"
      cd "$ROOT/firmware"
      bash "$ROOT/scripts/vision_flash.sh" check "$dev"
      VISION_FLASH_CONFIRM=yes bash "$ROOT/scripts/vision_flash.sh" update "$dev"
      echo "=================================================================="
      echo ">> FLASH SUCCESS: Stage 4b Vision firmware flashed to ESP32-S31 <<"
      echo "=================================================================="
      exit 0
    else
      echo "Port $dev did not match target MAC $EXPECT_MAC (output: $(echo "$OUT" | tr '\n' ' ' | head -c 100))."
    fi
  done
  sleep 1
done

echo "Timeout (300s): target ESP32-S31 was not detected."
exit 1
