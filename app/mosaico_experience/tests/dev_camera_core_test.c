// SPDX-License-Identifier: Apache-2.0
// Host test for the pure helpers of the camera preview (firmware/vision_components/exp_vision/include/dev_camera_core.h and
// firmware/main/dev_camera_gate.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../firmware/main/dev_camera_gate.h"
#include "../../../firmware/vision_components/exp_vision/include/dev_camera_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(void)
{
    /* ---- crop: the two sensor profiles of the BSP camera module ---- */
    {
        const dev_cam_crop_t c = dev_cam_square_crop(1024, 768, 2048);       /* OV3640 UYVY 1024x768 */
        CHECK(c.valid && c.size == 768 && c.off_x == 128 && c.off_y == 0 && c.stride_px == 1024);
        const dev_cam_crop_t d = dev_cam_square_crop(1280, 720, 2560);       /* SC101IOT 1280x720 */
        CHECK(d.valid && d.size == 720 && d.off_x == 280 && d.off_y == 0);
        const dev_cam_crop_t e = dev_cam_square_crop(1280, 720, 0);          /* stride 0 = tightly packed */
        CHECK(e.valid && e.stride_px == 1280);
        const dev_cam_crop_t f = dev_cam_square_crop(1280, 720, 2562);       /* padded stride is fine */
        CHECK(f.valid && f.stride_px == 1281);
        const dev_cam_crop_t g = dev_cam_square_crop(1280, 720, 2561);       /* odd byte stride: refused */
        CHECK(!g.valid);
        const dev_cam_crop_t h = dev_cam_square_crop(1280, 720, 2000);       /* stride narrower than the picture: refused */
        CHECK(!h.valid);
        const dev_cam_crop_t z = dev_cam_square_crop(0, 0, 0);
        CHECK(!z.valid);
        const dev_cam_crop_t odd = dev_cam_square_crop(641, 481, 1282);      /* odd dimensions: size and offsets stay even */
        CHECK(odd.valid && odd.size == 480 && (odd.off_x % 2) == 0 && (odd.off_y % 2) == 0 && odd.off_x == 80 && odd.off_y == 0);
    }
    /* ---- packed YUV 4:2:2 byte orders (V4L2 fourcc values) ---- */
    {
        CHECK(DEV_CAM_FOURCC('U', 'Y', 'V', 'Y') == 0x59565955u);            /* V4L2_PIX_FMT_UYVY */
        CHECK(DEV_CAM_FOURCC('Y', 'U', 'Y', 'V') == 0x56595559u);            /* V4L2_PIX_FMT_YUYV */
        CHECK(dev_cam_yuv422_kind(0x59565955u) == DEV_CAM_YUV_UYVY);
        CHECK(dev_cam_yuv422_kind(0x56595559u) == DEV_CAM_YUV_YUYV);
        CHECK(dev_cam_yuv422_kind(DEV_CAM_FOURCC('V', 'Y', 'U', 'Y')) == DEV_CAM_YUV_VYUY);
        CHECK(dev_cam_yuv422_kind(DEV_CAM_FOURCC('Y', 'V', 'Y', 'U')) == DEV_CAM_YUV_YVYU);
        CHECK(dev_cam_yuv422_kind(DEV_CAM_FOURCC('R', 'G', 'B', 'P')) == DEV_CAM_YUV_NONE);   /* RGB565 */
        CHECK(dev_cam_yuv422_kind(DEV_CAM_FOURCC('M', 'J', 'P', 'G')) == DEV_CAM_YUV_NONE);   /* JPEG */
        CHECK(dev_cam_yuv422_kind(0) == DEV_CAM_YUV_NONE);
    }
    /* ---- scale factor exactness (PPA keeps 1/16 steps) ---- */
    {
        bool ex = false;
        float s = dev_cam_scale_for(480, 768, &ex);
        CHECK(ex && s > 0.6249f && s < 0.6251f);                              /* 0.625 = 10/16 */
        s = dev_cam_scale_for(480, 720, &ex);
        CHECK(!ex && s > 0.666f && s < 0.667f);                               /* 2/3 is not a multiple of 1/16 */
        s = dev_cam_scale_for(480, 0, &ex);
        CHECK(!ex && s == 0.0f);
    }
    /* ---- byte swap: matches __builtin_bswap16 on every pixel, in place and out of place ---- */
    {
        enum { N = 4096 };
        uint16_t *a = malloc(N * sizeof(uint16_t)), *b = malloc(N * sizeof(uint16_t));
        for (int i = 0; i < N; ++i) a[i] = (uint16_t)(i * 40503u + 7u);
        memcpy(b, a, N * sizeof(uint16_t));
        dev_cam_swap_pixels((uint32_t *)b, (const uint32_t *)a, N);
        int bad = 0;
        for (int i = 0; i < N; ++i) if (b[i] != __builtin_bswap16(a[i])) ++bad;
        CHECK(bad == 0);
        dev_cam_swap_pixels((uint32_t *)b, (const uint32_t *)b, N);          /* in place, twice = identity */
        CHECK(memcmp(a, b, N * sizeof(uint16_t)) == 0);
        CHECK(dev_cam_swap_word(0x12345678u) == 0x34127856u);
        free(a); free(b);
    }
    /* ---- orientation presets: eight distinct, preset 0 is the official example, wraps around ---- */
    {
        const dev_cam_orient_t o0 = dev_cam_orient(0);
        CHECK(o0.rot_deg == 270 && !o0.mirror_x && o0.mirror_y);
        for (int i = 0; i < DEV_CAM_ORIENT_N; ++i)
            for (int j = i + 1; j < DEV_CAM_ORIENT_N; ++j) {
                const dev_cam_orient_t a = dev_cam_orient(i), b = dev_cam_orient(j);
                CHECK(!(a.rot_deg == b.rot_deg && a.mirror_x == b.mirror_x && a.mirror_y == b.mirror_y));
            }
        int idx = 0;
        for (int i = 0; i < DEV_CAM_ORIENT_N; ++i) idx = dev_cam_orient_next(idx);
        CHECK(idx == 0);
        CHECK(dev_cam_orient(-1).rot_deg == dev_cam_orient(DEV_CAM_ORIENT_N - 1).rot_deg);
        for (int i = 0; i < DEV_CAM_ORIENT_N; ++i) {
            const int r = dev_cam_orient(i).rot_deg;
            CHECK(r == 0 || r == 90 || r == 180 || r == 270);
        }
    }
    /* ---- running average ---- */
    {
        dev_cam_avg_t a = {0};
        dev_cam_avg_add(&a, 8000);
        CHECK(a.avg_us == 8000);                                              /* first sample initialises */
        for (int i = 0; i < 200; ++i) dev_cam_avg_add(&a, 16000);
        CHECK(a.avg_us > 15900 && a.avg_us <= 16000);
    }
    /* ---- fps: 25 Hz source for 3 s -> 25 fps; a stalled stream keeps the last value until a window completes ---- */
    {
        dev_cam_fps_t f;
        dev_cam_fps_reset(&f);
        int64_t t = 5000000;
        for (int i = 0; i < 75; ++i) { dev_cam_fps_frame(&f, t); t += 40000; }
        CHECK(f.fps > 24.9f && f.fps < 25.1f);
        dev_cam_fps_reset(&f);
        CHECK(f.fps == 0.0f && f.win_frames == 0);
        for (int i = 0; i < 5; ++i) { dev_cam_fps_frame(&f, t); t += 100000; }   /* 10 Hz for 0.5 s: no window yet */
        CHECK(f.fps == 0.0f);
    }
    /* ---- slot gate: only a camera, or a bring-up board with an unreadable descriptor, may be opened ---- */
    {
        dev_cam_gate_t g = dev_cam_gate(DEV_SLOT_CAMERA, true);
        CHECK(g.allow && !g.allow_unidentified);
        g = dev_cam_gate(DEV_SLOT_UNKNOWN, false);
        CHECK(g.allow && g.allow_unidentified);
        g = dev_cam_gate(DEV_SLOT_UNKNOWN, true);                            /* some other reviewed-by-nobody board */
        CHECK(!g.allow && !g.allow_unidentified);
        g = dev_cam_gate(DEV_SLOT_NONE, false);
        CHECK(!g.allow);
        g = dev_cam_gate(DEV_SLOT_NOT_PROBED, false);
        CHECK(!g.allow);
        g = dev_cam_gate(DEV_SLOT_JOYSTICK, true);
        CHECK(!g.allow);
        g = dev_cam_gate(DEV_SLOT_INTERACT, true);
        CHECK(!g.allow);
        g = dev_cam_gate(DEV_SLOT_BUTTON_LED, true);
        CHECK(!g.allow);
        for (int c = DEV_SLOT_NOT_PROBED; c <= DEV_SLOT_UNKNOWN; ++c) {       /* every class has a reason text */
            g = dev_cam_gate((dev_slot_class_t)c, false);
            CHECK(g.reason && g.reason[0]);
        }
    }
    if (fails) { printf("dev_camera_core: %d FAILED\n", fails); return 1; }
    printf("dev_camera_core: all checks passed\n");
    return 0;
}
