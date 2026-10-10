// SPDX-License-Identifier: Apache-2.0
#include "dev_watchdog.h"

#include "esp_log.h"

#if EXP_USE_WDT
#include "esp_task_wdt.h"
#include "soc/soc_caps.h"

static const char *TAG = "dev_wdt";

void dev_wdt_start(void)
{
    const esp_task_wdt_config_t cfg = {
        .timeout_ms = EXP_WDT_TIMEOUT_MS,
        .idle_core_mask = (1U << SOC_CPU_CORES_NUM) - 1U,   /* as the IDF default: both idle tasks are watched */
        .trigger_panic = true,                              /* panic -> restart, instead of only printing */
    };
    esp_err_t e = esp_task_wdt_reconfigure(&cfg);
    if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_init(&cfg);   /* the IDF did not start it */
    if (e != ESP_OK) ESP_LOGW(TAG, "task watchdog could not be configured (%d): the unit will not restart by itself", (int)e);
    else ESP_LOGI(TAG, "task watchdog: %d ms, restart on expiry", EXP_WDT_TIMEOUT_MS);
}

void dev_wdt_subscribe(void)
{
    const esp_err_t e = esp_task_wdt_add(NULL);
    if (e != ESP_OK) ESP_LOGW(TAG, "task not subscribed to the watchdog (%d)", (int)e);
}

void dev_wdt_feed(void) { (void)esp_task_wdt_reset(); }
#else
void dev_wdt_start(void) {}
void dev_wdt_subscribe(void) {}
void dev_wdt_feed(void) {}
#endif
