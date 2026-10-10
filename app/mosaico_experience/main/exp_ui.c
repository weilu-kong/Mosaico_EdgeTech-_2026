// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>
#include "exp_internal.h"

int ui_hit(const exp_btn_t *btns, int n, int x, int y)
{
    for (int i = n - 1; i >= 0; --i) {
        const exp_btn_t *b = &btns[i];
        if (x >= b->r.x && y >= b->r.y && x < b->r.x + b->r.w && y < b->r.y + b->r.h) return i;
    }
    return -1;
}

void ui_draw_button(const exp_btn_t *b, const char *label, bool pressed, exp_icon_t icon, bool has_icon)
{
    const exp_rect_t r = b->r;
    Color fill, border, text;
    switch (b->style) {
    case UI_BTN_PRIMARY:
        fill = b->enabled ? (pressed ? EXP_RGB(40, 150, 170) : C_CYAN) : C_PANEL_HI;
        border = fill;
        text = b->enabled ? C_BG0 : C_TEXT3;
        break;
    case UI_BTN_SEGMENT:
    case UI_BTN_CHIP:
        if (b->selected && b->enabled) {
            fill = pressed ? EXP_RGB(30, 90, 108) : EXP_RGB(18, 72, 90);
            border = C_CYAN;
            text = C_TEXT;
        } else {
            fill = pressed ? EXP_RGB(30, 50, 84) : C_PANEL;
            border = C_BORDER;
            text = b->enabled ? C_TEXT2 : C_TEXT3;
        }
        break;
    case UI_BTN_ROW:
        fill = pressed ? EXP_RGB(26, 44, 76) : C_PANEL;
        border = pressed ? C_CYAN_DIM : C_BORDER;
        text = C_TEXT;
        break;
    case UI_BTN_SECONDARY:
    default:
        fill = pressed ? EXP_RGB(30, 54, 90) : C_PANEL_HI;
        border = pressed ? C_CYAN : C_BORDER;
        text = b->enabled ? C_TEXT : C_TEXT3;
        break;
    }
    if (b->style == UI_BTN_ROW) {
        exp_panel(r.x, r.y, r.w, r.h, 14, fill, border);
        return;
    }
    const int inset = pressed ? 1 : 0;
    exp_panel(r.x + inset, r.y + inset, r.w - 2 * inset, r.h - 2 * inset, EXP_RADIUS, fill, border);
    if (!label) return;
    const exp_font_id_t font = b->style == UI_BTN_PRIMARY ? EXP_FONT_TITLE : EXP_FONT_BODY;
    if (has_icon) {
        const int tw = exp_text_width(label, font);
        const int total = tw + 30;
        const int x0 = r.x + (r.w - total) / 2;
        exp_icon(icon, (float)x0 + 10.0f, (float)(r.y + r.h / 2), 20.0f, text);
        exp_rect_t tb = {x0 + 28, r.y, tw + 2, r.h};
        exp_text_in(tb, label, font, text, EXP_ALIGN_LEFT);
    } else {
        exp_text_in(r, label, font, text, EXP_ALIGN_CENTER);
    }
}

void ui_chip(int x, int y, const char *text, Color dot, Color text_color, int *out_w)
{
    const int tw = exp_text_width(text, EXP_FONT_CAP);
    const int w = 12 + 8 + 8 + tw + 12;
    exp_panel(x, y, w, 26, 13, C_PANEL_HI, C_BORDER);
    exp_disc((float)(x + 14), (float)(y + 13), 4.0f, dot);
    exp_rect_t tb = {x + 26, y, tw + 2, 26};
    exp_text_in(tb, text, EXP_FONT_CAP, text_color, EXP_ALIGN_LEFT);
    if (out_w) *out_w = w;
}

void ui_tile(int x, int y, int w, int h, const char *label)
{
    exp_panel(x, y, w, h, 14, C_PANEL, C_BORDER);
    if (label) exp_text(label, x + 12, y + 8, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
}

void ui_bar(int x, int y, int w, int h, float frac, Color fill)
{
    exp_rrect(x, y, w, h, h / 2, EXP_RGB(10, 18, 34));
    frac = exp_clampf(frac, 0.0f, 1.0f);
    const int fw = (int)((float)w * frac);
    if (fw >= h) exp_rrect(x, y, fw, h, h / 2, fill);
    else if (fw > 0) exp_rrect(x, y, h, h, h / 2, exp_with_alpha(fill, (float)fw / (float)h));
}

void ui_battery(const exp_app_t *app, int right_x, int cy)
{
    if (!(app->s.flags & EXP_SENS_BATT)) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)app->s.battery_pct);
    const int tw = exp_text_width(buf, EXP_FONT_CAP);
    const Color c = app->s.charging ? C_OK : (app->s.battery_pct < 20 ? C_ERR : C_TEXT2);
    exp_text(buf, right_x, cy - 8, EXP_FONT_CAP, c, EXP_ALIGN_RIGHT);
    const float ix = (float)(right_x - tw - 18);
    exp_icon(EXP_ICON_BATTERY, ix, (float)cy, 20.0f, c);
    const float frac = (float)app->s.battery_pct / 100.0f;
    exp_rect((int)ix - 9, cy - 4, (int)(16.0f * frac), 8, app->s.charging ? C_OK : c);
    exp_layout_keepout((exp_rect_t){(int)ix - 11, cy - 7, 24, 14});
}
