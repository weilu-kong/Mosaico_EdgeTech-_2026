// SPDX-License-Identifier: Apache-2.0
// Motion (A): live board attitude, gravity, magnetic heading and motion events.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "exp_internal.h"

#define PI_F 3.14159265358979f
#define BOARD_X 16
#define BOARD_Y 56
#define BOARD_W 232
#define BOARD_H 230
#define COL_X 256
#define COL_W 208
#define ROW2_Y 294
#define ROW2_H 112

int page_motion_layout(const exp_app_t *app, exp_btn_t *out)
{
    (void)app;
    int n = 0;
    out[n++] = (exp_btn_t){BTN_YAW_RESET, {BOARD_X, BOARD_Y, BOARD_W, BOARD_H}, true, false, UI_BTN_ROW};
    out[n++] = (exp_btn_t){BTN_MAG_CAL, {136, ROW2_Y, 192, ROW2_H}, true, false, UI_BTN_ROW};
    return n;
}

void page_motion_button(exp_app_t *app, int id)
{
    if (id == BTN_YAW_RESET) {
        if (!app->yaw_absolute && app->imu_ok) {
            exp_fusion_reset_yaw(&app->fusion);
            app_sound(app, EXP_SND_TAP);
        }
    } else if (id == BTN_MAG_CAL) {
        if (app->mag_ok && !app->magcal.running) {
            exp_magcal_start(&app->magcal, app->now_ms);
            app->fusion.yaw_ref = 0.0f;
            app_sound(app, EXP_SND_NAV);
        }
    }
}

void page_motion_update(exp_app_t *app, float dt)
{
    for (int i = 0; i < 3; ++i) app->event_glow[i] = fmaxf(0.0f, app->event_glow[i] - dt / 1.2f);
    if (app->mag_ok && app->magcal.running) {
        { const exp_sensors_t *sn = &app->s; exp_magcal_feed(&app->magcal, sn->mag, app->now_ms); }
        if (app->magcal.done) {
            app_sound(app, EXP_SND_SUCCESS);
            app_haptic(app, 40);
        }
    }
}

static float nz(float v, float step) { return fabsf(v) < step * 0.5f ? 0.0f : v; }

static void fmt_deg(char *buf, size_t n, float v)
{
    const int i = fabsf(v) >= 178.0f ? 180 : (int)lroundf(v);  /* +/-180 flicker when face down: show unsigned */
    if (i == 180) snprintf(buf, n, "180\xC2\xB0");
    else if (i < 0) snprintf(buf, n, "\xE2\x88\x92%d\xC2\xB0", -i);  /* U+2212 minus, U+00B0 */
    else snprintf(buf, n, "+%d\xC2\xB0", i);
}

/* ---- 3D board ---------------------------------------------------------------- */
typedef struct { float x, y, depth; } pv_t;

static void rot_body(const float R[9], float yaw_ref_deg, const float v[3], float out[3])
{
    float w[3] = {R[0] * v[0] + R[1] * v[1] + R[2] * v[2], R[3] * v[0] + R[4] * v[1] + R[5] * v[2],
                  R[6] * v[0] + R[7] * v[1] + R[8] * v[2]};
    const float a = yaw_ref_deg * PI_F / 180.0f, c = cosf(a), s = sinf(a);
    out[0] = c * w[0] - s * w[1];
    out[1] = s * w[0] + c * w[1];
    out[2] = w[2];
}

static pv_t project(const float w[3], float cx, float cy, float scale, float elev)
{
    const float ce = cosf(elev), se = sinf(elev);
    const float ve = -w[1], vn = w[0], vu = w[2];
    const float up = vn * se + vu * ce;
    const float depth = -vn * ce + vu * se;
    const float persp = 1.0f / (1.0f - depth * 0.12f);
    pv_t p = {cx + ve * scale * persp, cy - up * scale * persp, depth};
    return p;
}

typedef struct { int v[4]; float n[3]; Color fill; Color edge; int kind; } face_t;

static float wrap180f(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

/* How far the board is "held up" (0 = lying flat, 1 = standing): smoothstep of the screen normal's vertical component
 * |uz|, 0.7 (flat-ish) .. 0.3 (standing). The normal, not the pitch, is used so that a standing board turned in its own
 * plane (which has a large pitch/roll in the numbers) still counts as standing. */
static float held_up_weight(const float R[9])
{
    const float t = exp_clampf((0.7f - fabsf(R[8])) / 0.4f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* EXP_MODEL_FOLLOW_YAW 0 (default): the picture is the board seen by its user, who stands in front of the screen. The
 * heading is removed from the picture (only the tilt is drawn), so a board held squarely in front of you looks square
 * whatever the yaw number says; yaw stays a number (and the compass). 1: the picture also turns with the yaw number
 * when lying flat (0.4.5 behaviour). Neither setting changes pitch/roll/yaw. */
#ifndef EXP_MODEL_FOLLOW_YAW
#define EXP_MODEL_FOLLOW_YAW 0
#endif

static void draw_board(exp_app_t *app, float cx, float cy, float scale)
{
    float R[9];
    exp_fusion_matrix(&app->fusion, R);
    const float yaw_ref = app->yaw_absolute ? 0.0f : app->fusion.yaw_ref;
    /* The camera looks from 52 degrees above the table when the board lies flat and drops to 10 degrees when it is
     * held up, where the heading of the top edge is meaningless; there the heading is that of the screen normal. */
    const float w_up = held_up_weight(R);
    scale *= 1.0f - 0.1f * w_up;                    /* a standing board turned in its plane needs the room */
    const float h_edge = atan2f(-R[4], R[1]) * 180.0f / PI_F;            /* heading of the top edge            */
    const float h_face = atan2f(-R[5], R[2]) * 180.0f / PI_F - 180.0f;   /* heading the top edge would have flat */
    const float hb = h_edge + w_up * wrap180f(h_face - h_edge);
#if EXP_MODEL_FOLLOW_YAW
    const float turn = hb - (1.0f - w_up) * wrap180f(hb - yaw_ref);       /* world rotation applied to the model */
#else
    (void)yaw_ref;
    const float turn = hb;                                                /* heading removed: viewer-relative   */
#endif
    const float elev = (52.0f - w_up * 42.0f) * PI_F / 180.0f;
    const float T = 0.14f;
    float bv[8][3];
    for (int i = 0; i < 8; ++i) {
        const float local[3] = {(i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? T : -T};
        rot_body(R, turn, local, bv[i]);
    }
    pv_t pv[8];
    for (int i = 0; i < 8; ++i) pv[i] = project(bv[i], cx, cy, scale, elev);

    /* Faces: indices into bv; normals in body space. */
    static const struct { int v[4]; float n[3]; int kind; } F[6] = {
        {{4, 5, 7, 6}, {0, 0, 1}, 0},   /* top: the display */
        {{0, 2, 3, 1}, {0, 0, -1}, 1},  /* back */
        {{1, 3, 7, 5}, {1, 0, 0}, 2},   /* right */
        {{0, 4, 6, 2}, {-1, 0, 0}, 2},  /* left */
        {{2, 6, 7, 3}, {0, 1, 0}, 3},   /* top edge (+Y) */
        {{0, 1, 5, 4}, {0, -1, 0}, 3},  /* bottom edge */
    };
    const float cam[3] = {0.0f, -cosf(elev), sinf(elev)};  /* (east, north, up) */
    struct { int idx; float depth; } order[6];
    int count = 0;
    for (int f = 0; f < 6; ++f) {
        float nw[3];
        rot_body(R, turn, F[f].n, nw);
        const float ne = -nw[1], nn = nw[0], nu = nw[2];
        if (ne * cam[0] + nn * cam[1] + nu * cam[2] <= 0.02f) continue;  /* back-face cull */
        float d = 0;
        for (int k = 0; k < 4; ++k) d += pv[F[f].v[k]].depth * 0.25f;
        int pos = count++;
        while (pos > 0 && order[pos - 1].depth > d) { order[pos] = order[pos - 1]; --pos; }
        order[pos].idx = f; order[pos].depth = d;
    }
    for (int o = 0; o < count; ++o) {
        const int f = order[o].idx;
        const pv_t *a = &pv[F[f].v[0]], *b = &pv[F[f].v[1]], *c = &pv[F[f].v[2]], *d = &pv[F[f].v[3]];
        float nw[3];
        rot_body(R, turn, F[f].n, nw);
        const float light = exp_clampf(0.45f + 0.55f * (nw[2] * 0.7f + nw[0] * 0.3f), 0.2f, 1.0f);
        Color fill, edge;
        if (F[f].kind == 0) { fill = EXP_RGB(9, 24, 46); edge = C_CYAN; }
        else if (F[f].kind == 1) { fill = EXP_RGB(30, 44, 70); edge = C_BORDER; }
        else { fill = exp_lerp(EXP_RGB(14, 28, 52), EXP_RGB(52, 82, 120), light); edge = C_CYAN_DIM; }
        exp_tri_fill(a->x, a->y, b->x, b->y, c->x, c->y, fill);
        exp_tri_fill(a->x, a->y, c->x, c->y, d->x, d->y, fill);
        if (F[f].kind == 0) {
            /* Display: inset screen, mosaic mark, and a gold wedge on the top edge. Every point is a bilinear blend of
             * all four projected corners, so the inner shapes stay inside the outline under perspective (a
             * parallelogram from three corners does not). u: left->right, v: bottom->top. */
#define QP(u, v, X, Y)                                                                                  \
    do {                                                                                                \
        (X) = (1 - (u)) * (1 - (v)) * a->x + (u) * (1 - (v)) * b->x + (u) * (v) * c->x + (1 - (u)) * (v) * d->x; \
        (Y) = (1 - (u)) * (1 - (v)) * a->y + (u) * (1 - (v)) * b->y + (u) * (v) * c->y + (1 - (u)) * (v) * d->y; \
    } while (0)
            const float m0 = 0.1f, m1 = 0.9f;
            float q[4][2];
            QP(m0, m0, q[0][0], q[0][1]); QP(m1, m0, q[1][0], q[1][1]);
            QP(m1, m1, q[2][0], q[2][1]); QP(m0, m1, q[3][0], q[3][1]);
            exp_tri_fill(q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], EXP_RGB(13, 40, 74));
            exp_tri_fill(q[0][0], q[0][1], q[2][0], q[2][1], q[3][0], q[3][1], EXP_RGB(13, 40, 74));
            /* four mosaic tiles in the centre */
            for (int ty = 0; ty < 2; ++ty)
                for (int tx = 0; tx < 2; ++tx) {
                    const float u0 = 0.38f + tx * 0.13f, v0 = 0.38f + ty * 0.13f, u1 = u0 + 0.11f, v1 = v0 + 0.11f;
                    float p[4][2];
                    QP(u0, v0, p[0][0], p[0][1]); QP(u1, v0, p[1][0], p[1][1]);
                    QP(u1, v1, p[2][0], p[2][1]); QP(u0, v1, p[3][0], p[3][1]);
                    const Color tc = (tx + ty) % 2 ? C_GOLD : C_CYAN;
                    exp_tri_fill(p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], tc);
                    exp_tri_fill(p[0][0], p[0][1], p[2][0], p[2][1], p[3][0], p[3][1], tc);
                }
            /* top-edge wedge */
            float mx, my, tx0, ty0, tx1, ty1;
            QP(0.5f, 1.0f, mx, my); QP(0.42f, 0.92f, tx0, ty0); QP(0.58f, 0.92f, tx1, ty1);
            exp_tri_fill(tx0, ty0, tx1, ty1, mx, my, C_GOLD);
#undef QP
        }
        const float th = F[f].kind == 0 ? 2.0f : 1.4f;
        exp_line(a->x, a->y, b->x, b->y, th, edge);
        exp_line(b->x, b->y, c->x, c->y, th, edge);
        exp_line(c->x, c->y, d->x, d->y, th, edge);
        exp_line(d->x, d->y, a->x, a->y, th, edge);
    }
}

/* ---- Small widgets ------------------------------------------------------------ */
static void draw_value_tile(int x, int y, int w, int h, const char *label, const char *value, const char *tag, Color vc)
{
    ui_tile(x, y, w, h, label);
    exp_text(value, x + 12, y + 26, EXP_FONT_NUM, vc, EXP_ALIGN_LEFT);
    if (tag) exp_text(tag, x + w - 12, y + 8, EXP_FONT_CAP, C_GOLD, EXP_ALIGN_RIGHT);
}

static void draw_gravity(exp_app_t *app, int x, int y, int w, int h)
{
    ui_tile(x, y, w, h, T_GRAVITY);
    const float cx = (float)(x + w / 2), cy = (float)(y + 68), r = 33.0f;
    exp_disc(cx, cy, r, EXP_RGB(9, 18, 34));
    exp_ring(cx, cy, r, r - 1.5f, C_BORDER);
    exp_ring(cx, cy, r * 0.5f, r * 0.5f - 1.0f, EXP_RGB(30, 48, 80));
    exp_line(cx - r, cy, cx + r, cy, 1.0f, EXP_RGB(30, 48, 80));
    exp_line(cx, cy - r, cx, cy + r, 1.0f, EXP_RGB(30, 48, 80));
    float gx = 0.0f, gy = 0.0f;
    if (app->imu_ok) { gx = -app->s.acc[0]; gy = app->s.acc[1]; }
    /* The marble rolls toward the lowered edge: screen +x for right-edge-down. */
    float ox = gx * (r - 8.0f) / 0.5f, oy = gy * (r - 8.0f) / 0.5f;
    const float len = sqrtf(ox * ox + oy * oy);
    if (len > r - 8.0f) { ox *= (r - 8.0f) / len; oy *= (r - 8.0f) / len; }
    if (app->imu_ok) {
        exp_glow(cx + ox, cy + oy, 14.0f, C_GOLD, 0.5f);
        exp_disc(cx + ox, cy + oy, 6.5f, C_GOLD);
    } else {
        exp_text("--", (int)cx, (int)cy - 8, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_CENTER);
    }
}

static void draw_compass(exp_app_t *app, int x, int y, int w, int h, bool pressed)
{
    (void)pressed;
    (void)h;
    const float cx = (float)(x + 52), cy = (float)(y + 66), r = 34.0f;
    exp_disc(cx, cy, r, EXP_RGB(9, 18, 34));
    exp_ring(cx, cy, r, r - 1.5f, C_BORDER);
    for (int k = 0; k < 12; ++k) {
        const float a = (float)k * 30.0f * PI_F / 180.0f;
        const float r0 = (k % 3 == 0) ? r - 8.0f : r - 5.0f;
        exp_line(cx + sinf(a) * r0, cy - cosf(a) * r0, cx + sinf(a) * (r - 2.5f), cy - cosf(a) * (r - 2.5f), 1.0f,
                 (k % 3 == 0) ? C_TEXT2 : C_TEXT3);
    }
    exp_text("N", (int)cx, (int)(cy - r + 9), EXP_FONT_CAP, C_GOLD, EXP_ALIGN_CENTER);
    const bool cal = app->magcal.done && app->mag_ok;
    char line1[16] = "--";
    if (cal) {
        float heading = app->yaw;
        if (heading < 0) heading += 360.0f;
        snprintf(line1, sizeof(line1), "%d\xC2\xB0", (int)lroundf(heading) % 360);
        const float a = -app->yaw * PI_F / 180.0f;   /* north needle opposes the heading */
        const float nx = sinf(a), ny = -cosf(a);
        exp_tri_fill(cx + nx * (r - 12.0f), cy + ny * (r - 12.0f), cx - ny * 4.0f, cy + nx * 4.0f, cx + ny * 4.0f, cy - nx * 4.0f, C_CYAN);
        exp_tri_fill(cx - nx * (r - 14.0f), cy - ny * (r - 14.0f), cx - ny * 4.0f, cy + nx * 4.0f, cx + ny * 4.0f, cy - nx * 4.0f, C_TEXT3);
    } else {
        exp_disc(cx, cy, 3.0f, C_TEXT3);
    }
    exp_text(T_COMPASS, x + 12, y + 8, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    const int tx = x + 98, tw_max = w - (tx - x) - 8;
    exp_text(line1, tx, y + 28, EXP_FONT_TITLE, cal ? C_TEXT : C_TEXT3, EXP_ALIGN_LEFT);
    char line2[24];
    /* Uncalibrated, the raw field includes the board's own magnets (hundreds of uT on the real board): only show a
     * number that can be Earth's field (about 25..65 uT), otherwise "--". */
    const bool field_ok = app->mag_ok && (cal || (app->mag_field_ut >= 15.0f && app->mag_field_ut <= 90.0f));
    if (field_ok) snprintf(line2, sizeof(line2), "%d\xC2\xB5T", (int)lroundf(app->mag_field_ut));
    else snprintf(line2, sizeof(line2), "--");
    exp_text(line2, tx, y + 62, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    if (!app->mag_ok) {
        exp_text(T_NO_MAG, tx, y + 86, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_LEFT);
    } else if (app->magcal.running) {
        exp_text(T_CAL_RUN, x + w - 12, y + 8, EXP_FONT_CAP, C_WARN, EXP_ALIGN_RIGHT);
        ui_bar(tx, y + 92, tw_max, 8, exp_magcal_progress(&app->magcal), C_CYAN);
    } else if (cal) {
        if (app->mag_disturbed) exp_text(T_MAG_DISTURB, tx, y + 86, EXP_FONT_CAP, C_WARN, EXP_ALIGN_LEFT);
        else exp_text(T_MAG_CAL, tx, y + 86, EXP_FONT_CAP, C_OK, EXP_ALIGN_LEFT);
    } else {
        exp_text(T_CAL_TAP, x + w - 12, y + 8, EXP_FONT_CAP, C_CYAN, EXP_ALIGN_RIGHT);
        exp_text(T_MAG_UNCAL, tx, y + 86, EXP_FONT_CAP, C_WARN, EXP_ALIGN_LEFT);
    }
}

static void event_chip(int x, int y, int w, const char *text, float glow, bool active)
{
    const float g = active ? 1.0f : glow;
    exp_panel(x, y, w, 24, 12, exp_lerp(C_PANEL, EXP_RGB(20, 84, 100), g), exp_lerp(C_BORDER, C_CYAN, g));
    exp_disc((float)(x + 12), (float)(y + 12), 4.0f, exp_lerp(C_TEXT3, C_CYAN, g));
    exp_rect_t b = {x + 24, y, w - 28, 24};
    exp_text_in(b, text, EXP_FONT_CAP, exp_lerp(C_TEXT2, C_TEXT, g), EXP_ALIGN_LEFT);
}

void page_motion_render(exp_app_t *app)
{
    char buf[48], buf2[48];
    exp_btn_t btns[EXP_MAX_BTN];
    page_motion_layout(app, btns);

    /* 3D attitude panel. */
    const bool p0 = app->ptr_down && app->press_id == BTN_YAW_RESET;
    ui_draw_button(&btns[0], NULL, p0, EXP_ICON_HOME, false);
    if (app->imu_ok) exp_layout_keepout((exp_rect_t){BOARD_X + 20, BOARD_Y + 30, BOARD_W - 40, 140});
    exp_text(app->yaw_absolute ? T_YAW_MAGREF : T_YAW_TAP, BOARD_X + 12, BOARD_Y + 8, EXP_FONT_CAP, C_TEXT3, EXP_ALIGN_LEFT);
    if (app->imu_ok) {
        draw_board(app, (float)(BOARD_X + BOARD_W / 2), (float)(BOARD_Y + 98), 62.0f);
        snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f", (double)nz(app->s.acc[0], 0.01f), (double)nz(app->s.acc[1], 0.01f), (double)nz(app->s.acc[2], 0.01f));
        snprintf(buf2, sizeof(buf2), "%ld  %ld  %ld", lroundf(app->s.gyr[0]), lroundf(app->s.gyr[1]), lroundf(app->s.gyr[2]));
    } else {
        const exp_rect_t eb = {BOARD_X + 14, BOARD_Y + 54, BOARD_W - 28, 100};
        exp_text_box(eb, T_ERR_IMU, EXP_FONT_BODY, C_ERR, EXP_ALIGN_CENTER, 2);
        const exp_rect_t eb2 = {BOARD_X + 14, BOARD_Y + 118, BOARD_W - 28, 50};
        exp_text_box(eb2, (app->debug_flags & EXP_DBG_LONG_TEXT) ? T_LONG_ERROR : T_ERR_IMU2, EXP_FONT_CAP, C_TEXT2,
                     EXP_ALIGN_CENTER, 3);
        snprintf(buf, sizeof(buf), "--  --  --");
        snprintf(buf2, sizeof(buf2), "--  --  --");
    }
    exp_text(T_ACC, BOARD_X + 12, BOARD_Y + 178, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    exp_text(buf, BOARD_X + BOARD_W - 12, BOARD_Y + 178, EXP_FONT_CAP, C_TEXT, EXP_ALIGN_RIGHT);
    exp_text(T_GYR, BOARD_X + 12, BOARD_Y + 202, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
    exp_text(buf2, BOARD_X + BOARD_W - 12, BOARD_Y + 202, EXP_FONT_CAP, C_TEXT, EXP_ALIGN_RIGHT);

    /* Pitch / Roll / Yaw. */
    const bool ok = app->imu_ok;
    fmt_deg(buf, sizeof(buf), app->pitch);
    draw_value_tile(COL_X, 56, COL_W, 70, T_PITCH, ok ? buf : "--", NULL, ok ? C_TEXT : C_TEXT3);
    fmt_deg(buf, sizeof(buf), app->roll);
    draw_value_tile(COL_X, 132, COL_W, 70, T_ROLL, ok ? buf : "--", NULL, ok ? C_TEXT : C_TEXT3);
    fmt_deg(buf, sizeof(buf), app->yaw);
    draw_value_tile(COL_X, 208, COL_W, 78, T_YAW, ok ? buf : "--", app->yaw_absolute ? T_MAGN : T_REL, ok ? C_TEXT : C_TEXT3);

    /* Row 2. */
    draw_gravity(app, 16, ROW2_Y, 112, ROW2_H);
    const bool p1 = app->ptr_down && app->press_id == BTN_MAG_CAL;
    ui_draw_button(&btns[1], NULL, p1, EXP_ICON_HOME, false);
    draw_compass(app, btns[1].r.x, btns[1].r.y, btns[1].r.w, btns[1].r.h, p1);

    const int ex = 336, ew = 128;
    ui_tile(ex, ROW2_Y, ew, ROW2_H, T_EVENTS);
    const exp_motion_t *m = &app->motion;
    char tilt_txt[24] = T_EV_TILT;
    if (m->tilted) {
        const char *d = (m->dir & EXP_DIR_LEFT) ? T_DIR_LEFT : (m->dir & EXP_DIR_RIGHT) ? T_DIR_RIGHT :
                        (m->dir & EXP_DIR_FAR) ? T_DIR_BACK : (m->dir & EXP_DIR_NEAR) ? T_DIR_FRONT : "";
        snprintf(tilt_txt, sizeof(tilt_txt), "%s %s", T_EV_TILT, d);
    }
    event_chip(ex + 12, ROW2_Y + 28, ew - 24, tilt_txt, app->event_glow[0], m->tilted && ok);
    event_chip(ex + 12, ROW2_Y + 56, ew - 24, m->flipped ? T_FACE_DOWN : T_EV_FLIP, app->event_glow[1], m->flipped && ok);
    event_chip(ex + 12, ROW2_Y + 84, ew - 24, T_EV_SHAKE, app->event_glow[2], false);
}
