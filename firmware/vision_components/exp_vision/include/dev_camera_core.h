// SPDX-License-Identifier: Apache-2.0
// Pure helpers of the camera preview (stage 4a): crop geometry, byte swap, orientation presets, running averages and the
// frame-rate estimator. No ESP-IDF, no hardware: everything here is host-tested (app/mosaico_experience/tests/dev_camera_core_test.c).
// No radio.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Centre square crop of the sensor frame --------------------------------------------------------------------
 * The panel is square (480x480) and the sensor is not (OV3640 1024x768, SC101IOT 1280x720), so the preview takes the largest
 * centred square. PPA works on even pixel counts for YUV422, therefore size and offsets are rounded down to even numbers.
 * `stride_px` is bytes_per_line / 2 (UYVY is 2 bytes per pixel); it may be larger than the width. */
typedef struct {
    bool valid;
    uint32_t size;        /* side of the square, even                            */
    uint32_t off_x, off_y;/* top-left corner of the square inside the frame, even */
    uint32_t stride_px;   /* picture width in pixels as PPA wants it (>= width)    */
} dev_cam_crop_t;

static inline dev_cam_crop_t dev_cam_square_crop(uint32_t width, uint32_t height, uint32_t bytes_per_line)
{
    dev_cam_crop_t c = {0};
    const uint32_t side = (width < height ? width : height) & ~1u;
    const uint32_t bpl = bytes_per_line ? bytes_per_line : width * 2u;
    if (side == 0 || (bpl & 1u) != 0 || bpl / 2u < width) return c;      /* odd stride or stride narrower than the picture */
    c.size = side;
    c.off_x = ((width - side) / 2u) & ~1u;
    c.off_y = ((height - side) / 2u) & ~1u;
    c.stride_px = bpl / 2u;
    c.valid = true;
    return c;
}

/* ---- Packed YUV 4:2:2 byte orders the PPA reads (input side only for all but UYVY) ----------------------------------
 * The BSP camera module lists OV3640 as UYVY and SC101IOT as "YUV422" without naming the byte order, so the order is taken from
 * the format the driver reports and mapped here; anything else is refused with a message instead of being converted wrongly.
 * Values are V4L2 fourcc codes (little-endian character packing). */
#define DEV_CAM_FOURCC(a, b, c, d) ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))
typedef enum { DEV_CAM_YUV_NONE = 0, DEV_CAM_YUV_UYVY, DEV_CAM_YUV_YUYV, DEV_CAM_YUV_VYUY, DEV_CAM_YUV_YVYU } dev_cam_yuv_t;

static inline dev_cam_yuv_t dev_cam_yuv422_kind(uint32_t fourcc)
{
    if (fourcc == DEV_CAM_FOURCC('U', 'Y', 'V', 'Y')) return DEV_CAM_YUV_UYVY;
    if (fourcc == DEV_CAM_FOURCC('Y', 'U', 'Y', 'V')) return DEV_CAM_YUV_YUYV;
    if (fourcc == DEV_CAM_FOURCC('V', 'Y', 'U', 'Y')) return DEV_CAM_YUV_VYUY;
    if (fourcc == DEV_CAM_FOURCC('Y', 'V', 'Y', 'U')) return DEV_CAM_YUV_YVYU;
    return DEV_CAM_YUV_NONE;
}

/* PPA scales by a float factor; the hardware keeps 1/16 steps, so a factor that is not a multiple of 1/16 is rounded by the
 * driver and the output block may differ from the panel size by a few pixels. Returns the factor and whether it is exact. */
static inline float dev_cam_scale_for(uint32_t out_px, uint32_t crop_px, bool *exact)
{
    const float f = crop_px ? (float)out_px / (float)crop_px : 0.0f;
    if (exact) {
        const float q = f * 16.0f;
        const float r = q - (float)(int)(q + 0.5f);
        *exact = f > 0.0f && r > -0.0001f && r < 0.0001f;
    }
    return f;
}

/* ---- Byte order of the panel ------------------------------------------------------------------------------------
 * PPA writes native RGB565; the CO5300 wants the two bytes of every pixel exchanged (exp_surface does the same for the UI).
 * Two pixels per 32-bit word; n_px must be even; src == dst (in place) is allowed. */
static inline uint32_t dev_cam_swap_word(uint32_t w) { return ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu); }

static inline void dev_cam_swap_pixels(uint32_t *dst, const uint32_t *src, size_t n_px)
{
    for (size_t i = 0; i < n_px / 2u; ++i) dst[i] = dev_cam_swap_word(src[i]);
}

/* ---- Orientation presets ----------------------------------------------------------------------------------------
 * The mounting of the camera module relative to the panel is not documented for this application, so the first bring-up lets
 * the person cycle through the eight rotation / mirror combinations by touch. Index 0 is what the official camera_lcd_preview
 * example uses (rotate 270, mirror y). */
typedef struct {
    int rot_deg;        /* 0, 90, 180, 270 (PPA: counter-clockwise) */
    bool mirror_x, mirror_y;
} dev_cam_orient_t;

#define DEV_CAM_ORIENT_N 8

static inline dev_cam_orient_t dev_cam_orient(int idx)
{
    static const dev_cam_orient_t tab[DEV_CAM_ORIENT_N] = {
        {270, false, true},  {270, false, false}, {90, false, true},  {90, false, false},
        {180, false, true},  {180, false, false}, {0, false, true},   {0, false, false},
    };
    return tab[((idx % DEV_CAM_ORIENT_N) + DEV_CAM_ORIENT_N) % DEV_CAM_ORIENT_N];
}

static inline int dev_cam_orient_next(int idx) { return (idx + 1) % DEV_CAM_ORIENT_N; }

/* ---- Running statistics -----------------------------------------------------------------------------------------
 * Exponential average with weight 1/8 (microseconds). The first sample initialises the average. */
typedef struct { int64_t avg_us; bool primed; } dev_cam_avg_t;

static inline void dev_cam_avg_add(dev_cam_avg_t *a, int64_t us)
{
    if (!a->primed) { a->avg_us = us; a->primed = true; return; }
    a->avg_us += (us - a->avg_us) / 8;
}

/* Frame rate from the frame count over windows of at least 1 s (a rate over a few frames would jump around). */
typedef struct {
    int64_t win_start_us;
    uint32_t win_frames;
    float fps;
} dev_cam_fps_t;

static inline void dev_cam_fps_frame(dev_cam_fps_t *f, int64_t now_us)
{
    if (f->win_frames == 0) f->win_start_us = now_us;
    ++f->win_frames;
    const int64_t dt = now_us - f->win_start_us;
    if (dt >= 1000000) {
        f->fps = (float)(f->win_frames - 1u) * 1e6f / (float)dt;     /* n frames span n-1 intervals */
        f->win_start_us = now_us;
        f->win_frames = 1;
    }
}

static inline void dev_cam_fps_reset(dev_cam_fps_t *f) { f->win_start_us = 0; f->win_frames = 0; f->fps = 0.0f; }
