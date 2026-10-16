// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DPNG_H
#define DPNG_H

#include "dtypes.h"

#define DPNG_MAX_WIDTH   512
#define DPNG_MAX_HEIGHT  512
#define DPNG_MAX_DECODED (DPNG_MAX_WIDTH * DPNG_MAX_HEIGHT * 4)

typedef struct dpng_info {
    u32 width;
    u32 height;
    u32 channels;
    const u8 *pixels;
} dpng_info_t;

int dpng_check(const void *buf, u64 size);
int dpng_parse(const void *buf, u64 size, dpng_info_t *out);
u32 dpng_get_pixel(const dpng_info_t *info, u64 x, u64 y);

#endif /* DPNG_H */