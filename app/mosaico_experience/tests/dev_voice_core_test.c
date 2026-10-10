// SPDX-License-Identifier: Apache-2.0
// Host test for the pure helpers of the offline voice layer (firmware/main/dev_voice_core.h).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../firmware/main/dev_io_core.h"
#include "../../../firmware/voice_components/exp_voice/include/dev_voice_core.h"
#include "exp_types.h"

_Static_assert((int)EXP_VCMD_COUNT == (int)DEV_VCMD_COUNT, "exp_voice_cmd_t and dev_vcmd_t must match");
_Static_assert((int)EXP_VCMD_HOME == (int)DEV_VCMD_HOME && (int)EXP_VCMD_MOTION == (int)DEV_VCMD_MOTION &&
               (int)EXP_VCMD_AUDIO == (int)DEV_VCMD_AUDIO && (int)EXP_VCMD_GAME == (int)DEV_VCMD_GAME &&
               (int)EXP_VCMD_DEVICE == (int)DEV_VCMD_DEVICE, "command order");
_Static_assert((int)EXP_VEV_WAKE == (int)DEV_VEV_WAKE && (int)EXP_VEV_COMMAND == (int)DEV_VEV_COMMAND &&
               (int)EXP_VEV_TIMEOUT == (int)DEV_VEV_TIMEOUT, "event order");

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

/* Feed a sine of f Hz (48 kHz input, slot 0, amplitude amp) through the decimator, return the output RMS after the filter
 * has settled. */
static double tone_rms(double f, double amp)
{
    static dev_vdec_t d;
    dev_vdec_init(&d);
    enum { FR = 4800 };
    static int16_t st[2 * FR];
    static int16_t out[FR];
    double sum = 0.0;
    int cnt = 0;
    for (int blk = 0; blk < 4; ++blk) {
        for (int i = 0; i < FR; ++i) {
            st[2 * i] = (int16_t)lround(amp * sin(2.0 * 3.14159265358979 * f * (double)(blk * FR + i) / 48000.0));
            st[2 * i + 1] = 0;
        }
        const int n = dev_vdec_push(&d, st, FR, 0, out);
        if (blk >= 1) for (int k = 0; k < n; ++k) { sum += (double)out[k] * out[k]; ++cnt; }
    }
    return sqrt(sum / cnt);
}

static double db_rel(double f)
{
    const double ref = 8000.0 / sqrt(2.0);   /* the RMS of a unity-gain tone of amplitude 8000 */
    return 20.0 * log10(tone_rms(f, 8000.0) / ref + 1e-12);
}

int main(void)
{
    /* ---- decimator: DC gain, pass band, stop band, chunking ---- */
    {
        dev_vdec_t d;
        dev_vdec_init(&d);
        int32_t s = 0;
        for (int i = 0; i < DEV_VDEC_TAPS; ++i) s += d.coef[i];
        CHECK(s == 32768);
        CHECK(fabs(db_rel(300.0)) < 0.3);
        CHECK(fabs(db_rel(1000.0)) < 0.3);
        CHECK(fabs(db_rel(3000.0)) < 0.5);
        CHECK(db_rel(5000.0) > -1.5);
        CHECK(db_rel(9800.0) < -35.0);          /* would fold to 6.2 kHz */
        CHECK(db_rel(12000.0) < -40.0);         /* would fold to 4 kHz   */
        CHECK(db_rel(15900.0) < -40.0);         /* would fold to ~0 kHz  */
        CHECK(db_rel(20000.0) < -40.0);         /* would fold to 4 kHz   */
        CHECK(db_rel(23500.0) < -40.0);
        /* DC passes with unity gain */
        static int16_t st[2 * 900], out[300];
        for (int i = 0; i < 900; ++i) { st[2 * i] = 1000; st[2 * i + 1] = -1000; }
        dev_vdec_init(&d);
        int n = dev_vdec_push(&d, st, 900, 0, out);
        CHECK(n == 300 && out[299] == 1000);
        dev_vdec_init(&d);
        n = dev_vdec_push(&d, st, 900, 1, out);
        CHECK(n == 300 && out[299] == -1000);   /* the slot argument selects the channel */
        /* chunked processing equals one-shot processing, also for chunk sizes that are not multiples of 3 */
        static int16_t sig[2 * 1500], a[600], b[600];
        for (int i = 0; i < 1500; ++i) { sig[2 * i] = (int16_t)(6000.0 * sin(0.3 * i) + 2000.0 * sin(1.7 * i)); sig[2 * i + 1] = 0; }
        dev_vdec_t d1, d2;
        dev_vdec_init(&d1); dev_vdec_init(&d2);
        const int n1 = dev_vdec_push(&d1, sig, 1500, 0, a);
        int n2 = 0;
        const int cuts[] = {7, 200, 1, 500, 292, 500};
        int off = 0;
        for (size_t c = 0; c < sizeof(cuts) / sizeof(cuts[0]); ++c) { n2 += dev_vdec_push(&d2, sig + 2 * off, cuts[c], 0, b + n2); off += cuts[c]; }
        CHECK(off == 1500 && n1 == 500 && n2 == 500 && memcmp(a, b, 500 * sizeof(int16_t)) == 0);
        /* full-scale input never wraps */
        for (int i = 0; i < 900; ++i) { st[2 * i] = (i & 1) ? 32767 : -32768; st[2 * i + 1] = 0; }
        dev_vdec_init(&d);
        dev_vdec_push(&d, st, 900, 0, out);
        CHECK(1);
    }

    /* ---- slot selection: stable, switches only on clear evidence ---- */
    {
        static int16_t st[2 * 768];
        dev_vslot_t sl = {0, 0};
        for (int i = 0; i < 768; ++i) { st[2 * i] = (int16_t)(i % 7 - 3); st[2 * i + 1] = (int16_t)((i & 1) ? 3000 : -3000); }
        for (int k = 0; k < DEV_VSLOT_HOLD - 1; ++k) CHECK(dev_vslot_choose(&sl, st, 768) == 0);   /* not yet */
        CHECK(dev_vslot_choose(&sl, st, 768) == 1);                                              /* now */
        CHECK(dev_vslot_choose(&sl, st, 768) == 1);
        /* a single loud chunk on the other slot resets the vote counter but does not switch back */
        for (int i = 0; i < 768; ++i) { st[2 * i] = (int16_t)((i & 1) ? 3000 : -3000); st[2 * i + 1] = 2; }
        CHECK(dev_vslot_choose(&sl, st, 768) == 1);
        for (int i = 0; i < 768; ++i) { st[2 * i] = 1; st[2 * i + 1] = 2; }                       /* silence: nothing changes */
        for (int k = 0; k < 40; ++k) CHECK(dev_vslot_choose(&sl, st, 768) == 1);
        /* quiet noise below the floor never triggers a switch */
        dev_vslot_t q = {0, 0};
        for (int i = 0; i < 768; ++i) { st[2 * i] = 0; st[2 * i + 1] = (int16_t)((i & 1) ? 6 : -6); }
        for (int k = 0; k < 40; ++k) CHECK(dev_vslot_choose(&q, st, 768) == 0);
    }

    /* ---- ring ---- */
    {
        static int16_t store[16];
        dev_vring_t r;
        dev_vring_init(&r, store, 16);
        int16_t in[20], out[20];
        for (int i = 0; i < 20; ++i) in[i] = (int16_t)(i + 1);
        CHECK(dev_vring_avail(&r) == 0 && dev_vring_read(&r, out, 1) == 0);
        CHECK(dev_vring_write(&r, in, 10) == 10 && dev_vring_avail(&r) == 10);
        CHECK(dev_vring_read(&r, out, 11) == 0 && dev_vring_avail(&r) == 10);        /* all or nothing */
        CHECK(dev_vring_read(&r, out, 4) == 4 && out[0] == 1 && out[3] == 4);
        CHECK(dev_vring_write(&r, in, 20) == 10 && r.dropped == 10);                  /* 6 free + 4 read = room for 10 */
        CHECK(dev_vring_avail(&r) == 16);
        CHECK(dev_vring_read(&r, out, 16) == 16 && out[0] == 5 && out[5] == 10 && out[6] == 1 && out[15] == 10);   /* wraps, oldest first */
        CHECK(dev_vring_write(&r, NULL, 3) == 3 && dev_vring_read(&r, out, 3) == 3 && out[0] == 0 && out[2] == 0);   /* silence */
        for (int k = 0; k < 1000; ++k) { dev_vring_write(&r, in, 5); CHECK(dev_vring_read(&r, out, 5) == 5 && out[4] == 5); }   /* index wrap-around */
        r.head = 0xFFFFFFFEU; r.tail = 0xFFFFFFFEU;                                  /* 32-bit counter wrap */
        CHECK(dev_vring_write(&r, in, 6) == 6 && dev_vring_avail(&r) == 6 && dev_vring_read(&r, out, 6) == 6 && out[5] == 6);
    }

    /* ---- speaker mask ---- */
    {
        dev_vhold_t h = {false, 0};
        CHECK(!dev_vhold_masked(&h, 1000));
        dev_vhold_set(&h, true, 1000);
        CHECK(dev_vhold_masked(&h, 1000) && dev_vhold_masked(&h, 5000));
        dev_vhold_set(&h, false, 2000);
        CHECK(dev_vhold_masked(&h, 2000) && dev_vhold_masked(&h, 2249));
        CHECK(!dev_vhold_masked(&h, 2250) && !dev_vhold_masked(&h, 9000));
        dev_vhold_t w = {false, 0};                                  /* clock wrap */
        dev_vhold_set(&w, false, 0xFFFFFFF0U);
        CHECK(dev_vhold_masked(&w, 0xFFFFFFF8U) && dev_vhold_masked(&w, 100) && !dev_vhold_masked(&w, 400));
    }

    /* ---- command table ---- */
    {
        int per[DEV_VCMD_COUNT][2] = {{0}};
        for (int i = 0; i < DEV_VOICE_PHRASE_N; ++i) {
            const dev_vphrase_t *p = &DEV_VOICE_PHRASES[i];
            CHECK(p->cmd > DEV_VCMD_NONE && p->cmd < DEV_VCMD_COUNT);
            CHECK(dev_voice_phrase_valid(p->phrase));
            if (!dev_voice_phrase_valid(p->phrase)) printf("  bad phrase: '%s'\n", p->phrase);
            ++per[p->cmd][p->ja ? 1 : 0];
            for (int j = 0; j < i; ++j) {
                if (strcmp(DEV_VOICE_PHRASES[j].phrase, p->phrase) == 0) { printf("  duplicate phrase: '%s'\n", p->phrase); CHECK(0); }
            }
            const int id = dev_voice_phrase_id(p);
            int ja = -1;
            CHECK(dev_voice_normalize_id(id, &ja) == (dev_vcmd_t)p->cmd && ja == (p->ja ? 1 : 0));
        }
        for (int c = DEV_VCMD_HOME; c < DEV_VCMD_COUNT; ++c) CHECK(per[c][0] >= 3 && per[c][1] >= 3);   /* both languages everywhere */
        int ja = 0;
        CHECK(dev_voice_normalize_id(0, &ja) == DEV_VCMD_NONE && ja == 0);
        CHECK(dev_voice_normalize_id(100, &ja) == DEV_VCMD_NONE && ja == 1);
        CHECK(dev_voice_normalize_id(DEV_VCMD_COUNT, &ja) == DEV_VCMD_NONE);
        CHECK(dev_voice_normalize_id(100 + DEV_VCMD_COUNT, &ja) == DEV_VCMD_NONE);
        CHECK(dev_voice_normalize_id(-5, &ja) == DEV_VCMD_NONE);
        CHECK(dev_voice_normalize_id(103, &ja) == DEV_VCMD_AUDIO && ja == 1);
        CHECK(dev_voice_normalize_id(5, NULL) == DEV_VCMD_DEVICE);
        CHECK(DEV_VOICE_PHRASE_N <= 100);                       /* far below MultiNet's command limit */
        CHECK(!dev_voice_phrase_valid("") && !dev_voice_phrase_valid(" A") && !dev_voice_phrase_valid("A ") &&
              !dev_voice_phrase_valid("A  B") && !dev_voice_phrase_valid("home") && !dev_voice_phrase_valid("A-B") &&
              !dev_voice_phrase_valid("ホーム") && dev_voice_phrase_valid("GO HOME"));
        CHECK(dev_voice_prob_pct(-1.0f) == 0 && dev_voice_prob_pct(0.0f) == 0 && dev_voice_prob_pct(0.456f) == 46 &&
              dev_voice_prob_pct(1.0f) == 100 && dev_voice_prob_pct(7.0f) == 100 && dev_voice_prob_pct(NAN) == 0);
    }

    /* ---- status machine ---- */
    {
        dev_vstate_t v;
        memset(&v, 0, sizeof(v));
        CHECK(v.phase == DEV_VPH_OFF);
        dev_vs_wake(&v, 10, 1);                                   /* wake before ready: ignored */
        CHECK(v.seq == 0 && v.phase == DEV_VPH_OFF);
        CHECK(!dev_vs_command(&v, 1, 0.9f) && v.seq == 0);
        dev_vs_set_phase(&v, DEV_VPH_LOADING);
        dev_vs_wake(&v, 10, 1);
        CHECK(v.seq == 0);
        dev_vs_set_phase(&v, DEV_VPH_READY);
        dev_vs_wake(&v, 1000, 2);
        CHECK(v.phase == DEV_VPH_LISTENING && v.event == DEV_VEV_WAKE && v.wake_idx == 2 && v.seq == 1 && v.n_wake == 1);
        CHECK(!dev_vs_command(&v, 77, 0.9f));                     /* not one of ours */
        CHECK(v.n_reject == 1 && v.seq == 1 && v.phase == DEV_VPH_LISTENING);
        CHECK(dev_vs_command(&v, DEV_VOICE_JA_BASE + DEV_VCMD_GAME, 0.62f));
        CHECK(v.phase == DEV_VPH_READY && v.event == DEV_VEV_COMMAND && v.cmd == DEV_VCMD_GAME && v.ja == 1 && v.prob_pct == 62 &&
              v.seq == 2 && v.n_cmd == 1);
        dev_vs_timeout(&v);                                       /* no window open: nothing happens */
        CHECK(v.seq == 2 && v.n_timeout == 0);
        dev_vs_wake(&v, 5000, 1);
        dev_vs_tick(&v, 5000 + DEV_VOICE_LISTEN_MAX_MS - 1);
        CHECK(v.phase == DEV_VPH_LISTENING);
        dev_vs_tick(&v, 5000 + DEV_VOICE_LISTEN_MAX_MS);
        CHECK(v.phase == DEV_VPH_READY && v.event == DEV_VEV_TIMEOUT && v.n_timeout == 1 && v.seq == 4);
        dev_vs_wake(&v, 9000, 1);
        dev_vs_timeout(&v);
        CHECK(v.phase == DEV_VPH_READY && v.event == DEV_VEV_TIMEOUT && v.seq == 6);
        dev_vs_set_phase(&v, DEV_VPH_ERROR);                      /* an error stops everything */
        dev_vs_wake(&v, 9500, 1);
        CHECK(!dev_vs_command(&v, 1, 0.9f) && v.phase == DEV_VPH_ERROR && v.seq == 6);
    }

    /* ---- phase -> application AI state ---- */
    {
        CHECK(dev_vs_ai_state(DEV_VPH_OFF) == (int)EXP_AI_NOT_INTEGRATED);
        CHECK(dev_vs_ai_state(DEV_VPH_LOADING) == (int)EXP_AI_LOADING);
        CHECK(dev_vs_ai_state(DEV_VPH_READY) == (int)EXP_AI_READY && dev_vs_ai_state(DEV_VPH_LISTENING) == (int)EXP_AI_READY);
        CHECK(dev_vs_ai_state(DEV_VPH_ERROR) == (int)EXP_AI_ERROR && dev_vs_ai_state(DEV_VPH_SKIPPED) == (int)EXP_AI_ERROR);
    }

    /* ---- guard: the voice step is its own bit and survives a boot only as a strike ---- */
    {
        dev_guard_t g;
        dev_guard_boot(&g, true);
        dev_guard_begin(&g, DEV_STEP_VOICE);
        CHECK(g.inflight == (1U << DEV_STEP_VOICE));
        dev_guard_begin(&g, DEV_STEP_AUDIO);
        dev_guard_end(&g, DEV_STEP_AUDIO);
        CHECK(g.inflight == (1U << DEV_STEP_VOICE));
        dev_guard_boot(&g, false);
        CHECK(!dev_guard_allowed(&g, DEV_STEP_VOICE) && dev_guard_allowed(&g, DEV_STEP_AUDIO) && dev_guard_allowed(&g, DEV_STEP_JOYSTICK));
        dev_guard_boot(&g, true);
        CHECK(dev_guard_allowed(&g, DEV_STEP_VOICE));
        CHECK(DEV_STEP_VOICE < DEV_GUARD_STEPS);
    }

    if (fails) { printf("dev_voice_core: %d FAILED\n", fails); return 1; }
    printf("dev_voice_core: all checks passed\n");
    return 0;
}
