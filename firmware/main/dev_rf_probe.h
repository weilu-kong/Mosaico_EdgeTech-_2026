// SPDX-License-Identifier: Apache-2.0
// Runtime RF evidence (stage 3c). Reports whether any radio entry point exists in the RUNNING image. It never calls
// one. Software-visible state only: it says nothing about ROM code, the PHY driver, add-on modules or emissions, and it
// is not an RF measurement or a compliance statement. The product status stays "RF Verification Pending".
#pragma once

#include <stdbool.h>

typedef struct {
    int checked;            /* number of entry points looked for                          */
    int present;            /* how many of them exist in this image (expected: 0)         */
    const char *first;      /* name of the first one found, NULL if none                  */
} dev_rf_probe_t;

dev_rf_probe_t dev_rf_probe_run(void);
