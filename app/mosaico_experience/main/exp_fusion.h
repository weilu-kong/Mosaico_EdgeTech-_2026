// SPDX-License-Identifier: Apache-2.0
// Orientation estimation, magnetometer calibration and motion-event detection.
// Hardware free; inputs are in the UI frame described in exp_types.h.
//
// Algorithm: Mahony complementary filter (quaternion, PI feedback).
//  * 6-axis (accelerometer + gyroscope): pitch/roll are absolute (gravity
//    referenced); yaw is RELATIVE and drifts with gyro bias. It is never
//    labelled as an absolute heading.
//  * 9-axis (adds calibrated magnetometer): yaw becomes a magnetic heading.
//    Only used after exp_magcal reports a complete calibration.
//  * Accuracy limits: no temperature/bias model, hard-iron + per-axis scale
//    calibration only, no soft-iron cross terms, no magnetic declination.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float q[4];        /* body -> world quaternion (w, x, y, z)               */
    float integral[3]; /* integral error term (rad/s)                         */
    float kp, ki;
    bool initialized;
    bool mag_in_use;   /* last update used the magnetometer                   */
    float yaw_ref;     /* degrees; subtracted from the reported yaw           */
    float mag_boost_s; /* seconds of raised magnetometer gain after it (re)joins */
    bool mag_was_used;
} exp_fusion_t;

void exp_fusion_init(exp_fusion_t *f);
/* mag may be NULL (6-axis). dt in seconds. */
void exp_fusion_update(exp_fusion_t *f, const float acc[3], const float gyr_dps[3],
                       const float *mag, float dt);
/* Degrees. pitch (-90..90): top edge up positive. roll (-180..180]: right edge down positive.
 * yaw: heading of the top edge, clockwise positive, relative to yaw_ref. */
void exp_fusion_euler(const exp_fusion_t *f, float *pitch, float *roll, float *yaw);
/* 3x3 body->world rotation matrix, row-major. */
void exp_fusion_matrix(const exp_fusion_t *f, float m[9]);
void exp_fusion_reset_yaw(exp_fusion_t *f);

/* ---- Magnetometer calibration (hard-iron + per-axis scale) ----------------- */
#define EXP_MAG_SENSORS 2
typedef struct {
    float min[EXP_MAG_SENSORS][3], max[EXP_MAG_SENSORS][3];
    float offset[EXP_MAG_SENSORS][3], scale[EXP_MAG_SENSORS][3];
    uint32_t samples;
    uint32_t started_ms;
    bool running, done;
    /* Orientation coverage: after every axis span is >= 40 uT the centre is
     * frozen and each of the 8 sign octants must be visited >= 10 times per
     * sensor, so a calibration cannot finish from a single quick wiggle. */
    bool spans_ok;
    float centre[EXP_MAG_SENSORS][3];
    uint16_t octant[EXP_MAG_SENSORS][8];
    /* Hard-iron offset by a least-squares sphere fit (0.5.3). The earlier min/max centre with a per-axis span scale
     * was fragile: on the real board a steady ~500 uT from the board's own magnets sits under an Earth field of only
     * ~46 uT, and an under-covered axis got a huge scale factor (heading 88 degrees off after a hand-held sweep).
     * Samples are shifted by the first sample and accumulated as normal equations of |p|^2 = 2 c.p + k, so float
     * precision is not eaten by the large offset. The fit is accepted only if the radius is a believable Earth field
     * and the residual is small; otherwise the collection restarts (fit_rejects counts the restarts). */
    double ref[EXP_MAG_SENSORS][3];
    double ata[EXP_MAG_SENSORS][16], aty[EXP_MAG_SENSORS][4], yy[EXP_MAG_SENSORS];
    float fit_radius[EXP_MAG_SENSORS];   /* uT, last fit        */
    float fit_rms[EXP_MAG_SENSORS];      /* relative to radius  */
    uint16_t fit_rejects;
} exp_magcal_t;

void exp_magcal_init(exp_magcal_t *c);
void exp_magcal_start(exp_magcal_t *c, uint32_t now_ms);
/* Feed raw readings while running; completes when every axis span >= 40 uT
 * and all 8 orientation octants were visited. */
void exp_magcal_feed(exp_magcal_t *c, const float mag[EXP_MAG_SENSORS][3], uint32_t now_ms);
float exp_magcal_progress(const exp_magcal_t *c);  /* 0..1 */
/* Applies calibration and averages the sensors into one vector. */
void exp_magcal_apply(const exp_magcal_t *c, const float mag[EXP_MAG_SENSORS][3], float out[3]);

/* ---- Motion events --------------------------------------------------------- */
enum { EXP_DIR_LEFT = 1, EXP_DIR_RIGHT = 2, EXP_DIR_FAR = 4, EXP_DIR_NEAR = 8 };

typedef struct {
    bool tilted;       /* leaning > 25 deg (hysteresis 18 deg)                */
    bool flipped;      /* face down for >= 300 ms                             */
    uint8_t dir;       /* EXP_DIR_* bits: which edge is lowered               */
    float tilt_deg;    /* angle between board normal and vertical             */
    uint32_t shake_ms, flip_ms, tilt_ms; /* time of the latest event (0 = never) */
    uint32_t shake_count, flip_count, tilt_count;
    /* internal */
    uint32_t flip_since_ms;
    bool above;
    uint32_t peak_times[4];
} exp_motion_t;

typedef struct { bool shake, flip, tilt; } exp_motion_events_t;

void exp_motion_init(exp_motion_t *m);
exp_motion_events_t exp_motion_update(exp_motion_t *m, const float acc[3], uint32_t now_ms);
