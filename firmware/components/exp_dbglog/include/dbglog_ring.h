// SPDX-License-Identifier: Apache-2.0
// Portable text ring buffer for log lines (no ESP-IDF dependency, so it is tested on the host).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DBGLOG_LINES 64   /* completed lines kept                              */
#define DBGLOG_COLS 128   /* characters per line including the terminating NUL */

typedef struct {
    char line[DBGLOG_LINES][DBGLOG_COLS];
    char cur[DBGLOG_COLS];   /* line being assembled                              */
    uint8_t cur_len;
    uint8_t esc;             /* 0 = text, 1 = after ESC, 2 = inside ESC [ ...      */
    uint16_t head;           /* next slot to write                                */
    uint16_t filled;         /* completed lines stored (<= DBGLOG_LINES)          */
} dbglog_ring_t;

void dbglog_ring_init(dbglog_ring_t *r);

/* Appends text. '\n' completes a line; '\r' is dropped; ANSI colour sequences are removed; tabs become spaces;
 * anything outside printable ASCII becomes '?'; characters beyond DBGLOG_COLS-1 in one line are dropped. */
void dbglog_ring_write(dbglog_ring_t *r, const char *s, size_t n);

size_t dbglog_ring_count(const dbglog_ring_t *r);

/* back = 0 is the newest completed line. Copies at most cap-1 characters. Returns false if there is no such line. */
bool dbglog_ring_get(const dbglog_ring_t *r, size_t back, char *out, size_t cap);
