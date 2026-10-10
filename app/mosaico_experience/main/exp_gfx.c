// SPDX-License-Identifier: Apache-2.0
#include "exp_gfx.h"

#include <math.h>
#include "exp_config.h"

#define PI_F 3.14159265358979f
#define DEG2RAD (PI_F / 180.0f)

float exp_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float exp_ease_out(float t)
{
    t = exp_clampf(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float exp_ease_in_out(float t)
{
    t = exp_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Color exp_lerp(Color a, Color b, float t)
{
    t = exp_clampf(t, 0.0f, 1.0f);
    return (Color){(uint8_t)(a.r + (b.r - a.r) * t + 0.5f), (uint8_t)(a.g + (b.g - a.g) * t + 0.5f),
                   (uint8_t)(a.b + (b.b - a.b) * t + 0.5f), (uint8_t)(a.a + (b.a - a.a) * t + 0.5f)};
}

Color exp_with_alpha(Color c, float alpha)
{
    c.a = (uint8_t)(exp_clampf(alpha, 0.0f, 1.0f) * c.a + 0.5f);
    return c;
}

static void put(int x, int y, float coverage, Color c)
{
    if (coverage <= 0.0f) return;
    if (coverage < 1.0f) c.a = (uint8_t)(c.a * coverage + 0.5f);
    if (c.a == 0) return;
    int ox, oy;
    exp_gfx_get_origin(&ox, &oy);
    DrawPixel(x + ox, y + oy, c);
}

/* ---- Rectangles ------------------------------------------------------------ */
void exp_rect(int x, int y, int w, int h, Color c)
{
    int ox, oy;
    exp_gfx_get_origin(&ox, &oy);
    c = exp_gfx_mod(c);
    if (c.a == 0 || w <= 0 || h <= 0) return;
    DrawRectangle(x + ox, y + oy, w, h, c);
}

void exp_rrect(int x, int y, int w, int h, int r, Color c)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { exp_rect(x, y, w, h, c); return; }
    const Color m = exp_gfx_mod(c);
    if (m.a == 0) return;
    /* Middle band is a single rectangle; only the 2*r corner rows are AA. */
    exp_rect(x, y + r, w, h - 2 * r, c);
    for (int i = 0; i < r; ++i) {
        const float dy = (float)r - ((float)i + 0.5f);
        const float inset = (float)r - sqrtf(fmaxf(0.0f, (float)r * (float)r - dy * dy));
        const int full = (int)ceilf(inset);
        const float cov = (float)full - inset;  /* partial pixel coverage (0..1) */
        const int rows[2] = {y + i, y + h - 1 - i};
        for (int k = 0; k < 2; ++k) {
            exp_rect(x + full, rows[k], w - 2 * full, 1, c);
            if (full > 0 && cov > 0.0f) {
                put(x + full - 1, rows[k], cov, m);
                put(x + w - full, rows[k], cov, m);
            }
        }
    }
}

void exp_panel(int x, int y, int w, int h, int r, Color fill, Color border)
{
    exp_rrect(x, y, w, h, r, border);
    exp_rrect(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, fill);
}

void exp_gradient_v(int x, int y, int w, int h, Color top, Color bottom, int step)
{
    if (step < 1) step = 1;
    for (int row = 0; row < h; row += step) {
        const int bh = (row + step > h) ? h - row : step;
        exp_rect(x, y + row, w, bh, exp_lerp(top, bottom, (float)(row + bh / 2) / (float)h));
    }
}

/* ---- Discs, rings, arcs ------------------------------------------------------ */
void exp_disc(float cx, float cy, float r, Color c)
{
    const Color m = exp_gfx_mod(c);
    if (m.a == 0 || r <= 0.0f) return;
    const int y0 = (int)floorf(cy - r - 1.0f), y1 = (int)ceilf(cy + r + 1.0f);
    const float inner = fmaxf(0.0f, r - 0.7f);
    for (int y = y0; y <= y1; ++y) {
        const float dy = (float)y + 0.5f - cy;
        const float outer_h2 = (r + 0.7f) * (r + 0.7f) - dy * dy;
        if (outer_h2 <= 0.0f) continue;
        const float outer_half = sqrtf(outer_h2);
        const float inner_h2 = inner * inner - dy * dy;
        const float inner_half = inner_h2 > 0.0f ? sqrtf(inner_h2) : 0.0f;
        const int xs_full = (int)ceilf(cx - inner_half);
        const int xe_full = (int)floorf(cx + inner_half);
        if (inner_half > 0.0f && xe_full > xs_full) {
            exp_rect(xs_full, y, xe_full - xs_full, 1, c);
        }
        const int bx0 = (int)floorf(cx - outer_half), bx1 = (int)ceilf(cx + outer_half);
        for (int x = bx0; x <= bx1; ++x) {
            if (inner_half > 0.0f && x >= xs_full && x < xe_full) { x = xe_full - 1; continue; }
            const float dx = (float)x + 0.5f - cx;
            const float d = sqrtf(dx * dx + dy * dy);
            put(x, y, exp_clampf(r + 0.5f - d, 0.0f, 1.0f), m);
        }
    }
}

static void dir_vec(float deg, float *ux, float *uy)
{
    *ux = sinf(deg * DEG2RAD);
    *uy = -cosf(deg * DEG2RAD);
}

static bool in_sector(float px, float py, float u0x, float u0y, float u1x, float u1y, float span)
{
    if (span >= 360.0f) return true;
    if (span <= 0.0f) return false;
    const bool a = (u0x * py - u0y * px) >= 0.0f;
    const bool b = (px * u1y - py * u1x) >= 0.0f;
    if (span <= 180.0f) return a && b;
    return a || b;
}

static void ring_core(float cx, float cy, float r_out, float r_in, float a0, float a1, Color c)
{
    const Color m = exp_gfx_mod(c);
    if (m.a == 0) return;
    float span = a1 - a0;
    if (span < 0.0f) span += 360.0f;
    if (span >= 359.9f) span = 360.0f;
    float u0x, u0y, u1x, u1y;
    dir_vec(a0, &u0x, &u0y);
    dir_vec(a0 + span, &u1x, &u1y);
    const int x0 = (int)floorf(cx - r_out - 1.0f), x1 = (int)ceilf(cx + r_out + 1.0f);
    const int y0 = (int)floorf(cy - r_out - 1.0f), y1 = (int)ceilf(cy + r_out + 1.0f);
    const float ro2 = (r_out + 0.6f) * (r_out + 0.6f);
    const float ri_edge = fmaxf(0.0f, r_in - 0.6f);
    const float ri2 = ri_edge * ri_edge;
    for (int y = y0; y <= y1; ++y) {
        const float dy = (float)y + 0.5f - cy;
        for (int x = x0; x <= x1; ++x) {
            const float dx = (float)x + 0.5f - cx;
            const float d2 = dx * dx + dy * dy;
            if (d2 > ro2 || d2 < ri2) continue;
            if (!in_sector(dx, dy, u0x, u0y, u1x, u1y, span)) continue;
            float cov = 1.0f;
            const float d = sqrtf(d2);
            cov = fminf(cov, exp_clampf(r_out + 0.5f - d, 0.0f, 1.0f));
            if (r_in > 0.0f) cov = fminf(cov, exp_clampf(d - r_in + 0.5f, 0.0f, 1.0f));
            put(x, y, cov, m);
        }
    }
}

void exp_ring(float cx, float cy, float r_out, float r_in, Color c)
{
    ring_core(cx, cy, r_out, r_in, 0.0f, 360.0f, c);
}

void exp_arc(float cx, float cy, float r_out, float r_in, float a0, float a1, Color c, bool caps)
{
    ring_core(cx, cy, r_out, r_in, a0, a1, c);
    if (caps) {
        const float rm = (r_out + r_in) * 0.5f, rw = (r_out - r_in) * 0.5f;
        float ux, uy;
        dir_vec(a0, &ux, &uy);
        exp_disc(cx + ux * rm, cy + uy * rm, rw, c);
        dir_vec(a1, &ux, &uy);
        exp_disc(cx + ux * rm, cy + uy * rm, rw, c);
    }
}

/* ---- Lines (Xiaolin Wu) ---------------------------------------------------- */
static float fpart(float v) { return v - floorf(v); }
static float rfpart(float v) { return 1.0f - fpart(v); }

static void wu_line(float x0, float y0, float x1, float y1, Color m)
{
    const bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    float t;
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    const float dx = x1 - x0, dy = y1 - y0;
    const float grad = dx < 1e-6f ? 1.0f : dy / dx;
    float xend = roundf(x0);
    float yend = y0 + grad * (xend - x0);
    float xgap = rfpart(x0 + 0.5f);
    const int xp1 = (int)xend, yp1 = (int)floorf(yend);
    if (steep) { put(yp1, xp1, rfpart(yend) * xgap, m); put(yp1 + 1, xp1, fpart(yend) * xgap, m); }
    else { put(xp1, yp1, rfpart(yend) * xgap, m); put(xp1, yp1 + 1, fpart(yend) * xgap, m); }
    float inter = yend + grad;
    xend = roundf(x1);
    yend = y1 + grad * (xend - x1);
    xgap = fpart(x1 + 0.5f);
    const int xp2 = (int)xend, yp2 = (int)floorf(yend);
    if (steep) { put(yp2, xp2, rfpart(yend) * xgap, m); put(yp2 + 1, xp2, fpart(yend) * xgap, m); }
    else { put(xp2, yp2, rfpart(yend) * xgap, m); put(xp2, yp2 + 1, fpart(yend) * xgap, m); }
    for (int x = xp1 + 1; x < xp2; ++x) {
        const int yi = (int)floorf(inter);
        if (steep) { put(yi, x, rfpart(inter), m); put(yi + 1, x, fpart(inter), m); }
        else { put(x, yi, rfpart(inter), m); put(x, yi + 1, fpart(inter), m); }
        inter += grad;
    }
}

void exp_line(float x0, float y0, float x1, float y1, float thick, Color c)
{
    const Color m = exp_gfx_mod(c);
    if (m.a == 0) return;
    const float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    if (thick <= 1.0f || len < 0.001f) { wu_line(x0, y0, x1, y1, m); return; }
    const float nx = -(y1 - y0) / len, ny = (x1 - x0) / len;
    const int n = (int)ceilf(thick);
    for (int i = 0; i < n; ++i) {
        const float o = (float)i - (float)(n - 1) * 0.5f;
        wu_line(x0 + nx * o, y0 + ny * o, x1 + nx * o, y1 + ny * o, m);
    }
}

void exp_polyline(const float *xy, int count, float thick, Color c)
{
    for (int i = 0; i + 1 < count; ++i) {
        exp_line(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], thick, c);
    }
}

void exp_tri_fill(float x0, float y0, float x1, float y1, float x2, float y2, Color c)
{
    int ox, oy;
    exp_gfx_get_origin(&ox, &oy);
    c = exp_gfx_mod(c);
    if (c.a == 0) return;
    DrawTriangle((Vector2){x0 + (float)ox, y0 + (float)oy}, (Vector2){x1 + (float)ox, y1 + (float)oy},
                 (Vector2){x2 + (float)ox, y2 + (float)oy}, c);
}

void exp_glow(float cx, float cy, float r, Color c, float strength)
{
    for (int i = 5; i >= 1; --i) {
        const float f = (float)i / 5.0f;
        exp_disc(cx, cy, r * f, exp_with_alpha(c, strength * (1.0f - f) * 0.35f + 0.02f));
    }
}

void exp_scissor(int x, int y, int w, int h)
{
    int ox, oy;
    exp_gfx_get_origin(&ox, &oy);
    BeginScissorMode(x + ox, y + oy, w, h);
}

void exp_scissor_end(void) { EndScissorMode(); }

/* ---- Background ---------------------------------------------------------------- */
void exp_background(uint32_t now_ms)
{
    exp_gradient_v(0, 0, EXP_W, EXP_H, C_BG0, C_BG1, 6);
    const Color grid = EXP_RGBA(120, 170, 255, 9);
    for (int x = 24; x < EXP_W; x += 48) exp_rect(x, 0, 1, EXP_H, grid);
    for (int y = 24; y < EXP_H; y += 48) exp_rect(0, y, EXP_W, 1, grid);
    /* Slow breathing glow in the lower right corner keeps the idle screen alive. */
    const float phase = 0.5f + 0.5f * sinf((float)(now_ms % 6000U) / 6000.0f * 2.0f * PI_F);
    exp_glow(EXP_W - 20.0f, EXP_H - 10.0f, 150.0f, C_CYAN, 0.10f + 0.06f * phase);
}

/* ---- Icons (stroke based, scale with `size`) ------------------------------------ */
static void line_pts(float cx, float cy, float s, const float *p, int n, float th, Color c)
{
    float pts[24];
    for (int i = 0; i < n && i < 12; ++i) {
        pts[i * 2] = cx + p[i * 2] * s;
        pts[i * 2 + 1] = cy + p[i * 2 + 1] * s;
    }
    exp_polyline(pts, n, th, c);
}

void exp_icon(exp_icon_t icon, float cx, float cy, float size, Color c)
{
    const float s = size * 0.5f;                 /* half extent */
    const float th = size >= 36.0f ? 2.4f : 1.9f;
    switch (icon) {
    case EXP_ICON_HOME: {
        const float roof[] = {-0.95f, -0.05f, 0.0f, -0.85f, 0.95f, -0.05f};
        line_pts(cx, cy, s, roof, 3, th, c);
        const float body[] = {-0.65f, -0.2f, -0.65f, 0.8f, 0.65f, 0.8f, 0.65f, -0.2f};
        line_pts(cx, cy, s, body, 4, th, c);
        const float door[] = {-0.2f, 0.8f, -0.2f, 0.25f, 0.2f, 0.25f, 0.2f, 0.8f};
        line_pts(cx, cy, s, door, 4, th, c);
        break;
    }
    case EXP_ICON_MOTION: {  /* tilted square with orbit */
        const float quad[] = {-0.7f, -0.2f, 0.1f, -0.75f, 0.85f, -0.2f, 0.05f, 0.35f, -0.7f, -0.2f};
        line_pts(cx, cy, s, quad, 5, th, c);
        exp_arc(cx, cy + s * 0.15f, s * 0.95f, s * 0.95f - th, 200.0f, 340.0f, exp_with_alpha(c, 0.7f), false);
        exp_disc(cx, cy - s * 0.2f, th * 0.9f, c);
        break;
    }
    case EXP_ICON_AUDIO: {   /* equaliser bars */
        const float h[] = {0.35f, 0.8f, 0.55f, 0.95f, 0.45f};
        for (int i = 0; i < 5; ++i) {
            const float x = cx + (i - 2) * s * 0.42f;
            exp_line(x, cy - h[i] * s, x, cy + h[i] * s, th + 0.6f, c);
        }
        break;
    }
    case EXP_ICON_GAME: {    /* maze corner with ball */
        const float wall[] = {-0.85f, 0.85f, -0.85f, -0.85f, 0.85f, -0.85f, 0.85f, 0.2f, 0.1f, 0.2f, 0.1f, -0.35f};
        line_pts(cx, cy, s, wall, 6, th, c);
        exp_disc(cx - s * 0.35f, cy + s * 0.45f, s * 0.26f, c);
        break;
    }
    case EXP_ICON_DEVICE: {  /* chip with pins */
        const float body[] = {-0.55f, -0.55f, 0.55f, -0.55f, 0.55f, 0.55f, -0.55f, 0.55f, -0.55f, -0.55f};
        line_pts(cx, cy, s, body, 5, th, c);
        for (int i = -1; i <= 1; ++i) {
            const float o = (float)i * 0.35f * s;
            exp_line(cx + o, cy - s * 0.55f, cx + o, cy - s * 0.9f, th - 0.4f, c);
            exp_line(cx + o, cy + s * 0.55f, cx + o, cy + s * 0.9f, th - 0.4f, c);
            exp_line(cx - s * 0.55f, cy + o, cx - s * 0.9f, cy + o, th - 0.4f, c);
            exp_line(cx + s * 0.55f, cy + o, cx + s * 0.9f, cy + o, th - 0.4f, c);
        }
        break;
    }
    case EXP_ICON_CHEVRON_RIGHT: {
        const float p[] = {-0.35f, -0.7f, 0.35f, 0.0f, -0.35f, 0.7f};
        line_pts(cx, cy, s, p, 3, th, c);
        break;
    }
    case EXP_ICON_CHEVRON_LEFT: {
        const float p[] = {0.35f, -0.7f, -0.35f, 0.0f, 0.35f, 0.7f};
        line_pts(cx, cy, s, p, 3, th, c);
        break;
    }
    case EXP_ICON_SPEAKER: {
        const float p[] = {-0.9f, -0.3f, -0.45f, -0.3f, 0.1f, -0.75f, 0.1f, 0.75f, -0.45f, 0.3f, -0.9f, 0.3f, -0.9f, -0.3f};
        line_pts(cx, cy, s, p, 7, th, c);
        exp_arc(cx + s * 0.1f, cy, s * 0.7f, s * 0.7f - th, 40.0f, 140.0f, c, false);
        exp_arc(cx + s * 0.1f, cy, s * 1.05f, s * 1.05f - th, 40.0f, 140.0f, exp_with_alpha(c, 0.7f), false);
        break;
    }
    case EXP_ICON_PLAY:
        exp_tri_fill(cx - s * 0.5f, cy - s * 0.7f, cx - s * 0.5f, cy + s * 0.7f, cx + s * 0.7f, cy, c);
        break;
    case EXP_ICON_CLOSE: {
        exp_line(cx - s * 0.6f, cy - s * 0.6f, cx + s * 0.6f, cy + s * 0.6f, th, c);
        exp_line(cx - s * 0.6f, cy + s * 0.6f, cx + s * 0.6f, cy - s * 0.6f, th, c);
        break;
    }
    case EXP_ICON_BATTERY: {
        const float p[] = {-0.9f, -0.5f, 0.7f, -0.5f, 0.7f, 0.5f, -0.9f, 0.5f, -0.9f, -0.5f};
        line_pts(cx, cy, s, p, 5, 1.4f, c);
        exp_rect((int)(cx + s * 0.78f), (int)(cy - s * 0.2f), 2, (int)(s * 0.4f), c);
        break;
    }
    default: break;
    }
}
