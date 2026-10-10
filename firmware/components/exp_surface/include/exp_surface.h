// SPDX-License-Identifier: Apache-2.0
// Frame buffer owner for the Route B drawing shim.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define EXP_SURFACE_W 480
#define EXP_SURFACE_H 480

/* Called from EndDrawing with the finished frame (RGB565, native byte order). */
typedef void (*exp_surface_present_fn)(const uint16_t *pixels, size_t stride_px, void *ctx);

/* `pixels` must hold EXP_SURFACE_H rows of `stride_px` uint16_t and outlive the surface. */
void exp_surface_attach(uint16_t *pixels, size_t stride_px, exp_surface_present_fn present, void *ctx);
uint32_t exp_surface_frames_presented(void);

/* Double buffering: choose the buffer the NEXT BeginDrawing() renders into. Call from the present
 * callback (or between frames) once the finished frame has been handed to the panel. */
void exp_surface_set_target(uint16_t *pixels);
