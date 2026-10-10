// SPDX-License-Identifier: Apache-2.0
#include "exp_maze.h"

#include <math.h>
#include <string.h>

#define WALL_T 4.0f
#define ACCEL_PX_PER_G 1100.0f
#define DAMPING 1.8f
#define SPEED_CAP 320.0f
#define RESTITUTION 0.35f
#define FALL_PENALTY_S 5.0f
#define COUNTDOWN_STEP_S 0.8f
#define SUBSTEPS 4

static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

void exp_maze_init(exp_maze_t *m)
{
    memset(m, 0, sizeof(*m));
    m->seed_next = 0x4D5A0001u;
    m->diff = MAZE_EASY;
    m->phase = MZ_MENU;
}

bool exp_maze_is_hole(const exp_maze_t *m, int cx, int cy)
{
    for (int i = 0; i < m->hole_count; ++i)
        if (m->hole_x[i] == cx && m->hole_y[i] == cy) return true;
    return false;
}

/* BFS over open passages; path[0] is the start cell. */
int exp_maze_solve(exp_maze_t *m, int fx, int fy, bool avoid_holes)
{
    const int n = m->n;
    int8_t px[MAZE_MAX_N][MAZE_MAX_N], py[MAZE_MAX_N][MAZE_MAX_N];
    bool seen[MAZE_MAX_N][MAZE_MAX_N];
    memset(seen, 0, sizeof(seen));
    int qx[MAZE_MAX_N * MAZE_MAX_N], qy[MAZE_MAX_N * MAZE_MAX_N];
    int head = 0, tail = 0;
    qx[tail] = fx; qy[tail++] = fy;
    seen[fy][fx] = true;
    px[fy][fx] = -1; py[fy][fx] = -1;
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    static const uint8_t bit[4] = {MW_N, MW_E, MW_S, MW_W};
    while (head < tail) {
        const int x = qx[head], y = qy[head++];
        if (x == n - 1 && y == n - 1) break;
        for (int d = 0; d < 4; ++d) {
            if (m->walls[y][x] & bit[d]) continue;
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= n || ny >= n || seen[ny][nx]) continue;
            if (avoid_holes && exp_maze_is_hole(m, nx, ny)) continue;
            seen[ny][nx] = true;
            px[ny][nx] = (int8_t)x; py[ny][nx] = (int8_t)y;
            qx[tail] = nx; qy[tail++] = ny;
        }
    }
    m->path_len = 0;
    if (!seen[n - 1][n - 1]) return 0;
    int8_t rx[MAZE_MAX_N * MAZE_MAX_N], ry[MAZE_MAX_N * MAZE_MAX_N];
    int len = 0, x = n - 1, y = n - 1;
    while (x >= 0 && len < MAZE_MAX_N * MAZE_MAX_N) {
        rx[len] = (int8_t)x; ry[len] = (int8_t)y; ++len;
        const int nx = px[y][x], ny = py[y][x];
        x = nx; y = ny;
    }
    for (int i = 0; i < len; ++i) {
        m->path_x[i] = rx[len - 1 - i];
        m->path_y[i] = ry[len - 1 - i];
    }
    m->path_len = (uint8_t)len;
    m->path_from_x = (int8_t)fx; m->path_from_y = (int8_t)fy;
    return len;
}

static void build(exp_maze_t *m, maze_diff_t diff, uint32_t seed)
{
    static const uint8_t sizes[MAZE_DIFF_COUNT] = {7, 9, 11};
    const int n = sizes[diff];
    const int32_t best[MAZE_DIFF_COUNT] = {m->best[0], m->best[1], m->best[2]};
    const uint32_t seed_next = m->seed_next;
    const bool demo = m->demo;
    memset(m, 0, sizeof(*m));
    m->best[0] = best[0]; m->best[1] = best[1]; m->best[2] = best[2];
    m->seed_next = seed_next;
    m->demo = demo;
    m->diff = diff;
    m->n = (uint8_t)n;
    m->cell = (uint8_t)(MAZE_AREA_PX / n);
    m->seed = seed;
    m->rng = seed;
    m->radius = (float)m->cell * 0.2f;
    memset(m->walls, 0x0F, sizeof(m->walls));

    /* Recursive backtracker. */
    bool visited[MAZE_MAX_N][MAZE_MAX_N];
    memset(visited, 0, sizeof(visited));
    int sx[MAZE_MAX_N * MAZE_MAX_N], sy[MAZE_MAX_N * MAZE_MAX_N], sp = 0;
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    static const uint8_t bit[4] = {MW_N, MW_E, MW_S, MW_W}, opp[4] = {MW_S, MW_W, MW_N, MW_E};
    sx[sp] = 0; sy[sp++] = 0; visited[0][0] = true;
    while (sp > 0) {
        const int x = sx[sp - 1], y = sy[sp - 1];
        int cand[4], cn = 0;
        for (int d = 0; d < 4; ++d) {
            const int nx = x + dx[d], ny = y + dy[d];
            if (nx >= 0 && ny >= 0 && nx < n && ny < n && !visited[ny][nx]) cand[cn++] = d;
        }
        if (cn == 0) { --sp; continue; }
        const int d = cand[rnd(&m->rng) % (uint32_t)cn];
        const int nx = x + dx[d], ny = y + dy[d];
        m->walls[y][x] &= (uint8_t)~bit[d];
        m->walls[ny][nx] &= (uint8_t)~opp[d];
        visited[ny][nx] = true;
        sx[sp] = nx; sy[sp++] = ny;
    }
    /* Braiding: knock out extra walls so harder mazes have alternative routes. */
    const int extra = diff == MAZE_HARD ? n * n / 5 : (diff == MAZE_NORMAL ? n * n / 12 : 0);
    for (int i = 0; i < extra; ++i) {
        const int x = (int)(rnd(&m->rng) % (uint32_t)(n - 1)), y = (int)(rnd(&m->rng) % (uint32_t)(n - 1));
        if (rnd(&m->rng) & 1u) { m->walls[y][x] &= (uint8_t)~MW_E; m->walls[y][x + 1] &= (uint8_t)~MW_W; }
        else { m->walls[y][x] &= (uint8_t)~MW_S; m->walls[y + 1][x] &= (uint8_t)~MW_N; }
    }
    /* Holes sit beside the solution path, never on it. */
    if (diff == MAZE_HARD) {
        exp_maze_solve(m, 0, 0, false);
        bool on_path[MAZE_MAX_N][MAZE_MAX_N];
        memset(on_path, 0, sizeof(on_path));
        for (int i = 0; i < m->path_len; ++i) on_path[m->path_y[i]][m->path_x[i]] = true;
        int tries = 0;
        while (m->hole_count < 5 && tries++ < 400) {
            const int x = (int)(rnd(&m->rng) % (uint32_t)n), y = (int)(rnd(&m->rng) % (uint32_t)n);
            if (on_path[y][x] || (x + y) < 3 || (x == n - 1 && y == n - 1) || exp_maze_is_hole(m, x, y)) continue;
            bool beside = false;
            for (int d = 0; d < 4; ++d) {
                const int nx = x + dx[d], ny = y + dy[d];
                if (nx >= 0 && ny >= 0 && nx < n && ny < n && on_path[ny][nx] && !(m->walls[y][x] & bit[d])) beside = true;
            }
            if (!beside) continue;
            m->hole_x[m->hole_count] = (uint8_t)x; m->hole_y[m->hole_count++] = (uint8_t)y;
        }
    }
    m->bx = m->cell * 0.5f; m->by = m->cell * 0.5f;
    m->time_left_s = MAZE_TIME_LIMIT_S;
    m->path_from_x = -1;
}

void exp_maze_preview(exp_maze_t *m, maze_diff_t diff, uint32_t seed)
{
    build(m, diff, seed ? seed : m->seed_next);
    m->phase = MZ_MENU;
}

void exp_maze_to_menu(exp_maze_t *m)
{
    uint32_t s = m->seed_next;
    rnd(&s);
    m->seed_next = s;
    m->demo = false;
    exp_maze_preview(m, m->diff, 0);
}

void exp_maze_start(exp_maze_t *m, maze_diff_t diff, uint32_t seed, bool demo)
{
    m->demo = demo;
    build(m, diff, seed ? seed : m->seed_next);
    m->phase = MZ_COUNTDOWN;
    m->countdown = 3;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void rect_collide(exp_maze_t *m, float rx0, float ry0, float rx1, float ry1, maze_events_t *ev)
{
    const float cx = clampf(m->bx, rx0, rx1), cy = clampf(m->by, ry0, ry1);
    float dx = m->bx - cx, dy = m->by - cy;
    float d2 = dx * dx + dy * dy;
    const float r = m->radius;
    if (d2 >= r * r) return;
    float nx, ny, push;
    if (d2 > 1e-8f) {
        const float d = sqrtf(d2);
        nx = dx / d; ny = dy / d; push = r - d;
    } else {  /* centre inside the rectangle: leave through the nearest face */
        const float l = m->bx - rx0, rr = rx1 - m->bx, t = m->by - ry0, b = ry1 - m->by;
        float mn = l; nx = -1; ny = 0;
        if (rr < mn) { mn = rr; nx = 1; ny = 0; }
        if (t < mn) { mn = t; nx = 0; ny = -1; }
        if (b < mn) { mn = b; nx = 0; ny = 1; }
        push = mn + r;
    }
    m->bx += nx * push; m->by += ny * push;
    const float vn = m->vx * nx + m->vy * ny;
    if (vn < 0.0f) {
        m->vx -= (1.0f + RESTITUTION) * vn * nx;
        m->vy -= (1.0f + RESTITUTION) * vn * ny;
        const float strength = clampf(-vn / 220.0f, 0.0f, 1.0f);
        if (-vn > 45.0f && m->last_hit_s > 0.12f) {
            ev->hit = true;
            if (strength > ev->hit_strength) ev->hit_strength = strength;
            ++m->hits;
            m->last_hit_s = 0.0f;
        }
    }
}

static void collide_walls(exp_maze_t *m, maze_events_t *ev)
{
    const int n = m->n;
    const float c = (float)m->cell, h = WALL_T * 0.5f;
    int ci = (int)(m->bx / c), cj = (int)(m->by / c);
    ci = ci < 0 ? 0 : (ci >= n ? n - 1 : ci);
    cj = cj < 0 ? 0 : (cj >= n ? n - 1 : cj);
    for (int j = cj - 1; j <= cj + 1; ++j) {
        for (int i = ci - 1; i <= ci + 1; ++i) {
            if (i < 0 || j < 0 || i >= n || j >= n) continue;
            const uint8_t w = m->walls[j][i];
            const float x0 = (float)i * c, y0 = (float)j * c, x1 = x0 + c, y1 = y0 + c;
            if (w & MW_N) rect_collide(m, x0 - h, y0 - h, x1 + h, y0 + h, ev);
            if (w & MW_S) rect_collide(m, x0 - h, y1 - h, x1 + h, y1 + h, ev);
            if (w & MW_W) rect_collide(m, x0 - h, y0 - h, x0 + h, y1 + h, ev);
            if (w & MW_E) rect_collide(m, x1 - h, y0 - h, x1 + h, y1 + h, ev);
        }
    }
}

static void respawn(exp_maze_t *m)
{
    m->bx = m->cell * 0.5f; m->by = m->cell * 0.5f;
    m->vx = m->vy = 0.0f;
    m->freeze_s = 0.6f;
}

static void finish_success(exp_maze_t *m)
{
    const int32_t base = 300 * ((int)m->diff + 1);
    int32_t score = base + (int32_t)(m->time_left_s * 20.0f) - (int32_t)m->hits * 5 - (int32_t)m->falls * 100;
    if (score < 50) score = 50;
    m->score = score;
    m->new_best = false;
    if (!m->demo && score > m->best[m->diff]) { m->best[m->diff] = score; m->new_best = true; }
    m->phase = MZ_SUCCESS;
    m->phase_s = 0.0f;
}

maze_events_t exp_maze_step(exp_maze_t *m, float ax, float ay, float dt)
{
    maze_events_t ev = {0};
    m->phase_s += dt;
    m->last_hit_s += dt;
    switch (m->phase) {
    case MZ_COUNTDOWN: {
        const int digit = 3 - (int)(m->phase_s / COUNTDOWN_STEP_S);
        if (digit != m->countdown && digit >= 1) { m->countdown = (int8_t)digit; ev.countdown_tick = true; }
        if (m->phase_s >= 3.0f * COUNTDOWN_STEP_S) {
            m->phase = MZ_PLAYING;
            m->phase_s = 0.0f;
            m->countdown = 0;
            ev.go = true;
        }
        break;
    }
    case MZ_PLAYING: {
        m->time_left_s -= dt;
        if (m->time_left_s <= 0.0f) {
            m->time_left_s = 0.0f;
            m->phase = MZ_TIMEUP;
            m->phase_s = 0.0f;
            ev.timeup = true;
            break;
        }
        if (m->freeze_s > 0.0f) { m->freeze_s -= dt; break; }
        ax = clampf(ax, -1.0f, 1.0f); ay = clampf(ay, -1.0f, 1.0f);
        const float h = dt / (float)SUBSTEPS;
        for (int s = 0; s < SUBSTEPS; ++s) {
            m->vx += ax * ACCEL_PX_PER_G * h;
            m->vy += ay * ACCEL_PX_PER_G * h;
            const float damp = 1.0f - DAMPING * h;
            m->vx *= damp; m->vy *= damp;
            const float sp = sqrtf(m->vx * m->vx + m->vy * m->vy);
            if (sp > SPEED_CAP) { m->vx *= SPEED_CAP / sp; m->vy *= SPEED_CAP / sp; }
            m->bx += m->vx * h; m->by += m->vy * h;
            collide_walls(m, &ev);
        }
        const float c = (float)m->cell;
        for (int i = 0; i < m->hole_count; ++i) {
            const float hx = ((float)m->hole_x[i] + 0.5f) * c, hy = ((float)m->hole_y[i] + 0.5f) * c;
            const float dx = m->bx - hx, dy = m->by - hy;
            if (dx * dx + dy * dy < (0.27f * c) * (0.27f * c)) {
                ev.fell = true;
                ++m->falls;
                m->time_left_s -= FALL_PENALTY_S;
                respawn(m);
                break;
            }
        }
        const float gx = ((float)m->n - 0.5f) * c, gy = gx;
        const float dgx = m->bx - gx, dgy = m->by - gy;
        if (dgx * dgx + dgy * dgy < (0.33f * c) * (0.33f * c)) {
            ev.goal = true;
            finish_success(m);
        }
        break;
    }
    default: break;
    }
    return ev;
}

void exp_maze_bot(exp_maze_t *m, float *ax, float *ay)
{
    *ax = *ay = 0.0f;
    if (m->phase != MZ_PLAYING || m->freeze_s > 0.0f) return;
    const float c = (float)m->cell;
    int ci = (int)(m->bx / c), cj = (int)(m->by / c);
    ci = ci < 0 ? 0 : (ci >= m->n ? m->n - 1 : ci);
    cj = cj < 0 ? 0 : (cj >= m->n ? m->n - 1 : cj);
    if (m->path_from_x != ci || m->path_from_y != cj) exp_maze_solve(m, ci, cj, true);
    float tx = ((float)m->n - 0.5f) * c, ty = tx;
    if (m->path_len >= 2) {
        /* Aim at the next cell centre; look two cells ahead on straight runs. */
        int k = 1;
        if (m->path_len >= 3 &&
            (m->path_x[2] - m->path_x[0] == 2 * (m->path_x[1] - m->path_x[0])) &&
            (m->path_y[2] - m->path_y[0] == 2 * (m->path_y[1] - m->path_y[0]))) k = 2;
        tx = ((float)m->path_x[k] + 0.5f) * c;
        ty = ((float)m->path_y[k] + 0.5f) * c;
        /* Slide along the corridor axis before turning: align the other axis first. */
        const float cxm = ((float)m->path_x[0] + 0.5f) * c, cym = ((float)m->path_y[0] + 0.5f) * c;
        if (m->path_x[1] != m->path_x[0]) ty = cym; else tx = cxm;
        if (k == 2) { if (m->path_x[1] != m->path_x[0]) ty = cym; else tx = cxm; }
    }
    const float ex = tx - m->bx, ey = ty - m->by;
    const float dist = sqrtf(ex * ex + ey * ey) + 1e-3f;
    const float want = fminf(150.0f, 2.2f * dist + 20.0f);
    const float vdx = ex / dist * want, vdy = ey / dist * want;
    *ax = clampf((vdx - m->vx) * 3.2f / ACCEL_PX_PER_G, -0.55f, 0.55f);
    *ay = clampf((vdy - m->vy) * 3.2f / ACCEL_PX_PER_G, -0.55f, 0.55f);
}
