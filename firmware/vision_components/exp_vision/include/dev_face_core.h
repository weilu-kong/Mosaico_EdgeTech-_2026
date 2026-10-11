// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x;
    int y;
    int w;
    int h;
} dev_face_box_t;

typedef struct {
    bool found;
    dev_face_box_t box;
    float score;
    int infer_us;
    float fps;
    uint32_t total_runs;
} dev_face_result_t;

static inline dev_face_box_t dev_face_box_clamp(dev_face_box_t b, int max_w, int max_h)
{
    int x0 = b.x < 0 ? 0 : b.x;
    int y0 = b.y < 0 ? 0 : b.y;
    int x1 = b.x + b.w > max_w ? max_w : b.x + b.w;
    int y1 = b.y + b.h > max_h ? max_h : b.y + b.h;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    dev_face_box_t out = {x0, y0, x1 - x0, y1 - y0};
    return out;
}

static inline void dev_face_box_center(dev_face_box_t b, int *cx, int *cy)
{
    if (cx) *cx = b.x + b.w / 2;
    if (cy) *cy = b.y + b.h / 2;
}

#ifdef __cplusplus
}
#endif
