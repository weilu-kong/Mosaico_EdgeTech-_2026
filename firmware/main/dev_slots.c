// SPDX-License-Identifier: Apache-2.0
#include "dev_slots.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "mosaico_module_mgr.h"

static const char *TAG = "dev_slots";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_slots_t s_s = {
#if EXP_USE_SLOTS
    .state = "starting",
#else
    .state = "off",
#endif
};
static bool s_running;

#if EXP_USE_SLOTS
void dev_slots_open(void)
{
    const esp_err_t e = mosaico_module_mgr_init(NULL);      /* the manager's default scan configuration */
    portENTER_CRITICAL(&s_lock);
    s_s.state = e == ESP_OK ? "ok" : "init failed";
    portEXIT_CRITICAL(&s_lock);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "module manager init failed: %s - slots stay 'not probed'", esp_err_to_name(e));
        return;
    }
    s_running = true;
    ESP_LOGI(TAG, "module manager running (EEPROM descriptor scan only; no module is claimed or driven)");
}

void dev_slots_poll(void)
{
    if (!s_running) return;
    dev_slots_t n;
    memset(&n, 0, sizeof(n));
    for (int i = 0; i < 2; ++i) {
        mosaico_module_mgr_info_t info;
        memset(&info, 0, sizeof(info));
        const esp_err_t e = mosaico_module_mgr_get_info(i == 0 ? MOSAICO_MODULE_MGR_SLOT_LEFT : MOSAICO_MODULE_MGR_SLOT_RIGHT, &info);
        if (e != ESP_OK) {
            n.last_error[i] = (int)e;
            n.cls[i] = DEV_SLOT_NOT_PROBED;
            continue;
        }
        n.presence[i] = (uint8_t)info.presence;
        n.descriptor_valid[i] = info.descriptor_state == MOSAICO_MODULE_DESCRIPTOR_VALID;
        n.board_type[i] = n.descriptor_valid[i] ? info.eeprom.board_type : 0;
        n.last_error[i] = (int)info.last_error;
        n.cls[i] = dev_slot_classify((int)info.presence, n.descriptor_valid[i], n.board_type[i]);
    }
    portENTER_CRITICAL(&s_lock);
    const dev_slots_t prev = s_s;
    n.state = s_s.state;
    n.polls = s_s.polls + 1;
    s_s = n;
    portEXIT_CRITICAL(&s_lock);
    for (int i = 0; i < 2; ++i)
        if (prev.cls[i] != n.cls[i] || prev.board_type[i] != n.board_type[i])
            ESP_LOGI(TAG, "slot %s: class %d, presence %u, descriptor %s, board type 0x%02X%s", i == 0 ? "left" : "right",
                     (int)n.cls[i], (unsigned)n.presence[i], n.descriptor_valid[i] ? "valid" : "not valid",
                     (unsigned)n.board_type[i], n.cls[i] == DEV_SLOT_UNKNOWN ? "  (UNREVIEWED MODULE: RF cannot be vouched for)" : "");
}
#else
void dev_slots_open(void) {}
void dev_slots_poll(void) {}
#endif

void dev_slots_mark_skipped(void)
{
    portENTER_CRITICAL(&s_lock);
    s_s.state = "skipped (crash guard)";
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "slot probing skipped: it was running when the previous boot ended abnormally (power-cycle to retry)");
}

void dev_slots_take(dev_slots_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_s;
    portEXIT_CRITICAL(&s_lock);
}
