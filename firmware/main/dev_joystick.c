// SPDX-License-Identifier: Apache-2.0
#include "dev_joystick.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#if EXP_USE_JOYSTICK
#include "mosaico_module_joystick.h"
#endif

static const char *TAG = "dev_joy";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_joystick_t s_j = {
#if EXP_USE_JOYSTICK
    .state = "no module",
#else
    .state = "off",
#endif
    .slot = -1,
};
static int64_t s_last_ok_us = -1;

static void publish(const dev_joystick_t *n)
{
    portENTER_CRITICAL(&s_lock);
    s_j = *n;
    portEXIT_CRITICAL(&s_lock);
}

#if EXP_USE_JOYSTICK
#define RETRY_US 2000000
#define MAX_CONSECUTIVE_ERRORS 50

static mosaico_joystick_handle_t s_h;
static int64_t s_next_try_us;
static int s_consecutive_errors;

static void drop(dev_joystick_t *n, const char *why)
{
    if (s_h) {
        (void)mosaico_joystick_del(s_h);
        s_h = NULL;
    }
    n->state = why;
    n->ready = n->calibrating = false;
    n->x = n->y = 0.0f;
    n->buttons = 0;
    n->slot = -1;
    s_consecutive_errors = 0;
    s_last_ok_us = -1;
    ESP_LOGW(TAG, "joystick released: %s", why);
}
#endif

void dev_joystick_poll(const dev_slots_t *slots, dev_guard_t *guard)
{
#if EXP_USE_JOYSTICK
    dev_joystick_t n;
    dev_joystick_take(&n);
    const int64_t now = esp_timer_get_time();

    if (!dev_guard_allowed(guard, DEV_STEP_JOYSTICK)) {
        if (strcmp(n.state, "skipped (crash guard)") != 0) {
            n.state = "skipped (crash guard)";
            ESP_LOGW(TAG, "joystick skipped: it was running when the previous boot ended abnormally (power-cycle to retry)");
            publish(&n);
        }
        return;
    }

    if (!s_h) {
        int slot = -1;
        for (int i = 0; i < 2; ++i)
            if (slots->cls[i] == DEV_SLOT_JOYSTICK) { slot = i; break; }
        if (slot < 0) {
            if (strcmp(n.state, "no module") != 0) { n.state = "no module"; n.slot = -1; n.ready = n.calibrating = false; publish(&n); }
            return;
        }
        if (now < s_next_try_us) return;
        mosaico_joystick_config_t cfg = MOSAICO_JOYSTICK_DEFAULT_CONFIG();
        cfg.slot = slot == 0 ? MOSAICO_MODULE_MGR_SLOT_LEFT : MOSAICO_MODULE_MGR_SLOT_RIGHT;
        cfg.discovery_timeout_ms = 500;
        n.state = "claiming";
        publish(&n);
        dev_guard_begin(guard, DEV_STEP_JOYSTICK);
        const esp_err_t e = mosaico_joystick_new(&cfg, &s_h);
        dev_guard_end(guard, DEV_STEP_JOYSTICK);
        ++n.claims;
        if (e != ESP_OK) {
            s_h = NULL;
            ++n.claim_errors;
            n.last_error = (int)e;
            n.state = "claim failed";
            s_next_try_us = now + RETRY_US;
            ESP_LOGW(TAG, "joystick claim failed in the %s slot: %s", slot == 0 ? "left" : "right", esp_err_to_name(e));
            publish(&n);
            return;
        }
        n.slot = (int8_t)slot;
        n.calibrating = true;
        n.state = "calibrating: circle";
        ESP_LOGI(TAG, "joystick module claimed in the %s slot: circle the stick, then leave it centred", slot == 0 ? "left" : "right");
        publish(&n);
        return;
    }

    mosaico_joystick_data_t d;
    memset(&d, 0, sizeof(d));
    dev_guard_begin(guard, DEV_STEP_JOYSTICK);
    const esp_err_t e = mosaico_joystick_read(s_h, &d);
    dev_guard_end(guard, DEV_STEP_JOYSTICK);
    if (e == ESP_ERR_NOT_FOUND) {
        drop(&n, "removed");
        s_next_try_us = now + RETRY_US;
        publish(&n);
        return;
    }
    if (e != ESP_OK) {
        ++n.errors;
        n.last_error = (int)e;
        n.state = "read failed";
        n.ready = false;
        if (++s_consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
            drop(&n, "read failed");
            s_next_try_us = now + RETRY_US;
        }
        publish(&n);
        return;
    }
    s_consecutive_errors = 0;
    ++n.reads;
    n.raw_x = d.raw_x;
    n.raw_y = d.raw_y;
    n.drv_x = d.x;
    n.drv_y = d.y;
    if (d.state == MOSAICO_JOYSTICK_READY) {
        dev_joy_map(d.x, d.y, &n.x, &n.y);
        n.buttons = dev_joy_buttons(d.buttons, MOSAICO_JOYSTICK_BUTTON_COUNT);
        n.calibrating = false;
        n.ready = true;
        n.state = "ready";
        s_last_ok_us = now;
    } else {
        n.calibrating = true;
        n.ready = false;
        n.x = n.y = 0.0f;
        n.buttons = 0;
        n.state = d.state == MOSAICO_JOYSTICK_CAL_CIRCLE ? "calibrating: circle" : "calibrating: centre";
    }
    publish(&n);
#else
    (void)slots;
    (void)guard;
#endif
}

void dev_joystick_take(dev_joystick_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_j;
    const int64_t last = s_last_ok_us;
    portEXIT_CRITICAL(&s_lock);
    if (out->ready && (last < 0 || (esp_timer_get_time() - last) > (int64_t)DEV_JOY_FRESH_MS * 1000))
        out->ready = false;           /* a stale sample is not a value */
}
