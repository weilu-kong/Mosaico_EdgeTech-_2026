// SPDX-License-Identifier: Apache-2.0
// Pure helpers of the device I/O layer (stage 3c): microphone sample handling, UI cue synthesis, motor pulse limiting,
// expansion-slot classification and the start-up crash guard. No ESP-IDF, no hardware: everything here is host-tested.
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- Microphone ---------------------------------------------------------------------------------------------------
 * The BSP codec runs the I2S bus as 48 kHz, 16-bit, stereo (the configuration of the official audio_loopback example).
 * The ES8311 ADC is mono, so one of the two slots carries the signal; which one is not documented in the BSP, so the
 * louder slot of every chunk is used (and both peaks are shown in the log view for the first on-device check).
 * The application wants 16 kHz mono: three samples of the chosen slot are averaged (a 3-tap box filter, zero at 16 kHz). */
#define DEV_AUDIO_RATE 48000
#define DEV_MIC_DECIM 3
#define DEV_MIC_OUT_N 256                                 /* samples handed to the application per chunk (16 ms) */
#define DEV_MIC_FRAMES (DEV_MIC_OUT_N * DEV_MIC_DECIM)    /* stereo frames per codec read                          */

typedef struct {
    int peak_l, peak_r;   /* |sample| maximum of each slot in this chunk, 0..32768 */
    uint8_t used;         /* 0 = left slot used, 1 = right slot used               */
} dev_pcm_info_t;

/* st: interleaved L,R int16 frames. Writes frames / 3 mono samples to out, returns that count. */
static inline int dev_pcm_decimate(const int16_t *st, int frames, int16_t *out, dev_pcm_info_t *info)
{
    int64_t e[2] = {0, 0};
    int peak[2] = {0, 0};
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c) {
            const int v = st[2 * i + c];
            e[c] += (int64_t)v * v;
            const int a = v < 0 ? -v : v;
            if (a > peak[c]) peak[c] = a;
        }
    const int use = e[1] > e[0] ? 1 : 0;
    const int n = frames / DEV_MIC_DECIM;
    for (int k = 0; k < n; ++k) {
        int sum = 0;
        for (int j = 0; j < DEV_MIC_DECIM; ++j) sum += st[2 * (k * DEV_MIC_DECIM + j) + use];
        out[k] = (int16_t)(sum / DEV_MIC_DECIM);
    }
    if (info) { info->peak_l = peak[0]; info->peak_r = peak[1]; info->used = (uint8_t)use; }
    return n;
}

/* ---- UI cues -------------------------------------------------------------------------------------------------------
 * Short, quiet triangle-wave beeps. The cue numbers are the values of exp_sound_t (exp_types.h). */
enum { DEV_CUE_TAP = 0, DEV_CUE_NAV, DEV_CUE_SUCCESS, DEV_CUE_FAIL, DEV_CUE_HIT, DEV_CUE_COUNTDOWN, DEV_CUE_GO,
       DEV_CUE_TEST_TONE, DEV_CUE_COUNT };

typedef struct {
    uint16_t f0_hz, f1_hz;   /* start and end frequency of a linear sweep */
    uint16_t ms;
    uint8_t level;           /* percent of the full cue amplitude         */
} dev_tone_seg_t;

#define DEV_CUE_MAX_SEGS 3
#define DEV_TONE_RAMP_FRAMES 96   /* 2 ms attack/release inside every segment: no clicks */

typedef struct {
    uint8_t n;
    dev_tone_seg_t seg[DEV_CUE_MAX_SEGS];
} dev_cue_t;

static inline bool dev_cue_get(int cue, dev_cue_t *out)
{
    static const dev_cue_t k[DEV_CUE_COUNT] = {
        [DEV_CUE_TAP] = {1, {{1800, 1800, 25, 70}}},
        [DEV_CUE_NAV] = {1, {{900, 1300, 40, 70}}},
        [DEV_CUE_SUCCESS] = {2, {{880, 880, 80, 80}, {1320, 1320, 140, 80}}},
        [DEV_CUE_FAIL] = {1, {{330, 180, 220, 90}}},
        [DEV_CUE_HIT] = {1, {{220, 180, 50, 100}}},
        [DEV_CUE_COUNTDOWN] = {1, {{1000, 1000, 80, 80}}},
        [DEV_CUE_GO] = {2, {{1000, 1000, 60, 80}, {1500, 1500, 160, 90}}},
        [DEV_CUE_TEST_TONE] = {1, {{1000, 1000, 400, 80}}},
    };
    if (cue < 0 || cue >= DEV_CUE_COUNT || !out) return false;
    *out = k[cue];
    return true;
}

typedef struct {
    dev_cue_t cue;
    int seg;
    uint32_t pos;     /* frames played inside the current segment */
    uint32_t phase;   /* 32-bit oscillator phase                  */
    bool active;
} dev_tone_t;

static inline bool dev_tone_start(dev_tone_t *t, int cue)
{
    memset(t, 0, sizeof(*t));
    if (!dev_cue_get(cue, &t->cue) || t->cue.n == 0) return false;
    t->active = true;
    return true;
}

/* Renders up to `frames` stereo frames (both slots identical) with peak amplitude `amp` (int16 units). Returns the
 * number of frames written; fewer than requested means the cue has ended (the caller pads with silence). */
static inline int dev_tone_render(dev_tone_t *t, int16_t *stereo, int frames, int amp)
{
    int w = 0;
    while (w < frames && t->active) {
        const dev_tone_seg_t *s = &t->cue.seg[t->seg];
        const uint32_t total = (uint32_t)s->ms * (DEV_AUDIO_RATE / 1000U);
        if (t->pos >= total) {
            if (++t->seg >= t->cue.n) { t->active = false; break; }
            t->pos = 0;
            continue;
        }
        const int32_t df = (int32_t)s->f1_hz - (int32_t)s->f0_hz;
        const uint32_t f = (uint32_t)((int32_t)s->f0_hz + (int32_t)((int64_t)df * (int64_t)t->pos / (int64_t)(total > 1 ? total - 1 : 1)));
        t->phase += (uint32_t)(((uint64_t)f << 32) / DEV_AUDIO_RATE);
        const uint32_t p = t->phase >> 16;
        const int32_t tri = p < 32768U ? -32767 + 2 * (int32_t)p : 98303 - 2 * (int32_t)p;   /* -32767..32767 */
        uint32_t env = DEV_TONE_RAMP_FRAMES;
        if (t->pos < env) env = t->pos;
        const uint32_t left = total - t->pos - 1U;
        if (left < env) env = left;
        const int32_t v = (int32_t)(((int64_t)tri * amp * s->level * (int64_t)env) / (32767LL * 100LL * DEV_TONE_RAMP_FRAMES));
        stereo[2 * w] = stereo[2 * w + 1] = (int16_t)v;
        ++w;
        ++t->pos;
    }
    return w;
}

/* ---- Motor ---------------------------------------------------------------------------------------------------------
 * The battery is small (about 80 mAh) and the motor is a coin cell vibration motor: pulses are short and the on-time
 * in any window is limited. */
#define DEV_HAPTIC_MIN_MS 10U
#define DEV_HAPTIC_MAX_MS 120U
#define DEV_HAPTIC_WINDOW_MS 1000U
#define DEV_HAPTIC_BUDGET_MS 240U   /* at most 24 % duty over a sliding-ish 1 s window */

typedef struct {
    uint32_t win_start_ms;
    uint32_t used_ms;
    bool started;
} dev_haptic_gate_t;

/* Returns the pulse length that may run now (0 = refuse). */
static inline uint16_t dev_haptic_grant(dev_haptic_gate_t *g, uint32_t now_ms, uint16_t req_ms)
{
    if (!g->started || now_ms - g->win_start_ms >= DEV_HAPTIC_WINDOW_MS) {
        g->started = true;
        g->win_start_ms = now_ms;
        g->used_ms = 0;
    }
    uint32_t ms = req_ms > DEV_HAPTIC_MAX_MS ? DEV_HAPTIC_MAX_MS : req_ms;
    const uint32_t room = g->used_ms >= DEV_HAPTIC_BUDGET_MS ? 0U : DEV_HAPTIC_BUDGET_MS - g->used_ms;
    if (ms > room) ms = room;
    if (ms < DEV_HAPTIC_MIN_MS) return 0;
    g->used_ms += ms;
    return (uint16_t)ms;
}

/* ---- Expansion slots -----------------------------------------------------------------------------------------------
 * Board-type numbers are those of mosaico_board_type_t in mosaico_module_mgr.h; presence values are those of
 * mosaico_module_presence_t (0 unknown, 1 absent, 2 present). Anything present that is not one of the four known
 * modules (or whose descriptor does not validate) is "unknown": a module the exhibition has not reviewed. */
typedef enum {
    DEV_SLOT_NOT_PROBED = 0,   /* nothing is known (manager not running, no scan yet) */
    DEV_SLOT_NONE,             /* probed, nothing acknowledged                        */
    DEV_SLOT_JOYSTICK,
    DEV_SLOT_INTERACT,
    DEV_SLOT_BUTTON_LED,
    DEV_SLOT_CAMERA,
    DEV_SLOT_UNKNOWN,
} dev_slot_class_t;

static inline dev_slot_class_t dev_slot_classify(int presence, bool descriptor_valid, int board_type)
{
    if (presence == 1) return DEV_SLOT_NONE;
    if (presence != 2) return DEV_SLOT_NOT_PROBED;
    if (!descriptor_valid) return DEV_SLOT_UNKNOWN;
    switch (board_type) {
    case 0x04: return DEV_SLOT_JOYSTICK;    /* MOSAICO_BOARD_TYPE_HANDLE     */
    case 0x16: return DEV_SLOT_INTERACT;    /* MOSAICO_BOARD_TYPE_INTERACT   */
    case 0x14: return DEV_SLOT_BUTTON_LED;  /* MOSAICO_BOARD_TYPE_BUTTON_LED */
    case 0x07: return DEV_SLOT_CAMERA;      /* MOSAICO_BOARD_TYPE_CAMERA     */
    default: return DEV_SLOT_UNKNOWN;
    }
}

/* ---- Start-up crash guard ------------------------------------------------------------------------------------------
 * The microphone/speaker/motor/slot drivers have not run on this board yet. A driver that crashes or hangs during its
 * initialisation would put the unit into a reboot loop. Each risky step is marked "in flight" in RAM that survives a
 * software/panic/watchdog reset (not a power cycle). If a step was in flight at the previous reset it is skipped from
 * then on, until the next power-on. The guard changes nothing else. */
#define DEV_GUARD_MAGIC 0x47554452U   /* "GUDR" */
#define DEV_GUARD_STEPS 8

enum { DEV_STEP_AUDIO = 0, DEV_STEP_MOTOR, DEV_STEP_SLOTS, DEV_STEP_JOYSTICK, DEV_STEP_VOICE };

typedef struct {
    uint32_t magic;
    uint32_t inflight;                  /* bit per step that was running when the reset happened */
    uint8_t strikes[DEV_GUARD_STEPS];   /* resets caused by the step since the last power-on     */
} dev_guard_t;

static inline void dev_guard_boot(dev_guard_t *g, bool power_on)
{
    if (power_on || g->magic != DEV_GUARD_MAGIC) {
        memset(g, 0, sizeof(*g));
        g->magic = DEV_GUARD_MAGIC;
        return;
    }
    for (int i = 0; i < DEV_GUARD_STEPS; ++i)
        if (((g->inflight >> i) & 1U) != 0U && g->strikes[i] < 255) ++g->strikes[i];
    g->inflight = 0;
}

static inline bool dev_guard_allowed(const dev_guard_t *g, int step)
{
    return step >= 0 && step < DEV_GUARD_STEPS && g->strikes[step] == 0;
}

/* Atomic: the voice model loader (its own task) and the I/O task both mark steps. */
static inline void dev_guard_begin(dev_guard_t *g, int step) { if (step >= 0 && step < DEV_GUARD_STEPS) __atomic_fetch_or(&g->inflight, 1U << step, __ATOMIC_SEQ_CST); }
static inline void dev_guard_end(dev_guard_t *g, int step) { if (step >= 0 && step < DEV_GUARD_STEPS) __atomic_fetch_and(&g->inflight, ~(1U << step), __ATOMIC_SEQ_CST); }

/* ---- Joystick module ------------------------------------------------------------------------------------------------
 * The official driver delivers x, y in -1..1 in the MODULE's own frame. How the module sits relative to the display (and
 * whether the left and right slot differ) has not been checked on the board, so the mapping is a compile-time choice:
 * the defaults pass the values through unchanged. The raw values are on the I/O page of the log view to decide it.
 * Application frame: +X right, +Y up. */
#ifndef EXP_JOY_SWAP_XY
#define EXP_JOY_SWAP_XY 0
#endif
#ifndef EXP_JOY_INV_X
#define EXP_JOY_INV_X 0
#endif
#ifndef EXP_JOY_INV_Y
#define EXP_JOY_INV_Y 0
#endif

static inline float dev_joy_clamp(float v)
{
    if (!(v == v)) return 0.0f;   /* NaN */
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

static inline void dev_joy_map(float x, float y, float *ox, float *oy)
{
    float a = dev_joy_clamp(x), b = dev_joy_clamp(y);
#if EXP_JOY_SWAP_XY
    const float t = a; a = b; b = t;
#endif
#if EXP_JOY_INV_X
    a = -a;
#endif
#if EXP_JOY_INV_Y
    b = -b;
#endif
    *ox = a;
    *oy = b;
}

/* bit n = button n (B, 1, 2, 3, 4) */
static inline uint8_t dev_joy_buttons(const bool *b, int n)
{
    uint8_t m = 0;
    for (int i = 0; i < n && i < 8; ++i)
        if (b[i]) m |= (uint8_t)(1U << i);
    return m;
}

/* ---- Burn-in protection (AMOLED) --------------------------------------------------------------------------------------
 * The CO5300 is an AMOLED panel and the stand runs all day. After EXP_DIM_AFTER_S seconds without a touch the panel is
 * dimmed to EXP_DIM_LEVEL percent (the attract tour keeps running); any touch restores EXP_BRIGHT_LEVEL at once. */
#ifndef EXP_DIM_ENABLE
#define EXP_DIM_ENABLE 1
#endif
#ifndef EXP_DIM_AFTER_S
#define EXP_DIM_AFTER_S 600U
#endif
#ifndef EXP_DIM_LEVEL
#define EXP_DIM_LEVEL 50
#endif
#ifndef EXP_BRIGHT_LEVEL
#define EXP_BRIGHT_LEVEL 100
#endif

typedef struct { bool dimmed; } dev_dim_t;

/* Returns the brightness (percent) to apply now, or -1 when nothing changes. Wrap-safe for the 32-bit millisecond clock. */
static inline int dev_dim_step(dev_dim_t *d, uint32_t now_ms, uint32_t last_touch_ms, uint32_t after_s, int bright, int dim)
{
    const uint32_t idle_ms = now_ms - last_touch_ms;
    const bool want_dim = after_s > 0U && idle_ms >= after_s * 1000U;
    if (want_dim && !d->dimmed) { d->dimmed = true; return dim; }
    if (!want_dim && d->dimmed) { d->dimmed = false; return bright; }
    return -1;
}
