#!/usr/bin/env python3
"""Render every UI state headlessly and save PNG screenshots + a layout report.

Usage: python3 tools/capture_screens.py [output_dir]
All sensor data is synthetic (Host simulator) and every frame shows the SIM marker.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from host_runtime import DBG_LAYOUT, DBG_LONG_TEXT, PROJECT, Sim  # noqa: E402


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else PROJECT / "evidence/screenshots"
    out.mkdir(parents=True, exist_ok=True)
    report: dict[str, dict] = {}
    s = Sim()

    def shot(name: str, settle: float = 0.5) -> None:
        s.seconds(settle)
        s.png()                      # one render so the layout recorder sees this state
        st = s.save(out / f"{name}.png")
        report[name] = {"layout": st["layout"], "page": st["page"], "attract": st["attract"],
                        "maze_phase": st["maze_phase"]}

    def reset(layout: bool = True, long_text: bool = False) -> None:
        s.debug((DBG_LAYOUT if layout else 0) | (DBG_LONG_TEXT if long_text else 0))

    reset()
    s.heap_demo(False)
    s.goto("home"); shot("home")
    s.attitude(18, -22, 40); s.seconds(1.5)
    s.goto("motion"); shot("motion_tilted", 1.5)
    s.attitude(0, 0, 0); s.flip(True); s.seconds(1.5); shot("motion_flipped", 0.5)
    s.flip(False); s.attitude(0, 0, 0); s.seconds(1.0)
    s.mic("voice", -24); s.goto("audio"); shot("audio_voice", 1.5)
    s.mic("quiet"); shot("audio_quiet", 1.0)
    s.mic("off"); shot("audio_no_mic", 0.5)
    s.mic("quiet")
    # offline voice control states (simulated engine events; the real engine has not run on the board)
    s.voice_state(1); shot("audio_voice_loading", 0.4)
    s.voice_state(2); shot("audio_voice_ready", 0.4)
    s.voice_wake(); shot("audio_voice_listening", 0.3)
    s.voice_command(3, 94); shot("audio_voice_command", 0.5)
    s.voice_state(3); shot("audio_voice_error", 0.4)
    s.voice_state(0); s.goto("audio")
    s.goto("game"); shot("game_menu", 0.5)
    s.game_start(0); shot("game_countdown", 0.4)
    shot("game_playing", 3.0)
    s.game_start(2, demo=True); shot("game_demo_hard", 6.0)
    s.game_start(1, demo=True)
    t = 0.0
    while t < 60 and s.state()["maze_phase"] != 3:
        s.seconds(0.5); t += 0.5
    shot("game_result_success_demo", 1.0)
    s.goto("game"); s.attitude(0, 0, 0); s.game_start(0, demo=False); s.seconds(64.0); shot("game_result_timeup", 1.0)
    s.goto("home"); s.tap(384, 152); shot("attract_bar_motion", 1.5)
    s.seconds(14.0); shot("attract_audio", 1.0)
    s.tap(240, 200); s.seconds(0.5)
    s.heap_demo(True); s.goto("device"); shot("device_sim_filled", 0.5)
    s.heap_demo(False); shot("device_unknown", 0.2)
    s.modules("joystick", "interact"); s.battery(True, 14, True); shot("device_modules", 0.3)
    s.joy_calibrating(True); shot("device_joystick_calibrating", 0.3); s.joy_calibrating(False)
    s.modules("notprobed", "notprobed"); shot("device_modules_not_probed", 0.3)
    s.modules("none", "none"); s.battery(True, 82, False)
    reset(long_text=True); s.modules("camera", "unknown"); shot("device_long_text", 0.3)
    reset(); s.modules("none", "none")
    s.imu_fault(True); s.goto("motion"); shot("motion_imu_fault", 0.5)
    s.goto("game"); shot("game_menu_no_imu", 0.3)
    s.imu_fault(False); s.mag_present(False); s.goto("motion"); shot("motion_no_mag", 1.0)
    s.mag_present(True); s.battery(False)
    s.goto("home"); shot("home_no_battery", 0.3)
    s.battery(True, 82, False)
    s.goto("motion"); s.attitude(0, 0, 0); s.seconds(1.0); s.tap(232, 350); s.spin(True)
    for _ in range(80):
        s.seconds(0.5)
        if s.state()["magcal_done"]:
            break
    s.spin(False); s.attitude(0, 0, 0); s.seconds(3.0); shot("motion_compass_calibrated", 0.5)
    s.mag_disturb(True); shot("motion_compass_disturbed", 1.0); s.mag_disturb(False)
    (out / "layout_report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    bad = {k: v["layout"] for k, v in report.items()
           if any(v["layout"][f] for f in ("text_overlaps", "shape_overlaps", "off_screen", "small_targets", "missing_glyphs"))}
    print(json.dumps({"screens": len(report), "with_problems": bad}, indent=2, ensure_ascii=False))
    s.close()
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
