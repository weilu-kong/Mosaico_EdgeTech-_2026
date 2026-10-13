// SPDX-License-Identifier: Apache-2.0
// ESP-Mosaico Experience - public application interface (platform side).
//
// Threading: one owner calls every function below from the same task/loop
// (the engine's update/render callbacks). Touch/sensor producers on other
// tasks must hand data to that owner (queue or mutex) before calling.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "exp_types.h"
#include "exp_text.h"

typedef struct exp_app exp_app_t;

typedef enum {
    EXP_PAGE_HOME = 0,
    EXP_PAGE_MOTION,
    EXP_PAGE_AUDIO,
    EXP_PAGE_GAME,
    EXP_PAGE_DEVICE,
    EXP_PAGE_COUNT
} exp_page_t;

/* Debug switches used by the visual regression tests; never set on the show floor. */
enum {
    EXP_DBG_LONG_TEXT = 1u << 0,   /* swap dynamic strings for long Japanese variants */
    EXP_DBG_LAYOUT = 1u << 1,      /* record text/shape layout problems               */
};

typedef struct {
    uint32_t (*now_us)(void);   /* optional microsecond clock for DSP timing        */
    uint32_t debug_flags;
} exp_app_config_t;

exp_app_t *exp_app_create(const exp_app_config_t *config);
void exp_app_destroy(exp_app_t *app);

/* Platform -> app. The snapshot is copied; call once per tick before update. */
void exp_app_set_sensors(exp_app_t *app, const exp_sensors_t *sensors);

/* Compass calibration status for the diagnostics (log view / serial log). Read-only. */
typedef struct {
    bool running, done;
    float offset[3];      /* uT, sensor 0 */
    float radius, rms;    /* last sphere fit; rms is relative to the radius */
    unsigned rejects;     /* fits rejected as "not an Earth-field sphere" */
    float field[3];       /* calibrated field, UI frame, uT */
    float head_mag;       /* mag+gravity heading of the top edge, 0..360 (valid if head_ok) */
    bool head_ok;
    float yaw_fused;      /* the app's yaw, -180..180 */
} exp_magcal_status_t;
exp_magcal_status_t exp_app_magcal_status(const exp_app_t *app);
/* Touch: down=true on press and while held, false on release. */
void exp_app_pointer(exp_app_t *app, int x, int y, bool down);
/* Fixed-step update (30 Hz) and full-frame render. */
void exp_app_update(exp_app_t *app, float dt);
void exp_app_render(exp_app_t *app);
/* App -> platform: drain queued haptic/sound requests. Returns count written. */
size_t exp_app_pop_effects(exp_app_t *app, exp_effect_t *out, size_t capacity);

/* ---- Introspection for the Host simulator and tests ------------------------ */
exp_page_t exp_app_page(const exp_app_t *app);
bool exp_app_attract(const exp_app_t *app);
bool exp_app_want_camera(const exp_app_t *app);
const exp_layout_report_t *exp_app_layout(const exp_app_t *app);
int exp_app_state_json(const exp_app_t *app, char *out, size_t capacity);
uint32_t exp_app_state_hash(const exp_app_t *app);
/* Test hooks (Host only): jump straight to a page / game state. */
void exp_app_debug_goto(exp_app_t *app, exp_page_t page);
void exp_app_debug_flags(exp_app_t *app, uint32_t flags);
void exp_app_debug_game_start(exp_app_t *app, int diff, bool demo);
int exp_app_debug_maze_phase(const exp_app_t *app);

