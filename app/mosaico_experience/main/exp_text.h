// SPDX-License-Identifier: Apache-2.0
// Japanese-capable text rendering on the engine's RGB565 surface, plus a layout
// recorder used by the regression tests to find overlapping/overflowing text.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mosaico_raylib_fast.h"
#include "exp_font_data.h"

typedef enum { EXP_ALIGN_LEFT = 0, EXP_ALIGN_CENTER, EXP_ALIGN_RIGHT } exp_align_t;

typedef struct {
    int x, y, w, h;
} exp_rect_t;

/* Translation and opacity applied to every exp_* draw call (page transitions). */
void exp_gfx_set_origin(int ox, int oy);
void exp_gfx_set_alpha(uint8_t alpha);
void exp_gfx_get_origin(int *ox, int *oy);
Color exp_gfx_mod(Color c);

/* Text metrics (advance based). */
int exp_text_width(const char *utf8, exp_font_id_t font);
int exp_text_line_height(exp_font_id_t font);
int exp_text_ascent(exp_font_id_t font);

/* Single line. (x, y) is the top of the line box; align is relative to x.
 * Returns the advance width drawn. */
int exp_text(const char *utf8, int x, int y, exp_font_id_t font, Color color,
             exp_align_t align);

/* Single line optically centred in `box` (horizontally and vertically). */
int exp_text_in(exp_rect_t box, const char *utf8, exp_font_id_t font, Color color,
                exp_align_t align);

/* Wrapped paragraph inside `box` with Japanese line-break rules (kinsoku).
 * If it does not fit in `max_lines` the last line is ended with an ellipsis and
 * the layout recorder notes a truncation. Returns lines drawn. */
int exp_text_box(exp_rect_t box, const char *utf8, exp_font_id_t font, Color color,
                 exp_align_t align, int max_lines);

/* Number of lines exp_text_box would need (no drawing). */
int exp_text_count_lines(int width, const char *utf8, exp_font_id_t font);

/* ---- Layout recorder ----------------------------------------------------- */
typedef struct {
    int texts;            /* text runs recorded this frame                    */
    int text_overlaps;    /* ink boxes of two text runs intersect             */
    int shape_overlaps;   /* text ink intersects a keep-out shape             */
    int off_screen;       /* ink outside the 6 px display safe rectangle      */
    int truncated;        /* a text box had to clip or ellipsise              */
    int small_targets;    /* interactive area below EXP_MIN_HIT               */
    int missing_glyphs;   /* code points absent from the generated font       */
    char first_problem[96];
} exp_layout_report_t;

void exp_layout_enable(bool enable);
void exp_layout_begin_frame(void);
void exp_layout_keepout(exp_rect_t rect);   /* icons / badges text must avoid  */
void exp_layout_target(exp_rect_t rect);    /* a touch target                  */
void exp_layout_note_problem(const char *what);
void exp_layout_finish(exp_layout_report_t *out);
uint32_t exp_text_missing_total(void);
