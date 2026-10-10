#!/usr/bin/env bash
# Interactive Host preview (synthetic sensors, localhost only). Opens no outbound connection.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-8460}"
command -v python3 >/dev/null || { echo "python3 is required (>=3.10)" >&2; exit 2; }
python3 - <<'PY' || { echo "Pillow is required by the engine's Host runtime: python3 -m pip install pillow" >&2; exit 2; }
import PIL  # noqa: F401
PY
echo "Open http://127.0.0.1:${PORT}/  (Ctrl+C to stop)"
exec python3 "$ROOT/app/mosaico_experience/tools/preview_server.py" --port "$PORT"
