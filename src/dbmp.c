// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dbmp.h"
#define BMP_SIGNATURE   0x4D42
int dbmp_check(const void *buf, u64 size) {
    if (size < 54) return 0;
    const u8 *p = (const u8 *)buf;
    u16 sig = (u16)(p[0] | (p[1] << 8));
    if (sig != BMP_SIGNATURE) return 0;
    u32 dib_size = (u32)(p[14] | (p[15] << 8) | (p[16] << 16) | (p[17] << 24));
    if (dib_size < 40) return 0;
    return 1;
}
int dbmp_parse(const void *buf, u64 size, dbmp_info_t *out) {
    if (!dbmp_check(buf, size)) return -1;
    const u8 *p = (const u8 *)buf;
    u32 pixel_offset = (u32)(p[0x0A] | (p[0x0B] << 8) |
                             (p[0x0C] << 16) | (p[0x0D] << 24));
    i32 width = (i32)(p[0x12] | (p[0x13] << 8) |
                      (p[0x14] << 16) | (p[0x15] << 24));
    i32 height = (i32)(p[0x16] | (p[0x17] << 8) |
                       (p[0x18] << 16) | (p[0x19] << 24));
    u16 bpp = (u16)(p[0x1C] | (p[0x1D] << 8));
    u32 compression = (u32)(p[0x1E] | (p[0x1F] << 8) |
                            (p[0x20] << 16) | (p[0x21] << 24));
    if (compression != 0) return -1;
    if (bpp != 24 && bpp != 32) return -1;
    int top_down = 0;
    u64 abs_height;
    if (height < 0) {
        top_down = 1;
        abs_height = (u64)(-height);
    } else {
        abs_height = (u64)height;
    }
    u64 row_bits = (u64)width * bpp;
    u64 row_stride = ((row_bits + 31) / 32) * 4;
    u64 total = row_stride * abs_height;
    if (pixel_offset + total > size) return -1;
    out->width        = (u64)width;
    out->height       = abs_height;
    out->bpp          = bpp;
    out->pixel_offset = pixel_offset;
    out->top_down     = top_down;
    out->row_stride   = row_stride;
    out->pixels       = p + pixel_offset;
    return 0;
}

u32 dbmp_get_pixel(const dbmp_info_t *info, u64 x, u64 y) {
    if (x >= info->width || y >= info->height) return 0;
    u64 row;
    if (info->top_down) {
        row = y;
    } else {
        row = info->height - 1 - y;
    }
    const u8 *row_ptr = info->pixels + row * info->row_stride;
    if (info->bpp == 24) {
        const u8 *px = row_ptr + x * 3;
        u8 b = px[0];
        u8 g = px[1];
        u8 r = px[2];
        return ((u32)r << 16) | ((u32)g << 8) | b;
    } else { /* 32 bpp */
        const u8 *px = row_ptr + x * 4;
        u8 b = px[0];
        u8 g = px[1];
        u8 r = px[2];
        return ((u32)r << 16) | ((u32)g << 8) | b;
    }
}