// SPDX-License-Identifier: Apache-2.0
// Exhaustive check: the shim's fast blend (exact /255 trick + cached tables) is bit-identical to the original
// per-pixel formula `(old * (255 - a) + c * a) / 255`. Includes the shim source to reach its static functions.
// Run twice by scripts/run_all_tests.sh: plain build and -DEXP_SURFACE_SWAP_BYTES=1. Host only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../firmware/components/exp_surface/exp_surface.c"

static uint16_t ref_blend(uint16_t old, Color c)
{
    const unsigned a = c.a, ia = 255U - a;
    const unsigned r = ((((old >> 11) & 31U) << 3) * ia + c.r * a) / 255U;
    const unsigned g = ((((old >> 5) & 63U) << 2) * ia + c.g * a) / 255U;
    const unsigned b = (((old & 31U) << 3) * ia + c.b * a) / 255U;
    return (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | (b >> 3));
}

int main(void)
{
    const Color colors[] = {{0, 0, 0, 0},       {255, 255, 255, 0}, {255, 0, 0, 0},   {0, 255, 0, 0},
                            {0, 0, 255, 0},     {120, 170, 255, 0}, {13, 77, 201, 0}, {255, 255, 0, 0},
                            {64, 128, 192, 0},  {1, 2, 3, 0},       {250, 251, 252, 0}};
    unsigned long checked = 0, bad = 0;
    for (size_t ci = 0; ci < sizeof(colors) / sizeof(colors[0]); ++ci) {
        for (unsigned a = 1; a <= 254; ++a) {
            Color c = colors[ci];
            c.a = (unsigned char)a;
            // single pixel path, every possible old pixel
            for (unsigned old = 0; old < 65536; ++old) {
                const uint16_t want = ref_blend((uint16_t)old, c);
                const uint16_t got = PX_LOAD(PX_STORE(blend_px((uint16_t)old, c)));
                // blend_px works on native order; PX_* are symmetric swaps, so this is a round trip check
                if (got != want) ++bad;
                ++checked;
            }
            // span path (table) over a 64k run, stored in the build's memory order
            static uint16_t mem[65536];
            for (unsigned old = 0; old < 65536; ++old) mem[old] = PX_STORE((uint16_t)old);
            blend_span(mem, 65536, c);
            for (unsigned old = 0; old < 65536; ++old) {
                if (PX_LOAD(mem[old]) != ref_blend((uint16_t)old, c)) ++bad;
                ++checked;
            }
            // short span (< 32, takes the non-table path)
            for (unsigned old = 0; old < 65536; old += 997) {
                uint16_t m[8];
                for (int k = 0; k < 8; ++k) m[k] = PX_STORE((uint16_t)(old + (unsigned)k * 131U));
                blend_span(m, 8, c);
                for (int k = 0; k < 8; ++k) {
                    if (PX_LOAD(m[k]) != ref_blend((uint16_t)(old + (unsigned)k * 131U), c)) ++bad;
                    ++checked;
                }
            }
        }
    }
    /* solid fill: every start alignment (odd stride), every length 0..70, neighbours untouched */
    {
        enum { STRIDE = 481, ROWS = 480 };
        static uint16_t buf[STRIDE * ROWS], want[STRIDE * ROWS];
        const Color fills[] = {{0, 0, 0, 255}, {255, 255, 255, 255}, {12, 200, 77, 255}, {255, 0, 128, 255}};
        unsigned long fill_checked = 0;
        for (size_t fi = 0; fi < sizeof(fills) / sizeof(fills[0]); ++fi) {
            for (int x0 = 0; x0 < 5; ++x0) {
                for (int len = 0; len <= 70; ++len) {
                    for (int y0 = 0; y0 < 3; ++y0) {
                        for (size_t i = 0; i < (size_t)STRIDE * ROWS; ++i) buf[i] = want[i] = PX_STORE((uint16_t)(i * 2654435761u >> 16));
                        const uint16_t px = PX_STORE(rgb565(fills[fi]));
                        for (int y = y0; y < y0 + 2; ++y)
                            for (int x = x0; x < x0 + len; ++x) want[(size_t)y * STRIDE + (size_t)x] = px;
                        exp_surface_attach(buf, STRIDE, NULL, NULL);
                        MosaicoFastBeginDrawing();
                        MosaicoFastDrawRectangle(x0, y0, len, 2, fills[fi]);
                        MosaicoFastEndDrawing();
                        if (memcmp(buf, want, sizeof(buf)) != 0) { ++bad; printf("fill mismatch x0=%d len=%d y0=%d\n", x0, len, y0); }
                        ++fill_checked;
                    }
                }
            }
        }
        // full-width rows and a big block (the background bands) incl. the screen clip edge
        for (size_t i = 0; i < (size_t)STRIDE * ROWS; ++i) buf[i] = want[i] = PX_STORE((uint16_t)i);
        const uint16_t px2 = PX_STORE(rgb565(fills[2]));
        for (int y = 7; y < 480; ++y) for (int x = 3; x < 480; ++x) want[(size_t)y * STRIDE + (size_t)x] = px2;
        exp_surface_attach(buf, STRIDE, NULL, NULL);
        MosaicoFastBeginDrawing();
        MosaicoFastDrawRectangle(3, 7, 600, 600, fills[2]);      /* larger than the screen: clipped */
        MosaicoFastEndDrawing();
        if (memcmp(buf, want, sizeof(buf)) != 0) { ++bad; printf("big fill mismatch\n"); }
        ++fill_checked;
        checked += fill_checked;
        printf("fill_span: %lu rectangles compared\n", fill_checked);
    }
    printf("blend_exact: %lu comparisons, %lu mismatches (%s build)\n", checked, bad,
#ifdef EXP_SURFACE_SWAP_BYTES
           "byte-swapped"
#else
           "plain"
#endif
    );
    return bad ? 1 : 0;
}
