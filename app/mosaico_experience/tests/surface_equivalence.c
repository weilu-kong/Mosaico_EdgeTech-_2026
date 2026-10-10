// SPDX-License-Identifier: Apache-2.0
// Renders a fixed script of UI states through the Route B drawing shim (no engine) and writes
// raw RGB565 frames. tests/surface_equivalence.py renders the same script through the official
// engine and compares pixel by pixel. Host only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exp_app.h"
#include "exp_sim_source.h"
#include "exp_surface.h"
#include "mosaico_raylib_fast.h"

static uint16_t s_fb[EXP_SURFACE_H * EXP_SURFACE_W];
static const char *s_dir;
static int s_n;

static void present(const uint16_t *px, size_t stride, void *ctx)
{
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%02d.rgb565", s_dir, s_n++);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    for (int y = 0; y < EXP_SURFACE_H; ++y) fwrite(px + (size_t)y * stride, 2, EXP_SURFACE_W, f);
    fclose(f);
}

static exp_app_t *s_app;
static void ticks(int n) { while (n-- > 0) exp_sim_tick(1.0f / 30.0f); }
static void shot(void)
{
    BeginDrawing();
    if (MosaicoFastFrameAvailable()) exp_app_render(s_app);
    EndDrawing();
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir\n", argv[0]); return 2; }
    s_dir = argv[1];
    exp_surface_attach(s_fb, EXP_SURFACE_W, present, NULL);
    const exp_app_config_t cfg = {0};
    s_app = exp_app_create(&cfg);
    exp_sim_attach(s_app);
    exp_sim_set_heap_demo(0);
    /* keep in sync with tests/surface_equivalence.py */
    exp_sim_goto(0); ticks(15); shot();                                       /* 0 home        */
    exp_sim_set_attitude(18, -22, 40); ticks(45); exp_sim_goto(1); ticks(45); shot(); /* 1 motion */
    exp_sim_set_mic(3, -24); exp_sim_goto(2); ticks(45); shot();              /* 2 audio voice */
    exp_sim_set_mic(1, -22); exp_sim_goto(3); ticks(15); shot();              /* 3 game menu   */
    exp_sim_game_start(0, 0); ticks(120); shot();                             /* 4 playing     */
    exp_sim_set_modules(1, 2); exp_sim_set_battery(1, 14, 1); exp_sim_goto(4); ticks(15); shot(); /* 5 device */
    exp_sim_goto(0); ticks(4); shot();                                        /* 6 transition  */
    exp_sim_detach();
    exp_app_destroy(s_app);
    printf("frames=%d\n", s_n);
    return s_n == 7 ? 0 : 1;
}
