// SPDX-License-Identifier: Apache-2.0
// Tilt Maze (傾き迷路): hardware-free game model shared by Host and device.
// Static storage only - entering/leaving the game allocates and frees nothing.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAZE_MAX_N 11
#define MAZE_MAX_HOLES 6
#define MAZE_AREA_PX 400       /* playfield edge in px                       */
#define MAZE_TIME_LIMIT_S 60.0f

typedef enum { MAZE_EASY = 0, MAZE_NORMAL, MAZE_HARD, MAZE_DIFF_COUNT } maze_diff_t;
typedef enum {
    MZ_MENU = 0,
    MZ_COUNTDOWN,
    MZ_PLAYING,
    MZ_SUCCESS,
    MZ_TIMEUP,
} maze_phase_t;

enum { MW_N = 1, MW_E = 2, MW_S = 4, MW_W = 8 };

typedef struct {
    bool hit;          /* wall impact this step                               */
    float hit_strength;/* 0..1                                                */
    bool fell;         /* dropped into a hole                                 */
    bool goal;
    bool timeup;
    bool countdown_tick; /* a 3/2/1 digit changed                             */
    bool go;           /* countdown finished, play begins                     */
} maze_events_t;

typedef struct {
    maze_phase_t phase;
    maze_diff_t diff;
    uint8_t n;         /* cells per side                                      */
    uint8_t cell;      /* cell edge in px                                     */
    uint8_t walls[MAZE_MAX_N][MAZE_MAX_N];
    uint8_t hole_x[MAZE_MAX_HOLES], hole_y[MAZE_MAX_HOLES];
    uint8_t hole_count;
    float bx, by, vx, vy;          /* ball centre, px from maze top-left     */
    float radius;
    uint32_t seed, rng, seed_next;
    float phase_s;                 /* seconds spent in the current phase      */
    float time_left_s;
    float freeze_s;                /* respawn freeze after a fall             */
    int8_t countdown;              /* 3,2,1, 0 = GO                           */
    uint16_t hits, falls;
    int32_t score;
    int32_t best[MAZE_DIFF_COUNT];
    bool new_best;
    bool demo;                     /* bot plays (exhibition attract mode)     */
    float last_hit_s;
    /* demo bot: cached shortest path from the ball's cell to the goal */
    int8_t path_x[MAZE_MAX_N * MAZE_MAX_N], path_y[MAZE_MAX_N * MAZE_MAX_N];
    uint8_t path_len;
    int8_t path_from_x, path_from_y;
} exp_maze_t;

void exp_maze_init(exp_maze_t *m);
/* Builds a maze and enters COUNTDOWN. seed 0 picks a fixed per-difficulty seed. */
void exp_maze_start(exp_maze_t *m, maze_diff_t diff, uint32_t seed, bool demo);
/* Builds a maze only, for the menu preview (phase stays MZ_MENU). */
void exp_maze_preview(exp_maze_t *m, maze_diff_t diff, uint32_t seed);
void exp_maze_to_menu(exp_maze_t *m);
/* ax, ay: acceleration command in g, screen axes (+x right, +y down).
 * dt: seconds. Returns the events that occurred during this step. */
maze_events_t exp_maze_step(exp_maze_t *m, float ax, float ay, float dt);
/* Demo bot: returns the tilt (g) it would apply. */
void exp_maze_bot(exp_maze_t *m, float *ax, float *ay);
/* Shortest solution (cells) ignoring holes; fills m->path_*; returns length. */
int exp_maze_solve(exp_maze_t *m, int from_x, int from_y, bool avoid_holes);
bool exp_maze_is_hole(const exp_maze_t *m, int cx, int cy);
