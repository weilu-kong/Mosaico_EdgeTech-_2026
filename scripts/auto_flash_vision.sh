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

    echo "Found candidate serial port: $dev, running vision_flash.sh..."
    cd "$ROOT/firmware"
    # If a valid stamp already exists for this port within 30 min, go straight to update!
    STAMP="/tmp/mosaico_vision_check_$(echo -n "$dev" | tr '/:' '__')"
    if [[ -f "$STAMP" ]] && [[ $(( $(date +%s) - $(stat -f %m "$STAMP" 2>/dev/null || echo 0) )) -le 1800 ]]; then
      if VISION_FLASH_CONFIRM=yes bash "$ROOT/scripts/vision_flash.sh" update "$dev"; then
        echo "=================================================================="
        echo ">> FLASH SUCCESS: Stage 4c Vision firmware flashed to ESP32-S31 <<"
        echo "=================================================================="
        exit 0
      fi
    else
      if bash "$ROOT/scripts/vision_flash.sh" check "$dev"; then
        sleep 1
        if [[ -e "$dev" ]] && VISION_FLASH_CONFIRM=yes bash "$ROOT/scripts/vision_flash.sh" update "$dev"; then
          echo "=================================================================="
          echo ">> FLASH SUCCESS: Stage 4c Vision firmware flashed to ESP32-S31 <<"
          echo "=================================================================="
          exit 0
        else
          echo ">> Check passed and stamp saved! If port closed after check, re-enter BOOT+RESET once more to flash immediately. <<"
        fi
      fi
    fi
  done
  sleep 1
done

echo "Timeout (300s): target ESP32-S31 was not detected."
exit 1
