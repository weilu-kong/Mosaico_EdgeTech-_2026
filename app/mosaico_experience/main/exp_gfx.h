// SPDX-License-Identifier: Apache-2.0
// Anti-aliased 2D drawing helpers on top of the engine's RGB565 surface.
// All calls honour exp_gfx_set_origin() / exp_gfx_set_alpha() from exp_text.h.
#pragma once

#include "exp_text.h"

typedef enum {
    EXP_ICON_HOME,
    EXP_ICON_MOTION,
    EXP_ICON_AUDIO,
    EXP_ICON_GAME,
    EXP_ICON_DEVICE,
    EXP_ICON_CHEVRON_RIGHT,
    EXP_ICON_CHEVRON_LEFT,
    EXP_ICON_SPEAKER,
    EXP_ICON_PLAY,
    EXP_ICON_CLOSE,
    EXP_ICON_BATTERY,
    EXP_ICON_COUNT
} exp_icon_t;

Color exp_lerp(Color a, Color b, float t);
Color exp_with_alpha(Color c, float alpha);
float exp_clampf(float v, float lo, float hi);
float exp_ease_out(float t);
float exp_ease_in_out(float t);

void exp_rect(int x, int y, int w, int h, Color c);
void exp_rrect(int x, int y, int w, int h, int r, Color c);
void exp_panel(int x, int y, int w, int h, int r, Color fill, Color border);
void exp_gradient_v(int x, int y, int w, int h, Color top, Color bottom, int step);
void exp_disc(float cx, float cy, float r, Color c);
void exp_ring(float cx, float cy, float r_out, float r_in, Color c);
/* Angles in degrees, clockwise from 12 o'clock. Round caps when `caps`. */
void exp_arc(float cx, float cy, float r_out, float r_in, float a0, float a1, Color c,
             bool caps);
void exp_line(float x0, float y0, float x1, float y1, float thick, Color c);
void exp_polyline(const float *xy, int count, float thick, Color c);
void exp_tri_fill(float x0, float y0, float x1, float y1, float x2, float y2, Color c);
void exp_glow(float cx, float cy, float r, Color c, float strength);
void exp_background(uint32_t now_ms);
void exp_icon(exp_icon_t icon, float cx, float cy, float size, Color c);
void exp_scissor(int x, int y, int w, int h);
void exp_scissor_end(void);
