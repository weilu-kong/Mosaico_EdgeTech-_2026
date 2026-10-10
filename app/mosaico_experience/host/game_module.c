// SPDX-License-Identifier: Apache-2.0
// Host simulator entry (Raylib Lite engine ABI v1). The same exp_app core is
// compiled into the ESP-IDF firmware; only this file and exp_sim_source.c are
// Host specific.
#include <stdio.h>
#include "exp_app.h"
#include "exp_sim_source.h"
#include "mosaico_game_module.h"
#include "mosaico_raylib_fast.h"

typedef struct {
    exp_app_t *app;
} host_state_t;

static int initialize(void *value, const char *asset_root)
{
    (void)asset_root;
    host_state_t *state = value;
    const exp_app_config_t config = {0};
    state->app = exp_app_create(&config);
    if (!state->app) return -1;
    exp_sim_attach(state->app);
    InitWindow(EXP_W, EXP_H, "ESP-Mosaico Experience");
    SetTargetFPS(EXP_TICK_HZ);
    return 0;
}

static void shutdown(void *value)
{
    host_state_t *state = value;
    exp_sim_detach();
    exp_app_destroy(state->app);
    state->app = NULL;
}

static void input(void *value, const mosaico_host_input_v1_t *event)
{
    host_state_t *state = value;
    if (!event) return;
    if (event->type == MOSAICO_HOST_INPUT_POINTER && event->track_id == 0)
        exp_app_pointer(state->app, event->x, event->y, event->pressed);
}

static void update(void *value)
{
    (void)value;
    exp_sim_tick(1.0f / (float)EXP_TICK_HZ);
}

static int render(void *value)
{
    host_state_t *state = value;
    BeginDrawing();
    if (!MosaicoFastFrameAvailable()) {
        EndDrawing();
        return -1;
    }
    exp_app_render(state->app);
    EndDrawing();
    return 0;
}

static uint32_t state_hash(const void *value)
{
    return exp_app_state_hash(((const host_state_t *)value)->app);
}

static int state_json(const void *value, char *output, size_t capacity)
{
    if (!output || !capacity) return -1;
    const int n = exp_app_state_json(((const host_state_t *)value)->app, output, capacity);
    return n < 0 || (size_t)n >= capacity ? -1 : n;
}

static const mosaico_game_module_v1_t s_module = {
    .descriptor = {MOSAICO_HOST_GAME_ABI_V1, "mosaico_experience", "ESP-Mosaico Experience", EXP_W, EXP_H, EXP_TICK_HZ, 1},
    .state_size = sizeof(host_state_t),
    .initialize = initialize,
    .shutdown = shutdown,
    .input = input,
    .update = update,
    .render = render,
    .state_hash = state_hash,
    .state_json = state_json,
};

const mosaico_game_module_v1_t *mosaico_game_module_v1(void) { return &s_module; }
