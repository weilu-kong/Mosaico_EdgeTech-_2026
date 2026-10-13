// SPDX-License-Identifier: Apache-2.0
// Host simulator ONLY. Synthetic sensor source for the desktop preview and the
// regression tests. Every snapshot it produces carries EXP_SENS_SIMULATED and
// the UI shows a permanent "SIM" marker. It is never linked into the firmware.
#pragma once

#include <stdint.h>
#include "exp_app.h"

void exp_sim_attach(exp_app_t *app);
void exp_sim_detach(void);
/* Advance the physical model by dt, push a snapshot into the app and run one
 * app update. Queued haptic/sound effects are kept for exp_sim_pop_effects. */
void exp_sim_tick(float dt);

/* ---- Controls (called from the preview server via ctypes) ------------------ */
void exp_sim_set_attitude(float pitch_deg, float roll_deg, float yaw_deg);
void exp_sim_set_flip(int face_down);
void exp_sim_shake(void);
void exp_sim_spin(int on);                      /* figure-eight sweep for mag calibration */
void exp_sim_set_mic(int mode, float level_db); /* 0 absent 1 quiet 2 tone 3 voice-like 4 noise 5 sweep */
void exp_sim_set_modules(int left, int right);  /* exp_module_t */
void exp_sim_set_battery(int present, int pct, int charging);
void exp_sim_set_imu_fault(int fault);
void exp_sim_set_mag_present(int present);
void exp_sim_set_joystick(float x, float y);
void exp_sim_set_mag_disturb(int on);         /* a strong foreign field next to the board (+150 uT on x) */
void exp_sim_set_joy_calibrating(int on);      /* joystick module present but not calibrated yet */
void exp_sim_set_face(int found, int x, int y, int w, int h, int conf_pct, int infer_ms); /* found: -1 starting, 0 none, 1 found */
void exp_sim_set_voice_state(int ai_state);     /* exp_ai_state_t: the simulated speech engine (default: not integrated) */
void exp_sim_voice_event(int event, int cmd, int prob_pct, int wake_idx);   /* exp_voice_event_t / exp_voice_cmd_t */
void exp_sim_set_heap_demo(int on);             /* layout-only filler numbers, off by default */
void exp_sim_set_debug(uint32_t flags);
void exp_sim_goto(int page);
void exp_sim_game_start(int diff, int demo);
int exp_sim_pop_effects(uint32_t *out, int max); /* triplets: type, arg, at_ms */
void exp_sim_get_attitude(float *out3);          /* ground-truth pitch, roll, yaw */

