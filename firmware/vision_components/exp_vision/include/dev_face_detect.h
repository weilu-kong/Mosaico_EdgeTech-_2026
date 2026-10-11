// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dev_face_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void dev_face_detect_init(void);
void dev_face_detect_start(void);
void dev_face_detect_stop(void);
void dev_face_detect_get_result(dev_face_result_t *out);

// Called by camera pipeline when a new 480x480 frame is ready.
// Non-blocking: drops frame if previous inference is still running.
void dev_face_detect_submit_frame(const uint16_t *rgb565_frame);

#ifdef __cplusplus
}
#endif
