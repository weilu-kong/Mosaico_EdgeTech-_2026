// SPDX-License-Identifier: Apache-2.0
// Task watchdog for unattended running (stage 3d). A frozen screen on a stand is worse than a reboot: if the drawing loop or
// the IMU task stops running for EXP_WDT_TIMEOUT_MS, the chip is reset (panic -> restart) and the demo comes back by itself.
// It does nothing about a task that is merely slow, and it is not a substitute for finding the cause: the reset reason is
// shown in the log view of the development build (TASK_WDT). NOT YET RUN ON THE BOARD.
#pragma once

#ifndef EXP_USE_WDT
#define EXP_USE_WDT 1
#endif
#ifndef EXP_WDT_TIMEOUT_MS
#define EXP_WDT_TIMEOUT_MS 10000
#endif

void dev_wdt_start(void);       /* once, from app_main: (re)configures the task watchdog (timeout, panic on expiry) */
void dev_wdt_subscribe(void);   /* from the task that is to be watched */
void dev_wdt_feed(void);        /* from that task, once per loop iteration */
