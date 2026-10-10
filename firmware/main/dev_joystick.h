// SPDX-License-Identifier: Apache-2.0
// Joystick module (stage 3d): the official mosaico_module_joystick driver (ADC + GPIO, no radio) used ONLY when the slot
// manager has identified a valid descriptor of the Handle/Joystick board type. Any other module stays unclaimed.
// The driver runs its own calibration (circle the stick, then centre); no axis data is reported before it is READY.
// No radio. NOT YET RUN ON THE BOARD.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dev_io_core.h"
#include "dev_slots.h"

#ifndef EXP_USE_JOYSTICK
#define EXP_USE_JOYSTICK 1
#endif
#if !EXP_USE_SLOTS
#undef EXP_USE_JOYSTICK
#define EXP_USE_JOYSTICK 0      /* the module can only be found through the slot manager */
#endif
#define DEV_JOY_FRESH_MS 200

typedef struct {
    const char *state;          /* "off" | "no module" | "claiming" | "calibrating: circle" | "calibrating: centre" | "ready" |
                                   "claim failed" | "read failed" | "removed" | "skipped (crash guard)"                    */
    bool calibrating;           /* claimed, not READY yet                                                                    */
    bool ready;                 /* READY and a sample younger than DEV_JOY_FRESH_MS: x/y/buttons are valid                  */
    float x, y;                 /* application frame (dev_joy_map)                                                           */
    uint8_t buttons;
    int raw_x, raw_y;           /* driver ADC values, for choosing the axis mapping                                           */
    float drv_x, drv_y;         /* driver-normalised values before the mapping                                               */
    int8_t slot;                /* 0 left, 1 right, -1 none                                                                  */
    uint32_t reads, errors, claims, claim_errors;
    int last_error;
} dev_joystick_t;

/* io task only. Cheap when nothing is plugged in; call every ~20 ms. */
void dev_joystick_poll(const dev_slots_t *slots, dev_guard_t *guard);
void dev_joystick_take(dev_joystick_t *out);
