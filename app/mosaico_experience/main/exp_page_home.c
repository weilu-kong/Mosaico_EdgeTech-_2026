// SPDX-License-Identifier: Apache-2.0
// Home: product title, three experience entries (A/B/C), status and auto demo.
#include <stdio.h>
#include "exp_internal.h"

#define ROW_Y0 182
#define ROW_H 70
#define ROW_GAP 6

int page_home_layout(const exp_app_t *app, exp_btn_t *out)
{
    (void)app;
    int n = 0;
    out[n++] = (exp_btn_t){BTN_AUTO_DEMO, {EXP_W - EXP_MARGIN - 160, 130, 160, 44}, true, false, UI_BTN_SECONDARY};
    static const int ids[3] = {BTN_HOME_A, BTN_HOME_B, BTN_HOME_C};
    for (int i = 0; i < 3; ++i) {
        out[n++] = (exp_btn_t){ids[i], {EXP_MARGIN, ROW_Y0 + i * (ROW_H + ROW_GAP), EXP_W - 2 * EXP_MARGIN, ROW_H},
                               true, false, UI_BTN_ROW};
    }
    return n;
}

void page_home_button(exp_app_t *app, int id)
{
    switch (id) {
    case BTN_AUTO_DEMO:
        app->attract = true;
        app->attract_step = -1;
        app->attract_step_ms = 0;
        app_sound(app, EXP_SND_NAV);
        break;
    case BTN_HOME_A: app_goto(app, EXP_PAGE_MOTION, true); break;
    case BTN_HOME_B: app_goto(app, EXP_PAGE_AUDIO, true); break;
    case BTN_HOME_C: app_goto(app, EXP_PAGE_GAME, true); break;
    default: break;
    }
}

static void status_chip_right(int right_x, int y, bool ok, const char *ok_text, const char *bad_text)
{
    const char *t = ok ? ok_text : bad_text;
    const int tw = exp_text_width(t, EXP_FONT_CAP);
    const Color c = ok ? C_OK : C_TEXT3;
    exp_text(t, right_x, y, EXP_FONT_CAP, ok ? C_OK : C_TEXT2, EXP_ALIGN_RIGHT);
    exp_disc((float)(right_x - tw - 10), (float)(y + 8), 4.0f, c);
}

void page_home_render(exp_app_t *app)
{
    /* Title block. */
    exp_text(H_APP_NAME, EXP_MARGIN, 58, EXP_FONT_HEAD, C_TEXT, EXP_ALIGN_LEFT);
    exp_text(T_APP_SUB, EXP_MARGIN, 104, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);

    /* Mode chip: interactive vs. auto demo. */
    const float pulse = 0.5f + 0.5f * ((float)(app->now_ms % 2000U) / 1000.0f > 1.0f ?
                        2.0f - (float)(app->now_ms % 2000U) / 1000.0f : (float)(app->now_ms % 2000U) / 1000.0f);
    int w = 0;
    ui_chip(EXP_MARGIN, 139, T_MODE_INTERACTIVE, exp_lerp(C_CYAN_DIM, C_OK, pulse), C_TEXT, &w);

    exp_btn_t btns[EXP_MAX_BTN];
    const int n = page_home_layout(app, btns);
    for (int i = 0; i < n; ++i) {
        if (btns[i].id == BTN_AUTO_DEMO) {
            ui_draw_button(&btns[i], T_AUTO_DEMO, app->ptr_down && app->press_id == BTN_AUTO_DEMO, EXP_ICON_PLAY, true);
        }
    }

    /* Experience rows. */
    static const char *const titles[3] = {T_HOME_A_TITLE, T_HOME_B_TITLE, T_HOME_C_TITLE};
    static const char *const subs[3] = {"A  " T_HOME_A_SUB, "B  " T_HOME_B_SUB, "C  " T_HOME_C_SUB};
    static const exp_icon_t icons[3] = {EXP_ICON_MOTION, EXP_ICON_AUDIO, EXP_ICON_GAME};
    const bool ok[3] = {app->imu_ok, app->mic_ok, true};
    for (int i = 0; i < 3; ++i) {
        const exp_btn_t *b = &btns[1 + i];
        const bool pressed = app->ptr_down && app->press_id == b->id;
        ui_draw_button(b, NULL, pressed, EXP_ICON_HOME, false);
        const int x = b->r.x, y = b->r.y;
        exp_rrect(x + 10, y + 11, 48, 48, 12, pressed ? EXP_RGB(30, 70, 96) : C_PANEL_HI);
        exp_icon(icons[i], (float)(x + 34), (float)(y + 35), 28.0f, C_CYAN);
        exp_layout_keepout((exp_rect_t){x + 10, y + 11, 48, 48});
        exp_text(titles[i], x + 72, y + 8, EXP_FONT_TITLE, C_TEXT, EXP_ALIGN_LEFT);
        exp_text(subs[i], x + 72, y + 42, EXP_FONT_CAP, C_TEXT2, EXP_ALIGN_LEFT);
        status_chip_right(x + b->r.w - 40, y + 12, ok[i], T_STATUS_OK, T_STATUS_NA);
        exp_icon(EXP_ICON_CHEVRON_RIGHT, (float)(x + b->r.w - 22), (float)(y + 35), 18.0f, C_TEXT3);
    }
    (void)w;
}
