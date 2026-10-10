// SPDX-License-Identifier: Apache-2.0
#include "exp_text.h"

#include <string.h>
#include "exp_config.h"

/* ---- Global draw state ---------------------------------------------------- */
static int s_ox, s_oy;
static uint8_t s_alpha = 255;

void exp_gfx_set_origin(int ox, int oy) { s_ox = ox; s_oy = oy; }
void exp_gfx_get_origin(int *ox, int *oy) { if (ox) *ox = s_ox; if (oy) *oy = s_oy; }
void exp_gfx_set_alpha(uint8_t alpha) { s_alpha = alpha; }

Color exp_gfx_mod(Color c)
{
    if (s_alpha != 255) c.a = (uint8_t)(((unsigned)c.a * s_alpha + 127U) / 255U);
    return c;
}

/* ---- UTF-8 ------------------------------------------------------------------ */
static uint32_t next_cp(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp;
    if (s[0] < 0x80) { cp = s[0]; *p += 1; }
    else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); *p += 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *p += 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *p += 4;
    } else { cp = 0xFFFD; *p += 1; }
    return cp;
}

/* ---- Metrics ---------------------------------------------------------------- */
int exp_text_ascent(exp_font_id_t font)
{
    return (exp_fonts[font].pixel_size * 84 + 50) / 100;  /* baseline below ink top */
}

int exp_text_line_height(exp_font_id_t font)
{
    return (exp_fonts[font].pixel_size * 14 + 5) / 10;    /* 1.4 em line pitch */
}

static const exp_glyph_t *find_glyph(exp_font_id_t font, uint32_t cp)
{
    const exp_font_t *f = &exp_fonts[font];
    int lo = 0, hi = (int)f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static uint32_t s_missing_total;
static int s_missing_frame;
uint32_t exp_text_missing_total(void) { return s_missing_total; }

static int glyph_advance(exp_font_id_t font, uint32_t cp)
{
    const exp_glyph_t *g = find_glyph(font, cp);
    if (g) return g->advance;
    return (exp_fonts[font].pixel_size * 8) / 10;  /* tofu advance */
}

int exp_text_width(const char *utf8, exp_font_id_t font)
{
    int w = 0;
    const char *p = utf8;
    while (*p) w += glyph_advance(font, next_cp(&p));
    return w;
}

/* ---- Layout recorder --------------------------------------------------------- */
/* Copies at most cap-1 bytes without splitting a UTF-8 sequence. */
static void copy_utf8(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) --n;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

#define MAX_REC 160
typedef struct { exp_rect_t r; char tag[20]; } rec_t;
static bool s_layout_on;
static rec_t s_text_rec[MAX_REC];
static int s_text_n;
static exp_rect_t s_keepout[48];
static int s_keepout_n;
static exp_layout_report_t s_report;

void exp_layout_enable(bool enable) { s_layout_on = enable; }

void exp_layout_begin_frame(void)
{
    s_text_n = 0;
    s_keepout_n = 0;
    memset(&s_report, 0, sizeof(s_report));
    s_missing_frame = 0;
}

void exp_layout_note_problem(const char *what)
{
    if (s_report.first_problem[0] == '\0' && what) {
        copy_utf8(s_report.first_problem, sizeof(s_report.first_problem), what);
    }
}

void exp_layout_keepout(exp_rect_t rect)
{
    if (!s_layout_on || s_keepout_n >= (int)(sizeof(s_keepout) / sizeof(s_keepout[0]))) return;
    s_keepout[s_keepout_n++] = rect;
}

void exp_layout_target(exp_rect_t rect)
{
    if (!s_layout_on) return;
    if (rect.w < EXP_MIN_HIT || rect.h < EXP_MIN_HIT) {
        s_report.small_targets++;
        exp_layout_note_problem("touch target below 44px");
    }
}

static bool rect_hit(exp_rect_t a, exp_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void record_text(exp_rect_t ink, const char *s)
{
    if (!s_layout_on || ink.w <= 0 || ink.h <= 0) return;
    if (s_text_n < MAX_REC) {
        rec_t *rec = &s_text_rec[s_text_n++];
        rec->r = ink;
        copy_utf8(rec->tag, sizeof(rec->tag), s);
    }
    s_report.texts++;
    if (ink.x < 6 || ink.y < 4 || ink.x + ink.w > EXP_W - 6 || ink.y + ink.h > EXP_H - 4) {
        s_report.off_screen++;
        exp_layout_note_problem(s);
    }
}

void exp_layout_finish(exp_layout_report_t *out)
{
    for (int i = 0; i < s_text_n; ++i) {
        for (int j = i + 1; j < s_text_n; ++j) {
            if (rect_hit(s_text_rec[i].r, s_text_rec[j].r)) {
                s_report.text_overlaps++;
                exp_layout_note_problem(s_text_rec[i].tag);
            }
        }
        for (int k = 0; k < s_keepout_n; ++k) {
            if (rect_hit(s_text_rec[i].r, s_keepout[k])) {
                s_report.shape_overlaps++;
                exp_layout_note_problem(s_text_rec[i].tag);
            }
        }
    }
    s_report.missing_glyphs = s_missing_frame;
    if (out) *out = s_report;
}

/* ---- Rendering ------------------------------------------------------------- */
typedef struct { int minx, miny, maxx, maxy; } ink_t;

static void ink_add(ink_t *ink, int x0, int y0, int x1, int y1)
{
    if (x0 < ink->minx) ink->minx = x0;
    if (y0 < ink->miny) ink->miny = y0;
    if (x1 > ink->maxx) ink->maxx = x1;
    if (y1 > ink->maxy) ink->maxy = y1;
}

static void draw_glyph(const exp_glyph_t *g, int pen_x, int base_y, Color c, ink_t *ink)
{
    if (g->w == 0 || g->h == 0) return;
    const uint8_t *bits = &exp_font_bitmap[g->offset];
    const int x0 = pen_x + g->bx;
    const int y0 = base_y + g->by;
    ink_add(ink, x0, y0, x0 + g->w, y0 + g->h);
    c = exp_gfx_mod(c);
    if (c.a == 0) return;
    const unsigned base_a = c.a;
    for (int y = 0; y < g->h; ++y) {
        for (int x = 0; x < g->w; ++x) {
            const unsigned i = (unsigned)y * g->w + (unsigned)x;
            const unsigned nib = (i & 1U) ? (bits[i >> 1] & 0x0FU) : (bits[i >> 1] >> 4);
            if (!nib) continue;
            Color px = c;
            px.a = (uint8_t)((nib * 17U * base_a + 127U) / 255U);
            DrawPixel(x0 + x, y0 + y, px);
        }
    }
}

static void draw_tofu(int x, int base_y, exp_font_id_t font, Color c, ink_t *ink)
{
    const int s = exp_fonts[font].pixel_size;
    const int w = (s * 6) / 10, h = (s * 8) / 10;
    DrawRectangleLines(x + 1, base_y - h, w, h, exp_gfx_mod(c));
    ink_add(ink, x + 1, base_y - h, x + 1 + w, base_y);
}

/* Draws bytes [s, s+len) with the pen starting at x; returns advance. */
static int draw_run(const char *s, size_t len, int x, int top_y, exp_font_id_t font,
                    Color color, ink_t *ink)
{
    const int base_y = top_y + exp_text_ascent(font);
    const char *p = s;
    const char *end = s + len;
    int pen = x;
    while (p < end) {
        const uint32_t cp = next_cp(&p);
        const exp_glyph_t *g = find_glyph(font, cp);
        if (g) {
            draw_glyph(g, pen, base_y, color, ink);
            pen += g->advance;
        } else {
            ++s_missing_total;
            ++s_missing_frame;
            draw_tofu(pen, base_y, font, color, ink);
            pen += glyph_advance(font, cp);
        }
    }
    return pen - x;
}

static void commit_ink(const ink_t *ink, const char *tag)
{
    if (ink->maxx <= ink->minx) return;
    exp_rect_t r = {ink->minx, ink->miny, ink->maxx - ink->minx, ink->maxy - ink->miny};
    record_text(r, tag);
}

int exp_text(const char *utf8, int x, int y, exp_font_id_t font, Color color, exp_align_t align)
{
    if (!utf8 || !*utf8) return 0;
    const int w = exp_text_width(utf8, font);
    int px = x;
    if (align == EXP_ALIGN_CENTER) px = x - w / 2;
    else if (align == EXP_ALIGN_RIGHT) px = x - w;
    ink_t ink = {1 << 20, 1 << 20, -(1 << 20), -(1 << 20)};
    draw_run(utf8, strlen(utf8), px + s_ox, y + s_oy, font, color, &ink);
    commit_ink(&ink, utf8);
    return w;
}

int exp_text_in(exp_rect_t box, const char *utf8, exp_font_id_t font, Color color,
                exp_align_t align)
{
    /* Centre the ink band (0.84 em above to 0.12 em below the baseline). */
    const int size = exp_fonts[font].pixel_size;
    const int top = box.y + box.h / 2 - (size * 48) / 100;
    int x = box.x + box.w / 2;
    if (align == EXP_ALIGN_LEFT) x = box.x;
    else if (align == EXP_ALIGN_RIGHT) x = box.x + box.w;
    return exp_text(utf8, x, top, font, color, align);
}

/* ---- Wrapped paragraphs (kinsoku) ---------------------------------------- */
typedef struct { uint16_t start, len; uint16_t w; uint32_t first_cp, last_cp; bool space; } unit_t;
#define MAX_UNITS 160

static bool no_start(uint32_t cp)
{
    switch (cp) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0x30FB: case 0xFF1A:
    case 0xFF1B: case 0xFF1F: case 0xFF01: case 0xFF09: case 0x300D: case 0x300F:
    case 0x3011: case 0x3009: case 0x300B: case 0x30FC: case 0x309B: case 0x309C:
    case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063:
    case 0x3083: case 0x3085: case 0x3087: case 0x308E: case 0x30A1: case 0x30A3:
    case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3: case 0x30E5:
    case 0x30E7: case 0x30EE: case 0x3005: case ')': case ']': case '}': case ',':
    case '.': case '!': case '?': case ':': case ';': case '%': case 0x2026: case 0xFF0F:
        return true;
    default: return false;
    }
}

static bool no_end(uint32_t cp)
{
    switch (cp) {
    case 0xFF08: case 0x300C: case 0x300E: case 0x3010: case 0x3008: case 0x300A:
    case '(': case '[': case '{':
        return true;
    default: return false;
    }
}

static bool ascii_word_cp(uint32_t cp) { return cp > 0x20 && cp < 0x7F; }

static int build_units(const char *s, int width, exp_font_id_t font, unit_t *u)
{
    int n = 0;
    const char *p = s;
    while (*p && n < MAX_UNITS) {
        const char *start = p;
        const uint32_t cp = next_cp(&p);
        unit_t cur = {(uint16_t)(start - s), 0, 0, cp, cp, cp == ' '};
        cur.w = (uint16_t)glyph_advance(font, cp);
        if (ascii_word_cp(cp)) {  /* keep ASCII words together when they fit */
            const char *q = p;
            int ww = cur.w;
            uint32_t last = cp;
            while (*q && ascii_word_cp((uint8_t)*q)) {
                const char *prev = q;
                uint32_t c2 = next_cp(&q);
                ww += glyph_advance(font, c2);
                last = c2;
                (void)prev;
            }
            if (ww <= width) { cur.w = (uint16_t)ww; cur.last_cp = last; p = q; }
        }
        cur.len = (uint16_t)(p - start);
        u[n++] = cur;
    }
    return n;
}

int exp_text_count_lines(int width, const char *utf8, exp_font_id_t font)
{
    if (!utf8 || !*utf8 || width <= 0) return 0;
    unit_t u[MAX_UNITS];
    const int n = build_units(utf8, width, font, u);
    int lines = 0, i = 0;
    while (i < n) {
        int w = 0, j = i;
        while (j < n && (w + u[j].w <= width || j == i)) { w += u[j].w; ++j; }
        if (j < n) {
            while (j > i + 1 && (no_start(u[j].first_cp) || no_end(u[j - 1].last_cp))) --j;
        }
        ++lines;
        i = j;
        while (i < n && u[i].space) ++i;
    }
    return lines;
}

int exp_text_box(exp_rect_t box, const char *utf8, exp_font_id_t font, Color color,
                 exp_align_t align, int max_lines)
{
    if (!utf8 || !*utf8 || box.w <= 0) return 0;
    unit_t u[MAX_UNITS];
    const int n = build_units(utf8, box.w, font, u);
    const int pitch = exp_text_line_height(font);
    int fit_lines = max_lines;
    if (box.h > 0) {
        const int by_height = (box.h + (pitch - exp_fonts[font].pixel_size)) / pitch;
        if (by_height < fit_lines) fit_lines = by_height;
    }
    if (fit_lines < 1) fit_lines = 1;

    int i = 0, line = 0;
    while (i < n && line < fit_lines) {
        int w = 0, j = i;
        while (j < n && (w + u[j].w <= box.w || j == i)) { w += u[j].w; ++j; }
        if (j < n) {
            while (j > i + 1 && (no_start(u[j].first_cp) || no_end(u[j - 1].last_cp))) --j;
        }
        int end_unit = j;
        bool ellipsis = false;
        if (end_unit < n && line == fit_lines - 1) {  /* last allowed line, text remains */
            ellipsis = true;
            const int dots = glyph_advance(font, 0x2026);
            int ww = 0;
            for (int k = i; k < end_unit; ++k) ww += u[k].w;
            while (end_unit > i + 1 && ww + dots > box.w) { --end_unit; ww -= u[end_unit].w; }
            s_report.truncated++;
            exp_layout_note_problem("text truncated");
        }
        while (end_unit > i && u[end_unit - 1].space) --end_unit;
        int line_w = 0;
        for (int k = i; k < end_unit; ++k) line_w += u[k].w;
        if (ellipsis) line_w += glyph_advance(font, 0x2026);
        int px = box.x;
        if (align == EXP_ALIGN_CENTER) px = box.x + (box.w - line_w) / 2;
        else if (align == EXP_ALIGN_RIGHT) px = box.x + box.w - line_w;
        ink_t ink = {1 << 20, 1 << 20, -(1 << 20), -(1 << 20)};
        const int top = box.y + line * pitch + s_oy;
        int pen = px + s_ox;
        for (int k = i; k < end_unit; ++k) {
            pen += draw_run(utf8 + u[k].start, u[k].len, pen, top, font, color, &ink);
        }
        if (ellipsis) {
            static const char dots[] = "\xE2\x80\xA6";
            draw_run(dots, sizeof(dots) - 1, pen, top, font, color, &ink);
        }
        commit_ink(&ink, utf8 + u[i].start);
        ++line;
        i = j;
        while (i < n && u[i].space) ++i;
    }
    return line;
}
