#!/usr/bin/env bash
# READ-ONLY audit of the Mac development environment. Installs nothing, changes nothing,
# opens no serial port and sends nothing over the network.
# Usage: scripts/mac_env_audit.sh > evidence/mac_env_audit.txt
set -uo pipefail
# macOS has no `timeout`; use gtimeout if present, else run directly.
if command -v timeout >/dev/null; then TO="timeout 15"; elif command -v gtimeout >/dev/null; then TO="gtimeout 15"; else TO=""; fi
echo "## date"; date -u +%FT%TZ
echo "## os"; sw_vers 2>/dev/null; uname -m
echo "## tools"
for c in python3 pip3 git cmake ninja cc clang gcc make node; do printf '%-8s' "$c"; command -v "$c" >/dev/null && { "$c" --version 2>&1 | head -1; } || echo "(not found)"; done
echo "## python modules"; python3 - <<'PY'
import importlib.util
for m in ("PIL", "serial", "esptool", "yaml"):
    print(f"{m:8s}", "present" if importlib.util.find_spec(m) else "absent")
PY
echo "## existing ESP-IDF checkouts (read-only git describe; nothing is modified)"
for d in "$HOME"/esp/esp-idf*/; do
  [ -d "$d" ] || continue
  printf '%s  ' "$(basename "$d")"
  ( cd "$d" && { cat version.txt 2>/dev/null | head -1 || true; $TO git rev-parse --short HEAD 2>/dev/null || echo "(no git)"; } | tr '\n' ' ' ); echo
done
echo "## pinned IDF commit object present in esp-idf-master?"
( cd "$HOME/esp/esp-idf-master" 2>/dev/null && $TO git cat-file -t 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe 2>&1 ) || echo "(esp-idf-master not found)"
echo "## serial devices present (names only; ports are not opened)"
ls /dev/cu.* 2>/dev/null | head -20 || true
echo "## IDF related env vars (names only)"; env | grep -E '^(IDF_|ESP_)' | cut -d= -f1 | sort -u
