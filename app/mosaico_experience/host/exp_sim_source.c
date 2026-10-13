// SPDX-License-Identifier: Apache-2.0
#include "exp_sim_source.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define EFFECT_RING 128

typedef struct {
    exp_app_t *app;
    uint32_t tick;
    float tgt_pitch, tgt_roll, tgt_yaw, tgt_flip;
    float pitch, roll, yaw, flip;
    float M[9], Mprev[9];
    bool have_prev;
    float shake_s, spin_s, spin_t;
    int mic_mode;
    float mic_db;
    uint64_t samples;
    uint32_t rng;
    int left, right;
    bool batt_present, charging;
    uint8_t batt;
    bool imu_fault, mag_present, heap_demo;
    float joy[2];
    bool joy_cal;
    bool mag_disturb;
    int face_state;               /* -1 starting, 0 none, 1 found (default 1 when camera in left slot) */
    int face_x, face_y, face_w, face_h, face_conf, face_ms;
    int ai;                       /* exp_ai_state_t */
    bool voice_listening;
    int voice_event, voice_cmd, voice_prob, voice_wake;
    uint16_t voice_seq;
    exp_effect_t ring[EFFECT_RING];
    int ring_head, ring_n;
    exp_sensors_t s;
} sim_t;

static sim_t g;

static float frand(void)
{
    g.rng ^= g.rng << 13;
    g.rng ^= g.rng >> 17;
    g.rng ^= g.rng << 5;
    return (float)(int32_t)g.rng / 2147483648.0f;
}

static float wrap180(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static void defaults(void)
{
    memset(&g, 0, sizeof(g));
    g.rng = 0x2468ACE1u;
    g.mic_mode = 1;
    g.mic_db = -22.0f;
    g.batt_present = true;
    g.batt = 82;
    g.mag_present = true;
    g.face_state = 1;
    g.face_x = 140;
    g.face_y = 140;
    g.face_w = 200;
    g.face_h = 200;
    g.face_conf = 87;
    g.face_ms = 47;
}

void exp_sim_attach(exp_app_t *app)
{
    defaults();
    g.app = app;
}

void exp_sim_detach(void) { memset(&g, 0, sizeof(g)); }

/* Body->world rotation (rows: north, west, up expressed in body axes), matching
 * exp_fusion_matrix(). `heading` is the compass heading of the top edge. */
static void build_matrix(float pitch, float roll, float heading, float flip, float M[9])
{
    const float p = pitch * PI_F / 180.0f, r = roll * PI_F / 180.0f, h = heading * PI_F / 180.0f;
    /* Elevation p of the top edge, then roll r about the top edge axis (matches exp_fusion_euler). */
    float u[3] = {-sinf(r) * cosf(p), sinf(p), cosf(r) * cosf(p)};
    const float a = flip * PI_F / 180.0f, ca = cosf(a), sa = sinf(a);
    const float x = u[0] * ca + u[2] * sa, z = -u[0] * sa + u[2] * ca;
    u[0] = x; u[2] = z;
    /* t: horizontal projection of body +Y. */
    float t[3] = {-u[1] * u[0], 1.0f - u[1] * u[1], -u[1] * u[2]};
    float tn = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (tn < 1e-4f) { t[0] = 1.0f; t[1] = 0.0f; t[2] = 0.0f; tn = 1.0f; }  /* board vertical: pick body +X */
    for (int i = 0; i < 3; ++i) t[i] /= tn;
    const float q[3] = {u[1] * t[2] - u[2] * t[1], u[2] * t[0] - u[0] * t[2], u[0] * t[1] - u[1] * t[0]};
    const float ch = cosf(h), sh = sinf(h);
    for (int i = 0; i < 3; ++i) {
        M[0 + i] = ch * t[i] + sh * q[i];   /* north */
        M[3 + i] = -sh * t[i] + ch * q[i];  /* west  */
        M[6 + i] = u[i];                    /* up    */
    }
}

static void emit_effects(void)
{
    exp_effect_t fx[EXP_EFFECT_QUEUE];
    const size_t n = exp_app_pop_effects(g.app, fx, EXP_EFFECT_QUEUE);
    for (size_t i = 0; i < n; ++i) {
        g.ring[(g.ring_head + g.ring_n) % EFFECT_RING] = fx[i];
        if (g.ring_n < EFFECT_RING) ++g.ring_n;
        else g.ring_head = (g.ring_head + 1) % EFFECT_RING;
    }
}

static void synth_mic(int16_t *pcm)
{
    const uint64_t n0 = g.samples;
    const float amp = powf(10.0f, g.mic_db / 20.0f);
    for (int i = 0; i < EXP_PCM_N; ++i) {
        const double t = (double)(n0 + (uint64_t)i) / 16000.0;
        float v = 0.0f;
        switch (g.mic_mode) {
        case 1: v = 0.0012f * frand(); break;
        case 2: v = amp * sinf((float)fmod(2.0 * (double)PI_F * 1000.0 * t, 2.0 * (double)PI_F)); break;
        case 3: {
            const double f0 = 130.0 + 25.0 * sin(2.0 * (double)PI_F * 0.7 * t);
            const double ph = 2.0 * (double)PI_F * 130.0 * t - (25.0 / 0.7) * cos(2.0 * (double)PI_F * 0.7 * t);
            const float env = powf(fmaxf(0.0f, 0.5f + 0.5f * sinf((float)(2.0 * (double)PI_F * 3.5 * t))), 0.6f);
            float acc = 0.0f;
            for (int k = 1; k <= 24; ++k) {
                const float fk = (float)(f0 * k);
                if (fk > 6000.0f) break;
                const float w = expf(-powf((fk - 700.0f) / 260.0f, 2.0f)) + 0.7f * expf(-powf((fk - 1200.0f) / 300.0f, 2.0f)) +
                                0.35f * expf(-powf((fk - 2600.0f) / 420.0f, 2.0f)) + 0.02f;
                acc += w * sinf((float)fmod((double)k * ph, 2.0 * (double)PI_F)) / (float)k;
            }
            v = amp * 1.6f * env * acc + 0.0012f * frand();
            break;
        }
        case 4: v = amp * frand(); break;
        case 5: {
            const double tp = fmod(t, 2.0);
            v = amp * sinf((float)fmod(2.0 * (double)PI_F * (200.0 * tp + 1450.0 * tp * tp), 2.0 * (double)PI_F));
            break;
        }
        default: break;
        }
        const float c = fmaxf(-1.0f, fminf(1.0f, v));
        pcm[i] = (int16_t)lroundf(c * 32767.0f);
    }
    g.samples += (uint64_t)(16000.0f / (float)EXP_TICK_HZ + 0.5f);
}

void exp_sim_tick(float dt)
{
    if (!g.app) return;
    ++g.tick;
    /* Automatic figure-eight sweep used to calibrate the magnetometer. */
    if (g.spin_s > 0.0f) {
        g.spin_s -= dt;
        g.spin_t += dt;
        g.tgt_yaw = wrap180(g.tgt_yaw + 110.0f * dt);
        g.tgt_pitch = 75.0f * sinf(0.9f * g.spin_t);
        g.tgt_roll = 75.0f * sinf(0.65f * g.spin_t + 1.3f);
        g.tgt_flip = sinf(0.33f * g.spin_t) < -0.2f ? 180.0f : 0.0f;
    }
    const float k = 1.0f - expf(-dt / 0.12f);
    g.pitch += (g.tgt_pitch - g.pitch) * k;
    g.roll += (g.tgt_roll - g.roll) * k;
    g.yaw = wrap180(g.yaw + wrap180(g.tgt_yaw - g.yaw) * k);
    g.flip += (g.tgt_flip - g.flip) * k;

    memcpy(g.Mprev, g.M, sizeof(g.M));
    build_matrix(g.pitch, g.roll, g.yaw, g.flip, g.M);
    float w[3] = {0, 0, 0};
    if (g.have_prev) {
        /* A = Mprev * M^T gives the body-frame increment; extract the skew part. */
        float A[9];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                float acc = 0.0f;
                for (int c = 0; c < 3; ++c) acc += g.Mprev[c * 3 + i] * g.M[c * 3 + j];
                A[i * 3 + j] = acc;
            }
        w[0] = 0.5f * (A[7] - A[5]) / dt;
        w[1] = 0.5f * (A[2] - A[6]) / dt;
        w[2] = 0.5f * (A[3] - A[1]) / dt;
        for (int i = 0; i < 3; ++i) w[i] *= 180.0f / PI_F;
    }
    g.have_prev = true;

    exp_sensors_t *s = &g.s;
    memset(s, 0, sizeof(*s));
    s->now_ms = g.tick * 1000U / EXP_TICK_HZ;
    s->uptime_s = g.tick / EXP_TICK_HZ;
    s->flags = EXP_SENS_SIMULATED | EXP_SENS_BATT * (g.batt_present ? 1U : 0U);
    s->fw_version = "sim-0.1.0";
    s->rf = EXP_RF_UNKNOWN;       /* a desktop simulator cannot observe radios */
    s->ai = (exp_ai_state_t)g.ai;
    s->voice_listening = g.voice_listening;
    s->voice_event = (exp_voice_event_t)g.voice_event;
    s->voice_cmd = (exp_voice_cmd_t)g.voice_cmd;
    s->voice_prob_pct = (uint8_t)g.voice_prob;
    s->voice_wake_idx = (uint8_t)g.voice_wake;
    s->voice_seq = g.voice_seq;
    s->slot_left = (exp_module_t)g.left;
    s->slot_right = (exp_module_t)g.right;
    s->battery_pct = g.batt;
    s->charging = g.charging;

    if (!g.imu_fault) {
        s->flags |= EXP_SENS_IMU;
        float shake[3] = {0, 0, 0}, gshake[3] = {0, 0, 0};
        if (g.shake_s > 0.0f) {
            const float ph = 2.0f * PI_F * 6.0f * (1.0f - g.shake_s);
            shake[0] = 1.6f * sinf(ph);
            shake[1] = 0.8f * sinf(ph * 1.3f);
            gshake[0] = 220.0f * cosf(ph);
            gshake[1] = 120.0f * cosf(ph * 1.3f);
            g.shake_s -= dt;
        }
        for (int i = 0; i < 3; ++i) {
            s->acc[i] = g.M[6 + i] + shake[i] + 0.004f * frand();
            s->gyr[i] = w[i] + gshake[i] + 0.15f * frand();
        }
        s->gyr[0] += 0.35f; s->gyr[1] -= 0.25f; s->gyr[2] += 0.5f;  /* constant gyro bias */
    }
    if (g.mag_present) {
        s->flags |= EXP_SENS_MAG;
        static const float hard[2][3] = {{8.0f, -5.0f, 12.0f}, {-6.0f, 9.0f, -4.0f}};
        const float bn = 30.0f, bu = -35.0f;  /* Tokyo-like: ~30 uT horizontal, ~35 uT downward */
        for (int m = 0; m < 2; ++m)
            for (int i = 0; i < 3; ++i)
                s->mag[m][i] = bn * g.M[0 + i] + bu * g.M[6 + i] + hard[m][i] + 0.3f * frand() + (g.mag_disturb && i == 0 ? 150.0f : 0.0f);
    }
    if (g.mic_mode != 0) {
        s->flags |= EXP_SENS_MIC;
        synth_mic(s->pcm);
        s->pcm_count = EXP_PCM_N;
    }
    if (g.left == EXP_MOD_JOYSTICK || g.right == EXP_MOD_JOYSTICK) {
        if (g.joy_cal) {
            s->joy_state = EXP_JOY_CALIBRATING;   /* like the device: no axis data until the stick has been circled */
        } else {
            s->flags |= EXP_SENS_JOY;
            s->joy_state = EXP_JOY_READY;
            s->joy[0] = g.joy[0];
            s->joy[1] = g.joy[1];
        }
    }
    if (g.left == EXP_MOD_CAMERA && exp_app_want_camera(g.app) && g.face_state >= 0) {
        s->flags |= EXP_SENS_FACE;
        s->face_found = g.face_state > 0;
        s->face_x = (int16_t)g.face_x;
        s->face_y = (int16_t)g.face_y;
        s->face_w = (int16_t)g.face_w;
        s->face_h = (int16_t)g.face_h;
        s->face_conf_pct = (uint8_t)g.face_conf;
        s->face_infer_ms = (uint16_t)g.face_ms;
    }
    if (g.heap_demo) {
        s->heap_internal_total = 512U * 1024U; s->heap_internal_free = 301U * 1024U;
        s->heap_psram_total = 16U * 1024U * 1024U; s->heap_psram_free = 12U * 1024U * 1024U + 400U * 1024U;
        s->heap_largest_free = 11U * 1024U * 1024U;
    }
    exp_app_set_sensors(g.app, s);
    exp_app_update(g.app, dt);
    emit_effects();
}

/* ---- Controls ---------------------------------------------------------------- */
void exp_sim_set_attitude(float p, float r, float y)
{
    if (p > 80.0f) p = 80.0f;
    if (p < -80.0f) p = -80.0f;
    if (r > 80.0f) r = 80.0f;
    if (r < -80.0f) r = -80.0f;
    g.tgt_pitch = p; g.tgt_roll = r; g.tgt_yaw = wrap180(y);
    g.spin_s = 0.0f;
}
void exp_sim_set_flip(int f) { g.tgt_flip = f ? 180.0f : 0.0f; }
void exp_sim_shake(void) { g.shake_s = 1.0f; }
void exp_sim_spin(int on) { g.spin_s = on ? 40.0f : 0.0f; if (on) g.spin_t = 0.0f; }
void exp_sim_set_mic(int mode, float db) { g.mic_mode = mode; g.mic_db = db; }
void exp_sim_set_modules(int l, int r) { g.left = l; g.right = r; }
void exp_sim_set_battery(int present, int pct, int charging)
{
    g.batt_present = present != 0;
    g.batt = (uint8_t)(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
    g.charging = charging != 0;
}
void exp_sim_set_imu_fault(int f) { g.imu_fault = f != 0; }
void exp_sim_set_mag_present(int p) { g.mag_present = p != 0; }
void exp_sim_set_joystick(float x, float y) { g.joy[0] = x; g.joy[1] = y; }
void exp_sim_set_mag_disturb(int on) { g.mag_disturb = on != 0; }
void exp_sim_set_joy_calibrating(int on) { g.joy_cal = on != 0; }
void exp_sim_set_face(int found, int x, int y, int w, int h, int conf_pct, int infer_ms)
{
    g.face_state = found;
    g.face_x = x;
    g.face_y = y;
    g.face_w = w;
    g.face_h = h;
    g.face_conf = conf_pct < 0 ? 0 : (conf_pct > 100 ? 100 : conf_pct);
    g.face_ms = infer_ms < 0 ? 0 : infer_ms;
}
void exp_sim_set_voice_state(int ai_state)
{
    g.ai = ai_state < 0 ? 0 : (ai_state > (int)EXP_AI_ERROR ? (int)EXP_AI_ERROR : ai_state);
    if (g.ai != (int)EXP_AI_READY) g.voice_listening = false;
}
void exp_sim_voice_event(int event, int cmd, int prob_pct, int wake_idx)
{
    g.voice_event = event;
    g.voice_cmd = cmd;
    g.voice_prob = prob_pct;
    g.voice_wake = wake_idx;
    g.voice_listening = event == (int)EXP_VEV_WAKE;
    ++g.voice_seq;
}
void exp_sim_set_heap_demo(int on) { g.heap_demo = on != 0; }
void exp_sim_set_debug(uint32_t f) { if (g.app) exp_app_debug_flags(g.app, f); }
void exp_sim_goto(int page) { if (g.app) exp_app_debug_goto(g.app, (exp_page_t)page); }
void exp_sim_game_start(int diff, int demo) { if (g.app) exp_app_debug_game_start(g.app, diff, demo != 0); }

int exp_sim_pop_effects(uint32_t *out, int max)
{
    int n = 0;
    while (g.ring_n > 0 && n < max) {
        const exp_effect_t *e = &g.ring[g.ring_head];
        out[n * 3] = (uint32_t)e->type;
        out[n * 3 + 1] = e->arg;
        out[n * 3 + 2] = e->at_ms;
        g.ring_head = (g.ring_head + 1) % EFFECT_RING;
        --g.ring_n;
        ++n;
    }
    return n;
}

void exp_sim_get_attitude(float *out3)
{
    out3[0] = g.pitch; out3[1] = g.roll; out3[2] = g.yaw;
}
