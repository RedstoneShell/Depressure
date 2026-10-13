// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "ddriver.h"
#include "dfont8x8.h"

typedef struct dkernel_api {
    u32 version;
    u32 _pad;
    int (*register_driver)(ddriver_t *drv);
} dkernel_api_t;

typedef struct dws_api {
    u32 version;
    u32 _pad;
    void (*put_pixel)(u64 x, u64 y, u32 rgb);
    void (*fill)(u32 rgb);
    void (*fill_rect)(u64 x, u64 y, u64 w, u64 h, u32 rgb);
    u32  (*convert_color)(u32 rgb);
    void (*blit_scaled)(u64 x, u64 y, u64 dst_w, u64 dst_h,
                        const u32 *src, u64 src_w, u64 src_h);
    void (*draw_char)(u64 x, u64 y, char ch, u32 fg, u32 bg);
    void (*draw_text)(u64 x, u64 y, const char *str, u32 fg, u32 bg);
} dws_api_t;

typedef struct fb_info {
    u64 base;
    u64 width;
    u64 height;
    u64 pitch;
    u64 format;
    u64 size;
} fb_info_t;

static fb_info_t g_fb;
static dws_api_t g_api;

static u32 dws_convert_color(u32 rgb) {
    u8 r = (rgb >> 16) & 0xFF;
    u8 g = (rgb >> 8)  & 0xFF;
    u8 b =  rgb        & 0xFF;

    switch (g_fb.format) {
        case 0:
            return ((u32)0xFF << 24) | ((u32)b << 16) | ((u32)g << 8) | r;
        case 1:
            return ((u32)0xFF << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
        default:
            return rgb;
    }
}

static void dws_put_pixel(u64 x, u64 y, u32 rgb) {
    if (x >= g_fb.width || y >= g_fb.height) return;
    if (!g_fb.base) return;

    u64 stride = g_fb.pitch;
    volatile u32 *p = (volatile u32 *)g_fb.base;
    p[y * stride + x] = dws_convert_color(rgb);
}

static void dws_fill(u32 rgb) {
    if (!g_fb.base) return;
    u32 color = dws_convert_color(rgb);
    u64 stride = g_fb.pitch;
    u64 count = stride * g_fb.height;
    volatile u32 *p = (volatile u32 *)g_fb.base;
    for (u64 i = 0; i < count; i++) {
        p[i] = color;
    }
}

static void dws_blit_scaled(u64 x, u64 y, u64 dst_w, u64 dst_h,
                            const u32 *src, u64 src_w, u64 src_h) {
    if (!g_fb.base) return;
    if (src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) return;

    u64 stride = g_fb.pitch;

    for (u64 dy = 0; dy < dst_h; dy++) {
        u64 py = y + dy;
        if (py >= g_fb.height) break;

        u64 sy = (dy * src_h) / dst_h;
        if (sy >= src_h) sy = src_h - 1;

        const u32 *src_row = src + sy * src_w;
        volatile u32 *dst_row = (volatile u32 *)g_fb.base + py * stride;

        for (u64 dx = 0; dx < dst_w; dx++) {
            u64 px = x + dx;
            if (px >= g_fb.width) break;

            u64 sx = (dx * src_w) / dst_w;
            if (sx >= src_w) sx = src_w - 1;

            dst_row[px] = dws_convert_color(src_row[sx]);
        }
    }
}

static void dws_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 rgb) {
    if (!g_fb.base) return;
    u32 color = dws_convert_color(rgb);
    u64 stride = g_fb.pitch;
    volatile u32 *p = (volatile u32 *)g_fb.base;

    for (u64 dy = 0; dy < h; dy++) {
        u64 py = y + dy;
        if (py >= g_fb.height) break;
        for (u64 dx = 0; dx < w; dx++) {
            u64 px = x + dx;
            if (px >= g_fb.width) break;
            p[py * stride + px] = color;
        }
    }
}

static void dws_draw_char(u64 x, u64 y, char ch, u32 fg, u32 bg) {
    if (ch < 32 || ch > 126) ch = '?';
    const u8 *glyph = font8x8[(u8)ch - 32];
    
    for (u64 row = 0; row < 8; row++) {
        u8 bits = glyph[row];
        for (u64 col = 0; col < 8; col++) {
            u32 color = (bits & (1 << col)) ? fg : bg;
            dws_fill_rect(x + col*2, y + row*2, 2, 2, color);
        }
    }
}

static void dws_draw_text(u64 x, u64 y, const char *str, u32 fg, u32 bg) {
    u64 cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += 16;
        } else {
            dws_draw_char(cx, y, *str, fg, bg);
            cx += 8;
        }
        str++;
    }
}

static ddriver_t dws_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_VIDEO,
    .name    = "dws",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void dws_main(dkernel_api_t *api, fb_info_t *fb) {
    if (fb) {
        g_fb = *fb;
    }
    g_api.version       = 0x00010000;
    g_api._pad          = 0;
    g_api.put_pixel     = dws_put_pixel;
    g_api.fill          = dws_fill;
    g_api.fill_rect     = dws_fill_rect;
    g_api.convert_color = dws_convert_color;
    g_api.blit_scaled   = dws_blit_scaled;
    g_api.draw_char     = dws_draw_char;
    g_api.draw_text     = dws_draw_text;

    if (api && api->register_driver) {
        dws_driver.api = (u64)&g_api;
        api->register_driver(&dws_driver);
    }
}