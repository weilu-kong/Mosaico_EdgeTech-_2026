// SPDX-License-Identifier: Apache-2.0
// Expansion slots (stage 3c): which module sits in the left / right slot, read through the official module manager
// (BSP component mosaico_module_mgr). The manager only probes the slot EEPROM (AT24C02, I2C address-select GPIOs 14/39,
// bus GPIO0/GPIO1) and reads its descriptor; this firmware never claims a module, never starts a module driver and
// never talks to a module beyond that. A module that is present but not one of the four reviewed types, or whose
// descriptor does not validate, is reported as "unknown": the exhibition has not reviewed it (RF cannot be vouched for).
// No radio. NOT YET RUN ON THE BOARD.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dev_io_core.h"

#ifndef EXP_USE_SLOTS
#define EXP_USE_SLOTS 1
#endif

typedef struct {
    const char *state;               /* "off" | "starting" | "ok" | "init failed" | "skipped (crash guard)"      */
    dev_slot_class_t cls[2];         /* [0] left, [1] right                                                       */
    uint8_t presence[2];             /* 0 unknown, 1 absent, 2 present                                            */
    bool descriptor_valid[2];
    uint8_t board_type[2];           /* valid only when descriptor_valid                                          */
    int last_error[2];               /* esp_err_t of the manager, 0 = none                                        */
    uint32_t polls;
} dev_slots_t;

/* Sensor-task thread only (it starts the manager, which creates the subboard I2C bus). */
void dev_slots_open(void);
void dev_slots_mark_skipped(void);
void dev_slots_poll(void);           /* cheap; call a few times per second from the same task                     */
void dev_slots_take(dev_slots_t *out);
