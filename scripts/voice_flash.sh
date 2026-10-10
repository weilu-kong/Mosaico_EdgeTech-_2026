#!/usr/bin/env bash
# Voice build (EXP_VOICE) flashing helper for the Mac. Written in the cloud and NOT RUN YET.
#
#   scripts/voice_flash.sh check <port>   read-only: chip type, MAC, and the partition table currently on the board
#   scripts/voice_flash.sh write <port>   writes partition table (0x8000) + app (0x10000) + models (0x510000)
#   scripts/voice_flash.sh update <port>  writes partition table (0x8000) + app (0x10000) only (models already on the board)
#                                         write/update need VOICE_FLASH_CONFIRM=yes in the environment; the board is reset
#                                         at the end (hard reset), so the new image starts by itself. A battery keeps the
#                                         chip powered, so unplugging USB is NOT a power cycle: use the Reset button.
#   scripts/voice_flash.sh coredump <port> read-only: reads the crash dump partition (0x910000) into build_voice/coredump.bin
#                                         and decodes it with esp-coredump against build_voice/mosaico_experience.elf
#
# Owner's consent for the partition-table / model write was given on 2026-10-09 (see docs/11 section 4).
# Never writes: bootloader, eFuse, security settings; never erases the whole flash.
# Run from firmware/ after:  idf.py -B build_voice -DEXP_VOICE=1 build
set -euo pipefail

MODE="${1:-}"; PORT="${2:-}"
EXPECT_MAC="1c:29:04:d0:a3:36"
BUILD="${BUILD_DIR:-build_voice}"
OFF_PT=0x8000; OFF_APP=0x10000; OFF_MODEL=0x510000; OFF_CORE=0x910000; CORE_SIZE=0x80000
APP_MAX=$((0x500000)); MODEL_MAX=$((0x400000))

die() { echo "ERROR: $*" >&2; exit 1; }
[[ "$MODE" == "check" || "$MODE" == "write" || "$MODE" == "update" || "$MODE" == "coredump" ]] || die "usage: $0 check|write|update|coredump <port>"
[[ -n "$PORT" ]] || die "give the serial port explicitly (never auto-detected)"
[[ "$PORT" == *11201* ]] && die "$PORT is the other project's ESP32-S3: refusing"
[[ -e "$PORT" ]] || die "$PORT does not exist (is the board in download mode: unplug, hold Boot, plug in, release?)"
command -v esptool >/dev/null || die "esptool not found (source the ESP-IDF export script first)"

ESP_BASE=(esptool --chip esp32s31 -p "$PORT" --before no-reset --no-stub)
ESP=("${ESP_BASE[@]}" --after no-reset)      # read-only modes keep the board in download mode

# NOTE: on this board the USB port can vanish between two separate esptool
# runs (seen 2026-10-09: 2nd run -> "Device not configured"), so everything
# that must happen in download mode is done in ONE esptool invocation (esptool v5 has no plain command chaining; the connect banner prints the MAC).
if [[ "$MODE" == "check" ]]; then
echo "== chip / MAC + partition table currently on the board (read-only, one connection)"
PT=$(mktemp -t pt_now)
OUT="$("${ESP[@]}" read-flash $OFF_PT 0x1000 "$PT" 2>&1)" || { echo "$OUT" | tail -15; die "could not read the board (re-enter download mode and retry)"; }
echo "$OUT" | grep -i "mac:" | sort -u
echo "$OUT" | grep -qi "$EXPECT_MAC" || { rm -f "$PT"; die "MAC is not $EXPECT_MAC: wrong board, refusing"; }
python3 "${IDF_PATH:?IDF_PATH not set}/components/partition_table/gen_esp32part.py" "$PT" || echo "(table could not be decoded: do not write)"
rm -f "$PT"
echo "check finished; nothing was written"; exit 0
fi

if [[ "$MODE" == "coredump" ]]; then
  CORE="$BUILD/coredump.bin"
  echo "== reading the crash-dump partition ($CORE_SIZE bytes at $OFF_CORE, read-only)"
  OUT="$("${ESP[@]}" read-flash $OFF_CORE $CORE_SIZE "$CORE" 2>&1)" || { echo "$OUT" | tail -15; die "could not read the board"; }
  echo "$OUT" | grep -i "mac:" | sort -u
  echo "$OUT" | grep -qi "$EXPECT_MAC" || die "MAC is not $EXPECT_MAC: wrong board, refusing"
  command -v esp-coredump >/dev/null || die "esp-coredump not found (source the ESP-IDF export script first)"
  esp-coredump info_corefile --core "$CORE" --core-format raw "$BUILD/mosaico_experience.elf" || echo "(decoding failed: send this output and $CORE)"
  exit 0
fi

[[ "${VOICE_FLASH_CONFIRM:-}" == "yes" ]] || die "$MODE needs VOICE_FLASH_CONFIRM=yes"
for f in partition_table/partition-table.bin mosaico_experience.bin srmodels/srmodels.bin; do
  [[ -f "$BUILD/$f" ]] || die "$BUILD/$f missing: build the voice image first"
done
sz() { stat -f%z "$1" 2>/dev/null || stat -c%s "$1"; }
(( $(sz "$BUILD/mosaico_experience.bin") <= APP_MAX )) || die "app does not fit the 5 MB factory partition"
(( $(sz "$BUILD/srmodels/srmodels.bin") <= MODEL_MAX )) || die "models do not fit the 4 MB model partition"
# (no "grep -q" in a pipe: with pipefail it can fail on SIGPIPE although the string is there)
VER_HITS=$(LC_ALL=C grep -a -c -- "-voice-" "$BUILD/mosaico_experience.bin" || true)
(( VER_HITS > 0 )) || die "image is not a voice build (version string): refusing"

echo "== writing (no whole-flash erase; bootloader untouched). MAC must have been verified by a successful check just before; MAC is printed below."
FILES=($OFF_PT "$BUILD/partition_table/partition-table.bin" $OFF_APP "$BUILD/mosaico_experience.bin")
[[ "$MODE" == "write" ]] && FILES+=($OFF_MODEL "$BUILD/srmodels/srmodels.bin")
# last command of the run: default "--after hard-reset" so the new image starts by itself
"${ESP_BASE[@]}" write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m "${FILES[@]}"
echo "done. The board was reset. If the screen stays dark press the Reset button (unplugging USB does not power-cycle a board with a battery)."
