#!/usr/bin/env python3
"""Interactive Host preview for ESP-Mosaico Experience.

Serves a 480x480 canvas (mouse = touch) plus a developer panel that drives the
*synthetic* sensor source. It binds to 127.0.0.1 only, opens no outbound
connection, and never touches hardware, serial ports or radios.

    python3 tools/preview_server.py [--port 8460]
"""
from __future__ import annotations

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from host_runtime import DBG_LAYOUT, DBG_LONG_TEXT, MIC, MOD, PAGE, TICK_HZ, Sim

HERE = Path(__file__).resolve().parent


class Preview:
    def __init__(self) -> None:
        self.sim = Sim()
        self.lock = self.sim.lock
        self.speed = 1
        self.paused = False
        self.effects: list[list[int]] = []
        self.ticks = 0
        self.started = time.monotonic()
        self.stop = False
        threading.Thread(target=self.loop, daemon=True).start()

    def loop(self) -> None:
        deadline = time.monotonic()
        while not self.stop:
            if self.paused:
                time.sleep(0.02)
                deadline = time.monotonic()
                continue
            with self.lock:
                self.sim.tick(self.speed)
                self.ticks += self.speed
                fx = self.sim.effects()
            if fx and self.speed == 1:
                self.effects += [list(e) for e in fx]
                self.effects = self.effects[-64:]
            deadline += 1.0 / TICK_HZ
            time.sleep(max(0.0, deadline - time.monotonic()))
            if time.monotonic() - deadline > 0.5:
                deadline = time.monotonic()

    def control(self, c: dict) -> None:
        s = self.sim
        kind = c.get("cmd")
        with self.lock:
            if kind == "attitude": s.attitude(float(c["pitch"]), float(c["roll"]), float(c["yaw"]))
            elif kind == "flip": s.flip(bool(c["on"]))
            elif kind == "shake": s.shake()
            elif kind == "spin": s.spin(bool(c["on"]))
            elif kind == "mic": s.mic(c["mode"], float(c.get("db", -22)))
            elif kind == "modules": s.modules(c["left"], c["right"])
            elif kind == "battery": s.battery(bool(c["present"]), int(c["pct"]), bool(c["charging"]))
            elif kind == "imu_fault": s.imu_fault(bool(c["on"]))
            elif kind == "mag": s.mag_present(bool(c["on"]))
            elif kind == "joystick": s.joystick(float(c["x"]), float(c["y"]))
            elif kind == "goto": s.goto(c["page"])
            elif kind == "game_start": s.game_start(int(c["diff"]), bool(c.get("demo")))
            elif kind == "heap_demo": s.heap_demo(bool(c["on"]))
            elif kind == "debug":
                flags = (DBG_LAYOUT if c.get("layout") else 0) | (DBG_LONG_TEXT if c.get("long_text") else 0)
                s.debug(flags)
            elif kind == "speed": self.speed = max(1, min(30, int(c["value"])))
            elif kind == "pause": self.paused = bool(c["on"])
            elif kind == "step": s.tick(1)
            elif kind == "pointer":
                (s.down if c["down"] else s.up)(int(c["x"]), int(c["y"]))
            else:
                raise ValueError(f"unknown command {kind!r}")

    def snapshot(self) -> dict:
        with self.lock:
            st = self.sim.state()
        for k in ("raster",):
            st.pop(k, None)
        st["preview"] = {"speed": self.speed, "paused": self.paused, "ticks": self.ticks,
                         "logic_hz": self.ticks / max(0.001, time.monotonic() - self.started)}
        return st


def make_handler(pv: Preview):
    page = (HERE / "preview.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def _send(self, body: bytes, ctype: str, status: int = 200) -> None:
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802
            path = self.path.split("?", 1)[0]
            if path == "/frame.png":
                with pv.lock:
                    body = pv.sim.png()
                self._send(body, "image/png")
            elif path == "/state":
                self._send(json.dumps(pv.snapshot()).encode(), "application/json")
            elif path == "/effects":
                fx, pv.effects = pv.effects, []
                self._send(json.dumps(fx).encode(), "application/json")
            elif path == "/favicon.ico":
                self._send(b"", "image/x-icon", 204)
            elif path in ("/", "/index.html"):
                self._send(page, "text/html; charset=utf-8")
            else:
                self._send(b"not found", "text/plain", 404)

        def do_POST(self) -> None:  # noqa: N802
            if self.path != "/ctl":
                self._send(b"not found", "text/plain", 404)
                return
            try:
                length = min(int(self.headers.get("Content-Length", "0")), 65536)
                pv.control(json.loads(self.rfile.read(length) or b"{}"))
                self._send(b"{}", "application/json")
            except (ValueError, KeyError, TypeError) as e:
                self._send(json.dumps({"error": str(e)}).encode(), "application/json", 400)

        def log_message(self, fmt: str, *args: object) -> None:
            return

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8460)
    args = ap.parse_args()
    pv = Preview()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(pv))
    print(json.dumps({"preview_url": f"http://127.0.0.1:{args.port}/", "note": "synthetic sensors, localhost only"}), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    pv.stop = True
    pv.sim.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
