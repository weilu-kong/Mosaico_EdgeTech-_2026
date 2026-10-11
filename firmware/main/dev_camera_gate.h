// SPDX-License-Identifier: Apache-2.0
// Decides whether the camera preview may be started, from what the slot manager has read out of the left slot's EEPROM.
// Pure logic, host-tested (app/mosaico_experience/tests/dev_camera_core_test.c). No radio.
//
// The BSP camera driver drives the left slot's pins as a DVP bus whatever is plugged in ("allow_unidentified"), and it takes
// over the USB Serial/JTAG pad (camera D2). So the preview is refused when a different, identified module sits in the left
// slot, and a module with an unreadable descriptor (a bring-up camera board with an unprogrammed EEPROM) is accepted only as
// an explicit, visible exception.
#pragma once

#include <stdbool.h>

#include "dev_io_core.h"

typedef struct {
    bool allow;
    bool allow_unidentified;     /* open the slot although no valid camera descriptor was read (pass to the BSP driver) */
    const char *reason;          /* short text for the panel / log                                                       */
} dev_cam_gate_t;

/* descriptor_valid: the descriptor in the EEPROM validated. An UNKNOWN module with a VALID descriptor is some other board that the
 * exhibition has not reviewed: never opened as a camera. Only a module whose descriptor could not be read is a bring-up candidate. */
static inline dev_cam_gate_t dev_cam_gate(dev_slot_class_t left, bool descriptor_valid)
{
    switch (left) {
    case DEV_SLOT_CAMERA: return (dev_cam_gate_t){true, false, "camera module identified in the left slot"};
    case DEV_SLOT_UNKNOWN:
        if (descriptor_valid) return (dev_cam_gate_t){false, false, "an unreviewed module (valid descriptor, not a camera) sits in the left slot"};
        return (dev_cam_gate_t){true, true, "left slot: module present but descriptor not valid (accepted for bring-up)"};
    case DEV_SLOT_NONE: return (dev_cam_gate_t){false, false, "no module in the left slot"};
    case DEV_SLOT_NOT_PROBED: return (dev_cam_gate_t){false, false, "slots not probed yet"};
    case DEV_SLOT_JOYSTICK:
    case DEV_SLOT_INTERACT:
    case DEV_SLOT_BUTTON_LED:
    default: return (dev_cam_gate_t){false, false, "another module sits in the left slot (camera is left-slot only)"};
    }
}
