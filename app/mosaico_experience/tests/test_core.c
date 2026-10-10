// SPDX-License-Identifier: Apache-2.0
// Native unit tests for the hardware-free core (maze, fusion, audio).
// Build: cc -std=c11 -O2 -Wall -Wextra -I../main tests/test_core.c main/exp_maze.c main/exp_fusion.c main/exp_audio.c -lm
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exp_audio.h"
#include "exp_fusion.h"
#include "exp_maze.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_maze_bot(void)
{
    int solved[3] = {0}, total[3] = {0};
    float worst[3] = {0}, sum[3] = {0};
    for (int d = 0; d < 3; ++d) {
        for (uint32_t seed = 1; seed <= 150; ++seed) {
            exp_maze_t m;
            exp_maze_init(&m);
            exp_maze_start(&m, (maze_diff_t)d, seed * 7919u, true);
            CHECK(exp_maze_solve(&m, 0, 0, true) > 0, "diff %d seed %u: no path avoiding holes", d, seed);
            float t = 0; int guard = 0; (void)t;
            while (m.phase != MZ_SUCCESS && m.phase != MZ_TIMEUP && guard++ < 30 * 90) {
                float ax = 0, ay = 0;
                exp_maze_bot(&m, &ax, &ay);
                exp_maze_step(&m, ax, ay, 1.0f / 30.0f);
                t += 1.0f / 30.0f;
            }
            ++total[d];
            if (m.phase == MZ_SUCCESS) { ++solved[d]; sum[d] += MAZE_TIME_LIMIT_S - m.time_left_s; if (MAZE_TIME_LIMIT_S - m.time_left_s > worst[d]) worst[d] = MAZE_TIME_LIMIT_S - m.time_left_s; }
        }
        printf("maze diff %d: bot solved %d/%d, mean %.1fs worst %.1fs\n", d, solved[d], total[d], solved[d] ? sum[d] / solved[d] : 0, worst[d]);
        CHECK(solved[d] >= total[d] * 98 / 100, "bot solve rate too low for diff %d", d);
    }
}

static void test_fusion(void)
{
    exp_fusion_t f; exp_fusion_init(&f);
    const float acc_flat[3] = {0, 0, 1}, gyr0[3] = {0, 0, 0};
    for (int i = 0; i < 90; ++i) exp_fusion_update(&f, acc_flat, gyr0, NULL, 1.0f / 30.0f);
    float p, r, y; exp_fusion_euler(&f, &p, &r, &y);
    CHECK(fabsf(p) < 1 && fabsf(r) < 1, "flat pitch/roll %.2f %.2f", p, r);
    /* Top edge raised 30 deg => acc_y = sin(30), acc_z = cos(30). */
    const float a30[3] = {0, 0.5f, 0.8660254f};
    for (int i = 0; i < 150; ++i) exp_fusion_update(&f, a30, gyr0, NULL, 1.0f / 30.0f);
    exp_fusion_euler(&f, &p, &r, &y);
    CHECK(fabsf(p - 30.0f) < 1.5f && fabsf(r) < 1.5f, "pitch30 -> %.2f %.2f", p, r);
    /* Right edge down 20 deg => acc_x = -sin(20). */
    const float r20[3] = {-0.3420201f, 0, 0.9396926f};
    for (int i = 0; i < 150; ++i) exp_fusion_update(&f, r20, gyr0, NULL, 1.0f / 30.0f);
    exp_fusion_euler(&f, &p, &r, &y);
    CHECK(fabsf(r - 20.0f) < 1.5f && fabsf(p) < 1.5f, "roll20 -> %.2f %.2f", p, r);
    /* Yaw integration: 90 deg/s about +Z for 1 s => heading decreases by 90 (CCW seen from above). */
    exp_fusion_init(&f);
    for (int i = 0; i < 30; ++i) exp_fusion_update(&f, acc_flat, gyr0, NULL, 1.0f / 30.0f);
    exp_fusion_reset_yaw(&f);
    const float gz[3] = {0, 0, 90};
    for (int i = 0; i < 30; ++i) exp_fusion_update(&f, acc_flat, gz, NULL, 1.0f / 30.0f);
    exp_fusion_euler(&f, &p, &r, &y);
    CHECK(fabsf(y + 90.0f) < 4.0f, "yaw after +90 deg CCW should read -90 (clockwise convention), got %.2f", y);
}

static void body_field(float heading_deg, float tilt_roll_deg, float out[3], float acc[3])
{
    /* Board rolled about its Y axis by tilt_roll (right edge down), top edge at the
     * given magnetic heading. World: x north, y west, z up. Field 30 uT north, 35 uT down. */
    const float h = heading_deg * 3.14159265f / 180.0f, r = tilt_roll_deg * 3.14159265f / 180.0f;
    const float ey_w[3] = {cosf(h), -sinf(h), 0.0f};                 /* body +Y in world (level)   */
    const float ez_w[3] = {0.0f, 0.0f, 1.0f};
    const float ex_w[3] = {ey_w[1] * ez_w[2] - ey_w[2] * ez_w[1], ey_w[2] * ez_w[0] - ey_w[0] * ez_w[2],
                           ey_w[0] * ez_w[1] - ey_w[1] * ez_w[0]};
    /* roll about body Y: x' = x cos r - z sin r ; z' = x sin r + z cos r (right edge down for r>0) */
    float bx[3], bz[3];
    for (int i = 0; i < 3; ++i) {
        bx[i] = ex_w[i] * cosf(r) - ez_w[i] * sinf(r);
        bz[i] = ex_w[i] * sinf(r) + ez_w[i] * cosf(r);
    }
    const float B[3] = {30.0f, 0.0f, -35.0f};
    for (int a = 0; a < 3; ++a) {
        const float *axis = a == 0 ? bx : (a == 1 ? ey_w : bz);
        out[a] = axis[0] * B[0] + axis[1] * B[1] + axis[2] * B[2];
        acc[a] = axis[2];   /* world up expressed in the body frame */
    }
}

static void test_heading(void)
{
    const float gyr[3] = {0, 0, 0};
    for (int k = 0; k < 4; ++k) {
        const float hdg[4] = {60.0f, -120.0f, 170.0f, -20.0f}, roll[4] = {0.0f, 25.0f, -30.0f, 10.0f};
        exp_fusion_t f;
        exp_fusion_init(&f);
        float m[3], a[3];
        body_field(hdg[k], roll[k], m, a);
        exp_fusion_update(&f, a, gyr, NULL, 1.0f / 30.0f);
        for (int i = 0; i < 60; ++i) exp_fusion_update(&f, a, gyr, m, 1.0f / 30.0f);
        float p, r, y;
        exp_fusion_euler(&f, &p, &r, &y);
        float e = y - hdg[k];
        while (e > 180.0f) e -= 360.0f;
        while (e < -180.0f) e += 360.0f;
        CHECK(fabsf(e) < 3.0f, "heading %.0f roll %.0f converged to %.1f within 2 s", hdg[k], roll[k], y);
        CHECK(fabsf(r - roll[k]) < 1.5f && fabsf(p) < 1.5f, "magnetometer must not tilt the horizon: roll %.1f pitch %.1f", r, p);
    }
}

static void test_magcal(void)
{
    exp_magcal_t c;
    exp_magcal_init(&c);
    exp_magcal_start(&c, 0);
    /* Wide spans reached by a few extreme samples, then a long wiggle that never
     * leaves one half-space: spans are fine but orientation coverage is not. */
    for (int i = 0; i < 400; ++i) {
        float m[2][3];
        for (int s = 0; s < 2; ++s) {
            m[s][0] = 5.0f + 25.0f * sinf((float)i * 0.3f);
            m[s][1] = -3.0f + 25.0f * cosf((float)i * 0.3f);
            m[s][2] = (i < 5) ? -40.0f + (float)i * 0.0f : 40.0f;
            if (i == 5) m[s][2] = 80.0f;
        }
        exp_magcal_feed(&c, (const float (*)[3])m, (uint32_t)i * 33U);
    }
    CHECK(!c.done, "extreme samples plus a one-sided wiggle must not finish calibration (progress %.2f)",
          exp_magcal_progress(&c));
    /* A full sphere sweep with a hard-iron offset must finish and remove the offset. */
    exp_magcal_start(&c, 0);
    uint32_t n = 0;
    for (int ph = 0; ph < 40 && !c.done; ++ph)
        for (int th = 0; th < 20 && !c.done; ++th, ++n) {
            const float a = (float)ph * 0.4f, b = (float)th * 0.31f;
            const float d[3] = {sinf(b) * cosf(a), sinf(b) * sinf(a), cosf(b)};
            float m[2][3];
            for (int s = 0; s < 2; ++s)
                for (int k = 0; k < 3; ++k) m[s][k] = 46.0f * d[k] + (s ? -6.0f : 8.0f) * (k == 0 ? 1.0f : 0.5f);
            exp_magcal_feed(&c, (const float (*)[3])m, n * 33U);
        }
    CHECK(c.done, "full sphere sweep must complete calibration");
    CHECK(fabsf(c.offset[0][0] - 8.0f) < 4.0f, "hard-iron offset estimate %.2f (expect ~8)", c.offset[0][0]);

    /* Real-board case (0.5.3): a steady ~530 uT from the board's own edge magnets under a 46 uT Earth field, with
     * noise. The sphere fit must recover the offset to a few uT and the Earth radius, and apply() must give a field
     * whose magnitude stays near 46 uT in every direction. */
    exp_magcal_start(&c, 0);
    uint32_t seed = 12345U;
    const float hi[3] = {-495.0f, 131.0f, 121.0f};
    n = 0;
    for (int ph = 0; ph < 80 && !c.done; ++ph)
        for (int th = 0; th < 25 && !c.done; ++th, ++n) {
            const float a = (float)ph * 0.37f, b = (float)th * 0.27f;
            const float d[3] = {sinf(b) * cosf(a), sinf(b) * sinf(a), cosf(b)};
            float m[2][3];
            for (int s = 0; s < 2; ++s)
                for (int k = 0; k < 3; ++k) {
                    seed = seed * 1664525U + 1013904223U;
                    const float noise = ((float)(seed >> 16) / 65536.0f - 0.5f) * 3.0f;
                    m[s][k] = 46.0f * d[k] + hi[k] * (s ? 0.8f : 1.0f) + noise;
                }
            exp_magcal_feed(&c, (const float (*)[3])m, n * 33U);
        }
    CHECK(c.done, "530 uT hard-iron sweep must complete (rejects %u)", c.fit_rejects);
    for (int k = 0; k < 3; ++k)
        CHECK(fabsf(c.offset[0][k] - hi[k]) < 4.0f, "axis %d offset %.1f (expect %.1f)", k, c.offset[0][k], hi[k]);
    CHECK(fabsf(c.fit_radius[0] - 46.0f) < 4.0f, "fit radius %.1f (expect 46)", c.fit_radius[0]);
    {
        float worst = 0.0f;
        for (int i = 0; i < 200; ++i) {
            const float a = (float)i * 0.71f, b = (float)i * 0.43f;
            const float d[3] = {sinf(b) * cosf(a), sinf(b) * sinf(a), cosf(b)};
            float m[2][3], o[3];
            for (int s = 0; s < 2; ++s)
                for (int k = 0; k < 3; ++k) m[s][k] = 46.0f * d[k] + hi[k] * (s ? 0.8f : 1.0f);
            exp_magcal_apply(&c, (const float (*)[3])m, o);
            const float mag = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            if (fabsf(mag - 46.0f) > worst) worst = fabsf(mag - 46.0f);
        }
        CHECK(worst < 6.0f, "calibrated |B| deviates by %.1f uT from 46 (expect < 6)", worst);
    }

    /* A "sphere" that is not Earth-sized (here 3 uT of wobble around a big offset) must be rejected, never accepted. */
    exp_magcal_start(&c, 0);
    n = 0;
    for (int i = 0; i < 4000; ++i, ++n) {
        const float a = (float)i * 0.37f, b = (float)i * 0.19f;
        const float d[3] = {sinf(b) * cosf(a), sinf(b) * sinf(a), cosf(b)};
        float m[2][3];
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 3; ++k) m[s][k] = 3.0f * d[k] + 500.0f + 100.0f * (float)k;
        exp_magcal_feed(&c, (const float (*)[3])m, n * 33U);
    }
    CHECK(!c.done, "a 3 uT sphere must not be accepted as an Earth-field calibration");
}

static void test_motion(void)
{
    exp_motion_t m; exp_motion_init(&m);
    uint32_t t = 100; const float flat[3] = {0, 0, 1};
    for (int i = 0; i < 30; ++i, t += 33) exp_motion_update(&m, flat, t);
    CHECK(!m.tilted && !m.flipped, "flat should be idle");
    const float tilt[3] = {0.5f, 0, 0.866f};
    exp_motion_events_t ev = exp_motion_update(&m, tilt, t += 33);
    CHECK(ev.tilt && m.tilted && (m.dir & EXP_DIR_LEFT), "tilt left event expected");
    int shakes = 0;
    for (int i = 0; i < 40; ++i, t += 33) {
        const float s[3] = {0, 0, (i % 2) ? 2.2f : -1.0f};
        shakes += exp_motion_update(&m, s, t).shake;
    }
    CHECK(shakes >= 1, "shake not detected (%d)", shakes);
    int flips = 0; const float down[3] = {0, 0, -1};
    for (int i = 0; i < 20; ++i, t += 33) flips += exp_motion_update(&m, down, t).flip;
    CHECK(flips == 1 && m.flipped, "flip once expected, got %d", flips);
}

static void test_audio(void)
{
    exp_audio_t a; exp_audio_init(&a);
    int16_t pcm[EXP_PCM_N];
    /* 1 kHz sine at -20 dBFS: spectrum peak lands in the band that contains bin 16. */
    for (int frame = 0; frame < 40; ++frame) {
        for (int i = 0; i < EXP_PCM_N; ++i) pcm[i] = (int16_t)(3276.0f * sinf(2.0f * 3.14159265f * 1000.0f * (float)(i + frame * EXP_PCM_N) / 16000.0f));
        exp_audio_update(&a, pcm, EXP_PCM_N);
    }
    CHECK(a.signal_present, "signal present");
    CHECK(a.level_db > -26.0f && a.level_db < -20.0f, "level %.1f dBFS (expect ~-23)", a.level_db);
    int best = 0; for (int b = 1; b < EXP_SPEC_BARS; ++b) if (a.bars[b] > a.bars[best]) best = b;
    printf("audio: 1kHz level %.1f dB, peak band %d, voice_ratio %.2f, vad %d\n", a.level_db, best, a.voice_ratio, a.vad);
    CHECK(best >= 12 && best <= 17, "1 kHz peak band %d unexpected", best);
    /* Silence -> VAD off. */
    memset(pcm, 0, sizeof(pcm));
    for (int frame = 0; frame < 60; ++frame) exp_audio_update(&a, pcm, EXP_PCM_N);
    CHECK(!a.vad, "VAD must be off in silence");
    exp_audio_update(&a, NULL, 0);
    CHECK(!a.signal_present, "no samples => no signal");
}

int main(void)
{
    test_maze_bot();
    test_fusion();
    test_heading();
    test_magcal();
    test_motion();
    test_audio();
    printf(failures ? "UNIT TESTS FAILED (%d)\n" : "UNIT TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
