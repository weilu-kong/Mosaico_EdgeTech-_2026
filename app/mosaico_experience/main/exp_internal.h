// SPDX-License-Identifier: Apache-2.0
// Private state shared by the app core and its pages. Not a public interface.
#pragma once

#include "exp_app.h"
#include "exp_audio.h"
#include "exp_fusion.h"
#include "exp_gfx.h"
#include "exp_maze.h"
#include "exp_strings.h"

/* Button ids. Tabs use TAB_BASE + page. Page buttons use small positive ids. */
enum {
    BTN_NONE = 0,
    BTN_AUTO_DEMO = 1,
    BTN_HOME_A, BTN_HOME_B, BTN_HOME_C,
    BTN_YAW_RESET, BTN_MAG_CAL,
    BTN_SPK_TEST,
    BTN_DIFF_0, BTN_DIFF_1, BTN_DIFF_2,
    BTN_IN_TILT, BTN_IN_JOY, BTN_IN_TOUCH,
    BTN_START, BTN_EXIT_GAME, BTN_RETRY, BTN_TO_MENU,
    TAB_BASE = 100,
};

typedef enum { UI_BTN_PRIMARY, UI_BTN_SECONDARY, UI_BTN_SEGMENT, UI_BTN_CHIP, UI_BTN_ROW } ui_btn_style_t;

typedef struct {
    int id;
    exp_rect_t r;
    bool enabled;
    bool selected;
    ui_btn_style_t style;
} exp_btn_t;

#define EXP_MAX_BTN 20
#define EXP_PTR_QUEUE 8

typedef struct {
    int x, y;
    uint8_t kind;  /* 0 down, 1 move, 2 up */
} ptr_event_t;

typedef enum { INPUT_TILT = 0, INPUT_JOY, INPUT_TOUCH } game_input_t;

struct exp_app {
    exp_app_config_t cfg;
    /* time */
    uint32_t now_ms;
    uint64_t tick;
    float dt;
    /* inputs / derived */
    exp_sensors_t s;
    bool have_sensors;
    bool imu_ok, mic_ok, mag_ok;
    exp_fusion_t fusion;
    exp_magcal_t magcal;
    exp_motion_t motion;
    exp_audio_t audio;
    float gyro_bias[3];          /* dps, learned while the board is at rest       */
    float still_s;               /* seconds the board has been at rest            */
    float pitch, roll, yaw;
    bool yaw_absolute;           /* magnetometer in use after calibration        */
    float mag_field_ut;
    float mag_cal_v[3];      /* diagnostics: field after calibration (UI frame, uT)                     */
    float mag_head_deg;      /* diagnostics: heading of the top edge from magnetometer + gravity only   */
    bool mag_head_ok;
    bool mag_disturbed;   /* calibrated field magnitude far from the calibration radius: the magnetometer is ignored */
    float dsp_us;                /* smoothed audio DSP duration (0 = unknown)    */
    /* offline voice control (stage 3e): only what the platform reported */
    bool voice_seq_init;
    uint16_t voice_seq_seen;
    bool voice_listening;        /* the command window is open                    */
    exp_voice_cmd_t voice_last_cmd;
    uint8_t voice_last_prob;     /* percent                                       */
    bool voice_last_ignored;     /* the last command arrived during a game round  */
    float voice_flash_s;         /* highlight of the voice row after an event     */
    float event_glow[3];         /* tilt / flip / shake chip highlight (0..1)    */
    float spk_playing_s;         /* speaker test indicator                       */
    /* pages */
    exp_page_t page, prev_page;
    float trans;                 /* 1 = settled, 0..1 while sliding              */
    int trans_dir;               /* +1: new page enters from the right           */
    bool attract;
    int attract_step;
    uint32_t attract_step_ms;
    uint32_t last_activity_ms;
    uint32_t motion_active_ms;
    uint32_t page_enter_ms;
    /* pointer */
    ptr_event_t pq[EXP_PTR_QUEUE];
    int pq_n;
    bool ptr_down;
    int ptr_x, ptr_y;
    int press_id;
    float ripple_x, ripple_y, ripple_age;
    /* game */
    exp_maze_t maze;
    int sel_diff;
    game_input_t input_mode;     /* what the visitor selected                    */
    game_input_t play_input;     /* what the current round actually uses         */
    float result_age_s;
    float go_banner_s;
    float touch_ax, touch_ay;
    /* effects */
    exp_effect_t fx[EXP_EFFECT_QUEUE];
    int fx_n;
    uint32_t last_haptic_ms;
    /* debug / layout */
    uint32_t debug_flags;
    exp_layout_report_t layout;
};

/* app core helpers (exp_app.c) */
void app_effect(exp_app_t *app, exp_effect_type_t type, uint16_t arg);
void app_haptic(exp_app_t *app, uint16_t ms);
void app_sound(exp_app_t *app, exp_sound_t cue);
void app_goto(exp_app_t *app, exp_page_t page, bool user);
void app_activity(exp_app_t *app);
bool app_game_fullscreen(const exp_app_t *app);
bool app_joystick_present(const exp_app_t *app);
const char *app_module_name(exp_module_t m);

/* ui helpers (exp_ui.c) */
void ui_draw_button(const exp_btn_t *b, const char *label, bool pressed, exp_icon_t icon, bool has_icon);
void ui_chip(int x, int y, const char *text, Color dot, Color text_color, int *out_w);
void ui_tile(int x, int y, int w, int h, const char *label);
void ui_bar(int x, int y, int w, int h, float frac, Color fill);
int ui_hit(const exp_btn_t *btns, int n, int x, int y);
void ui_battery(const exp_app_t *app, int right_x, int cy);

/* pages. layout() fills hit regions for the current state (used by update AND
 * render, so taps work even when no frame was rendered in between). */
int page_home_layout(const exp_app_t *app, exp_btn_t *out);
void page_home_render(exp_app_t *app);
void page_home_button(exp_app_t *app, int id);

int page_motion_layout(const exp_app_t *app, exp_btn_t *out);
void page_motion_update(exp_app_t *app, float dt);
void page_motion_render(exp_app_t *app);
void page_motion_button(exp_app_t *app, int id);

int page_audio_layout(const exp_app_t *app, exp_btn_t *out);
void page_audio_render(exp_app_t *app);
void page_audio_button(exp_app_t *app, int id);

int page_game_layout(const exp_app_t *app, exp_btn_t *out);
void page_game_enter(exp_app_t *app);
void page_game_leave(exp_app_t *app);
void page_game_update(exp_app_t *app, float dt);
void page_game_render(exp_app_t *app);
void page_game_button(exp_app_t *app, int id);
void page_game_pointer(exp_app_t *app, int x, int y, bool down);
game_input_t page_game_input(const exp_app_t *app);

int page_device_layout(const exp_app_t *app, exp_btn_t *out);
void page_device_render(exp_app_t *app);
