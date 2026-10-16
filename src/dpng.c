// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dpng.h"

#define PNG_SIG_0  0x89
#define PNG_SIG_1  'P'
#define PNG_SIG_2  'N'
#define PNG_SIG_3  'G'

#define PNG_COLOR_RGB   2
#define PNG_COLOR_RGBA  6

static u32 g_png_out[DPNG_MAX_WIDTH * DPNG_MAX_HEIGHT];
static u8  g_png_scanline[DPNG_MAX_WIDTH * 4 + 1];
static u8  g_png_prev[DPNG_MAX_WIDTH * 4 + 1];
static u8  g_png_idat[DPNG_MAX_DECODED * 2];

static u8  g_png_zbuf[DPNG_MAX_DECODED * 4];

typedef struct png_bits {
    const u8 *data;
    u64       size;
    u64       pos;
    u32       bitbuf;
    u32       bitcnt;
} png_bits_t;

static void bits_init(png_bits_t *b, const u8 *data, u64 size) {
    b->data = data;
    b->size = size;
    b->pos = 0;
    b->bitbuf = 0;
    b->bitcnt = 0;
}

static u32 bits_get(png_bits_t *b, u32 n) {
    while (b->bitcnt < n) {
        u8 byte = (b->pos < b->size) ? b->data[b->pos++] : 0;
        b->bitbuf |= (u32)byte << b->bitcnt;
        b->bitcnt += 8;
    }
    u32 v = b->bitbuf & ((1u << n) - 1);
    b->bitbuf >>= n;
    b->bitcnt -= n;
    return v;
}

typedef struct huff {
    u16 counts[16];
    u16 symbols[288];
} huff_t;

static void huff_build(huff_t *h, const u8 *lengths, u32 n) {
    for (u32 i = 0; i < 16; i++) h->counts[i] = 0;
    for (u32 i = 0; i < n; i++) h->counts[lengths[i]]++;
    h->counts[0] = 0;

    u16 offs[16];
    offs[0] = 0;
    for (u32 i = 1; i < 16; i++) {
        offs[i] = offs[i-1] + h->counts[i-1];
    }
    for (u32 i = 0; i < n; i++) {
        if (lengths[i]) h->symbols[offs[lengths[i]]++] = (u16)i;
    }
}

static int huff_decode(png_bits_t *b, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= (int)bits_get(b, 1);
        int count = h->counts[len];
        if (code - first < count) return h->symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static const u16 len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const u16 dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const u8 dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int inflate_block(png_bits_t *b, u8 *out, u64 out_cap, u64 *out_len) {
    static const u16 len_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    huff_t lit, dist, code_len;
    u8 lengths[320];

    u32 hlit = bits_get(b, 5) + 257;
    u32 hdist = bits_get(b, 5) + 1;
    u32 hclen = bits_get(b, 4) + 4;

    if (hlit > 286 || hdist > 30) return -1;

    for (u32 i = 0; i < 19; i++) lengths[i] = 0;
    for (u32 i = 0; i < hclen; i++) {
        lengths[len_order[i]] = (u8)bits_get(b, 3);
    }
    huff_build(&code_len, lengths, 19);

    u32 i = 0;
    while (i < hlit + hdist) {
        int sym = huff_decode(b, &code_len);
        if (sym < 0) return -1;

        if (sym < 16) {
            lengths[i++] = (u8)sym;
        } else if (sym == 16) {
            if (i == 0) return -1;
            u8 prev = lengths[i-1];
            u32 rep = 3 + bits_get(b, 2);
            while (rep-- && i < hlit + hdist) lengths[i++] = prev;
        } else if (sym == 17) {
            u32 rep = 3 + bits_get(b, 3);
            while (rep-- && i < hlit + hdist) lengths[i++] = 0;
        } else if (sym == 18) {
            u32 rep = 11 + bits_get(b, 7);
            while (rep-- && i < hlit + hdist) lengths[i++] = 0;
        }
    }

    huff_build(&lit, lengths, hlit);
    huff_build(&dist, lengths + hlit, hdist);

    u64 out_pos = 0;

    for (;;) {
        int sym = huff_decode(b, &lit);
        if (sym < 0) return -1;

        if (sym < 256) {
            if (out_pos >= out_cap) return -1;
            out[out_pos++] = (u8)sym;
        } else if (sym == 256) {
            break;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;

            u32 length = len_base[sym] + bits_get(b, len_extra[sym]);
            int dsym = huff_decode(b, &dist);
            if (dsym < 0 || dsym >= 30) return -1;

            u32 distance = dist_base[dsym] + bits_get(b, dist_extra[dsym]);
            if (distance > out_pos) return -1;

            for (u32 k = 0; k < length; k++) {
                if (out_pos >= out_cap) return -1;
                out[out_pos] = out[out_pos - distance];
                out_pos++;
            }
        }
    }

    *out_len = out_pos;
    return 0;
}

static int png_inflate(const u8 *data, u64 size, u8 *out, u64 out_cap, u64 *out_len) {
    png_bits_t b;
    bits_init(&b, data, size);

    u64 total = 0;

    for (;;) {
        u32 final = bits_get(&b, 1);
        u32 type  = bits_get(&b, 2);

        if (type == 0) {
            b.bitbuf = 0;
            b.bitcnt = 0;
            if (b.pos + 4 > b.size) return -1;
            u16 len  = b.data[b.pos] | (b.data[b.pos+1] << 8);
            b.pos += 4;
            if (b.pos + len > b.size) return -1;
            if (total + len > out_cap) return -1;
            for (u32 i = 0; i < len; i++) out[total++] = b.data[b.pos++];
        } else if (type == 1) {
            static const u8 fixed_lit_len[288] = {
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
                8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,
            };
            huff_t hl, hd;
            huff_build(&hl, fixed_lit_len, 288);
            u8 dist_len[30];
            for (int k = 0; k < 30; k++) dist_len[k] = 5;
            huff_build(&hd, dist_len, 30);

            (void)hd;
            return -1;
        } else if (type == 2) {
            u64 block_out = 0;
            if (inflate_block(&b, out + total, out_cap - total, &block_out) != 0) return -1;
            total += block_out;
        } else {
            return -1;
        }

        if (final) break;
    }

    *out_len = total;
    return 0;
}

static u32 paeth(u32 a, u32 b, u32 c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int dpng_check(const void *buf, u64 size) {
    if (size < 8) return 0;
    const u8 *p = (const u8 *)buf;
    return p[0] == PNG_SIG_0 && p[1] == PNG_SIG_1 &&
           p[2] == PNG_SIG_2 && p[3] == PNG_SIG_3;
}

int dpng_parse(const void *buf, u64 size, dpng_info_t *out) {
    if (!dpng_check(buf, size)) return -1;
    if (!out) return -1;

    const u8 *p = (const u8 *)buf;
    u64 pos = 8;

    u32 width = 0, height = 0, bit_depth = 0, color_type = 0;
    u32 interlace = 0;
    u64 idat_len = 0;
    int have_ihdr = 0;

    while (pos + 8 <= size) {
        u32 chunk_len = (u32)p[pos] | ((u32)p[pos+1] << 8)
                      | ((u32)p[pos+2] << 16) | ((u32)p[pos+3] << 24);
        const u8 *type = p + pos + 4;
        const u8 *data = p + pos + 8;

        if (pos + 12 + chunk_len > size) return -1;

        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            if (chunk_len < 13) return -1;
            width      = ((u32)data[0] << 24) | ((u32)data[1] << 16)
                       | ((u32)data[2] << 8)  | (u32)data[3];
            height     = ((u32)data[4] << 24) | ((u32)data[5] << 16)
                       | ((u32)data[6] << 8)  | (u32)data[7];
            bit_depth  = data[8];
            color_type = data[9];
            interlace  = data[12];
            have_ihdr = 1;
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (idat_len + chunk_len > sizeof(g_png_idat)) return -1;
            for (u32 i = 0; i < chunk_len; i++) {
                g_png_idat[idat_len + i] = data[i];
            }
            idat_len += chunk_len;
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            break;
        }

        pos += 12 + chunk_len;
    }

    if (!have_ihdr) return -1;
    if (bit_depth != 8) return -1;
    if (color_type != PNG_COLOR_RGB && color_type != PNG_COLOR_RGBA) return -1;
    if (interlace != 0) return -1;
    if (width == 0 || height == 0) return -1;
    if (width > DPNG_MAX_WIDTH || height > DPNG_MAX_HEIGHT) return -1;
    if (idat_len == 0) return -1;

    u32 channels = (color_type == PNG_COLOR_RGBA) ? 4 : 3;
    u64 raw_size = 0;
    if (png_inflate(g_png_idat, idat_len, g_png_zbuf, sizeof(g_png_zbuf), &raw_size) != 0) {
        return -1;
    }

    u64 stride = (u64)width * channels;
    u64 expected = (stride + 1) * height;
    if (raw_size < expected) return -1;

    u8 *prev = g_png_prev;
    u8 *cur  = g_png_scanline;

    for (u32 i = 0; i < (u32)stride; i++) prev[i] = 0;

    u64 src = 0;
    u32 *dst = g_png_out;

    for (u32 y = 0; y < height; y++) {
        u8 filter = g_png_zbuf[src++];

        for (u32 i = 0; i < (u32)stride; i++) {
            cur[i] = g_png_zbuf[src++];
        }

        switch (filter) {
            case 0:
                break;
            case 1:
                for (u32 i = (u32)channels; i < (u32)stride; i++) {
                    cur[i] = (u8)(cur[i] + cur[i - channels]);
                }
                break;
            case 2:
                for (u32 i = 0; i < (u32)stride; i++) {
                    cur[i] = (u8)(cur[i] + prev[i]);
                }
                break;
            case 3:
                for (u32 i = 0; i < (u32)stride; i++) {
                    u32 a = (i >= channels) ? cur[i - channels] : 0;
                    u32 b = prev[i];
                    cur[i] = (u8)(cur[i] + ((a + b) >> 1));
                }
                break;
            case 4:
                for (u32 i = 0; i < (u32)stride; i++) {
                    u32 a = (i >= channels) ? cur[i - channels] : 0;
                    u32 b = prev[i];
                    u32 c = (i >= channels) ? prev[i - channels] : 0;
                    cur[i] = (u8)(cur[i] + paeth(a, b, c));
                }
                break;
            default:
                return -1;
        }

        for (u32 x = 0; x < width; x++) {
            u8 r = cur[x * channels + 0];
            u8 g = cur[x * channels + 1];
            u8 b = cur[x * channels + 2];
            dst[y * width + x] = ((u32)r << 16) | ((u32)g << 8) | b;
        }

        u8 *tmp = prev;
        prev = cur;
        cur = tmp;
    }

    out->width = width;
    out->height = height;
    out->channels = channels;
    out->pixels = (const u8 *)g_png_out;
    return 0;
}

u32 dpng_get_pixel(const dpng_info_t *info, u64 x, u64 y) {
    if (!info || x >= info->width || y >= info->height) return 0;
    const u32 *p = (const u32 *)info->pixels;
    return p[y * info->width + x];
}