// SPDX-License-Identifier: Apache-2.0
// ESP-Mosaico Experience - shared configuration (Host simulator and device).
#pragma once

/* Display: ESP-Mosaico V1.2 CO5300 480x480. All coordinates in this app are
 * design pixels on that surface; nothing is scaled at run time. */
#define EXP_W 480
#define EXP_H 480
#define EXP_TICK_HZ 30

/* Layout grid. Every screen is built from these constants so that safe
 * margins, chrome heights and hit targets stay consistent. */
#define EXP_MARGIN 16          /* horizontal safe margin                    */
#define EXP_STATUS_H 44        /* top bar                                   */
#define EXP_CONTENT_Y 52       /* first content row (below top bar)         */
#define EXP_CONTENT_BOTTOM 408 /* last content row + 1 (above tab bar)      */
#define EXP_TAB_Y 412          /* tab bar top                               */
#define EXP_TAB_H 60           /* tab bar height (bottom inset 8 px)        */
#define EXP_GAP 8              /* default gap between panels                */
#define EXP_MIN_HIT 44         /* minimum touch target edge                 */
#define EXP_RADIUS 12          /* default panel corner radius               */

/* Exhibition behaviour (time is taken from the platform clock in ms). */
#define EXP_IDLE_RETURN_MS 45000U     /* any page -> Home after no activity */
#define EXP_ATTRACT_AFTER_MS 30000U   /* Home idle -> auto showcase         */
#define EXP_ATTRACT_PAGE_MS 12000U    /* dwell time of each showcase page   */
#define EXP_RESULT_AUTO_MS 15000U     /* game result auto-return            */

/* Sensor snapshot sizes. */
#define EXP_PCM_N 256          /* mono samples per snapshot (16 kHz)        */
#define EXP_SPEC_BARS 24
#define EXP_WAVE_PTS 128

/* Palette (RGB). Ratios against PANEL: TEXT 14.6:1, TEXT2 8.1:1, CYAN 9.2:1. */
#define EXP_RGB(r, g, b) ((Color){(r), (g), (b), 255})
#define EXP_RGBA(r, g, b, a) ((Color){(r), (g), (b), (a)})

#define C_BG0 EXP_RGB(6, 10, 18)
#define C_BG1 EXP_RGB(12, 22, 40)
#define C_PANEL EXP_RGB(15, 26, 46)
#define C_PANEL_HI EXP_RGB(22, 38, 66)
#define C_BORDER EXP_RGB(38, 58, 94)
#define C_CYAN EXP_RGB(63, 208, 230)
#define C_CYAN_DIM EXP_RGB(26, 96, 112)
#define C_GOLD EXP_RGB(232, 180, 90)
#define C_TEXT EXP_RGB(242, 246, 255)
#define C_TEXT2 EXP_RGB(159, 177, 207)
#define C_TEXT3 EXP_RGB(108, 127, 160)
#define C_OK EXP_RGB(82, 214, 138)
#define C_WARN EXP_RGB(242, 169, 59)
#define C_ERR EXP_RGB(242, 99, 91)
