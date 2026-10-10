#!/usr/bin/env python3
"""Compares the Route B drawing shim (firmware/components/exp_surface) with the official engine.

Both render the same scripted UI states from the same synthetic sensor source. Pixels are compared in
RGB565. Host only: proves the shim draws like the engine, says nothing about the device."""
from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from host_runtime import PROJECT, Sim  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = PROJECT.parents[1]
STEPS = 7


def run_shim(outdir: Path, swap: bool = False) -> list[bytes]:
    outdir.mkdir(parents=True, exist_ok=True)
    exe = outdir / "surface_equivalence"
    srcs = sorted(str(p) for p in (PROJECT / "main").glob("exp_*.c"))
    cmd = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DMOSAICO_HOST_SIMULATION=1",
           *(["-DEXP_SURFACE_SWAP_BYTES=1"] if swap else []),
           "-I", str(ROOT / "firmware/components/exp_surface/include"), "-I", str(PROJECT / "main"),
           "-I", str(PROJECT / "host"), str(PROJECT / "tests/surface_equivalence.c"),
           str(PROJECT / "host/exp_sim_source.c"), str(ROOT / "firmware/components/exp_surface/exp_surface.c"),
           *srcs, "-lm", "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(outdir)], check=True, capture_output=True)
    return [(outdir / f"frame_{i:02d}.rgb565").read_bytes() for i in range(STEPS)]


def run_engine() -> list[bytes]:
    s = Sim()
    frames = []

    def shot() -> None:
        im = Image.open(io.BytesIO(s.png())).convert("RGB")
        out = bytearray()
        for r, g, b in im.getdata():
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out += v.to_bytes(2, "little")
        frames.append(bytes(out))

    s.heap_demo(False)
    s.goto("home"); s.tick(15); shot()
    s.attitude(18, -22, 40); s.tick(45); s.goto("motion"); s.tick(45); shot()
    s.mic("voice", -24); s.goto("audio"); s.tick(45); shot()
    s.mic("quiet", -22); s.goto("game"); s.tick(15); shot()
    s.game_start(0); s.tick(120); shot()
    s.modules("joystick", "interact"); s.battery(True, 14, True); s.goto("device"); s.tick(15); shot()
    s.goto("home"); s.tick(4); shot()
    s.close()
    return frames


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        a = run_shim(Path(d) / "plain")
        sw = run_shim(Path(d) / "swapped", swap=True)
    b = run_engine()
    report = []
    worst = 0
    for i, (x, y) in enumerate(zip(a, b)):
        n = len(x) // 2
        diff = sum(1 for k in range(0, len(x), 2) if x[k:k + 2] != y[k:k + 2])
        report.append({"frame": i, "pixels": n, "different": diff})
        worst = max(worst, diff)
    # The device build stores the panel's big-endian order: every pixel must be the plain pixel, bytes swapped.
    swap_bad = 0
    for x, y in zip(a, sw):
        swap_bad += sum(1 for k in range(0, len(x), 2) if x[k:k + 2] != y[k + 1:k - 1 if k else None:-1][:2])
    print(json.dumps({"scope": "Host: Route B shim vs official engine, synthetic data", "frames": report,
                      "worst_different_pixels": worst, "byte_swapped_build_wrong_pixels": swap_bad}, indent=2))
    return 0 if worst == 0 and swap_bad == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
