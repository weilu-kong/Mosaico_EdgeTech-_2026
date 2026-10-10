#!/usr/bin/env bash
# Fetches the OFFICIAL esp-mosaico-vibe workspace (with submodules) at the commits recorded in
# versions.lock.json into ./workspace. It never touches ~/esp or any other existing checkout,
# never installs software and never flashes a device.
#   scripts/bootstrap_workspace.sh            # dry run: prints what would happen
#   scripts/bootstrap_workspace.sh --apply    # clone + check out the pinned commits
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="$ROOT/versions.lock.json"
DEST="$ROOT/workspace/esp-mosaico-vibe"
read -r URL SHA < <(python3 -c "import json;d=json.load(open('$LOCK'))['upstream']['esp-mosaico-vibe'];print(d['url'],d['commit'])")
echo "repo:   $URL"; echo "commit: $SHA"; echo "dest:   $DEST"
if [ "${1:-}" != "--apply" ]; then echo "(dry run, pass --apply to clone)"; exit 0; fi
[ -e "$DEST" ] && { echo "$DEST already exists; refusing to overwrite" >&2; exit 3; }
mkdir -p "$ROOT/workspace"
git clone "$URL" "$DEST"
git -C "$DEST" checkout --detach "$SHA"
git -C "$DEST" submodule update --init --recursive
python3 - <<PY
import json,subprocess
lock=json.load(open("$LOCK"))["upstream"]
for name,rel in (("esp-mosaico-bsp","submodule/esp-mosaico-bsp"),("esp-mosaico-utils","submodule/esp-mosaico-utils"),("raylib-lite-engine","submodule/raylib-lite-engine")):
    got=subprocess.check_output(["git","-C","$DEST/"+rel,"rev-parse","HEAD"],text=True).strip()
    ok = got==lock[name]["commit"]
    print(("OK   " if ok else "DIFF ")+name, got)
PY
