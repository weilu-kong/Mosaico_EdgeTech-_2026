// SPDX-License-Identifier: Apache-2.0
#include "dbglog_ring.h"

#include <string.h>

void dbglog_ring_init(dbglog_ring_t *r) { memset(r, 0, sizeof(*r)); }

static void commit_line(dbglog_ring_t *r)
{
    r->cur[r->cur_len] = '\0';
    memcpy(r->line[r->head], r->cur, (size_t)r->cur_len + 1U);
    r->head = (uint16_t)((r->head + 1U) % DBGLOG_LINES);
    if (r->filled < DBGLOG_LINES) ++r->filled;
    r->cur_len = 0;
}

void dbglog_ring_write(dbglog_ring_t *r, const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (r->esc == 1) { r->esc = (c == '[') ? 2 : 0; continue; }
        if (r->esc == 2) { if (c >= 0x40 && c <= 0x7e) r->esc = 0; continue; }
        if (c == 0x1b) { r->esc = 1; continue; }
        if (c == '\r') continue;
        if (c == '\n') { commit_line(r); continue; }
        if (c == '\t') c = ' ';
        if (c < 0x20 || c > 0x7e) c = '?';
        if (r->cur_len < DBGLOG_COLS - 1) r->cur[r->cur_len++] = (char)c;
    }
}

size_t dbglog_ring_count(const dbglog_ring_t *r) { return r->filled; }

bool dbglog_ring_get(const dbglog_ring_t *r, size_t back, char *out, size_t cap)
{
    if (!cap) return false;
    out[0] = '\0';
    if (back >= r->filled) return false;
    const size_t idx = ((size_t)r->head + DBGLOG_LINES - 1U - back) % DBGLOG_LINES;
    strncpy(out, r->line[idx], cap - 1U);
    out[cap - 1U] = '\0';
    return true;
}
