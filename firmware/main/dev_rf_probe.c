// SPDX-License-Identifier: Apache-2.0
// Looks for radio entry points in the running firmware image WITHOUT calling any of them.
//
// How: every name below is declared as a weak undefined object. A weak undefined symbol does not make the linker pull
// anything in; its address is NULL unless some other part of the image defines a symbol of that name. So a non-NULL
// address means "this entry point is linked into the image I am running". Nothing is ever called or read through it.
//
// scripts/rf_check.py exempts exactly this file and only these two line shapes (the declaration and the table row);
// any other radio-related text here is still a finding. If you add a name, add it in both places.
#include "dev_rf_probe.h"

#include <stddef.h>

extern const char esp_wifi_init __attribute__((weak));
extern const char esp_wifi_start __attribute__((weak));
extern const char esp_now_init __attribute__((weak));
extern const char esp_bt_controller_init __attribute__((weak));
extern const char esp_bt_controller_enable __attribute__((weak));
extern const char esp_ieee802154_enable __attribute__((weak));
extern const char esp_ieee802154_receive __attribute__((weak));

typedef struct {
    const char *name;
    const void *addr;
} probe_entry_t;

dev_rf_probe_t dev_rf_probe_run(void)
{
    /* The PHY driver (docs/10 B14) is deliberately not in this list: the IDF core links it for sleep handling, it is
     * reported by scripts/rf_map_check.py, and it does not by itself start any radio. */
    const probe_entry_t table[] = {
        {"esp_wifi_init", &esp_wifi_init},
        {"esp_wifi_start", &esp_wifi_start},
        {"esp_now_init", &esp_now_init},
        {"esp_bt_controller_init", &esp_bt_controller_init},
        {"esp_bt_controller_enable", &esp_bt_controller_enable},
        {"esp_ieee802154_enable", &esp_ieee802154_enable},
        {"esp_ieee802154_receive", &esp_ieee802154_receive},
    };
    dev_rf_probe_t r = {0, 0, NULL};
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        ++r.checked;
        if (table[i].addr != NULL) {
            if (r.present == 0) r.first = table[i].name;
            ++r.present;
        }
    }
    return r;
}
