// SPDX-License-Identifier: Apache-2.0
// Host test for the pure helpers of the device I/O layer (firmware/main/dev_io_core.h).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../firmware/main/dev_io_core.h"
#include "exp_types.h"

/* the application's sound numbers are used directly as cue numbers by the device adapter */
_Static_assert((int)EXP_SND_COUNT == (int)DEV_CUE_COUNT, "exp_sound_t and DEV_CUE_* must match");
_Static_assert((int)EXP_SND_TAP == (int)DEV_CUE_TAP && (int)EXP_SND_NAV == (int)DEV_CUE_NAV && (int)EXP_SND_SUCCESS == (int)DEV_CUE_SUCCESS &&
               (int)EXP_SND_FAIL == (int)DEV_CUE_FAIL && (int)EXP_SND_HIT == (int)DEV_CUE_HIT && (int)EXP_SND_COUNTDOWN == (int)DEV_CUE_COUNTDOWN &&
               (int)EXP_SND_GO == (int)DEV_CUE_GO && (int)EXP_SND_TEST_TONE == (int)DEV_CUE_TEST_TONE, "cue order");

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static int sign_changes(const int16_t *st, int frames)
{
    int n = 0, prev = 0;
    for (int i = 0; i < frames; ++i) {
        const int s = st[2 * i] > 0 ? 1 : (st[2 * i] < 0 ? -1 : 0);
        if (s != 0 && prev != 0 && s != prev) ++n;
        if (s != 0) prev = s;
    }
    return n;
}

int main(void)
{
    /* ---- microphone: pick the louder slot, average three samples, report both peaks ---- */
    {
        static int16_t st[2 * DEV_MIC_FRAMES];
        static int16_t out[DEV_MIC_OUT_N];
        dev_pcm_info_t info;
        for (int i = 0; i < DEV_MIC_FRAMES; ++i) { st[2 * i] = 10; st[2 * i + 1] = (int16_t)((i % 3 == 0) ? 3000 : (i % 3 == 1 ? 0 : -3000)); }
        const int n = dev_pcm_decimate(st, DEV_MIC_FRAMES, out, &info);
        CHECK(n == DEV_MIC_OUT_N);
        CHECK(info.used == 1 && info.peak_r == 3000 && info.peak_l == 10);
        CHECK(out[0] == 0 && out[100] == 0);                       /* 3000 + 0 - 3000 averages to zero */
        for (int i = 0; i < DEV_MIC_FRAMES; ++i) { st[2 * i] = 600; st[2 * i + 1] = 0; }
        dev_pcm_decimate(st, DEV_MIC_FRAMES, out, &info);
        CHECK(info.used == 0 && out[0] == 600 && out[DEV_MIC_OUT_N - 1] == 600);
        /* a 1 kHz tone at 48 kHz survives the 3:1 average as a 1 kHz tone at 16 kHz (period 16 samples) */
        for (int i = 0; i < DEV_MIC_FRAMES; ++i) { st[2 * i] = (int16_t)(8000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * i / 48000.0)); st[2 * i + 1] = 0; }
        dev_pcm_decimate(st, DEV_MIC_FRAMES, out, &info);
        int zc = 0;
        for (int i = 1; i < DEV_MIC_OUT_N; ++i) if ((out[i - 1] < 0) != (out[i] < 0)) ++zc;
        CHECK(zc >= 30 && zc <= 34);                               /* 256 samples = 16 periods = 32 zero crossings */
        int mx = 0;
        for (int i = 0; i < DEV_MIC_OUT_N; ++i) if (abs(out[i]) > mx) mx = abs(out[i]);
        CHECK(mx > 6500 && mx < 8000);                             /* box filter attenuates 1 kHz by only ~1 % */
        CHECK(dev_pcm_decimate(st, 7, out, NULL) == 2);            /* partial chunk */
    }

    /* ---- cues ---- */
    for (int c = 0; c < DEV_CUE_COUNT; ++c) {
        dev_cue_t cue;
        CHECK(dev_cue_get(c, &cue) && cue.n >= 1 && cue.n <= DEV_CUE_MAX_SEGS);
        for (int s = 0; s < cue.n; ++s) CHECK(cue.seg[s].f0_hz >= 150 && cue.seg[s].f1_hz <= 4000 && cue.seg[s].ms >= 20 && cue.seg[s].ms <= 500 && cue.seg[s].level <= 100);
    }
    {
        dev_cue_t cue;
        CHECK(!dev_cue_get(-1, &cue) && !dev_cue_get(DEV_CUE_COUNT, &cue) && !dev_cue_get(0, NULL));
        static int16_t buf[2 * 48000];
        dev_tone_t t;
        CHECK(dev_tone_start(&t, DEV_CUE_TEST_TONE));
        int total = 0, w;
        int16_t chunk[2 * 480];
        int maxabs = 0, first_nonzero_ok = 1;
        while ((w = dev_tone_render(&t, chunk, 480, 6000)) > 0) {
            for (int i = 0; i < w; ++i) {
                CHECK(chunk[2 * i] == chunk[2 * i + 1]);
                const int a = abs(chunk[2 * i]);
                if (a > maxabs) maxabs = a;
            }
            memcpy(&buf[2 * total], chunk, (size_t)w * 4);
            total += w;
            if (w < 480) break;
        }
        CHECK(total == 400 * 48);                                   /* 400 ms at 48 kHz */
        CHECK(!t.active);
        CHECK(maxabs <= 6000 * 80 / 100 && maxabs > 6000 * 80 / 100 - 200);   /* level 80 % of the cue amplitude */
        CHECK(buf[0] == 0 && abs(buf[2 * (total - 1)]) < 100);                  /* ramps in and out */
        const int sc = sign_changes(buf, total);
        CHECK(sc >= 790 && sc <= 802);                              /* 1 kHz for 0.4 s: ~800 zero crossings */
        (void)first_nonzero_ok;
        CHECK(dev_tone_render(&t, chunk, 480, 6000) == 0);          /* finished cue renders nothing */
        /* two-segment cue lasts the sum of its segments */
        CHECK(dev_tone_start(&t, DEV_CUE_SUCCESS));
        total = 0;
        while ((w = dev_tone_render(&t, chunk, 480, 6000)) > 0) { total += w; if (w < 480) break; }
        CHECK(total == (80 + 140) * 48);
        CHECK(!dev_tone_start(&t, 99) && !t.active);
        /* a sweep cue ends lower than it starts */
        CHECK(dev_tone_start(&t, DEV_CUE_FAIL));
        static int16_t fb[2 * 48 * 230];
        const int fw = dev_tone_render(&t, fb, 48 * 230, 6000);
        CHECK(fw == 220 * 48);
        const int head = sign_changes(fb, 48 * 40), tail = sign_changes(&fb[2 * (fw - 48 * 40)], 48 * 40);
        CHECK(head > tail);
    }

    /* ---- motor gate ---- */
    {
        dev_haptic_gate_t g;
        memset(&g, 0, sizeof(g));
        CHECK(dev_haptic_grant(&g, 1000, 500) == DEV_HAPTIC_MAX_MS);              /* clamped */
        CHECK(dev_haptic_grant(&g, 1010, 120) == DEV_HAPTIC_MAX_MS);              /* 240 used */
        CHECK(dev_haptic_grant(&g, 1020, 50) == 0);                               /* budget gone in this window */
        CHECK(dev_haptic_grant(&g, 1999, 50) == 0);
        CHECK(dev_haptic_grant(&g, 2000, 50) == 50);                              /* new window */
        CHECK(dev_haptic_grant(&g, 2001, 5) == 0);                                /* below the minimum */
        CHECK(dev_haptic_grant(&g, 2002, 0) == 0);
        dev_haptic_gate_t h;
        memset(&h, 0, sizeof(h));
        CHECK(dev_haptic_grant(&h, 0, 100) == 100);                               /* works from time 0 */
        CHECK(dev_haptic_grant(&h, 5, 100) == 100);
        CHECK(dev_haptic_grant(&h, 6, 100) == 40);                                /* only the remaining budget */
        CHECK(dev_haptic_grant(&h, 7, 100) == 0);
    }

    /* ---- slots: anything present that is not a known, valid module is "unknown" (fail closed) ---- */
    CHECK(dev_slot_classify(0, true, 0x16) == DEV_SLOT_NOT_PROBED);
    CHECK(dev_slot_classify(1, false, 0) == DEV_SLOT_NONE);
    CHECK(dev_slot_classify(2, true, 0x16) == DEV_SLOT_INTERACT);
    CHECK(dev_slot_classify(2, true, 0x04) == DEV_SLOT_JOYSTICK);
    CHECK(dev_slot_classify(2, true, 0x14) == DEV_SLOT_BUTTON_LED);
    CHECK(dev_slot_classify(2, true, 0x07) == DEV_SLOT_CAMERA);
    CHECK(dev_slot_classify(2, true, 0x08) == DEV_SLOT_UNKNOWN);                  /* a sensor board: not reviewed */
    CHECK(dev_slot_classify(2, false, 0x16) == DEV_SLOT_UNKNOWN);                 /* descriptor invalid: never trust the type */
    CHECK(dev_slot_classify(7, true, 0x16) == DEV_SLOT_NOT_PROBED);               /* nonsense presence value */

    /* ---- crash guard ---- */
    {
        dev_guard_t g;
        memset(&g, 0xA5, sizeof(g));                                              /* uninitialised RAM after a cold start */
        dev_guard_boot(&g, false);
        CHECK(g.magic == DEV_GUARD_MAGIC && g.inflight == 0 && dev_guard_allowed(&g, DEV_STEP_AUDIO));
        dev_guard_begin(&g, DEV_STEP_AUDIO);
        dev_guard_end(&g, DEV_STEP_AUDIO);                                        /* finished normally */
        dev_guard_boot(&g, false);                                                /* software reset later: no strike */
        CHECK(dev_guard_allowed(&g, DEV_STEP_AUDIO));
        dev_guard_begin(&g, DEV_STEP_AUDIO);                                      /* crashes inside the step */
        dev_guard_begin(&g, DEV_STEP_SLOTS);
        dev_guard_end(&g, DEV_STEP_SLOTS);
        dev_guard_boot(&g, false);
        CHECK(!dev_guard_allowed(&g, DEV_STEP_AUDIO));                            /* skipped from now on */
        CHECK(dev_guard_allowed(&g, DEV_STEP_SLOTS) && dev_guard_allowed(&g, DEV_STEP_MOTOR));
        dev_guard_boot(&g, false);                                                /* still skipped on the next reboot */
        CHECK(!dev_guard_allowed(&g, DEV_STEP_AUDIO));
        dev_guard_boot(&g, true);                                                 /* power cycle: try again */
        CHECK(dev_guard_allowed(&g, DEV_STEP_AUDIO));
        CHECK(!dev_guard_allowed(&g, -1) && !dev_guard_allowed(&g, DEV_GUARD_STEPS));
    }

    /* ---- joystick mapping (defaults: pass-through) and button mask ---- */
    {
        float x, y;
        dev_joy_map(0.25f, -0.5f, &x, &y);
        CHECK(fabsf(x - 0.25f) < 1e-6f && fabsf(y + 0.5f) < 1e-6f);
        dev_joy_map(3.0f, -7.0f, &x, &y);
        CHECK(x == 1.0f && y == -1.0f);
        dev_joy_map(NAN, INFINITY, &x, &y);
        CHECK(x == 0.0f && y == 1.0f);
        const bool b[5] = {true, false, true, false, true};
        CHECK(dev_joy_buttons(b, 5) == 0x15);
        CHECK(dev_joy_buttons(b, 0) == 0);
        dev_guard_t g;
        dev_guard_boot(&g, true);
        dev_guard_begin(&g, DEV_STEP_JOYSTICK);
        dev_guard_boot(&g, false);
        CHECK(!dev_guard_allowed(&g, DEV_STEP_JOYSTICK) && dev_guard_allowed(&g, DEV_STEP_AUDIO));
    }

    /* ---- burn-in dimming ---- */
    {
        dev_dim_t d = {false};
        CHECK(dev_dim_step(&d, 1000, 0, 600, 100, 50) == -1);               /* 1 s idle: nothing */
        CHECK(dev_dim_step(&d, 599999, 0, 600, 100, 50) == -1);             /* just below the limit */
        CHECK(dev_dim_step(&d, 600000, 0, 600, 100, 50) == 50 && d.dimmed); /* limit reached: dim once */
        CHECK(dev_dim_step(&d, 700000, 0, 600, 100, 50) == -1);             /* stays dimmed, no repeat */
        CHECK(dev_dim_step(&d, 700100, 700000, 600, 100, 50) == 100 && !d.dimmed);   /* touch: restore once */
        CHECK(dev_dim_step(&d, 700200, 700000, 600, 100, 50) == -1);
        dev_dim_t w = {false};                                              /* clock wrap: last touch just before 2^32 ms */
        CHECK(dev_dim_step(&w, 100, 0xFFFFFF00U, 600, 100, 50) == -1);
        CHECK(dev_dim_step(&w, 600000U - 256U, 0xFFFFFF00U, 600, 100, 50) == 50);
        dev_dim_t z = {false};
        CHECK(dev_dim_step(&z, 99999999U, 0, 0, 100, 50) == -1);            /* after_s = 0 disables dimming */
    }

    if (fails) { printf("dev_io_core: %d FAILED\n", fails); return 1; }
    printf("dev_io_core: all checks passed\n");
    return 0;
}
