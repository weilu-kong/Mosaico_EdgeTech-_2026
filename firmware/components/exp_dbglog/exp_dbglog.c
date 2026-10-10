// SPDX-License-Identifier: Apache-2.0
#include "exp_dbglog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dbglog_ring.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#define PERSIST_MAGIC 0x45584C47u   /* "EXLG" */

typedef struct {
    uint32_t magic;
    uint32_t boot_count;
    char stage[24];
} persist_t;

/* Not cleared by a software reset or a panic reboot; random after a power cycle, hence the magic. */
static __NOINIT_ATTR persist_t s_persist;
static dbglog_ring_t s_ring;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_prev_vprintf;
static char s_prev_stage[24];
static const char *s_reset_name = "?";
static bool s_inited;

static const char *reset_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_UNKNOWN: return "unknown";
    default: return "other";
    }
}

static void ring_put(const char *s, size_t n)
{
    portENTER_CRITICAL(&s_lock);
    dbglog_ring_write(&s_ring, s, n);
    portEXIT_CRITICAL(&s_lock);
}

static int log_hook(const char *fmt, va_list ap)
{
    char tmp[192];
    va_list ap2;
    va_copy(ap2, ap);
    const int n = vsnprintf(tmp, sizeof(tmp), fmt, ap2);
    va_end(ap2);
    if (n > 0) {
        if ((size_t)n < sizeof(tmp)) {
            ring_put(tmp, (size_t)n);
        } else {                                   /* truncated: keep the line boundary */
            ring_put(tmp, sizeof(tmp) - 1U);
            ring_put("\n", 1);
        }
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}

void exp_dbglog_init(void)
{
    if (s_inited) return;
    s_inited = true;
    dbglog_ring_init(&s_ring);
    const esp_reset_reason_t rr = esp_reset_reason();
    s_reset_name = reset_name(rr);
    if (s_persist.magic != PERSIST_MAGIC || rr == ESP_RST_POWERON) {
        memset(&s_persist, 0, sizeof(s_persist));
        s_persist.magic = PERSIST_MAGIC;
    }
    memcpy(s_prev_stage, s_persist.stage, sizeof(s_prev_stage));
    s_prev_stage[sizeof(s_prev_stage) - 1U] = '\0';
    memset(s_persist.stage, 0, sizeof(s_persist.stage));
    ++s_persist.boot_count;
    s_prev_vprintf = esp_log_set_vprintf(log_hook);

    char b[80];
    const int n = snprintf(b, sizeof(b), "boot #%u reset %s (%d) prev stage '%s'\n", (unsigned)s_persist.boot_count,
                           s_reset_name, (int)rr, s_prev_stage);
    if (n > 0) ring_put(b, (size_t)n < sizeof(b) ? (size_t)n : sizeof(b) - 1U);
}

void exp_dbglog_stage(const char *name)
{
    if (!s_inited || !name) return;
    strncpy(s_persist.stage, name, sizeof(s_persist.stage) - 1U);
    s_persist.stage[sizeof(s_persist.stage) - 1U] = '\0';
}

uint32_t exp_dbglog_boot_count(void) { return s_persist.boot_count; }
const char *exp_dbglog_reset_name(void) { return s_reset_name; }
const char *exp_dbglog_prev_stage(void) { return s_prev_stage; }

size_t exp_dbglog_lines(void)
{
    portENTER_CRITICAL(&s_lock);
    const size_t n = dbglog_ring_count(&s_ring);
    portEXIT_CRITICAL(&s_lock);
    return n;
}

bool exp_dbglog_line(size_t back, char *out, size_t cap)
{
    portENTER_CRITICAL(&s_lock);
    const bool ok = dbglog_ring_get(&s_ring, back, out, cap);
    portEXIT_CRITICAL(&s_lock);
    return ok;
}
