#!/usr/bin/env python3
"""Thin wrapper around the official Raylib Lite engine Host runtime.

The engine (esp-mosaico-vibe/submodule/raylib-lite-engine) compiles our C
sources listed in game.sim.json into a shared library. This module adds the
ctypes bindings for the Host-only simulator controls in host/exp_sim_source.c.
Nothing here talks to hardware, a serial port or the network.
"""
from __future__ import annotations

import contextlib
import ctypes
import importlib.util
import os
import sys
import tempfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]


def engine_root() -> Path:
    env = os.environ.get("MOSAICO_ENGINE_ROOT")
    candidates = [Path(env)] if env else []
    candidates += [
        PROJECT.parents[1] / "workspace/esp-mosaico-vibe/submodule/raylib-lite-engine",
        PROJECT.parent.parent / "esp-mosaico-vibe/submodule/raylib-lite-engine",
        PROJECT.parent / "esp-mosaico-vibe/submodule/raylib-lite-engine",
        PROJECT.parent.parent.parent / "esp-mosaico-vibe/submodule/raylib-lite-engine",
        Path("/home/claude/work/esp-mosaico-vibe/submodule/raylib-lite-engine"),
    ]
    for c in candidates:
        if (c / "host/run_game.py").is_file():
            return c
    raise RuntimeError("raylib-lite-engine not found; set MOSAICO_ENGINE_ROOT")


def _load_engine():
    root = engine_root()
    spec = importlib.util.spec_from_file_location("mosaico_run_game", root / "host/run_game.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["mosaico_run_game"] = module
    spec.loader.exec_module(module)
    return module


# Page ids (exp_page_t), module ids (exp_module_t), mic modes.
PAGE = {"home": 0, "motion": 1, "audio": 2, "game": 3, "device": 4}
MOD = {"none": 0, "joystick": 1, "interact": 2, "buttonled": 3, "camera": 4, "unknown": 5, "notprobed": 6}
MIC = {"off": 0, "quiet": 1, "tone": 2, "voice": 3, "noise": 4, "sweep": 5}
DBG_LONG_TEXT = 1
DBG_LAYOUT = 2
TICK_HZ = 30


@contextlib.contextmanager
def _tolerant_cc():
    """The engine compiles with -Werror, and newer compilers (e.g. Apple clang 21) flag
    -Wmisleading-indentation in UPSTREAM engine sources. We must not edit upstream, so put a tiny
    `cc` wrapper first on PATH that appends -Wno-error=misleading-indentation (that single
    category stays a warning; everything else, including our own code, remains an error)."""
    import shutil
    real = next((v for n in ("cc", "gcc", "clang") if (v := shutil.which(n))), None)
    if real is None or os.name == "nt":
        yield
        return
    with tempfile.TemporaryDirectory(prefix="mosaico-cc-") as d:
        wrapper = Path(d) / "cc"
        wrapper.write_text('#!/bin/sh\nexec "%s" "$@" -Wno-error=misleading-indentation\n' % real)
        wrapper.chmod(0o755)
        old = os.environ.get("PATH", "")
        os.environ["PATH"] = d + os.pathsep + old
        try:
            yield
        finally:
            os.environ["PATH"] = old


class Sim:
    """One compiled instance of the experience app plus its synthetic sensors."""

    def __init__(self, directory: Path | None = None, generation: int = 0) -> None:
        self._tmp = None
        if directory is None:
            self._tmp = tempfile.TemporaryDirectory(prefix="mosaico-exp-")
            directory = Path(self._tmp.name)
        self.engine = _load_engine()
        with _tolerant_cc():
            self.rt = self.engine.GenericHostRuntime(PROJECT, directory, generation)
        api = self.rt.api
        f32 = ctypes.c_float
        api.exp_sim_set_attitude.argtypes = [f32, f32, f32]
        api.exp_sim_set_flip.argtypes = [ctypes.c_int]
        api.exp_sim_spin.argtypes = [ctypes.c_int]
        api.exp_sim_set_mic.argtypes = [ctypes.c_int, f32]
        api.exp_sim_set_modules.argtypes = [ctypes.c_int, ctypes.c_int]
        api.exp_sim_set_battery.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        api.exp_sim_set_imu_fault.argtypes = [ctypes.c_int]
        api.exp_sim_set_mag_present.argtypes = [ctypes.c_int]
        api.exp_sim_set_joystick.argtypes = [f32, f32]
        api.exp_sim_set_heap_demo.argtypes = [ctypes.c_int]
        api.exp_sim_set_joy_calibrating.argtypes = [ctypes.c_int]
        api.exp_sim_set_mag_disturb.argtypes = [ctypes.c_int]
        api.exp_sim_set_voice_state.argtypes = [ctypes.c_int]
        api.exp_sim_voice_event.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        api.exp_sim_set_debug.argtypes = [ctypes.c_uint32]
        api.exp_sim_goto.argtypes = [ctypes.c_int]
        api.exp_sim_game_start.argtypes = [ctypes.c_int, ctypes.c_int]
        api.exp_sim_pop_effects.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
        api.exp_sim_pop_effects.restype = ctypes.c_int
        api.exp_sim_get_attitude.argtypes = [ctypes.POINTER(ctypes.c_float)]
        self.api = api
        self.lock = self.rt.lock

    # -- stepping ---------------------------------------------------------
    def tick(self, n: int = 1) -> None:
        for _ in range(n):
            self.api.mosaico_host_game_update_v1(self.rt.context)
            self.rt.frames += 1

    def seconds(self, s: float) -> None:
        self.tick(int(round(s * TICK_HZ)))

    # -- input ------------------------------------------------------------
    def down(self, x: int, y: int) -> None:
        self.rt.pointer(0, int(x), int(y), True)

    def up(self, x: int, y: int) -> None:
        self.rt.pointer(0, int(x), int(y), False)

    def tap(self, x: int, y: int, hold_ticks: int = 3) -> None:
        self.down(x, y)
        self.tick(hold_ticks)
        self.up(x, y)
        self.tick(2)

    # -- controls ---------------------------------------------------------
    def attitude(self, pitch=0.0, roll=0.0, yaw=0.0) -> None:
        self.api.exp_sim_set_attitude(pitch, roll, yaw)

    def flip(self, on=True) -> None: self.api.exp_sim_set_flip(1 if on else 0)
    def shake(self) -> None: self.api.exp_sim_shake()
    def spin(self, on=True) -> None: self.api.exp_sim_spin(1 if on else 0)
    def mic(self, mode="quiet", db=-22.0) -> None: self.api.exp_sim_set_mic(MIC[mode] if isinstance(mode, str) else mode, db)
    def modules(self, left="none", right="none") -> None: self.api.exp_sim_set_modules(MOD[left], MOD[right])
    def battery(self, present=True, pct=82, charging=False) -> None: self.api.exp_sim_set_battery(int(present), pct, int(charging))
    def imu_fault(self, on=True) -> None: self.api.exp_sim_set_imu_fault(1 if on else 0)
    def mag_present(self, on=True) -> None: self.api.exp_sim_set_mag_present(1 if on else 0)
    def joystick(self, x=0.0, y=0.0) -> None: self.api.exp_sim_set_joystick(x, y)
    def voice_state(self, ai_state: int) -> None: self.api.exp_sim_set_voice_state(ai_state)   # 0 not integrated, 1 loading, 2 ready, 3 error
    def voice_wake(self, wake_idx=1) -> None: self.api.exp_sim_voice_event(1, 0, 0, wake_idx)
    def voice_command(self, cmd: int, prob=90) -> None: self.api.exp_sim_voice_event(2, cmd, prob, 0)   # cmd: 1 home 2 motion 3 audio 4 game 5 device
    def voice_timeout(self) -> None: self.api.exp_sim_voice_event(3, 0, 0, 0)
    def mag_disturb(self, on=True) -> None: self.api.exp_sim_set_mag_disturb(1 if on else 0)
    def joy_calibrating(self, on=True) -> None: self.api.exp_sim_set_joy_calibrating(1 if on else 0)
    def heap_demo(self, on=True) -> None: self.api.exp_sim_set_heap_demo(1 if on else 0)
    def debug(self, flags: int) -> None: self.api.exp_sim_set_debug(flags)
    def goto(self, page) -> None: self.api.exp_sim_goto(PAGE[page] if isinstance(page, str) else page)
    def game_start(self, diff=0, demo=False) -> None: self.api.exp_sim_game_start(diff, 1 if demo else 0)

    def effects(self) -> list[tuple[int, int, int]]:
        buf = (ctypes.c_uint32 * 96)()
        n = self.api.exp_sim_pop_effects(buf, 32)
        return [(buf[i * 3], buf[i * 3 + 1], buf[i * 3 + 2]) for i in range(n)]

    def true_attitude(self) -> tuple[float, float, float]:
        out = (ctypes.c_float * 3)()
        self.api.exp_sim_get_attitude(out)
        return out[0], out[1], out[2]

    # -- observation ------------------------------------------------------
    def state(self) -> dict:
        return self.rt.metadata()

    def png(self) -> bytes:
        return self.rt.frame()

    def save(self, path: Path) -> dict:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(self.png())
        return self.state()

    def close(self) -> None:
        self.rt.close()
        if self._tmp:
            self._tmp.cleanup()
