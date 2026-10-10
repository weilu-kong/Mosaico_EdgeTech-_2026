// SPDX-License-Identifier: Apache-2.0
// Offline Japanese/English voice control (stage 3e) on the ESP-SR speech stack (AFE + WakeNet + MultiNet), following the
// owner's "Full Demo" project. Everything runs on the chip: no network, no cloud, no radio. Audio is processed in RAM only:
// nothing is recorded, stored or sent anywhere.
//
// Built only with EXP_VOICE=1 (separate build directory and sdkconfig, see docs/11_VOICE_SETUP.md).
// NOT YET RUN ON THE BOARD: the state starts as "loading" and becomes "ready" only after the models and the command list
// really loaded; a recognised command is reported only when MultiNet really returned it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dev_voice_core.h"

typedef struct {
    dev_vstate_t vs;          /* phase, last event/command, counters                                                   */
    const char *detail;       /* "" or a short English reason for ERROR / SKIPPED (log view only)                       */
    bool wake2_loaded;        /* the second wake word model (Japanese) was found in the model partition                 */
    uint8_t mic_slot;         /* I2S slot used for speech (0 = left, 1 = right)                                         */
    uint32_t ring_dropped;    /* 16 kHz samples dropped because the speech engine could not keep up                     */
    uint32_t fetch_loops;     /* AFE results received since boot (proves the engine is running)                         */
    int32_t load_ms;          /* time the models took to load (-1 = not finished)                                       */
    uint32_t heap_internal_free, heap_psram_free;   /* just after loading                                              */
} dev_voice_status_t;

/* Callbacks of the device layer (the component cannot see the main component):
 *  guard(true/false): mark the model load as "in flight" / finished in the start-up crash guard (step DEV_STEP_VOICE);
 *  mic_ready():       true while the microphone task delivers audio. */
typedef struct {
    void (*guard)(bool begin);
    bool (*mic_ready)(void);
} dev_voice_hooks_t;

/* Starts the loader task. Call once from the I/O task after the audio codecs were opened. */
void dev_voice_open(const dev_voice_hooks_t *hooks);
void dev_voice_mark_skipped(void);                       /* the crash guard refused the step */
void dev_voice_take(dev_voice_status_t *out);

/* Microphone task (any thread, never blocks): one codec chunk of interleaved 48 kHz stereo frames. */
void dev_voice_feed_stereo(const int16_t *st, int frames);
/* Speaker task: a cue starts / ends. The microphone is fed silence meanwhile (no echo cancellation is used). */
void dev_voice_speaker_active(bool active);
