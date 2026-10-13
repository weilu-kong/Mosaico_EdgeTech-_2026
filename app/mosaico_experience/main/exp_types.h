// SPDX-License-Identifier: Apache-2.0
// Platform <-> application contract. The application core is hardware free:
// the platform (Host simulator or device adapter) pushes an exp_sensors_t
// snapshot in and drains exp_effect_t requests out. Nothing in the core calls
// a peripheral, Wi-Fi, Bluetooth, ESP-NOW or 802.15.4 API.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "exp_config.h"

/* Validity / provenance flags for exp_sensors_t.flags. A flag must only be set
 * when the platform really produced that data. */
enum {
    EXP_SENS_IMU = 1u << 0,
    EXP_SENS_MAG = 1u << 1,
    EXP_SENS_MIC = 1u << 2,
    EXP_SENS_BATT = 1u << 3,
    EXP_SENS_JOY = 1u << 4,
    EXP_SENS_FACE = 1u << 5,      /* camera + face detector running in 480x480 view */
    EXP_SENS_SIMULATED = 1u << 8, /* every value is synthetic (Host only)   */
};

typedef enum {
    EXP_MOD_NONE = 0,
    EXP_MOD_JOYSTICK,
    EXP_MOD_INTERACT,
    EXP_MOD_BUTTON_LED,
    EXP_MOD_CAMERA,
    EXP_MOD_UNKNOWN,    /* present, but not one of the four reviewed modules */
    EXP_MOD_NOT_PROBED, /* the platform has not (yet) read the slot       */
    EXP_MOD_COUNT
} exp_module_t;

typedef enum {
    EXP_JOY_NONE = 0,       /* no joystick module in use                    */
    EXP_JOY_CALIBRATING,    /* module claimed; the stick must be circled    */
    EXP_JOY_READY,          /* calibrated: joy[] / joy_buttons are valid    */
} exp_joy_state_t;

typedef enum {
    EXP_RF_UNKNOWN = 0,     /* platform cannot tell (simulator)             */
    EXP_RF_SOFT_OFF_OK,     /* all software-visible radio states are off    */
    EXP_RF_SOFT_CHECK_FAIL, /* a radio subsystem reported an active state   */
} exp_rf_state_t;

typedef enum {
    EXP_AI_NOT_INTEGRATED = 0, /* no verified on-device model yet           */
    EXP_AI_LOADING,
    EXP_AI_READY,
    EXP_AI_ERROR,
} exp_ai_state_t;

/* Offline voice control (stage 3e). The platform reports only what the speech engine really produced. */
typedef enum {
    EXP_VCMD_NONE = 0,
    EXP_VCMD_HOME,
    EXP_VCMD_MOTION,
    EXP_VCMD_AUDIO,
    EXP_VCMD_GAME,
    EXP_VCMD_DEVICE,
    EXP_VCMD_COUNT
} exp_voice_cmd_t;

typedef enum {
    EXP_VEV_NONE = 0,
    EXP_VEV_WAKE,      /* a wake word was heard: the command window is open */
    EXP_VEV_COMMAND,   /* a command was recognised (voice_cmd)               */
    EXP_VEV_TIMEOUT,   /* the command window closed without a command        */
} exp_voice_event_t;

typedef struct {
    uint32_t now_ms;      /* monotonic platform clock                        */
    uint32_t uptime_s;
    uint32_t flags;       /* EXP_SENS_*                                      */

    /* UI frame: +X toward the right edge of the display, +Y toward the top
     * edge, +Z out of the glass toward the viewer. A board lying face-up on a
     * table reads acc = (0, 0, +1 g). The device adapter maps BMI270/BMM150
     * axes into this frame; the core never sees chip axes. */
    float acc[3];         /* g                                               */
    float gyr[3];         /* deg/s                                           */
    float mag[2][3];      /* uT, two BMM150 in the UI frame                  */

    int16_t pcm[EXP_PCM_N]; /* newest 256 mono microphone samples, 16 kHz    */
    uint16_t pcm_count;     /* number of valid samples in pcm (0 = no audio) */

    float joy[2];         /* -1..1, +X right, +Y up (valid with EXP_SENS_JOY) */
    uint8_t joy_buttons;  /* bit n = button n (B,1,2,3,4) */
    exp_joy_state_t joy_state;

    bool face_found;      /* valid with EXP_SENS_FACE                        */
    int16_t face_x, face_y, face_w, face_h; /* 480x480 view coordinates      */
    uint8_t face_conf_pct;/* 0..100 confidence                               */
    uint16_t face_infer_ms;/* last inference latency in ms                   */

    uint8_t battery_pct;  /* valid with EXP_SENS_BATT                        */
    bool charging;

    exp_module_t slot_left;
    exp_module_t slot_right;

    uint32_t heap_internal_free, heap_internal_total; /* bytes               */
    uint32_t heap_psram_free, heap_psram_total;
    uint32_t heap_largest_free;

    exp_rf_state_t rf;
    exp_ai_state_t ai;       /* state of the offline speech engine: never READY unless it really loaded */
    bool voice_listening;    /* the command window is open (after a wake word)                           */
    exp_voice_event_t voice_event;  /* last engine event; a new event is signalled by voice_seq changing  */
    exp_voice_cmd_t voice_cmd;      /* last recognised command                                            */
    uint8_t voice_wake_idx;         /* 1 = first wake word, 2 = second (0 = none yet)                     */
    uint8_t voice_prob_pct;         /* confidence of the last command, 0..100                              */
    uint16_t voice_seq;
    uint32_t dsp_time_us; /* last platform-measured audio DSP duration (0=n/a) */
    const char *fw_version;
} exp_sensors_t;

/* Maps a 480x480 face bounding box to normalized maze tilt acceleration (ax, ay) in [-0.6, +0.6].
 * Deadzone of 12 px around the frame centre (240, 240); full deflection (0.6) at 110 px offset. */
static inline void exp_face_box_to_tilt(int x, int y, int w, int h, float *ax, float *ay)
{
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const int dx = cx - 240;
    const int dy = cy - 240;
    const int dead = 12;
    const float span = 98.0f; /* 110 - 12 */
    float ox = 0.0f, oy = 0.0f;
    if (dx > dead) ox = (float)(dx - dead) / span * 0.6f;
    else if (dx < -dead) ox = (float)(dx + dead) / span * 0.6f;
    if (dy > dead) oy = (float)(dy - dead) / span * 0.6f;
    else if (dy < -dead) oy = (float)(dy + dead) / span * 0.6f;
    if (ox > 0.6f) ox = 0.6f;
    if (ox < -0.6f) ox = -0.6f;
    if (oy > 0.6f) oy = 0.6f;
    if (oy < -0.6f) oy = -0.6f;
    if (ax) *ax = ox;
    if (ay) *ay = oy;
}

typedef enum {
    EXP_EFFECT_NONE = 0,
    EXP_EFFECT_HAPTIC,  /* arg = pulse length in ms (motor, GPIO8)            */
    EXP_EFFECT_SOUND,   /* arg = exp_sound_t cue to play through ES8311       */
} exp_effect_type_t;

typedef enum {
    EXP_SND_TAP = 0,
    EXP_SND_NAV,
    EXP_SND_SUCCESS,
    EXP_SND_FAIL,
    EXP_SND_HIT,
    EXP_SND_COUNTDOWN,
    EXP_SND_GO,
    EXP_SND_TEST_TONE,
    EXP_SND_COUNT
} exp_sound_t;

typedef struct {
    exp_effect_type_t type;
    uint16_t arg;
    uint32_t at_ms;
} exp_effect_t;

#define EXP_EFFECT_QUEUE 16
