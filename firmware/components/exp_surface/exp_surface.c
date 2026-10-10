// SPDX-License-Identifier: Apache-2.0
#include "exp_surface.h"
#include "mosaico_raylib_fast.h"

#include <stdint.h>
#include <string.h>

static uint16_t *s_fb;
static size_t s_fb_stride;
static exp_surface_present_fn s_present;
static void *s_present_ctx;
static uint32_t s_frames;

static uint16_t *s_pixels;      /* non-NULL only between BeginDrawing and EndDrawing */
static size_t s_stride;
static bool s_scissor_active;
static int s_sx0, s_sy0, s_sx1, s_sy1;

void exp_surface_attach(uint16_t *pixels, size_t stride_px, exp_surface_present_fn present, void *ctx)
{
    s_fb = pixels;
    s_fb_stride = stride_px;
    s_present = present;
    s_present_ctx = ctx;
}

uint32_t exp_surface_frames_presented(void) { return s_frames; }

void exp_surface_set_target(uint16_t *pixels) { s_fb = pixels; }

/* The CO5300 QSPI panel takes RGB565 big-endian (high byte first); the CPU stores it little-endian.
 * With EXP_SURFACE_SWAP_BYTES the frame buffer holds the panel's byte order, so the finished frame
 * can be handed to the panel as-is. The official game app does the same (swap_bytes = true).
 * Host builds leave it off so the pixel-equivalence check against the official engine stays exact. */
static inline uint16_t px_order(uint16_t v)
{
#ifdef EXP_SURFACE_SWAP_BYTES
    return (uint16_t)((uint16_t)(v << 8) | (uint16_t)(v >> 8));
#else
    return v;
#endif
}
#define PX_STORE(v) px_order(v)
#define PX_LOAD(v) px_order(v)

static inline uint16_t rgb565(Color c)
{
    return (uint16_t)(((uint16_t)(c.r & 0xf8U) << 8) | ((uint16_t)(c.g & 0xfcU) << 3) | (c.b >> 3));
}

/* x / 255 for 0 <= x <= 65025, exact (checked exhaustively; tests/blend_exact.c). The blend numerators never
 * exceed 255*255 because old <= 255, colour <= 255 and a + (255 - a) = 255. */
static inline unsigned div255(unsigned x) { return (x + 1U + (x >> 8)) >> 8; }

/* Source-over blend of one pixel; bit-identical to the engine's `(old * (255 - a) + c * a) / 255`. */
static inline uint16_t blend_px(uint16_t old, Color color)
{
    const unsigned a = color.a, ia = 255U - a;
    const unsigned r = div255((((old >> 11) & 31U) << 3) * ia + color.r * a);
    const unsigned g = div255((((old >> 5) & 63U) << 2) * ia + color.g * a);
    const unsigned b = div255(((old & 31U) << 3) * ia + color.b * a);
    return (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | (b >> 3));
}

/* Long semi-transparent spans repeat one colour and alpha thousands of times (glow discs, panels, grid lines).
 * For such a span the result only depends on the old pixel's 5/6/5-bit channels, so three small tables
 * (built with the same formula) replace all arithmetic. Same output, bit for bit. */
static struct {
    uint32_t key;
    uint16_t r[32], g[64], b[32];
} s_tab = {.key = 0};

static void tab_prepare(Color c)
{
    const uint32_t key = ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | c.a;
    if (key == s_tab.key) return;
    const unsigned a = c.a, ia = 255U - a;
    for (unsigned i = 0; i < 32; ++i) {
        s_tab.r[i] = (uint16_t)((div255((i << 3) * ia + c.r * a) & 0xf8U) << 8);
        s_tab.b[i] = (uint16_t)(div255((i << 3) * ia + c.b * a) >> 3);
    }
    for (unsigned i = 0; i < 64; ++i) s_tab.g[i] = (uint16_t)((div255((i << 2) * ia + c.g * a) & 0xfcU) << 3);
    s_tab.key = key;
}

static inline void blend_span(uint16_t *dst, int n, Color color)
{
    if (n >= 32) {
        tab_prepare(color);
        for (; n > 0; --n, ++dst) {
            const uint16_t old = PX_LOAD(*dst);
            *dst = PX_STORE((uint16_t)(s_tab.r[old >> 11] | s_tab.g[(old >> 5) & 63U] | s_tab.b[old & 31U]));
        }
    } else {
        for (; n > 0; --n, ++dst) *dst = PX_STORE(blend_px(PX_LOAD(*dst), color));
    }
}

/* Solid fill of a run of pixels. The panel buffers live in PSRAM behind a cache and the CPU stores 16 bits per
 * instruction, so a plain per-pixel loop is instruction-bound (about 4 cycles per pixel on the board). Two pixels
 * per 32-bit store, unrolled, writes the same bytes with a quarter of the instructions. Needs little endian and a
 * 4-byte aligned store, otherwise (or on a big-endian host) it falls back to the scalar loop. */
typedef uint32_t __attribute__((may_alias)) px_pair_t;

static inline void fill_span(uint16_t *dst, int n, uint16_t px)
{
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (n > 0 && ((uintptr_t)dst & 2U)) { *dst++ = px; --n; }
    const uint32_t pair = (uint32_t)px | ((uint32_t)px << 16);
    px_pair_t *d = (px_pair_t *)dst;
    for (; n >= 8; n -= 8, d += 4) { d[0] = pair; d[1] = pair; d[2] = pair; d[3] = pair; }
    for (; n >= 2; n -= 2) *d++ = pair;
    if (n > 0) *(uint16_t *)d = px;
#else
    for (; n > 0; --n) *dst++ = px;
#endif
}

static inline void put_pixel(int x, int y, Color color)
{
    if (!s_pixels || (unsigned)x >= EXP_SURFACE_W || (unsigned)y >= EXP_SURFACE_H) return;
    if (s_scissor_active && (x < s_sx0 || y < s_sy0 || x >= s_sx1 || y >= s_sy1)) return;
    uint16_t *dst = &s_pixels[(size_t)y * s_stride + (size_t)x];
    if (color.a == 255) {
        *dst = PX_STORE(rgb565(color));
    } else if (color.a) {
        *dst = PX_STORE(blend_px(PX_LOAD(*dst), color));
    }
}

void MosaicoFastBeginDrawing(void)
{
    s_pixels = s_fb;
    s_stride = s_fb_stride;
    s_scissor_active = false;
}

bool MosaicoFastFrameAvailable(void) { return s_pixels != NULL; }

void MosaicoFastEndDrawing(void)
{
    if (s_pixels && s_present) s_present(s_pixels, s_stride, s_present_ctx);
    if (s_pixels) ++s_frames;
    s_pixels = NULL;
    s_stride = 0;
    s_scissor_active = false;
}

void MosaicoFastBeginScissorMode(int x, int y, int width, int height)
{
    s_sx0 = x < 0 ? 0 : x;
    s_sy0 = y < 0 ? 0 : y;
    s_sx1 = x + width > EXP_SURFACE_W ? EXP_SURFACE_W : x + width;
    s_sy1 = y + height > EXP_SURFACE_H ? EXP_SURFACE_H : y + height;
    s_scissor_active = width > 0 && height > 0 && s_sx0 < s_sx1 && s_sy0 < s_sy1;
}

void MosaicoFastEndScissorMode(void) { s_scissor_active = false; }

void MosaicoFastDrawPixel(int x, int y, Color color) { put_pixel(x, y, color); }

void MosaicoFastDrawRectangle(int x, int y, int width, int height, Color color)
{
    if (!s_pixels || width <= 0 || height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width > EXP_SURFACE_W ? EXP_SURFACE_W : x + width;
    int y1 = y + height > EXP_SURFACE_H ? EXP_SURFACE_H : y + height;
    if (s_scissor_active) {
        if (x0 < s_sx0) x0 = s_sx0;
        if (y0 < s_sy0) y0 = s_sy0;
        if (x1 > s_sx1) x1 = s_sx1;
        if (y1 > s_sy1) y1 = s_sy1;
    }
    if (x0 >= x1 || y0 >= y1) return;
    if (color.a != 255) {
        if (color.a == 0) return;
        for (int yy = y0; yy < y1; ++yy) blend_span(s_pixels + (size_t)yy * s_stride + (size_t)x0, x1 - x0, color);
        return;
    }
    const uint16_t px = PX_STORE(rgb565(color));
    for (int yy = y0; yy < y1; ++yy) fill_span(s_pixels + (size_t)yy * s_stride + (size_t)x0, x1 - x0, px);
}

void MosaicoFastDrawRectangleLines(int x, int y, int width, int height, Color color)
{
    MosaicoFastDrawRectangle(x, y, width, 1, color);
    MosaicoFastDrawRectangle(x, y + height - 1, width, 1, color);
    MosaicoFastDrawRectangle(x, y + 1, 1, height - 2, color);
    MosaicoFastDrawRectangle(x + width - 1, y + 1, 1, height - 2, color);
}

static inline int edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void MosaicoFastDrawTriangle(Vector2 av, Vector2 bv, Vector2 cv, Color color)
{
    const int ax = (int)av.x, ay = (int)av.y, bx = (int)bv.x, by = (int)bv.y;
    const int cx = (int)cv.x, cy = (int)cv.y;
    int minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= EXP_SURFACE_W) maxx = EXP_SURFACE_W - 1;
    if (maxy >= EXP_SURFACE_H) maxy = EXP_SURFACE_H - 1;
    if (s_scissor_active) {
        if (minx < s_sx0) minx = s_sx0;
        if (miny < s_sy0) miny = s_sy0;
        if (maxx >= s_sx1) maxx = s_sx1 - 1;
        if (maxy >= s_sy1) maxy = s_sy1 - 1;
    }
    const int area = edge(ax, ay, bx, by, cx, cy);
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            const int w0 = edge(bx, by, cx, cy, x, y), w1 = edge(cx, cy, ax, ay, x, y);
            const int w2 = edge(ax, ay, bx, by, x, y);
            if ((area >= 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) || (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0))
                put_pixel(x, y, color);
        }
    }
}
