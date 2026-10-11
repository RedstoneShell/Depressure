#ifndef DBMP_H
#define DBMP_H

#include "dtypes.h"

typedef struct dbmp_info {
    u64 width;
    u64 height;
    u64 bpp;
    u64 pixel_offset;
    int top_down;
    u64 row_stride;
    const u8 *pixels;
} dbmp_info_t;

int dbmp_check(const void *buf, u64 size);

int dbmp_parse(const void *buf, u64 size, dbmp_info_t *out);

u32 dbmp_get_pixel(const dbmp_info_t *info, u64 x, u64 y);

#endif /* DBMP_H */