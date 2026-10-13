// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DSTORAGE_H
#define DSTORAGE_H

#include "dtypes.h"

void dstorage_detect(void *BootServices);
u64  dstorage_get_total(void);
u32  dstorage_get_count(void);
u64  dstorage_get_device_size(u32 index);

#endif