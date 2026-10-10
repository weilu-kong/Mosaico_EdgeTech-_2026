// SPDX-License-Identifier: Apache-2.0
// Device sensor tasks (stage 3b): BMI270 accelerometer + gyroscope and the BQ27220 fuel gauge through the official
// BSP. Read-only toward the outside world; no radio. Everything here is real data or it is flagged as missing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* IMU axes -> UI frame (+X right edge, +Y top edge, +Z out of the glass). A signed-permutation matrix with det +1:
 * ui = EXP_IMU_MAP * chip.
 * Checked on the device (0.4.1, three static poses, log-view photos): flat face-up reads chip (0,0,+1 g); upright
 * with the screen's top edge up reads (0,+1,0); rotated so the screen's right edge is up reads (+1,0,0). That makes
 * the identity map correct for the accelerometer. The gyroscope uses the same matrix. Checked on the device
 * (0.4.3, integrated-angle line of the log view): turning the board 90 deg counter-clockwise seen from the glass
 * side, flat on a table, read z = +88 deg; lifting the top edge to vertical read x = +89 deg. Both are the
 * right-handed positive direction, so the identity map holds for the gyroscope on X and Z. The Y gyro axis has not
 * been turned on its own. */
#ifndef EXP_IMU_MAP
#define EXP_IMU_MAP {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}
#endif

/* The BQ27220 fuel gauge. Two ways to talk to it:
 *  - EXP_BATT_WRITE_PROFILE=1: the official bsp_battery_init(), which verifies the stored battery profile and, when it
 *    differs, REWRITES it (design capacity 80 mAh, EDV thresholds) in a ~6 s CONFIG UPDATE. The owner agreed to that on
 *    2026-10-09, but on the device it rewrote the profile on EVERY power-on (gauging_config expected 3345, actual
 *    4138 on the second boot), i.e. the write does not stick or the field is not stable. Off by default since 0.4.4.
 *  - EXP_BATT_WRITE_PROFILE=0 (default): read-only. Only the standard commands Voltage (0x08), Current (0x0C) and
 *    StateOfCharge (0x2C) are read; nothing is ever written to the gauge. */
#ifndef EXP_USE_BATTERY
#define EXP_USE_BATTERY 1
#endif
#ifndef EXP_BATT_WRITE_PROFILE
#define EXP_BATT_WRITE_PROFILE 0
#endif

/* The two BMM150 magnetometers (I2C 0x11 = "right", 0x12 = "left" in the official classifier; mag0 / mag1 here), same
 * bus. Read-only toward the outside world; the sensor itself is only put into normal mode / regular preset by the
 * official BSP.
 *
 * Device findings (0.5.1 photos, module removed or fitted, see docs/10 B38):
 *  - 1 LSB = 1 uT (the vendored Bosch fixed-point driver divides by 16 at the end).
 *  - Both sensors sit next to the board's own edge magnets (the board is built to detect magnetic tiles): mag0 reads a
 *    steady ~530 uT, mag1 ~1.58 mT, which is at the BMM150's range limit (+-1300 uT on X/Y) so it overflows
 *    intermittently. Earth's field (about 30 uT horizontal, 35 uT vertical in Tokyo) is a small swing on top.
 *    mag0 is usable once its hard-iron offset is calibrated away; mag1 is not (it clips).
 *  - Axes of mag0, from a turn-table sequence (top edge to N/E/S/W) and a face-up/face-down pair:
 *    chip = (+UI_x, -UI_y, -UI_z), i.e. UI = diag(1,-1,-1) * chip (a proper rotation). Estimated with the board held
 *    by hand, so good to roughly +-20 degrees; the compass check against a phone compass is the real verification.
 *  - mag1 (provisional, weak evidence because it clips): UI = diag(-1,1,-1) * chip.
 * EXP_MAG_FEED_APP=1 hands mag0 to the app (compass / 9-axis yaw). The app still shows "uncalibrated" until the user
 * taps the compass tile and turns the board through all orientations, so nothing is displayed as a heading before that.
 * EXP_MAG_SINGLE_SENSOR=1: mag0 is copied into both app slots (calibration and averaging then equal using mag0 alone);
 * 0 would require both sensors, which cannot work while mag1 clips.
 *  EXP_MAG_LSB_PER_UT: 1.0.
 *  EXP_MAG_MAP_0 / _1: chip axes -> UI frame, signed permutation with det +1. */
#ifndef EXP_USE_MAG
#define EXP_USE_MAG 1
#endif
#ifndef EXP_MAG_FEED_APP
#define EXP_MAG_FEED_APP 1
#endif
#ifndef EXP_MAG_SINGLE_SENSOR
#define EXP_MAG_SINGLE_SENSOR 1
#endif
#ifndef EXP_MAG_LSB_PER_UT
#define EXP_MAG_LSB_PER_UT 1.0f
#endif
#ifndef EXP_MAG_MAP_0
#define EXP_MAG_MAP_0 {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}}
#endif
#ifndef EXP_MAG_MAP_1
#define EXP_MAG_MAP_1 {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}
#endif
#define DEV_MAG_N 2

typedef struct {
    bool imu_ok;                  /* a sample younger than 300 ms exists                          */
    float acc[3];                 /* UI frame, g, newest sample                                    */
    float gyr[3];                 /* UI frame, deg/s, mean since the previous take                 */
    float acc_raw[3], gyr_raw[3]; /* chip frame, newest sample (for the axis check)                */
    float gyr_angle[3];           /* chip frame, degrees turned since dev_sensors_reset_angles()    */
    uint32_t imu_age_ms;          /* time since the last good sample (UINT32_MAX = never)          */
    uint32_t imu_samples, imu_errors;
    const char *imu_state;        /* "starting" | "ok" | "init failed" | "start failed" | "bad axis map" */
    bool batt_ok;                 /* a plausible reading younger than 6 s exists                   */
    uint8_t batt_pct;
    bool charging;                /* derived from the sign of the gauge current: > +5 mA            */
    int batt_mv, batt_ma;
    uint32_t batt_reads, batt_errors;
    const char *batt_state;       /* "off" | "starting" | "ok" | "init failed" | "read failed" | "implausible" */
    bool mag_ok[DEV_MAG_N];       /* a valid sample younger than 1 s exists                         */
    int mag_raw[DEV_MAG_N][3];    /* chip frame, LSB, newest sample                                 */
    float mag_ut[DEV_MAG_N][3];   /* UI frame, uT (through the map and the LSB scale)               */
    uint32_t mag_reads[DEV_MAG_N], mag_errors[DEV_MAG_N];
    const char *mag_state[DEV_MAG_N]; /* "off" | "starting" | "ok" | "init failed" | "read failed" | "overflow" */
} dev_sensors_t;

void dev_sensors_start(void);
void dev_sensors_take(dev_sensors_t *out);
void dev_sensors_reset_angles(void);
