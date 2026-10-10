// SPDX-License-Identifier: Apache-2.0
// Host test for the portable screen-log ring (firmware/components/exp_dbglog/dbglog_ring.c).
#include <stdio.h>
#include <string.h>

#include "../../../firmware/components/exp_dbglog/dbglog_ring.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void expect(const dbglog_ring_t *r, size_t back, const char *want)
{
    char b[DBGLOG_COLS + 8];
    const bool ok = dbglog_ring_get(r, back, b, sizeof(b));
    if (!ok || strcmp(b, want) != 0) { printf("FAIL back=%zu got '%s' ok=%d want '%s'\n", back, b, ok, want); ++fails; }
}

int main(void)
{
    static dbglog_ring_t r;
    char b[16];

    dbglog_ring_init(&r);
    CHECK(dbglog_ring_count(&r) == 0);
    CHECK(!dbglog_ring_get(&r, 0, b, sizeof(b)));

    dbglog_ring_write(&r, "one\ntwo\n", 8);
    CHECK(dbglog_ring_count(&r) == 2);
    expect(&r, 0, "two");
    expect(&r, 1, "one");
    CHECK(!dbglog_ring_get(&r, 2, b, sizeof(b)));

    /* a line split over several writes; CRLF; empty line */
    dbglog_ring_write(&r, "par", 3);
    dbglog_ring_write(&r, "tial\r", 5);
    CHECK(dbglog_ring_count(&r) == 2);          /* not complete yet */
    dbglog_ring_write(&r, "\n\n", 2);
    CHECK(dbglog_ring_count(&r) == 4);
    expect(&r, 0, "");
    expect(&r, 1, "partial");

    /* ESP-IDF colour codes are removed */
    const char *col = "\033[0;32mI (12) tag: hello\033[0m\n";
    dbglog_ring_write(&r, col, strlen(col));
    expect(&r, 0, "I (12) tag: hello");
    const char *lone = "a\033Zb\n";            /* ESC not followed by '[' drops both */
    dbglog_ring_write(&r, lone, strlen(lone));
    expect(&r, 0, "ab");

    /* tab, non-ASCII (UTF-8 bytes) and control characters */
    const char *odd = "x\ty\xE3\x81\x82z\x01\n";
    dbglog_ring_write(&r, odd, strlen(odd));
    expect(&r, 0, "x y???z?");

    /* over-long line is cut at DBGLOG_COLS-1 characters, the rest is dropped, next line is intact */
    char longl[200];
    memset(longl, 'L', sizeof(longl));
    longl[199] = '\n';
    dbglog_ring_write(&r, longl, sizeof(longl));
    dbglog_ring_write(&r, "next\n", 5);
    expect(&r, 0, "next");
    char b2[DBGLOG_COLS + 8];
    CHECK(dbglog_ring_get(&r, 1, b2, sizeof(b2)));
    CHECK(strlen(b2) == DBGLOG_COLS - 1);

    /* output buffer smaller than the line: truncated, always terminated */
    dbglog_ring_write(&r, "0123456789\n", 11);
    CHECK(dbglog_ring_get(&r, 0, b, 5));
    CHECK(strcmp(b, "0123") == 0);

    /* wrap-around: only the newest DBGLOG_LINES lines remain, in order */
    dbglog_ring_init(&r);
    for (int i = 0; i < DBGLOG_LINES * 3 + 5; ++i) {
        char l[24];
        int n = snprintf(l, sizeof(l), "line %d\n", i);
        dbglog_ring_write(&r, l, (size_t)n);
    }
    CHECK(dbglog_ring_count(&r) == DBGLOG_LINES);
    for (int back = 0; back < DBGLOG_LINES; ++back) {
        char want[24];
        snprintf(want, sizeof(want), "line %d", DBGLOG_LINES * 3 + 4 - back);
        expect(&r, (size_t)back, want);
    }
    CHECK(!dbglog_ring_get(&r, DBGLOG_LINES, b2, sizeof(b2)));

    /* zero-sized output is refused without touching memory */
    CHECK(!dbglog_ring_get(&r, 0, NULL, 0));

    if (fails) { printf("dbglog_ring: %d FAILED\n", fails); return 1; }
    printf("dbglog_ring: all checks passed\n");
    return 0;
}
