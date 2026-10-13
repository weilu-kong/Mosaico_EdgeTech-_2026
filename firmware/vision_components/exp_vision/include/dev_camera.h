// SPDX-License-Identifier: Apache-2.0
// Camera preview (stage 4a): one task owns the BSP camera module (left slot) and turns every sensor frame into a 480x480
// panel-ready RGB565 picture (centre crop -> PPA rotate / scale -> byte swap). It runs only while a view asks for it:
// opening the camera also switches the USB Serial/JTAG pad to camera data line D2, so nothing is started at boot.
// All numbers in dev_cam_status_t are measured on the board; nothing is estimated. No radio. NOT YET RUN ON THE BOARD.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dev_camera_core.h"       /* orientation presets are part of the interface */

#define DEV_CAM_VIEW_W 480
#define DEV_CAM_VIEW_H 480

typedef enum {
    DEV_CAM_OFF = 0,       /* not requested                                  */
    DEV_CAM_STARTING,      /* claiming the slot, probing the sensor          */
    DEV_CAM_RUNNING,       /* frames are flowing                             */
    DEV_CAM_ERROR,         /* see detail / last_err; leave the view to retry */
    DEV_CAM_STOPPING,
} dev_cam_phase_t;

typedef struct {
    dev_cam_phase_t phase;
    const char *detail;            /* static text */
    int last_err;                  /* esp_err_t of the last failing call, 0 = none */
    uint32_t src_w, src_h, src_bpl;/* sensor frame as opened */
    uint32_t src_fmt;              /* V4L2 fourcc */
    uint32_t crop;                 /* side of the centre square taken from the frame */
    bool scale_exact;              /* PPA scale factor is a multiple of 1/16 */
    uint32_t frames;               /* frames converted and published this session */
    uint32_t capture_fail;         /* get_frame errors this session */
    uint32_t restarts;             /* camera stream restarts this session */
    float fps;                     /* published frames per second (>= 1 s windows) */
    int wait_us, ppa_us, swap_us, total_us;   /* running averages: waiting for the sensor / PPA / byte swap / loop body */
    int open_ms;                   /* time from request to the first started stream */
    int orient;                    /* orientation preset in use (dev_cam_orient) */
    uint32_t psram_free;
} dev_cam_status_t;

void dev_cam_init(void);                                   /* creates the task; idle until requested */
void dev_cam_request(bool on, bool allow_unidentified);    /* on: open + stream; off: stream stopped, camera released */
void dev_cam_set_orient(int preset);                       /* takes effect from the next frame */
void dev_cam_take(dev_cam_status_t *out);
/* Copies the newest picture (480x480, panel byte order) into dst; false if no frame has been published yet. */
bool dev_cam_copy_view(uint16_t *dst);
/* Downsamples the newest 480x480 picture into a dst_w x dst_h rectangle at (dst_x, dst_y) inside a 480x480 framebuffer. */
bool dev_cam_copy_pip(uint16_t *fb480, int dst_x, int dst_y, int dst_w, int dst_h);

