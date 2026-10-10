// SPDX-License-Identifier: Apache-2.0
// Device microphone, speaker and vibration motor (stage 3c) through the official BSP (ES8311 over I2S, motor GPIO8).
// Nothing here records, stores or sends audio: the microphone samples go to the on-screen level meter / VAD only.
// No radio. NOT YET RUN ON THE BOARD: every status below starts as "starting" and only the real result changes it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dev_io_core.h"

#ifndef EXP_VOICE
#define EXP_VOICE 0              /* 1 = offline speech control (ESP-SR) in this build; set by CMake (-DEXP_VOICE=1)  */
#endif
#ifndef EXP_USE_AUDIO
#define EXP_USE_AUDIO 1          /* ES8311: microphone input and speaker output (0 = neither is touched)     */
#endif
#ifndef EXP_USE_MIC
#define EXP_USE_MIC 1            /* with EXP_USE_AUDIO: read the microphone                                   */
#endif
#ifndef EXP_USE_SPEAKER
#define EXP_USE_SPEAKER 1        /* with EXP_USE_AUDIO: play UI cues (0 = muted exhibition)                    */
#endif
#ifndef EXP_USE_MOTOR
#define EXP_USE_MOTOR 1          /* vibration pulses for events                                                */
#endif
#ifndef EXP_MIC_GAIN_DB
#if EXP_VOICE
#define EXP_MIC_GAIN_DB 34.0f    /* the value of the Full Demo voice service (speech recognition needs the headroom) */
#else
#define EXP_MIC_GAIN_DB 30.0f    /* the value of the official audio_loopback example                           */
#endif
#endif
#ifndef EXP_SPK_VOLUME
#define EXP_SPK_VOLUME 35        /* esp_codec_dev output volume 0..100: kept low on purpose (show floor)       */
#endif
#ifndef EXP_SPK_AMP
#define EXP_SPK_AMP 6000         /* peak sample value of a cue (about -15 dBFS)                                */
#endif
#ifndef EXP_MOTOR_STRENGTH
#define EXP_MOTOR_STRENGTH 60    /* percent PWM duty while a pulse runs                                        */
#endif
#define DEV_MIC_FRESH_MS 250

typedef struct {
    const char *mic_state;        /* "off" | "starting" | "ok" | "init failed" | "read failed" | "skipped (crash guard)" */
    const char *spk_state;        /* same vocabulary; "playing" is not a state: see spk_cues                            */
    const char *motor_state;
    bool mic_ok;                  /* a chunk younger than DEV_MIC_FRESH_MS exists                                       */
    int16_t pcm[DEV_MIC_OUT_N];   /* newest chunk, 16 kHz mono                                                          */
    uint16_t pcm_count;
    uint32_t mic_reads, mic_errors;
    int peak_l, peak_r;           /* both I2S slots of the newest chunk, to see which one carries the ES8311 signal     */
    uint8_t mic_slot;             /* slot in use: 0 = left, 1 = right                                                   */
    uint32_t spk_cues, spk_dropped, spk_errors;
    int last_cue;                 /* -1 = none yet */
    uint32_t haptic_pulses, haptic_denied, motor_errors;
} dev_audio_t;

/* Must be called from the thread that also does the other BSP peripheral initialisation (the sensor task): the BSP's
 * shared I2C bus is created lazily and that is not thread-safe. Starts the microphone and speaker tasks. */
void dev_audio_open_codecs(void);   /* ES8311 microphone + speaker; spawns the audio tasks on success */
void dev_motor_open(void);          /* LEDC/GPIO only, no I2C                                          */
void dev_audio_mark_skipped(bool audio, bool motor);   /* the crash guard refused the step: say so in the status */
void dev_audio_take(dev_audio_t *out);
bool dev_audio_mic_ready(void);      /* a fresh microphone chunk exists (used by the voice loader) */

/* Called from the UI thread. Never blocks. Cues arriving while the speaker is busy are dropped (spk_dropped). */
void dev_audio_play(int cue);
void dev_haptic_pulse(uint16_t ms);
