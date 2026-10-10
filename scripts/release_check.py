#!/usr/bin/env python3
"""Exhibition image gate: refuses a firmware image that still carries development aids.

Usage: python3 scripts/release_check.py firmware/build/mosaico_experience.bin

Checks (all on the built binary, not on the sources):
  * the version string ends in "-rel" (built with idf.py -DEXP_RELEASE=1), not "-dev";
  * none of the development-aid strings is present: the screen log view, the frame-time overlay, the boot benchmark.
This is a configuration gate only. It says nothing about RF: run scripts/rf_map_check.py on the link map as well, and
the product status stays "RF Verification Pending" until a real measurement and the Japan compliance review exist.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

DEV_MARKERS = {
    b"prev stage '%s'": "screen log view (EXP_DBGLOG)",
    b"present sem %d.%d call": "frame-time overlay (EXP_TIMING_OVERLAY)",
    b"now %d.%d max %d.%d d %d.%d wait": "frame-time overlay (EXP_TIMING_OVERLAY)",
    b"sweep %d/12": "compass sweep diagnostics (EXP_DBGLOG)",
}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    data = Path(sys.argv[1]).read_bytes()
    problems: list[str] = []
    versions = set(re.findall(rb"\d+\.\d+\.\d+-stage[0-9a-z]+-[a-z0-9]+-(?:rel|dev)", data))
    if not versions:
        problems.append("no firmware version string found")
    for v in sorted(versions):
        if not v.endswith(b"-rel"):
            problems.append(f"version {v.decode()} is not a release profile build")
    for marker, what in DEV_MARKERS.items():
        if marker in data:
            problems.append(f"development aid still present: {what}")
    result = {"image": sys.argv[1], "versions": [v.decode() for v in sorted(versions)], "problems": problems,
              "result": "PASS (configuration only)" if not problems else "FAIL",
              "status": "RF Verification Pending"}
    import json
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
