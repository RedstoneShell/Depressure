// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DREG_H
#define DREG_H

#include "dtypes.h"
#include "ddriver.h"

/* Magic "DKES" */
#define DKES_PATTERN    0x53454B44u

/* Krnl ABI ver */
#define DKES_VERSION    0x00010000u

/* You know what is that address */
#define DREG_ADDR       0x1300000ULL

typedef struct dkernel_registry {
    u32 pattern;          /* DKES_PATTERN */
    u32 version;          /* DKES_VERSION */
    u64 first_driver;     /* Pointer to first driver struc, 0 = NULL */
} dkernel_registry_t;

void dreg_init(void);

dkernel_registry_t *dreg_get(void);

int dreg_register_driver(ddriver_t *drv);

ddriver_t *dreg_first_driver(void);

#endif /* DREG_H */