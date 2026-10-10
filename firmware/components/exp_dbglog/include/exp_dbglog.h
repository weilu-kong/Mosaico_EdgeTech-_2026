// SPDX-License-Identifier: Apache-2.0
// Screen log (stage 3a debug aid): captures ESP_LOG output into RAM and remembers across a software reset
// how the previous run ended. No radio, no USB, no storage writes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Call first in app_main. Installs the log hook, reads the reset reason, and logs a boot line. */
void exp_dbglog_init(void);

/* Remembers the name of the start-up stage in RAM that survives a software reset / panic reboot
 * (not a power cycle). The next boot shows it as "prev stage". */
void exp_dbglog_stage(const char *name);

uint32_t exp_dbglog_boot_count(void);
const char *exp_dbglog_reset_name(void);   /* why this run started                                   */
const char *exp_dbglog_prev_stage(void);   /* last stage reached by the previous run, "" if unknown  */

size_t exp_dbglog_lines(void);
bool exp_dbglog_line(size_t back, char *out, size_t cap);   /* back = 0 is the newest line */
