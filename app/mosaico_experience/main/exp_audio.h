// SPDX-License-Identifier: Apache-2.0
// Microphone analysis: level meter, 24-band spectrum (256-point FFT, Hann
// window), waveform decimation and an energy/spectrum voice-activity detector.
// This is classic DSP, not a neural network: the UI never presents it as AI
// speech recognition. Hardware free; fed with PCM by the platform.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "exp_config.h"

typedef struct {
    float level_db;        /* smoothed RMS, dBFS (-90..0)                      */
    float peak_db;         /* peak hold with slow release                      */
    float noise_db;        /* adaptive noise floor estimate                    */
    float bars[EXP_SPEC_BARS];      /* 0..1 smoothed band energy               */
    float bar_peak[EXP_SPEC_BARS];  /* 0..1 peak caps                          */
    float wave[EXP_WAVE_PTS];       /* -1..1 decimated waveform                */
    float voice_ratio;     /* share of energy in 300-3400 Hz                   */
    float low_ratio;       /* share of energy in 125-1000 Hz (speech is bottom heavy) */
    bool signal_present;   /* the platform delivered samples this frame        */
    bool vad;              /* voice activity decision (with hang-over)         */
    uint8_t on_frames, off_frames;
    uint32_t frames;
} exp_audio_t;

void exp_audio_init(exp_audio_t *a);
/* n == 0 means "no microphone data": meters decay and signal_present clears. */
void exp_audio_update(exp_audio_t *a, const int16_t *pcm, uint16_t n);
