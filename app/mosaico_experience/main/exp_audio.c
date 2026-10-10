// SPDX-License-Identifier: Apache-2.0
#include "exp_audio.h"

#include <math.h>
#include <string.h>

#define FFT_N 256
#define PI_F 3.14159265358979f

static float s_window[FFT_N];
static float s_cos[FFT_N / 2], s_sin[FFT_N / 2];
static uint16_t s_rev[FFT_N];
static uint8_t s_edges[EXP_SPEC_BARS + 1];
static bool s_ready;

static void tables_init(void)
{
    if (s_ready) return;
    for (int i = 0; i < FFT_N; ++i) {
        s_window[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i / (float)(FFT_N - 1));
        int r = 0;
        for (int b = 0; b < 8; ++b) r |= ((i >> b) & 1) << (7 - b);
        s_rev[i] = (uint16_t)r;
    }
    for (int i = 0; i < FFT_N / 2; ++i) {
        s_cos[i] = cosf(2.0f * PI_F * (float)i / (float)FFT_N);
        s_sin[i] = -sinf(2.0f * PI_F * (float)i / (float)FFT_N);
    }
    /* Log-spaced band edges over bins 1..127 (62.5 Hz .. 7.9 kHz at 16 kHz). */
    const float ratio = powf(127.0f, 1.0f / (float)EXP_SPEC_BARS);
    int prev = 1;
    s_edges[0] = 1;
    for (int k = 1; k <= EXP_SPEC_BARS; ++k) {
        int e = (int)lroundf(powf(ratio, (float)k));
        if (e <= prev) e = prev + 1;
        if (e > 128) e = 128;
        s_edges[k] = (uint8_t)e;
        prev = e;
    }
    s_edges[EXP_SPEC_BARS] = 128;
    s_ready = true;
}

static void fft(float *re, float *im)
{
    for (int i = 0; i < FFT_N; ++i) {
        const int j = s_rev[i];
        if (j > i) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int size = 2; size <= FFT_N; size <<= 1) {
        const int half = size >> 1, step = FFT_N / size;
        for (int start = 0; start < FFT_N; start += size) {
            for (int k = 0, tw = 0; k < half; ++k, tw += step) {
                const int a = start + k, b = a + half;
                const float tr = re[b] * s_cos[tw] - im[b] * s_sin[tw];
                const float ti = re[b] * s_sin[tw] + im[b] * s_cos[tw];
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
        }
    }
}

void exp_audio_init(exp_audio_t *a)
{
    memset(a, 0, sizeof(*a));
    a->level_db = a->peak_db = -90.0f;
    a->noise_db = -60.0f;
    tables_init();
}

static float to_db(float lin) { return 20.0f * log10f(lin + 1e-9f); }

void exp_audio_update(exp_audio_t *a, const int16_t *pcm, uint16_t n)
{
    tables_init();
    ++a->frames;
    if (!pcm || n < FFT_N) {
        a->signal_present = false;
        a->level_db = fmaxf(-90.0f, a->level_db - 3.0f);
        a->peak_db = fmaxf(-90.0f, a->peak_db - 1.0f);
        for (int i = 0; i < EXP_SPEC_BARS; ++i) {
            a->bars[i] *= 0.85f;
            a->bar_peak[i] = fmaxf(a->bars[i], a->bar_peak[i] - 0.02f);
        }
        for (int i = 0; i < EXP_WAVE_PTS; ++i) a->wave[i] *= 0.8f;
        a->vad = false;
        a->on_frames = 0;
        return;
    }
    a->signal_present = true;
    const int16_t *x = pcm + (n - FFT_N);  /* newest 256 samples */

    float sum = 0.0f, peak = 0.0f;
    for (int i = 0; i < FFT_N; ++i) {
        const float v = (float)x[i] / 32768.0f;
        sum += v * v;
        if (fabsf(v) > peak) peak = fabsf(v);
    }
    const float level = to_db(sqrtf(sum / (float)FFT_N));
    const float peak_db = to_db(peak);
    a->level_db = level > a->level_db ? level : fmaxf(level, a->level_db - 3.0f);
    a->peak_db = peak_db > a->peak_db ? peak_db : fmaxf(peak_db, a->peak_db - 0.6f);

    for (int i = 0; i < EXP_WAVE_PTS; ++i) {
        a->wave[i] = ((float)x[i * 2] + (float)x[i * 2 + 1]) / 65536.0f;
    }

    float re[FFT_N], im[FFT_N];
    for (int i = 0; i < FFT_N; ++i) { re[i] = (float)x[i] * s_window[i] / 32768.0f; im[i] = 0.0f; }
    fft(re, im);

    float total = 1e-12f, voice = 0.0f, low = 0.0f;
    float mag[FFT_N / 2];
    for (int k = 1; k < FFT_N / 2; ++k) {
        mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]) / (FFT_N / 4.0f);  /* full-scale sine = 1 */
        const float e = mag[k] * mag[k];
        total += e;
        if (k >= 5 && k <= 54) voice += e;  /* 312 Hz .. 3.4 kHz */
        if (k >= 2 && k <= 16) low += e;    /* 125 Hz .. 1 kHz   */
    }
    a->voice_ratio = voice / total;
    a->low_ratio = low / total;
    for (int b = 0; b < EXP_SPEC_BARS; ++b) {
        float m = 0.0f;
        for (int k = s_edges[b]; k < s_edges[b + 1] && k < FFT_N / 2; ++k)
            if (mag[k] > m) m = mag[k];
        const float v = fminf(1.0f, fmaxf(0.0f, (to_db(m) + 75.0f) / 55.0f));
        a->bars[b] = v > a->bars[b] ? a->bars[b] + (v - a->bars[b]) * 0.7f : a->bars[b] * 0.88f + v * 0.12f;
        a->bar_peak[b] = fmaxf(a->bars[b], a->bar_peak[b] - 0.015f);
    }

    /* Noise floor: follows drops immediately, rises at ~1 dB/s. */
    if (level < a->noise_db) a->noise_db = level;
    else a->noise_db += 0.03f;
    if (a->noise_db > level) a->noise_db = level;
    if (a->noise_db < -80.0f) a->noise_db = -80.0f;

    const bool candidate = level > a->noise_db + 10.0f && level > -55.0f && a->voice_ratio > 0.45f &&
                           a->low_ratio > 0.30f;
    if (candidate) {
        a->off_frames = 0;
        if (a->on_frames < 255) ++a->on_frames;
        if (a->on_frames >= 3) a->vad = true;
    } else {
        a->on_frames = 0;
        if (a->off_frames < 255) ++a->off_frames;
        if (a->off_frames >= 12) a->vad = false;
    }
}
