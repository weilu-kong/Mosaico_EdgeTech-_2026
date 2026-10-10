// SPDX-License-Identifier: Apache-2.0
#include "exp_fusion.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f
#define DEG2RAD (PI_F / 180.0f)
#define RAD2DEG (180.0f / PI_F)

void exp_fusion_init(exp_fusion_t *f)
{
    memset(f, 0, sizeof(*f));
    f->q[0] = 1.0f;
    f->kp = 2.0f;
    f->ki = 0.02f;
}

static void normalize_q(float q[4])
{
    const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-9f) { q[0] = 1; q[1] = q[2] = q[3] = 0; return; }
    for (int i = 0; i < 4; ++i) q[i] /= n;
}

/* Initialise the quaternion so that "up" in the body frame equals the measured
 * acceleration direction (heading 0). */
static void seed_from_gravity(exp_fusion_t *f, const float a[3])
{
    const float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n < 0.2f) return;
    const float ux = a[0] / n, uy = a[1] / n, uz = a[2] / n;
    /* Rotation taking world up (0,0,1) -> body frame vector is the inverse of
     * body->world, so build body->world as the shortest rotation body-up -> z. */
    const float cx = uy, cy = -ux;              /* axis = u x z                  */
    const float s = sqrtf(cx * cx + cy * cy);
    const float angle = atan2f(s, uz);
    if (s < 1e-6f) {
        f->q[0] = uz >= 0 ? 1.0f : 0.0f; f->q[1] = uz >= 0 ? 0.0f : 1.0f; f->q[2] = f->q[3] = 0;
    } else {
        const float h = angle * 0.5f, k = sinf(h) / s;
        f->q[0] = cosf(h); f->q[1] = cx * k; f->q[2] = cy * k; f->q[3] = 0;
    }
    normalize_q(f->q);
    f->initialized = true;
}

void exp_fusion_update(exp_fusion_t *f, const float acc[3], const float gyr_dps[3],
                       const float *mag, float dt)
{
    if (!f->initialized) seed_from_gravity(f, acc);
    float q0 = f->q[0], q1 = f->q[1], q2 = f->q[2], q3 = f->q[3];
    float gx = gyr_dps[0] * DEG2RAD, gy = gyr_dps[1] * DEG2RAD, gz = gyr_dps[2] * DEG2RAD;
    float ex = 0, ey = 0, ez = 0;        /* gravity error (unit gain)                  */
    float mex = 0, mey = 0, mez = 0;     /* magnetometer error (unit gain)             */
    float mgain = 1.0f;                  /* extra proportional gain applied to the mag error only */

    float an = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
    /* Ignore the accelerometer during strong linear acceleration (|a| far from 1 g). */
    const bool acc_ok = an > 0.5f && an < 1.8f;
    if (acc_ok) {
        const float ax = acc[0] / an, ay = acc[1] / an, az = acc[2] / an;
        /* Estimated "up" direction in the body frame. */
        const float vx = 2.0f * (q1 * q3 - q0 * q2);
        const float vy = 2.0f * (q0 * q1 + q2 * q3);
        const float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
        ex = ay * vz - az * vy;
        ey = az * vx - ax * vz;
        ez = ax * vy - ay * vx;

        f->mag_in_use = false;
        if (f->mag_boost_s > 0.0f) f->mag_boost_s -= dt;
        if (mag) {
            float mn = sqrtf(mag[0] * mag[0] + mag[1] * mag[1] + mag[2] * mag[2]);
            if (mn > 5.0f) {
                const float mx = mag[0] / mn, my = mag[1] / mn, mz = mag[2] / mn;
                /* World-frame magnetic field direction (x = north, z = up). */
                const float hx = 2.0f * (mx * (0.5f - q2 * q2 - q3 * q3) + my * (q1 * q2 - q0 * q3) +
                                         mz * (q1 * q3 + q0 * q2));
                const float hy = 2.0f * (mx * (q1 * q2 + q0 * q3) + my * (0.5f - q1 * q1 - q3 * q3) +
                                         mz * (q2 * q3 - q0 * q1));
                const float bx = sqrtf(hx * hx + hy * hy);
                const float hz = 2.0f * (mx * (q1 * q3 - q0 * q2) + my * (q2 * q3 + q0 * q1) +
                                         mz * (0.5f - q1 * q1 - q2 * q2));
                const float wx = 2.0f * bx * (0.5f - q2 * q2 - q3 * q3) + 2.0f * hz * (q1 * q3 - q0 * q2);
                const float wy = 2.0f * bx * (q1 * q2 - q0 * q3) + 2.0f * hz * (q0 * q1 + q2 * q3);
                const float wz = 2.0f * bx * (q0 * q2 + q1 * q3) + 2.0f * hz * (0.5f - q1 * q1 - q2 * q2);
                /* Right after the magnetometer joins, the heading may be tens of degrees
                 * off (yaw was only relative before): converge fast, then settle. */
                mgain = f->mag_boost_s > 0.0f ? 12.0f : 3.0f;
                mex = my * wz - mz * wy;
                mey = mz * wx - mx * wz;
                mez = mx * wy - my * wx;
                /* Keep only the part of the error that rotates about the vertical, so the
                 * magnetometer can correct heading but never tilts the horizon. */
                const float dot = mex * vx + mey * vy + mez * vz;
                mex = dot * vx; mey = dot * vy; mez = dot * vz;
                f->mag_in_use = true;
            }
        }
        if (f->mag_in_use && !f->mag_was_used) f->mag_boost_s = 3.0f;
        f->mag_was_used = f->mag_in_use;
        if (f->ki > 0.0f) {
            /* The integral term (gyro bias) only sees unit-gain errors, so the
             * fast re-lock above cannot wind it up. */
            f->integral[0] += f->ki * (ex + mex) * dt;
            f->integral[1] += f->ki * (ey + mey) * dt;
            f->integral[2] += f->ki * (ez + mez) * dt;
            gx += f->integral[0]; gy += f->integral[1]; gz += f->integral[2];
        }
        gx += f->kp * (ex + mgain * mex); gy += f->kp * (ey + mgain * mey); gz += f->kp * (ez + mgain * mez);
    }

    gx *= 0.5f * dt; gy *= 0.5f * dt; gz *= 0.5f * dt;
    const float qa = q0, qb = q1, qc = q2;
    q0 += -qb * gx - qc * gy - q3 * gz;
    q1 += qa * gx + qc * gz - q3 * gy;
    q2 += qa * gy - qb * gz + q3 * gx;
    q3 += qa * gz + qb * gy - qc * gx;
    f->q[0] = q0; f->q[1] = q1; f->q[2] = q2; f->q[3] = q3;
    normalize_q(f->q);
}

void exp_fusion_matrix(const exp_fusion_t *f, float m[9])
{
    const float w = f->q[0], x = f->q[1], y = f->q[2], z = f->q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - w * z);     m[2] = 2 * (x * z + w * y);
    m[3] = 2 * (x * y + w * z);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - w * x);
    m[6] = 2 * (x * z - w * y);     m[7] = 2 * (y * z + w * x);     m[8] = 1 - 2 * (x * x + y * y);
}

static float wrap180(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

void exp_fusion_euler(const exp_fusion_t *f, float *pitch, float *roll, float *yaw)
{
    float m[9];
    exp_fusion_matrix(f, m);
    /* "up" expressed in the body frame is the third ROW of body->world. */
    const float ux = m[6], uy = m[7], uz = m[8];
    /* Pitch is the elevation of the top edge (-90..90); roll is the full-circle rotation about it. */
    if (pitch) *pitch = atan2f(uy, sqrtf(ux * ux + uz * uz)) * RAD2DEG;
    if (roll) *roll = atan2f(-ux, uz) * RAD2DEG;
    if (yaw) {
        /* Heading of the body +Y axis (top edge) in the world x(north) y(west) plane. */
        const float north = m[1], west = m[4];
        *yaw = wrap180(atan2f(-west, north) * RAD2DEG - f->yaw_ref);
    }
}

void exp_fusion_reset_yaw(exp_fusion_t *f)
{
    float m[9];
    exp_fusion_matrix(f, m);
    f->yaw_ref = atan2f(-m[4], m[1]) * RAD2DEG;
}

/* ---- Magnetometer calibration --------------------------------------------------- */
void exp_magcal_init(exp_magcal_t *c)
{
    memset(c, 0, sizeof(*c));
    for (int s = 0; s < EXP_MAG_SENSORS; ++s)
        for (int a = 0; a < 3; ++a) c->scale[s][a] = 1.0f;
}

static void magcal_reset_collection(exp_magcal_t *c)
{
    for (int s = 0; s < EXP_MAG_SENSORS; ++s) {
        for (int a = 0; a < 3; ++a) { c->min[s][a] = 1e9f; c->max[s][a] = -1e9f; c->ref[s][a] = 0.0; }
        memset(c->ata[s], 0, sizeof(c->ata[s]));
        memset(c->aty[s], 0, sizeof(c->aty[s]));
        c->yy[s] = 0.0;
    }
    c->samples = 0;
    c->spans_ok = false;
}

void exp_magcal_start(exp_magcal_t *c, uint32_t now_ms)
{
    magcal_reset_collection(c);
    c->fit_rejects = 0;
    c->running = true;
    c->done = false;
    c->started_ms = now_ms;
}

/* Solves the 4x4 system A x = b in place (Gaussian elimination, partial pivoting). */
static bool solve4(double A[16], double b[4], double x[4])
{
    double m[4][5];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) m[i][j] = A[i * 4 + j];
        m[i][4] = b[i];
    }
    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r)
            if (fabs(m[r][col]) > fabs(m[piv][col])) piv = r;
        if (fabs(m[piv][col]) < 1e-9) return false;
        if (piv != col)
            for (int j = 0; j < 5; ++j) { const double tmp = m[col][j]; m[col][j] = m[piv][j]; m[piv][j] = tmp; }
        for (int r = col + 1; r < 4; ++r) {
            const double f = m[r][col] / m[col][col];
            for (int j = col; j < 5; ++j) m[r][j] -= f * m[col][j];
        }
    }
    for (int i = 3; i >= 0; --i) {
        double v = m[i][4];
        for (int j = i + 1; j < 4; ++j) v -= m[i][j] * x[j];
        x[i] = v / m[i][i];
    }
    return true;
}

#define MAGCAL_RADIUS_MIN_UT 15.0f
#define MAGCAL_RADIUS_MAX_UT 90.0f
#define MAGCAL_RMS_MAX 0.20f

/* Sphere fit of sensor s. On success sets offset, scale = 1, fit_radius, fit_rms. */
static bool magcal_fit(exp_magcal_t *c, int s)
{
    double x[4];
    if (c->samples < 30 || !solve4(c->ata[s], c->aty[s], x)) return false;
    const double r2 = x[3] + x[0] * x[0] + x[1] * x[1] + x[2] * x[2];
    if (!(r2 > 0.0)) return false;
    const double r = sqrt(r2);
    /* residual sum of squares of y - A x, from the normal-equation sums */
    double rss = c->yy[s];
    for (int i = 0; i < 4; ++i) {
        rss -= 2.0 * x[i] * c->aty[s][i];
        for (int j = 0; j < 4; ++j) rss += x[i] * c->ata[s][i * 4 + j] * x[j];
    }
    if (rss < 0.0) rss = 0.0;
    const double rel = sqrt(rss / (double)c->samples) / (2.0 * r) / r;   /* |p-c|^2 - r^2 ~ 2 r (d - r) */
    c->fit_radius[s] = (float)r;
    c->fit_rms[s] = (float)rel;
    if (r < (double)MAGCAL_RADIUS_MIN_UT || r > (double)MAGCAL_RADIUS_MAX_UT || rel > (double)MAGCAL_RMS_MAX) return false;
    for (int a = 0; a < 3; ++a) {
        c->offset[s][a] = (float)(c->ref[s][a] + x[a]);
        c->scale[s][a] = 1.0f;
    }
    return true;
}

#define MAGCAL_SPAN_UT 40.0f
#define MAGCAL_OCT_MIN 10

static float span_progress(const exp_magcal_t *c)
{
    float worst = 1.0f;
    for (int s = 0; s < EXP_MAG_SENSORS; ++s)
        for (int a = 0; a < 3; ++a) {
            const float p = (c->max[s][a] - c->min[s][a]) / MAGCAL_SPAN_UT;
            if (p < worst) worst = p;
        }
    return worst < 0.0f ? 0.0f : (worst > 1.0f ? 1.0f : worst);
}

float exp_magcal_progress(const exp_magcal_t *c)
{
    if (c->done) return 1.0f;
    if (!c->running || c->samples < 2) return 0.0f;
    if (!c->spans_ok) return 0.5f * span_progress(c);
    float cov = 0.0f;
    for (int s = 0; s < EXP_MAG_SENSORS; ++s)
        for (int o = 0; o < 8; ++o) cov += (float)(c->octant[s][o] < MAGCAL_OCT_MIN ? c->octant[s][o] : MAGCAL_OCT_MIN);
    cov /= (float)(EXP_MAG_SENSORS * 8 * MAGCAL_OCT_MIN);
    return 0.5f + 0.5f * cov;
}

void exp_magcal_feed(exp_magcal_t *c, const float mag[EXP_MAG_SENSORS][3], uint32_t now_ms)
{
    (void)now_ms;
    if (!c->running) return;
    for (int s = 0; s < EXP_MAG_SENSORS; ++s) {
        for (int a = 0; a < 3; ++a) {
            if (mag[s][a] < c->min[s][a]) c->min[s][a] = mag[s][a];
            if (mag[s][a] > c->max[s][a]) c->max[s][a] = mag[s][a];
        }
        if (c->samples == 0)
            for (int a = 0; a < 3; ++a) c->ref[s][a] = (double)mag[s][a];
        const double p[3] = {(double)mag[s][0] - c->ref[s][0], (double)mag[s][1] - c->ref[s][1],
                             (double)mag[s][2] - c->ref[s][2]};
        const double f[4] = {2.0 * p[0], 2.0 * p[1], 2.0 * p[2], 1.0};
        const double y = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) c->ata[s][i * 4 + j] += f[i] * f[j];
            c->aty[s][i] += f[i] * y;
        }
        c->yy[s] += y * y;
    }
    ++c->samples;
    if (!c->spans_ok) {
        if (c->samples >= 60 && span_progress(c) >= 1.0f) {
            c->spans_ok = true;
            for (int s = 0; s < EXP_MAG_SENSORS; ++s) {
                for (int a = 0; a < 3; ++a) c->centre[s][a] = 0.5f * (c->max[s][a] + c->min[s][a]);
                memset(c->octant[s], 0, sizeof(c->octant[s]));
            }
        }
        return;
    }
    bool covered = true;
    for (int s = 0; s < EXP_MAG_SENSORS; ++s) {
        int o = 0;
        for (int a = 0; a < 3; ++a)
            if (mag[s][a] > c->centre[s][a]) o |= 1 << a;
        if (c->octant[s][o] < 65535) ++c->octant[s][o];
        for (int k = 0; k < 8; ++k)
            if (c->octant[s][k] < MAGCAL_OCT_MIN) covered = false;
    }
    if (!covered) return;
    bool fit_ok = true;
    for (int s = 0; s < EXP_MAG_SENSORS; ++s)
        if (!magcal_fit(c, s)) fit_ok = false;
    if (!fit_ok) {                         /* not an Earth-field-sized sphere: collect again, nothing is accepted */
        if (c->fit_rejects < 65535) ++c->fit_rejects;
        magcal_reset_collection(c);
        return;
    }
    c->running = false;
    c->done = true;
}

void exp_magcal_apply(const exp_magcal_t *c, const float mag[EXP_MAG_SENSORS][3], float out[3])
{
    out[0] = out[1] = out[2] = 0.0f;
    for (int s = 0; s < EXP_MAG_SENSORS; ++s)
        for (int a = 0; a < 3; ++a) {
            const float v = c->done ? (mag[s][a] - c->offset[s][a]) * c->scale[s][a] : mag[s][a];
            out[a] += v / (float)EXP_MAG_SENSORS;
        }
}

/* ---- Motion events --------------------------------------------------------------- */
void exp_motion_init(exp_motion_t *m) { memset(m, 0, sizeof(*m)); }

exp_motion_events_t exp_motion_update(exp_motion_t *m, const float acc[3], uint32_t now_ms)
{
    exp_motion_events_t ev = {false, false, false};
    const float n = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);

    /* Shake: >= 3 acceleration-magnitude excursions beyond +-0.7 g within 700 ms. */
    const bool excursion = fabsf(n - 1.0f) > 0.7f;
    if (excursion && !m->above) {
        for (int i = 0; i < 3; ++i) m->peak_times[i] = m->peak_times[i + 1];
        m->peak_times[3] = now_ms;
        int recent = 0;
        for (int i = 0; i < 4; ++i)
            if (m->peak_times[i] && now_ms - m->peak_times[i] <= 700U) ++recent;
        if (recent >= 3 && (m->shake_ms == 0 || now_ms - m->shake_ms > 1000U)) {
            ev.shake = true;
            m->shake_ms = now_ms ? now_ms : 1;
            ++m->shake_count;
            memset(m->peak_times, 0, sizeof(m->peak_times));
        }
    }
    m->above = excursion;

    /* Flip: face down (az < -0.75 g) held for 300 ms. */
    if (acc[2] < -0.75f) {
        if (m->flip_since_ms == 0) m->flip_since_ms = now_ms ? now_ms : 1;
        if (!m->flipped && now_ms - m->flip_since_ms >= 300U) {
            m->flipped = true;
            ev.flip = true;
            m->flip_ms = now_ms ? now_ms : 1;
            ++m->flip_count;
        }
    } else if (acc[2] > -0.4f) {
        m->flip_since_ms = 0;
        m->flipped = false;
    }

    /* Tilt: angle of the board normal from vertical. */
    float cosang = n > 0.2f ? acc[2] / n : 1.0f;
    if (cosang > 1.0f) cosang = 1.0f;
    if (cosang < -1.0f) cosang = -1.0f;
    m->tilt_deg = acosf(cosang) * RAD2DEG;
    const bool was = m->tilted;
    if (!m->flipped && m->tilt_deg > 25.0f) m->tilted = true;
    else if (m->flipped || m->tilt_deg < 18.0f) m->tilted = false;
    m->dir = 0;
    if (m->tilted) {
        /* Bits name the edge that is LOWERED. acc_x > 0: left edge down. */
        if (acc[0] > 0.18f) m->dir |= EXP_DIR_LEFT;
        if (acc[0] < -0.18f) m->dir |= EXP_DIR_RIGHT;
        if (acc[1] < -0.18f) m->dir |= EXP_DIR_FAR;    /* top edge down  */
        if (acc[1] > 0.18f) m->dir |= EXP_DIR_NEAR;    /* bottom edge down (toward viewer) */
    }
    if (m->tilted && !was) {
        ev.tilt = true;
        m->tilt_ms = now_ms ? now_ms : 1;
        ++m->tilt_count;
    }
    return ev;
}
