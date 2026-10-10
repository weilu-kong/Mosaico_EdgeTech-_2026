#!/usr/bin/env bash
# Compile every application source with gcc and clang (-Wall -Wextra -Werror) and
# run the native unit tests. Needs only a C compiler and the Raylib Lite engine headers.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/app/mosaico_experience"
ENGINE="${MOSAICO_ENGINE_ROOT:-$ROOT/workspace/esp-mosaico-vibe/submodule/raylib-lite-engine}"
[ -f "$ENGINE/host/run_game.py" ] || ENGINE="$(ls -d "$ROOT"/../*/submodule/raylib-lite-engine 2>/dev/null | head -1 || true)"
[ -f "${ENGINE:-/nonexistent}/host/run_game.py" ] || { echo "raylib-lite-engine not found. Run scripts/bootstrap_workspace.sh --apply or set MOSAICO_ENGINE_ROOT" >&2; exit 2; }
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
INC=(-I"$ENGINE/host/include" -I"$ENGINE/host" -I"$ENGINE/components/mosaico_game_2d/include" -I"$ENGINE/components/mosaico_raylib_fast/include" -I"$APP/main")
status=0
for CC in gcc clang; do
  command -v "$CC" >/dev/null 2>&1 || { echo "[skip] $CC not installed"; continue; }
  for f in "$APP"/main/exp_*.c "$APP"/host/*.c; do
    if ! "$CC" -std=c11 -O2 -Wall -Wextra -Werror -DMOSAICO_HOST_SIMULATION=1 "${INC[@]}" -I"$APP/host" -c "$f" -o "$OUT/x.o"; then
      echo "[FAIL] $CC $f"; status=1
    fi
  done
  echo "[done] $CC compile pass over main/ and host/"
done
CCNAT="$(command -v gcc || command -v clang || command -v cc)"
"$CCNAT" -std=c11 -O2 -Wall -Wextra -Wpedantic -I"$APP/main" "$APP/tests/test_core.c" "$APP/main/exp_maze.c" "$APP/main/exp_fusion.c" "$APP/main/exp_audio.c" -lm -o "$OUT/test_core"
"$OUT/test_core" || status=1
exit $status
