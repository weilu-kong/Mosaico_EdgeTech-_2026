#!/usr/bin/env python3
"""Behavioural regression tests for the ESP-Mosaico Experience Host simulator.

Everything runs against the real application C core compiled by the official
Raylib Lite engine Host runtime, driven with *synthetic* sensor data. A pass
means "simulator-verified"; it says nothing about the real device, the real
sensors, the display panel or radio behaviour.

Usage: python3 tests/ui_regression.py [--json out.json]
"""
from __future__ import annotations

import json
import sys
import time
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from host_runtime import PROJECT, Sim  # noqa: E402

RESULTS: list[dict] = []
NOTES: dict[str, object] = {}


def wrap180(a: float) -> float:
    while a > 180: a -= 360
    while a < -180: a += 360
    return a


def check(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def test(fn):
    def run(sim_factory):
        t0 = time.time()
        rec = {"name": fn.__name__, "status": "pass", "detail": ""}
        try:
            fn(sim_factory())
        except Exception as e:  # noqa: BLE001
            rec["status"] = "FAIL"
            rec["detail"] = f"{e}" or traceback.format_exc()
            if not isinstance(e, AssertionError):
                rec["detail"] = traceback.format_exc(limit=3)
        rec["seconds"] = round(time.time() - t0, 1)
        RESULTS.append(rec)
        print(f"[{rec['status']}] {rec['name']} {rec['detail']}", flush=True)
    run.__name__ = fn.__name__
    return run


TAB_Y = 442


def tab_x(i: int) -> int:
    return 16 + i * 90 + 44


@test
def nav_tabs_by_tap(s: Sim):
    for i in range(5):
        s.goto("home" if i != 0 else "motion"); s.seconds(0.4)
        s.tap(tab_x(i), TAB_Y); s.seconds(0.5)
        check(s.state()["page"] == i, f"tab {i} -> page {s.state()['page']}")
    s.close()


@test
def home_rows_and_auto_demo(s: Sim):
    for (y, page) in ((217, 1), (293, 2), (369, 3)):
        s.goto("home"); s.seconds(0.3)
        s.tap(240, y); s.seconds(0.4)
        check(s.state()["page"] == page, f"row y={y} -> {s.state()['page']}")
    s.goto("home"); s.seconds(0.3)
    s.tap(384, 152); s.seconds(1.0)
    st = s.state()
    check(st["attract"], "auto demo button must start the showcase")
    s.tap(240, 200); s.seconds(0.5)
    st = s.state()
    check(not st["attract"] and st["page"] == 0, "first touch must end the showcase and return Home")
    s.close()


@test
def slide_off_cancels_press(s: Sim):
    s.goto("home"); s.seconds(0.3)
    s.down(tab_x(2), TAB_Y); s.tick(3)
    s.down(240, 200); s.tick(3)       # finger slides away from the tab
    s.up(240, 200); s.seconds(0.5)
    check(s.state()["page"] == 0, "a press that slid off the button must not navigate")
    s.close()


@test
def attitude_matches_ground_truth(s: Sim):
    s.goto("motion"); s.seconds(0.5)
    worst = 0.0
    for p, r in ((30, 0), (0, 25), (-20, 15), (35, -30), (0, 0)):
        s.attitude(p, r, 0); s.seconds(3.0)
        st = s.state()
        tp, tr, _ = s.true_attitude()
        err = max(abs(st["pitch"] - tp), abs(st["roll"] - tr))
        worst = max(worst, err)
        check(err < 2.5, f"pitch/roll error {err:.1f} deg at ({p},{r}): shown {st['pitch']},{st['roll']} truth {tp:.1f},{tr:.1f}")
    NOTES["attitude_worst_error_deg"] = round(worst, 2)
    s.close()


@test
def relative_yaw_and_reset(s: Sim):
    s.goto("motion"); s.attitude(0, 0, 0); s.seconds(2.0)
    s.attitude(0, 0, 60); s.seconds(2.5)
    st = s.state()
    check(not st["yaw_absolute"], "yaw must be relative before magnetometer calibration")
    check(abs(st["yaw"] - 60) < 6, f"relative yaw {st['yaw']} should follow 60 deg")
    s.tap(132, 170); s.seconds(0.5)
    check(abs(s.state()["yaw"]) < 4, f"tap on the board must reset relative yaw ({s.state()['yaw']})")
    s.attitude(0, 0, 100); s.seconds(2.5)
    check(abs(s.state()["yaw"] - 40) < 6, f"yaw after reset {s.state()['yaw']} should be ~40")
    s.close()


@test
def magnetometer_calibration_and_heading(s: Sim):
    s.goto("motion"); s.attitude(0, 0, 0); s.seconds(1.0)
    check(not s.state()["magcal_done"], "must start uncalibrated")
    s.tap(232, 350); s.seconds(0.3)
    check(s.state()["magcal_running"], "tapping the compass tile must start calibration")
    s.spin(True)
    t = 0.0
    while t < 40 and not s.state()["magcal_done"]:
        s.seconds(0.5); t += 0.5
    check(s.state()["magcal_done"], "calibration did not complete within 40 s of a full sweep")
    NOTES["magcal_seconds"] = t
    s.spin(False)
    errs = []
    merrs = []
    for h in (0, 90, -90, 170, -45):
        s.attitude(8, -6, h); s.seconds(4.0)
        st = s.state()
        check(st["yaw_absolute"], "after calibration yaw must be magnetic heading")
        errs.append(abs(wrap180(st["yaw"] - h)))
        check(st["mag_head_ok"], "mag-only heading diagnostic must be available after calibration")
        merrs.append(abs(wrap180(st["mag_head"] - h)))
    NOTES["heading_errors_deg"] = [round(e, 1) for e in errs]
    NOTES["mag_only_heading_errors_deg"] = [round(e, 1) for e in merrs]
    check(max(errs) < 12, f"heading errors too large: {errs}")
    check(max(merrs) < 12, f"mag-only heading diagnostic disagrees with the simulated truth: {merrs}")
    # A strong foreign field (magnet, motor, steel): the magnetometer must be left out, then taken back when it is gone.
    s.attitude(0, 0, 0); s.seconds(4.0)
    check(not s.state()["mag_disturbed"], "no disturbance expected before the foreign field appears")
    s.mag_disturb(True); s.seconds(1.0)
    st = s.state()
    check(st["mag_disturbed"], "a foreign field far from the calibration radius must be reported as a disturbance")
    check(not st["yaw_absolute"], "while the field is disturbed the yaw must not claim to be a magnetic heading")
    s.mag_disturb(False); s.seconds(6.0)
    st = s.state()
    check(not st["mag_disturbed"], "the disturbance flag must clear once the field is plausible again")
    check(st["yaw_absolute"], "magnetic heading must resume after the disturbance")
    check(abs(wrap180(st["yaw"] - 0)) < 12, f"heading after the disturbance is off: {st['yaw']}")
    s.close()


@test
def motion_events(s: Sim):
    s.goto("motion"); s.attitude(0, 0, 0); s.seconds(2.0)
    c0 = s.state()
    s.shake(); s.seconds(2.0)
    check(s.state()["shake_count"] > c0["shake_count"], "shake not detected")
    s.attitude(0, 0, 0); s.seconds(1.5)
    c1 = s.state()
    s.attitude(40, 0, 0); s.seconds(2.0)
    check(s.state()["tilt_count"] > c1["tilt_count"], "tilt not detected")
    s.attitude(0, 0, 0); s.seconds(1.5)
    c2 = s.state()
    s.flip(True); s.seconds(2.0)
    check(s.state()["flip_count"] > c2["flip_count"], "flip not detected")
    s.close()


@test
def audio_vad_modes(s: Sim):
    s.goto("audio")
    s.mic("quiet"); s.seconds(2.0)
    check(not s.state()["vad"], "VAD must be off in a quiet room")
    s.mic("voice", -24); s.seconds(2.5)
    check(s.state()["vad"], f"VAD must detect voice-like signal (ratio {s.state()['vad_voice_ratio']})")
    s.mic("noise", -30); s.seconds(2.5)
    check(not s.state()["vad"], f"VAD must reject broadband noise (ratio {s.state()['vad_voice_ratio']})")
    s.mic("quiet"); s.seconds(2.0)
    check(not s.state()["vad"], "VAD must release after the signal stops")
    s.mic("off"); s.seconds(0.5)
    st = s.state()
    check(not st["mic_ok"], "mic absence must be reported")
    s.mic("quiet")
    s.close()


@test
def speaker_test_effect(s: Sim):
    s.goto("audio"); s.seconds(0.3); s.effects()
    s.tap(418, 334); s.seconds(0.3)
    fx = s.effects()
    check(any(t == 2 and a == 7 for t, a, _ in fx), f"speaker test must queue the test tone cue, got {fx}")
    check(all(a <= 100 for t, a, _ in fx if t == 1), "haptic pulses must be capped at 100 ms")
    s.close()


@test
def game_menu_controls(s: Sim):
    s.goto("game"); s.seconds(0.3)
    for i in range(3):
        s.tap(16 + i * 151 + 73, 266); s.seconds(0.2)
        check(s.state()["maze_n"] == (7, 9, 11)[i], f"difficulty {i} -> maze {s.state()['maze_n']}")
    s.tap(86, 336); s.seconds(0.2); check(s.state()["input"] == 0, "tilt input")
    s.tap(413, 336); s.seconds(0.2); check(s.state()["input"] == 2, "touch input")
    s.tap(259, 336); s.seconds(0.2); check(s.state()["input"] == 2, "joystick input must stay disabled without the module")
    s.close()


@test
def game_tilt_direction(s: Sim):
    s.attitude(0, 0, 0); s.goto("game"); s.seconds(0.3)
    s.tap(240, 386); s.seconds(3.0)
    check(s.state()["maze_phase"] == 2, f"expected PLAYING, got {s.state()['maze_phase']}")
    st0 = s.state()
    s.attitude(0, 28, 0); s.seconds(0.8)
    st1 = s.state()
    s.attitude(28, 0, 0); s.seconds(0.8)
    st2 = s.state()
    dx = st1["ball_x"] - st0["ball_x"]
    dy = st2["ball_y"] - st1["ball_y"]
    check(dx > 0.5 or st1["hits"] > st0["hits"], f"right edge down must roll right (dx={dx}, hits {st0['hits']}->{st1['hits']})")
    check(dy > 0.5 or st2["hits"] > st1["hits"], f"top edge up must roll toward the bottom (dy={dy})")
    s.attitude(0, -28, 0); s.seconds(1.2)
    s.attitude(0, 0, 0)
    s.close()


@test
def game_touch_control(s: Sim):
    s.goto("game"); s.seconds(0.3)
    s.tap(413, 336); s.tap(240, 386); s.seconds(3.0)
    st0 = s.state()
    check(st0["maze_phase"] == 2, "must be playing")
    ox = (480 - st0["maze_n"] * (400 // st0["maze_n"])) // 2
    s.down(int(ox + st0["ball_x"] + 140), 62 + int(st0["ball_y"])); s.seconds(0.9)
    st1 = s.state()
    s.up(0, 0)
    check(st1["ball_x"] > st0["ball_x"] + 1 or st1["hits"] > st0["hits"], "dragging right of the ball must move it right")
    s.close()


@test
def game_bot_completes_all_difficulties(s: Sim):
    out = {}
    for d in range(3):
        s.game_start(d, demo=True)
        t = 0.0
        while t < 70 and s.state()["maze_phase"] in (1, 2):
            s.seconds(0.5); t += 0.5
        st = s.state()
        out[d] = {"phase": st["maze_phase"], "score": st["score"], "seconds": t, "falls": st["falls"], "hits": st["hits"]}
        check(st["maze_phase"] == 3, f"bot did not reach the goal on difficulty {d}: {out[d]}")
    NOTES["bot_runs"] = out
    s.close()


@test
def game_result_buttons_and_auto_return(s: Sim):
    s.goto("game"); s.seconds(0.3)
    s.tap(240, 386)
    s.seconds(66.0)                        # no input: time runs out
    st = s.state()
    check(st["maze_phase"] == 4, f"expected TIMEUP, got {st['maze_phase']}")
    s.tap(156, 328); s.seconds(0.4)
    check(s.state()["maze_phase"] == 1, "Retry must restart with a countdown")
    s.seconds(66.0)
    check(s.state()["maze_phase"] == 4, "second round should time out too")
    s.tap(324, 328); s.seconds(0.4)
    check(s.state()["maze_phase"] == 0, "Menu button must return to the menu")
    s.tap(240, 386); s.seconds(66.0)
    s.seconds(16.0)
    check(s.state()["maze_phase"] == 0, "result screen must auto-return to the menu after 15 s")
    s.close()


@test
def game_exit_button(s: Sim):
    s.goto("game"); s.seconds(0.3)
    s.tap(240, 386); s.seconds(3.2)
    check(s.state()["maze_phase"] == 2, "playing")
    s.tap(446, 28); s.seconds(0.4)
    st = s.state()
    check(st["maze_phase"] == 0 and st["page"] == 3, "exit button must return to the game menu")
    s.close()


@test
def idle_return_and_attract_tour(s: Sim):
    s.attitude(0, 0, 0)
    s.goto("motion"); s.seconds(1.0)
    s.tap(240, 100); s.seconds(0.2)          # an interaction starts the idle timer
    s.seconds(44.0)
    check(s.state()["page"] == 1, "must stay on the page before 45 s")
    s.seconds(2.5)
    check(s.state()["page"] == 0, "idle for 45 s must return Home")
    check(not s.state()["attract"], "showcase must not start the instant Home is reached")
    s.seconds(29.0)
    check(not s.state()["attract"], "showcase must wait ~30 s on Home")
    s.seconds(3.0)
    check(s.state()["attract"], "showcase must start after 30 s idle on Home")
    seq, last = [], None
    for _ in range(int(150 * 2)):
        s.seconds(0.5)
        st = s.state()
        if st["page"] != last:
            seq.append(st["page"]); last = st["page"]
        s.effects()
        if len(seq) >= 6: break
    NOTES["attract_page_sequence"] = seq
    check(seq[:5] == [1, 2, 3, 0, 1], f"showcase must tour Motion, Audio, Game, Home: {seq}")
    st = s.state()
    s.tap(100, 300); s.seconds(0.6)
    st = s.state()
    check(not st["attract"] and st["page"] == 0, "touch must end the showcase")
    s.close()


@test
def attract_is_silent(s: Sim):
    s.goto("home"); s.tap(384, 152); s.seconds(0.5); s.effects()
    seen = []
    for _ in range(80):
        s.seconds(1.0)
        seen += s.effects()
    check(not seen, f"the showcase must not play sounds or vibrate: {seen[:4]}")
    s.close()


@test
def module_and_fault_states(s: Sim):
    s.goto("game"); s.seconds(0.3)
    s.imu_fault(True); s.seconds(0.5)
    check(not s.state()["imu_ok"], "IMU fault must be reported")
    s.tap(86, 336); s.seconds(0.2)
    s.tap(240, 386); s.seconds(3.2)
    check(s.state()["input"] != 0, "tilt input must not be selectable while the IMU is down")
    s.tap(446, 28); s.seconds(0.3)
    s.imu_fault(False); s.seconds(0.5)
    check(s.state()["imu_ok"], "IMU recovery must be detected")
    s.tap(324, 336); s.seconds(0.2)
    check(s.state()["input"] != 3, "camera input must stay disabled without the camera module")
    s.modules("joystick", "none"); s.joystick(0.0, 0.0); s.seconds(0.3)
    s.tap(259, 336); s.seconds(0.2)
    check(s.state()["input"] == 1, "joystick input must be selectable with the module present")
    s.tap(240, 386); s.seconds(3.2)
    st0 = s.state()
    s.joystick(1.0, 0.0); s.seconds(0.8)
    s.joystick(0.0, 0.0)
    st1 = s.state()
    check(st1["ball_x"] > st0["ball_x"] + 1 or st1["hits"] > st0["hits"], "joystick right must roll the ball right")
    s.joy_calibrating(True); s.seconds(0.6)
    check(s.state()["input"] != 1, "joystick input must not stay in use while the module is still calibrating (no axis data yet)")
    s.joy_calibrating(False); s.seconds(0.3)
    s.tap(446, 28); s.seconds(0.3)
    # Camera module in the left slot: enables face-tracking control (INPUT_FACE == 3)
    s.modules("camera", "none"); s.face(1, 140, 140, 200, 200, 88, 46); s.seconds(0.3)
    s.tap(324, 336); s.seconds(0.2)
    st_cam = s.state()
    check(st_cam["input"] == 3 and st_cam["face_ok"] and st_cam["face_found"], f"camera input must be selectable and active with camera module: {st_cam}")
    s.tap(240, 386); s.seconds(3.2)
    stf0 = s.state()
    s.face(1, 240, 140, 200, 200, 88, 46); s.seconds(0.8)  # face center at x=340 (+100 px right of 240)
    stf1 = s.state()
    check(stf1["ball_x"] > stf0["ball_x"] + 1 or stf1["hits"] > stf0["hits"], "moving face right of centre must roll the ball right")
    s.modules("none", "none"); s.seconds(0.3)
    check(s.state()["input"] != 3, "unplugging camera module must fall back from camera input")
    s.close()


@test
def voice_control(s: Sim):
    s.goto("home"); s.seconds(0.5)
    st = s.state()
    check(st["ai"] == 0, "the engine must start as 'not integrated' unless the platform says otherwise")
    s.voice_wake(); s.seconds(0.3)
    check(not s.state()["voice_listening"], "a wake event of an engine that is not ready must be ignored")
    s.voice_command(4); s.seconds(0.8)
    check(s.state()["page"] == 0, "a command of an engine that is not ready must not navigate")
    s.voice_state(1); s.seconds(0.3)
    s.voice_command(4); s.seconds(0.3)                      # arrives while still loading
    s.voice_state(2); s.seconds(0.8)
    check(s.state()["page"] == 0, "an event from before 'ready' must not be replayed afterwards")
    check(s.state()["ai"] == 2, "ready state must reach the app")
    s.effects()
    s.voice_wake(); s.seconds(0.3)
    check(s.state()["voice_listening"], "a wake word opens the listening state")
    check(len(s.effects()) >= 1, "a wake word must give feedback (cue / pulse)")
    s.voice_command(2, 91); s.seconds(0.9)
    st = s.state()
    check(st["page"] == 1 and st["voice_cmd"] == 2 and st["voice_prob"] == 91 and not st["voice_listening"], f"'motion' must open Motion: {st}")
    for cmd, page in ((3, 2), (4, 3), (5, 4), (1, 0)):
        s.voice_command(cmd); s.seconds(0.9)
        check(s.state()["page"] == page, f"command {cmd} must open page {page}")
    s.voice_command(0); s.seconds(0.4)                       # unknown command id: nothing happens
    check(s.state()["page"] == 0, "command none must do nothing")
    s.voice_timeout(); s.seconds(0.2)
    check(not s.state()["voice_listening"], "a timeout closes the listening state")
    # the page shows the last command and the state without overlaps
    s.goto("audio"); s.seconds(0.6)
    s.voice_wake(); s.seconds(0.3); s.png()
    st = s.state()
    check(st["layout"]["text_overlaps"] == 0 and st["layout"]["truncated"] == 0, f"voice row must not overlap or truncate: {st['layout']}")
    s.voice_command(3); s.seconds(0.6); s.png()
    st = s.state()
    check(st["layout"]["text_overlaps"] == 0 and st["layout"]["truncated"] == 0 and st["layout"]["missing_glyphs"] == 0, f"{st['layout']}")
    # a running game round is never interrupted by voice
    s.goto("game"); s.seconds(0.5); s.tap(240, 386); s.seconds(3.5)
    check(s.state()["maze_phase"] != 0, "round must be running")
    s.voice_command(1); s.seconds(0.8)
    st = s.state()
    check(st["page"] == 3 and st["voice_ignored"], "voice must not leave a running round")
    # a voice command ends the showcase like a touch
    s.goto("home"); s.mic("off"); s.seconds(34.0)
    check(s.state()["attract"], "showcase must be running for this check")
    s.voice_wake(); s.seconds(0.6)
    st = s.state()
    check(not st["attract"] and st["page"] == 0, "a wake word must end the showcase and show Home")
    # error state
    s.voice_state(3); s.seconds(0.3); s.png()
    st = s.state()
    check(st["ai"] == 3, "error state must reach the app")
    s.voice_command(4); s.seconds(0.5)
    check(s.state()["page"] != 3, "an errored engine cannot navigate")
    s.close()


@test
def determinism(s: Sim):
    def script(sim: Sim):
        sim.goto("motion"); sim.attitude(20, -10, 30); sim.seconds(2.0)
        sim.mic("voice", -24); sim.goto("audio"); sim.seconds(1.5)
        sim.goto("game"); sim.tap(240, 386); sim.seconds(5.0)
        return sim.png(), sim.state()["frames"]
    a_png, _ = script(s)
    s.close()
    s2 = Sim()
    b_png, _ = script(s2)
    s2.close()
    check(a_png == b_png, "identical input scripts must render identical frames")


def main() -> int:
    out = None
    if "--json" in sys.argv:
        out = Path(sys.argv[sys.argv.index("--json") + 1])
    t0 = time.time()
    # One compiled library per test keeps the tests independent.
    import host_runtime
    cache = {}

    def factory():
        return Sim()
    tests = [v for k, v in globals().items() if callable(v) and getattr(v, "__name__", "") in (
        "nav_tabs_by_tap", "home_rows_and_auto_demo", "slide_off_cancels_press", "attitude_matches_ground_truth",
        "relative_yaw_and_reset", "magnetometer_calibration_and_heading", "motion_events", "audio_vad_modes",
        "speaker_test_effect", "game_menu_controls", "game_tilt_direction", "game_touch_control",
        "game_bot_completes_all_difficulties", "game_result_buttons_and_auto_return", "game_exit_button",
        "idle_return_and_attract_tour", "attract_is_silent", "module_and_fault_states", "voice_control", "determinism")]
    for t in tests:
        t(factory)
    failed = [r for r in RESULTS if r["status"] != "pass"]
    summary = {"total": len(RESULTS), "passed": len(RESULTS) - len(failed), "failed": len(failed),
               "seconds": round(time.time() - t0, 1), "notes": NOTES, "results": RESULTS,
               "scope": "Host simulator with synthetic sensors; not a device or RF result"}
    if out:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("total", "passed", "failed", "seconds", "notes")}, indent=2, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
