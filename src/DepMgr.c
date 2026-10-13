// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "dfont8x8.h"
#include "dkrnl.h"
#include "pci.h"
#include "acpi.h"
#include "hpet.h"

typedef struct uart_api {
    u32 version; u32 _pad;
    void (*putc)(char c);
    void (*puts)(const char *str);
    void (*put_hex)(u64 value);
    int  (*has_data)(void);
    char (*getc)(void);
} uart_api_t;

typedef struct dkbd_api {
    u32 version; u32 _pad;
    int  (*has_key)(void);
    char (*get_key)(void);
    u8   (*get_scancode)(void);
} dkbd_api_t;

typedef struct dmouse_api {
    u32 version; u32 _pad;
    int  (*has_event)(void);
    void (*get_state)(int *x, int *y, int *buttons);
    int  (*get_x)(void);
    int  (*get_y)(void);
    int  (*get_buttons)(void);
    void (*set_position)(int x, int y);
} dmouse_api_t;

typedef struct dws_api {
    u32 version; u32 _pad;
    void (*put_pixel)(u64 x, u64 y, u32 rgb);
    void (*fill)(u32 rgb);
    void (*fill_rect)(u64 x, u64 y, u64 w, u64 h, u32 rgb);
    u32  (*convert_color)(u32 rgb);
    void (*blit_scaled)(u64 x, u64 y, u64 dst_w, u64 dst_h,
                        const u32 *src, u64 src_w, u64 src_h);
    void (*draw_char)(u64 x, u64 y, char ch, u32 fg, u32 bg);
    void (*draw_text)(u64 x, u64 y, const char *str, u32 fg, u32 bg);
} dws_api_t;

static uart_api_t    *g_uart  = 0;
static dws_api_t     *g_dws   = 0;
static dkbd_api_t    *g_kbd   = 0;
static dkernel_api_t *g_api   = 0;
static dmouse_api_t  *g_mouse = 0;
static acpi_api_t    *g_acpi  = 0;
static pci_api_t     *g_pci   = 0;

static u64 g_screen_w = 800;
static u64 g_screen_h = 600;

#define COL_BG      0x00990000
#define COL_FG      0x00FFFFFF
#define COL_ACCENT  0x00FFCC00
#define COL_DARK    0x00660000
#define COL_CORE_BG 0x00CC0000
#define COL_CORE_ON 0x0000FF00
#define COL_CORE_OFF 0x00880000
#define COL_BTN     0x00330000
#define COL_BTN_ON  0x0000AA00

#define CHAR_W 16
#define CHAR_H 16

#define SCREEN_MAIN       0
#define SCREEN_HWMAP      1
#define SCREEN_PCI        2
#define SCREEN_MANAGEMENT 3
#define SCREEN_TIMER      4

#define PCI_PAGE_SIZE 10
#define POWER_ACTION_NONE     0
#define POWER_ACTION_SHUTDOWN 1
#define POWER_ACTION_REBOOT   2
#define CORES_PER_ROW 5

#define BACKBUF_MAX_W 1024
#define BACKBUF_MAX_H 768
#define BACKBUF_ADDR  0x02000000ULL
#define BACKBUF_SIZE  ((u64)BACKBUF_MAX_W * BACKBUF_MAX_H * sizeof(u32))

#define DTIMER_SOURCE_PIT    0u
#define DTIMER_SOURCE_HPET   1u
#define DTIMER_SOURCE_LAPIC  2u
#define DTIMER_SOURCE_COUNT  3u

static int g_screen = SCREEN_MAIN;
static int g_dirty = 1;
static u32 g_pci_page = 0;

static u64 g_management_x = 0, g_management_y = 0;
static u64 g_management_w = 0, g_management_h = 0;
static u64 g_shutdown_x = 0, g_shutdown_y = 0;
static u64 g_reboot_x = 0,   g_reboot_y = 0;
static u64 g_confirm_x = 0,  g_confirm_y = 0;
static u64 g_cancel_x = 0,   g_cancel_y = 0;
static int g_power_confirmation = POWER_ACTION_NONE;
static dstatus_t g_power_action_status = DEPRESSURE_STATUS_OK;

static u64 g_icon_x = 0, g_icon_y = 0, g_icon_w = 0, g_icon_h = 0;
static u64 g_timer_icon_x = 0, g_timer_icon_y = 0;
static u64 g_timer_icon_w = 0, g_timer_icon_h = 0;

static u64 g_timer_btn_x[DTIMER_SOURCE_COUNT];
static u64 g_timer_btn_y = 0;
static u64 g_timer_btn_w = 200;
static u64 g_timer_btn_h = 56;

static int g_last_buttons = 0;
static int g_mouse_x = 400, g_mouse_y = 300;
static int g_prev_mouse_x = -1, g_prev_mouse_y = -1;
static int g_prev_buttons = 0;

static u32 g_ncpu = 1;
static u32 g_ncpu_cpuid = 1;
static char g_cpu_vendor[13] = {0};
static char g_cpu_name[49] = {0};
static u32 g_core_load[64] = {0};
static u32 g_prev_core_load[64] = {0};

static u32 *g_backbuf = (u32 *)BACKBUF_ADDR;
static u32  g_backbuf_w = 0;
static u32  g_backbuf_h = 0;
static int  g_drawn = 0;

static const char *ts_name(u32 s) {
    if (g_api && g_api->timer_source_name) return g_api->timer_source_name(s);
    switch (s) {
        case DTIMER_SOURCE_PIT:   return "PIT";
        case DTIMER_SOURCE_HPET:  return "HPET";
        case DTIMER_SOURCE_LAPIC: return "LAPIC";
        default:                  return "?";
    }
}

static void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile ("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(0));
}

static void cpu_detect(void) {
    u32 eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    *(u32 *)&g_cpu_vendor[0] = ebx;
    *(u32 *)&g_cpu_vendor[4] = edx;
    *(u32 *)&g_cpu_vendor[8] = ecx;
    g_cpu_vendor[12] = 0;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    u32 family = (eax >> 8) & 0xF;
    u32 model  = (eax >> 4) & 0xF;
    u32 ncpu   = (ebx >> 16) & 0xFF;
    if (ncpu > 0) g_ncpu_cpuid = ncpu;
    const char *v = g_cpu_vendor;
    int i = 0;
    while (v[i] && i < 48) { g_cpu_name[i] = v[i]; i++; }
    g_cpu_name[i++] = ' ';
    g_cpu_name[i++] = 'F';
    g_cpu_name[i++] = '0' + (family % 10);
    g_cpu_name[i++] = 'M';
    g_cpu_name[i++] = '0' + (model % 10);
    g_cpu_name[i] = 0;
}

static void bb_clear(u32 rgb) {
    u32 n = g_backbuf_w * g_backbuf_h;
    for (u32 i = 0; i < n; i++) g_backbuf[i] = rgb;
}

static void bb_pixel(u64 x, u64 y, u32 rgb) {
    if (x >= g_backbuf_w || y >= g_backbuf_h) return;
    g_backbuf[y * g_backbuf_w + x] = rgb;
}

static void bb_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 rgb) {
    if (x >= g_backbuf_w || y >= g_backbuf_h) return;
    if (x + w > g_backbuf_w) w = g_backbuf_w - x;
    if (y + h > g_backbuf_h) h = g_backbuf_h - y;
    for (u64 dy = 0; dy < h; dy++) {
        u32 *row = &g_backbuf[(y + dy) * g_backbuf_w + x];
        for (u64 dx = 0; dx < w; dx++) row[dx] = rgb;
    }
}

static void bb_char(u64 x, u64 y, char ch, u32 fg, u32 bg, const u8 *font) {
    if (ch < 32 || ch > 126) ch = '?';
    const u8 *g = font + (u8)(ch - 32) * 8;
    for (u64 row = 0; row < 8; row++) {
        u8 bits = g[row];
        for (u64 col = 0; col < 8; col++) {
            u32 c = (bits & (1 << col)) ? fg : bg;
            bb_fill_rect(x + col*2, y + row*2, 2, 2, c);
        }
    }
}

static void bb_puts(u64 x, u64 y, const char *s, u32 fg, u32 bg, const u8 *font) {
    u64 cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += CHAR_H; }
        else { bb_char(cx, y, *s, fg, bg, font); cx += CHAR_W; }
        s++;
    }
}

static u64 bb_u64(u64 x, u64 y, u64 v, u32 fg, u32 bg, const u8 *font) {
    char b[24]; int i = 0;
    if (v == 0) b[i++] = '0';
    else while (v > 0) { b[i++] = '0' + (v % 10); v /= 10; }
    b[i] = 0;
    for (int k = 0; k < i/2; k++) { char t = b[k]; b[k] = b[i-1-k]; b[i-1-k] = t; }
    bb_puts(x, y, b, fg, bg, font);
    return x + (u64)i * CHAR_W;
}

static u64 bb_hex(u64 x, u64 y, u64 v, int digits, u32 fg, u32 bg, const u8 *font) {
    static const char h[] = "0123456789ABCDEF";
    char b[20]; int i = 0;
    b[i++] = '0'; b[i++] = 'x';
    for (int k = (digits - 1) * 4; k >= 0; k -= 4) b[i++] = h[(v >> k) & 0xF];
    b[i] = 0;
    bb_puts(x, y, b, fg, bg, font);
    return x + (u64)i * CHAR_W;
}

static u64 bb_hex16(u64 x, u64 y, u16 v, u32 fg, u32 bg, const u8 *font) {
    static const char h[] = "0123456789ABCDEF";
    char b[5];
    for (int i = 0; i < 4; i++) b[i] = h[(v >> (12 - i*4)) & 0xF];
    b[4] = 0;
    bb_puts(x, y, b, fg, bg, font);
    return x + 4 * CHAR_W;
}

static void bb_button(u64 x, u64 y, u64 w, u64 h, const char *lbl,
                      u32 border, u32 fill, const u8 *font) {
    bb_fill_rect(x, y, w, h, fill);
    bb_fill_rect(x, y, w, 2, border);
    bb_fill_rect(x, y + h - 2, w, 2, border);
    bb_fill_rect(x, y, 2, h, border);
    bb_fill_rect(x + w - 2, y, 2, h, border);
    u64 ll = 0; while (lbl[ll]) ll++;
    bb_puts(x + (w - ll*CHAR_W)/2, y + (h - CHAR_H)/2, lbl, COL_FG, fill, font);
}

static void draw_timer_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    bb_clear(COL_BG);
    bb_puts(40, 20, "=== TIMER SOURCE ===", COL_ACCENT, COL_BG, font);

    u32 cur = (g_api && g_api->timer_get_source) ? g_api->timer_get_source() : 0;
    u32 hz  = (g_api && g_api->timer_get_hz)     ? g_api->timer_get_hz()     : 1000;

    bb_puts(40, 56, "Current:", COL_FG, COL_BG, font);
    bb_puts(40 + 10*CHAR_W, 56, ts_name(cur), COL_CORE_ON, COL_BG, font);
    bb_puts(40, 76, "Frequency:", COL_FG, COL_BG, font);
    u64 hx = bb_u64(40 + 12*CHAR_W, 76, hz, COL_FG, COL_BG, font);
    bb_puts(hx, 76, " Hz", COL_FG, COL_BG, font);
    bb_puts(40, 110, "Select timer source:", COL_ACCENT, COL_BG, font);

    g_timer_btn_y = 140;
    u64 tw = DTIMER_SOURCE_COUNT * g_timer_btn_w + (DTIMER_SOURCE_COUNT - 1) * 24;
    u64 sx = (g_screen_w - tw) / 2;
    for (u32 i = 0; i < DTIMER_SOURCE_COUNT; i++) {
        g_timer_btn_x[i] = sx + i * (g_timer_btn_w + 24);
        bb_button(g_timer_btn_x[i], g_timer_btn_y, g_timer_btn_w, g_timer_btn_h,
                  ts_name(i), COL_ACCENT, (i == cur) ? COL_BTN_ON : COL_BTN, font);
    }

    u64 y = g_timer_btn_y + g_timer_btn_h + 30;
    bb_puts(40, y, "PIT   - 8253/8254 programmable interval timer, IRQ0", COL_FG, COL_BG, font); y += CHAR_H + 4;
    bb_puts(40, y, "HPET  - High Precision Event Timer, comparator 0 -> IOAPIC GSI2", COL_FG, COL_BG, font); y += CHAR_H + 4;
    bb_puts(40, y, "LAPIC - Local APIC timer, per-CPU, LVT timer", COL_FG, COL_BG, font); y += CHAR_H + 4;
    y += 10;

    u32 av = (g_api && g_api->timer_get_ioapic_available) ? g_api->timer_get_ioapic_available() : 0;
    bb_puts(40, y, "IOAPIC:", COL_FG, COL_BG, font);
    bb_puts(40 + 8*CHAR_W, y, av ? "AVAILABLE" : "NOT AVAILABLE",
            av ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
    y += CHAR_H;

    u32 lid = (g_api && g_api->timer_get_lapic_id) ? g_api->timer_get_lapic_id() : 0;
    bb_puts(40, y, "LAPIC ID:", COL_FG, COL_BG, font);
    bb_u64(40 + 10*CHAR_W, y, lid, COL_FG, COL_BG, font);
    y += CHAR_H;

    if (g_acpi && g_acpi->get_info) {
        acpi_system_info_t info;
        if (DSTATUS_IS_OK(g_acpi->get_info(&info))) {
            bb_puts(40, y, "IOAPIC count:", COL_FG, COL_BG, font);
            bb_u64(40 + 14*CHAR_W, y, info.ioapic_count, COL_FG, COL_BG, font);
            y += CHAR_H;
            for (u32 i = 0; i < info.ioapic_count && i < ACPI_MAX_IOAPICS; i++) {
                bb_puts(80, y, "ioapic[", COL_FG, COL_BG, font);
                bb_u64(80 + 7*CHAR_W, y, i, COL_FG, COL_BG, font);
                bb_puts(80 + 8*CHAR_W, y, "]", COL_FG, COL_BG, font);
                bb_puts(80 + 10*CHAR_W, y, "addr=", COL_FG, COL_BG, font);
                bb_hex(80 + 15*CHAR_W, y, (u32)info.ioapic_address[i], 8, COL_FG, COL_BG, font);
                bb_puts(80 + 26*CHAR_W, y, "gsi=", COL_FG, COL_BG, font);
                bb_u64(80 + 30*CHAR_W, y, info.ioapic_gsi_base[i], COL_FG, COL_BG, font);
                y += CHAR_H;
            }
        }
    }

    if (g_api && g_api->hpet_info) {
        const volatile hpet_system_info_t *hp = g_api->hpet_info;
        bb_puts(40, y, "HPET:", COL_FG, COL_BG, font);
        bb_puts(40 + 6*CHAR_W, y, hp->enabled ? "ENABLED" : "DISABLED",
                hp->enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[1] PIT  [2] HPET  [3] LAPIC  [Esc] Back", COL_FG, COL_BG, font);
}

static const char *pci_class_label(u8 c) {
    static const char *t[] = {0, "STORAGE", "NETWORK", "DISPLAY", "MEDIA", 0,
                              "BRIDGE", "SERIAL", "SYSTEM", "INPUT", 0, 0, "BUS"};
    if (c == 0x01 || c == 0x02 || c == 0x03 || c == 0x04 || c == 0x06 ||
        c == 0x07 || c == 0x08 || c == 0x09 || c == 0x0C) return t[c];
    return "OTHER";
}

static void draw_pci_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    u32 count = 0, trunc = 0;
    bb_clear(COL_BG);
    bb_puts(40, 20, "=== PCI DEVICES ===", COL_ACCENT, COL_BG, font);

    if (!g_pci || !g_pci->get_count || !g_pci->get_device) {
        bb_puts(40, 64, "PCI driver/API is not available", COL_FG, COL_BG, font);
        bb_puts(40, g_screen_h - CHAR_H*2, "[H] Hardware Map  [Esc] Main", COL_FG, COL_BG, font);
        return;
    }
    if (DSTATUS_IS_ERR(g_pci->get_count(&count))) {
        bb_puts(40, 64, "PCI device query failed", COL_FG, COL_BG, font);
        return;
    }
    if (g_pci->get_scan_truncated &&
        DSTATUS_IS_ERR(g_pci->get_scan_truncated(&trunc))) trunc = 1;

    bb_puts(40, 44, "Found:", COL_FG, COL_BG, font);
    bb_u64(40 + 7*CHAR_W, 44, count, COL_FG, COL_BG, font);
    if (trunc)
        bb_puts(40 + 14*CHAR_W, 44, "SCAN LIMIT REACHED", COL_ACCENT, COL_BG, font);

    u32 pages = (count + PCI_PAGE_SIZE - 1) / PCI_PAGE_SIZE;
    if (pages == 0) pages = 1;
    if (g_pci_page >= pages) g_pci_page = pages - 1;

    bb_puts(40, 64, "BDF      Vendor:Device  Class", COL_ACCENT, COL_BG, font);

    u32 first = g_pci_page * PCI_PAGE_SIZE;
    u32 limit = first + PCI_PAGE_SIZE;
    if (limit > count) limit = count;
    for (u32 i = first; i < limit; i++) {
        const pci_device_t *d = 0;
        u64 y = 88 + (i - first) * 44;
        if (DSTATUS_IS_ERR(g_pci->get_device(i, &d)) || !d) continue;
        bb_puts(40, y, "B", COL_FG, COL_BG, font);
        bb_u64(40 + CHAR_W, y, d->bus, COL_FG, COL_BG, font);
        bb_puts(40 + 4*CHAR_W, y, "S", COL_FG, COL_BG, font);
        bb_u64(40 + 5*CHAR_W, y, d->slot, COL_FG, COL_BG, font);
        bb_puts(40 + 8*CHAR_W, y, "F", COL_FG, COL_BG, font);
        bb_u64(40 + 9*CHAR_W, y, d->func, COL_FG, COL_BG, font);
        bb_hex16(40 + 12*CHAR_W, y, d->vendor_id, COL_FG, COL_BG, font);
        bb_puts(40 + 16*CHAR_W, y, ":", COL_FG, COL_BG, font);
        bb_hex16(40 + 17*CHAR_W, y, d->device_id, COL_FG, COL_BG, font);
        bb_puts(40 + 22*CHAR_W, y, pci_class_label(d->class_code), COL_FG, COL_BG, font);
        bb_puts(56, y + 14, (d->command & 1) ? "IO:ON" : "IO:OFF", COL_FG, COL_BG, font);
        bb_puts(56 + 10*CHAR_W, y + 14, (d->command & 2) ? "MEM:ON" : "MEM:OFF", COL_FG, COL_BG, font);
        bb_puts(56 + 22*CHAR_W, y + 14, (d->command & 4) ? "MASTER:ON" : "MASTER:OFF", COL_FG, COL_BG, font);
    }
    bb_puts(40, g_screen_h - CHAR_H*2,
            "[N] Next  [B] Previous  [H] Hardware  [Esc] Main", COL_FG, COL_BG, font);
}

static const char *hpet_stat(dstatus_t s) {
    switch (s) {
        case DEPRESSURE_STATUS_OK:            return "ENABLED";
        case DEPRESSURE_HPET_NOT_FOUND:       return "NOT FOUND";
        case DEPRESSURE_HPET_NOT_SUPPORTED:   return "NOT SUPPORTED";
        case DEPRESSURE_HPET_INVALID_TABLE:   return "INVALID TABLE";
        case DEPRESSURE_HPET_INVALID_ADDRESS: return "INVALID ADDRESS";
        case DEPRESSURE_HPET_INVALID_PERIOD:  return "INVALID PERIOD";
        case DEPRESSURE_HPET_ENABLE_FAILED:   return "ENABLE FAILED";
        case DEPRESSURE_HPET_NOT_INITIALIZED: return "NOT INITIALIZED";
        case DEPRESSURE_NOT_FOUND:            return "ACPI N/A";
        case DEPRESSURE_CHECKSUM_ERROR:       return "ACPI CHECKSUM";
        case DEPRESSURE_CORRUPTED_DATA:       return "ACPI CORRUPT";
        default:                              return "INIT ERROR";
    }
}

static const char *fadt_stat(dstatus_t s) {
    switch (s) {
        case DEPRESSURE_STATUS_OK:                return "READY";
        case DEPRESSURE_ACPI_FADT_NOT_FOUND:      return "NOT FOUND";
        case DEPRESSURE_ACPI_FADT_INVALID:        return "INVALID";
        case DEPRESSURE_ACPI_S5_NOT_FOUND:        return "S5 NOT FOUND";
        case DEPRESSURE_ACPI_POWER_UNAVAILABLE:   return "PWR UNAVAIL";
        case DEPRESSURE_ACPI_RESET_UNAVAILABLE:   return "RST UNAVAIL";
        default:                                  return "ERROR";
    }
}

static void draw_mgmt_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    const volatile hpet_system_info_t *hp = g_api ? g_api->hpet_info : 0;
    acpi_system_info_t ai;
    int have_ai = 0;
    u64 y = 84;

    bb_clear(COL_BG);
    bb_puts(40, 20, "=== MANAGEMENT ===", COL_ACCENT, COL_BG, font);
    bb_puts(40, 56, "HPET STATUS", COL_ACCENT, COL_BG, font);

    if (g_acpi && g_acpi->version == ACPI_API_VERSION &&
        g_acpi->get_info && DSTATUS_IS_OK(g_acpi->get_info(&ai))) have_ai = 1;

    if (!hp) {
        bb_puts(56, y, "HPET state unavailable", COL_ACCENT, COL_BG, font);
    } else {
        bb_puts(56, y, "State:", COL_FG, COL_BG, font);
        bb_puts(56 + 8*CHAR_W, y, hpet_stat(hp->status),
                hp->enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(360, y, "MMIO:", COL_FG, COL_BG, font);
        bb_hex(360 + 7*CHAR_W, y, hp->mmio_base, 16, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "Frequency:", COL_FG, COL_BG, font);
        bb_u64(56 + 12*CHAR_W, y, hp->frequency_hz, COL_FG, COL_BG, font);
        bb_puts(56 + 23*CHAR_W, y, "Hz", COL_FG, COL_BG, font);
        bb_puts(360, y, "Counter:", COL_FG, COL_BG, font);
        bb_hex(360 + 10*CHAR_W, y, hp->counter_value, 16, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "Timers:", COL_FG, COL_BG, font);
        bb_u64(56 + 9*CHAR_W, y, hp->timer_count, COL_FG, COL_BG, font);
        bb_puts(240, y, "Vendor:", COL_FG, COL_BG, font);
        bb_hex16(240 + 8*CHAR_W, y, (u16)hp->vendor_id, COL_FG, COL_BG, font);
        bb_puts(360, y, "Period fs:", COL_FG, COL_BG, font);
        bb_u64(360 + 12*CHAR_W, y, hp->period_femtoseconds, COL_FG, COL_BG, font);
    }

    y += 24;
    bb_puts(40, y, "FACP / LEGACY HARDWARE", COL_ACCENT, COL_BG, font);
    y += 22;
    if (!have_ai) {
        bb_puts(56, y, "ACPI driver data unavailable", COL_ACCENT, COL_BG, font);
    } else {
        bb_puts(56, y, "FADT:", COL_FG, COL_BG, font);
        bb_puts(56 + 7*CHAR_W, y, fadt_stat(ai.fadt_status),
                DSTATUS_IS_OK(ai.fadt_status) ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(360, y, "SCI:", COL_FG, COL_BG, font);
        bb_u64(360 + 5*CHAR_W, y, ai.sci_interrupt, COL_FG, COL_BG, font);
        bb_puts(480, y, "SCI_EN:", COL_FG, COL_BG, font);
        bb_puts(480 + 9*CHAR_W, y, ai.acpi_enabled ? "ON" : "OFF",
                ai.acpi_enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM1 CNT A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, (u16)ai.pm1a_control_port, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, (u16)ai.pm1b_control_port, COL_FG, COL_BG, font);
        bb_puts(440, y, "len:", COL_FG, COL_BG, font);
        bb_u64(440 + 5*CHAR_W, y, ai.pm1_control_length, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "CNT VAL A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, ai.pm1a_control_value, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, ai.pm1b_control_value, COL_FG, COL_BG, font);
        bb_puts(440, y, "SLP_EN:", COL_FG, COL_BG, font);
        bb_puts(440 + 9*CHAR_W, y, (ai.pm1a_control_value & 0x2000) ? "ON" : "OFF", COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM1 EVT A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, (u16)ai.pm1a_event_port, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, (u16)ai.pm1b_event_port, COL_FG, COL_BG, font);
        bb_puts(440, y, "len:", COL_FG, COL_BG, font);
        bb_u64(440 + 5*CHAR_W, y, ai.pm1_event_length, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "EVT STAT A:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, ai.pm1a_event_status, COL_FG, COL_BG, font);
        bb_puts(280, y, "B:", COL_FG, COL_BG, font);
        bb_hex16(280 + 3*CHAR_W, y, ai.pm1b_event_status, COL_FG, COL_BG, font);
        bb_puts(440, y, "EVT EN A:", COL_FG, COL_BG, font);
        bb_hex16(440 + 11*CHAR_W, y, ai.pm1a_event_enable, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "EVT EN B:", COL_FG, COL_BG, font);
        bb_hex16(56 + 11*CHAR_W, y, ai.pm1b_event_enable, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM2:", COL_FG, COL_BG, font);
        bb_hex16(56 + 6*CHAR_W, y, (u16)ai.pm2_control_port, COL_FG, COL_BG, font);
        bb_puts(200, y, "PM TMR:", COL_FG, COL_BG, font);
        bb_hex16(200 + 9*CHAR_W, y, (u16)ai.pm_timer_port, COL_FG, COL_BG, font);
        bb_puts(440, y, "GPE0:", COL_FG, COL_BG, font);
        bb_hex16(440 + 7*CHAR_W, y, (u16)ai.gpe0_port, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "PM2 VALUE:", COL_FG, COL_BG, font);
        bb_hex16(56 + 12*CHAR_W, y, ai.pm2_control_value, COL_FG, COL_BG, font);
        bb_puts(240, y, "PM TMR NOW:", COL_FG, COL_BG, font);
        bb_hex(240 + 13*CHAR_W, y, ai.fadt_pm_timer_value, 8, COL_FG, COL_BG, font);
        y += 20;
        bb_puts(56, y, "S5:", COL_FG, COL_BG, font);
        bb_puts(56 + 5*CHAR_W, y, ai.s5_available ? "AVAILABLE" : "UNAVAILABLE",
                ai.s5_available ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts(240, y, "Sleep types A/B:", COL_FG, COL_BG, font);
        bb_u64(240 + 17*CHAR_W, y, ai.s5_type_a, COL_FG, COL_BG, font);
        bb_puts(240 + 20*CHAR_W, y, "/", COL_FG, COL_BG, font);
        bb_u64(240 + 22*CHAR_W, y, ai.s5_type_b, COL_FG, COL_BG, font);
    }

    g_shutdown_x = 80; g_reboot_x = 420;
    g_shutdown_y = g_screen_h - 100;
    g_reboot_y = g_shutdown_y;
    bb_button(g_shutdown_x, g_shutdown_y, 260, 40, "Shutdown", COL_ACCENT, COL_BTN, font);
    bb_button(g_reboot_x, g_reboot_y, 260, 40, "Reboot", COL_ACCENT, COL_BTN, font);

    if (g_power_confirmation != POWER_ACTION_NONE) {
        const char *prompt = (g_power_confirmation == POWER_ACTION_SHUTDOWN) ?
            "Confirm shutdown? Y/N" : "Confirm reboot? Y/N";
        u64 my = g_shutdown_y - 82;
        bb_fill_rect(120, my, 560, 72, COL_DARK);
        bb_puts(144, my + 6, prompt, COL_ACCENT, COL_DARK, font);
        g_confirm_x = 144; g_confirm_y = my + 30;
        g_cancel_x = 424; g_cancel_y = g_confirm_y;
        bb_button(g_confirm_x, g_confirm_y, 200, 40, "Confirm", COL_ACCENT, COL_BTN, font);
        bb_button(g_cancel_x, g_cancel_y, 200, 40, "Cancel", COL_ACCENT, COL_BTN, font);
    } else if (DSTATUS_IS_ERR(g_power_action_status)) {
        bb_puts(40, g_shutdown_y - 24,
                g_power_action_status == DEPRESSURE_TIMEOUT ?
                    "Power command issued" :
                g_power_action_status == DEPRESSURE_ACPI_RESET_UNAVAILABLE ?
                    "ACPI reset unavailable" :
                    "ACPI power unavailable",
                COL_ACCENT, COL_BG, font);
    }

    bb_puts(40, g_screen_h - CHAR_H*2,
            "[S] Shutdown  [R] Reboot  [Esc] Main Menu", COL_FG, COL_BG, font);
}

static void draw_gear(u64 cx, u64 cy, u64 sz) {
    u64 ro = sz/2, ri = ro*6/10, rh = ro*3/10;
    static const i8 dx[8] = {0,1,1,1,0,-1,-1,-1};
    static const i8 dy[8] = {-1,-1,0,1,1,1,0,-1};
    for (int i = 0; i < 8; i++) {
        i64 tx = (i64)cx + dx[i] * (i64)(ri + (ro - ri)/2);
        i64 ty = (i64)cy + dy[i] * (i64)(ri + (ro - ri)/2);
        u64 tw = sz/5, th = sz/5;
        bb_fill_rect((u64)(tx - (i64)tw/2), (u64)(ty - (i64)th/2), tw, th, COL_ACCENT);
    }
    for (i64 d = -(i64)ro; d <= (i64)ro; d++) {
        for (i64 e = -(i64)ro; e <= (i64)ro; e++) {
            i64 d2 = d*d + e*e;
            if (d2 <= (i64)(ro*ro) && d2 >= (i64)(ri*ri))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_ACCENT);
        }
    }
    for (i64 d = -(i64)rh; d <= (i64)rh; d++)
        for (i64 e = -(i64)rh; e <= (i64)rh; e++)
            if (d*d + e*e <= (i64)(rh*rh))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_BG);
}

static void draw_clock(u64 cx, u64 cy, u64 sz) {
    u64 ro = sz/2, ri = ro-4, rh = 3;
    for (i64 d = -(i64)ro; d <= (i64)ro; d++)
        for (i64 e = -(i64)ro; e <= (i64)ro; e++) {
            i64 d2 = d*d + e*e;
            if (d2 <= (i64)(ro*ro) && d2 >= (i64)(ri*ri))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_ACCENT);
        }
    for (i64 t = 0; t < (i64)ri-6; t++) {
        bb_pixel((u64)cx, (u64)((i64)cy - t), COL_ACCENT);
        bb_pixel((u64)((i64)cx + t), (u64)cy, COL_ACCENT);
    }
    for (i64 d = -(i64)rh; d <= (i64)rh; d++)
        for (i64 e = -(i64)rh; e <= (i64)rh; e++)
            if (d*d + e*e <= (i64)(rh*rh))
                bb_pixel((u64)((i64)cx+e), (u64)((i64)cy+d), COL_FG);
}

static void draw_main_screen(void) {
    bb_clear(COL_BG);
    const u8 *font = (const u8 *)font8x8;

    const char *title = "Depressure Manager";
    u64 tl = 0; while (title[tl]) tl++;
    bb_puts((g_screen_w - tl*CHAR_W)/2, 40, title, COL_ACCENT, COL_BG, font);

    u64 isz = 140, sp = 60;
    u64 tw = isz*2 + sp;
    u64 sx = (g_screen_w - tw)/2;
    u64 iy = (g_screen_h - isz)/2 - 60;

    g_icon_w = isz; g_icon_h = isz;
    g_icon_x = sx;  g_icon_y = iy;
    draw_gear(g_icon_x + isz/2, g_icon_y + isz/2, isz);

    g_timer_icon_w = isz; g_timer_icon_h = isz;
    g_timer_icon_x = sx + isz + sp; g_timer_icon_y = iy;
    draw_clock(g_timer_icon_x + isz/2, g_timer_icon_y + isz/2, isz);

    const char *l1 = "Hardware"; u64 l1l = 0; while (l1[l1l]) l1l++;
    bb_puts(g_icon_x + (isz - l1l*CHAR_W)/2, g_icon_y + isz + 10, l1, COL_FG, COL_BG, font);
    const char *l2 = "Timer"; u64 l2l = 0; while (l2[l2l]) l2l++;
    bb_puts(g_timer_icon_x + (isz - l2l*CHAR_W)/2, g_timer_icon_y + isz + 10, l2, COL_FG, COL_BG, font);

    g_management_w = 240; g_management_h = 44;
    g_management_x = (g_screen_w - g_management_w)/2;
    g_management_y = g_icon_y + isz + 50;
    bb_button(g_management_x, g_management_y, g_management_w, g_management_h,
              "Management", COL_ACCENT, COL_BTN, font);

    const char *ft = "Gear: Hardware | Clock: Timer | M: Management";
    u64 fl = 0; while (ft[fl]) fl++;
    bb_puts((g_screen_w - fl*CHAR_W)/2, g_screen_h - CHAR_H*2, ft, COL_FG, COL_BG, font);
}

static void draw_core_box(u64 x, u64 y, u64 w, u64 h, u32 idx, u32 load) {
    const u8 *font = (const u8 *)font8x8;
    bb_fill_rect(x, y, w, h, COL_CORE_BG);
    bb_fill_rect(x, y, w, 2, COL_ACCENT);
    bb_fill_rect(x, y + h - 2, w, 2, COL_ACCENT);
    bb_fill_rect(x, y, 2, h, COL_ACCENT);
    bb_fill_rect(x + w - 2, y, 2, h, COL_ACCENT);

    char lbl[8] = {'C','O','R','E',' ',  '0' + ((idx/10)%10), '0' + (idx%10), 0};
    bb_puts(x + 8, y + 6, lbl, COL_FG, COL_CORE_BG, font);
    bb_puts(x + 8, y + 26, "ONLINE", COL_CORE_ON, COL_CORE_BG, font);
    char ld[16] = {'L',':'};
    int li = 2; u32 v = load;
    if (v == 0) ld[li++] = '0';
    else { char t[8]; int ti = 0;
        while (v > 0) { t[ti++] = '0' + (v%10); v /= 10; }
        for (int k = ti-1; k >= 0; k--) ld[li++] = t[k]; }
    ld[li++] = '%'; ld[li] = 0;
    bb_puts(x + 8, y + 44, ld, COL_FG, COL_CORE_BG, font);

    u64 bw = w - 16;
    u64 fill = (bw * (load > 100 ? 100 : load))/100;
    bb_fill_rect(x + 8, y + h - 10, bw, 4, COL_CORE_OFF);
    bb_fill_rect(x + 8, y + h - 10, fill, 4, COL_CORE_ON);
}

static void draw_hwmap_screen(void) {
    bb_clear(COL_BG);
    const u8 *font = (const u8 *)font8x8;
    bb_puts(40, 20, "=== HARDWARE MAP ===", COL_ACCENT, COL_BG, font);

    char line[80]; int ci = 0;
    const char *cp = "CPU: ";
    while (*cp) line[ci++] = *cp++;
    cp = g_cpu_name;
    while (*cp) line[ci++] = *cp++;
    line[ci] = 0;
    bb_puts(40, 44, line, COL_ACCENT, COL_BG, font);

    int ni = 0;
    const char *np = "CORES: ";
    while (*np) line[ni++] = *np++;
    u32 v = g_ncpu; char tmp[8]; int ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = '0' + (v%10); v /= 10; }
    for (int k = ti-1; k >= 0; k--) line[ni++] = tmp[k];
    const char *sfx = " (UEFI MP)";
    while (*sfx) line[ni++] = *sfx++;
    line[ni] = 0;
    bb_puts(40, 60, line, COL_FG, COL_BG, font);

    u64 cy0 = 90, cw = 136, ch = 70, cg = 10, rh = ch + cg;
    u32 drawn = 0;
    for (u32 i = 0; i < g_ncpu && i < 64; i++) {
        u32 row = i / CORES_PER_ROW, col = i % CORES_PER_ROW;
        u64 cx = 40 + col*(cw + cg);
        u64 cy = cy0 + row*rh;
        if (cy + ch > g_screen_h - 200) break;
        draw_core_box(cx, cy, cw, ch, i, g_core_load[i]);
        drawn++;
    }

    u64 iy = cy0 + ((drawn + CORES_PER_ROW - 1)/CORES_PER_ROW)*rh + 20;
    bb_puts(40, iy, "RAM:", COL_ACCENT, COL_BG, font);

    u64 tram = (g_api && g_api->get_total_ram) ? g_api->get_total_ram() : 0;
    u64 tmb = tram/(1024*1024), tgb = tmb/1024;
    int ri = 0;
    const char *rp = "  TOTAL:  ";
    while (*rp) line[ri++] = *rp++;
    if (tgb > 0) {
        u64 u = tgb; char t[24]; int ti2 = 0;
        while (u > 0) { t[ti2++] = '0' + (u%10); u /= 10; }
        for (int k = ti2-1; k >= 0; k--) line[ri++] = t[k];
        const char *s = " GB"; while (*s) line[ri++] = *s++;
    } else {
        u64 u = tmb; char t[24]; int ti2 = 0;
        if (u == 0) t[ti2++] = '0';
        while (u > 0) { t[ti2++] = '0' + (u%10); u /= 10; }
        for (int k = ti2-1; k >= 0; k--) line[ri++] = t[k];
        const char *s = " MB"; while (*s) line[ri++] = *s++;
    }
    line[ri] = 0;
    bb_puts(40, iy + 20, line, COL_FG, COL_BG, font);

    u64 sy = iy + 50;
    bb_puts(40, sy, "STORAGE:", COL_ACCENT, COL_BG, font);

    u64 sb = 0; u32 sc = 0;
    if (g_api) {
        if (g_api->get_storage_bytes) sb = g_api->get_storage_bytes();
        if (g_api->get_storage_count) sc = g_api->get_storage_count();
    }
    if (sc == 0) {
        bb_puts(40, sy + 20, "  No storage devices found", COL_FG, COL_BG, font);
    } else {
        bb_puts(40, sy + 20, "  Devices: ", COL_FG, COL_BG, font);
        bb_u64(40 + 11*CHAR_W, sy + 20, sc, COL_FG, COL_BG, font);

        u64 gb = sb/(1024ULL*1024*1024), mb = sb/(1024ULL*1024);
        bb_puts(40, sy + 36, "  Total: ", COL_FG, COL_BG, font);
        u64 ux;
        if (gb > 0) { ux = bb_u64(40 + 9*CHAR_W, sy + 36, gb, COL_FG, COL_BG, font);
                      bb_puts(ux, sy + 36, " GB", COL_FG, COL_BG, font); }
        else        { ux = bb_u64(40 + 9*CHAR_W, sy + 36, mb, COL_FG, COL_BG, font);
                      bb_puts(ux, sy + 36, " MB", COL_FG, COL_BG, font); }

        for (u32 i = 0; i < sc && i < 4; i++) {
            u64 ds = g_api->get_storage_device_size ? g_api->get_storage_device_size(i) : 0;
            u64 dmb = ds/(1024ULL*1024), dgb = dmb/1024;
            u64 y = sy + 56 + i*16;
            bb_puts(40, y, "  [", COL_FG, COL_BG, font);
            bb_u64(40 + 3*CHAR_W, y, i % 10, COL_FG, COL_BG, font);
            bb_puts(40 + 4*CHAR_W, y, "] ", COL_FG, COL_BG, font);
            if (dgb > 0) { ux = bb_u64(40 + 6*CHAR_W, y, dgb, COL_FG, COL_BG, font);
                           bb_puts(ux, y, " GB", COL_FG, COL_BG, font); }
            else         { ux = bb_u64(40 + 6*CHAR_W, y, dmb, COL_FG, COL_BG, font);
                           bb_puts(ux, y, " MB", COL_FG, COL_BG, font); }
        }
    }
    bb_puts(40, g_screen_h - CHAR_H*2,
            "[P] PCI Devices  [T] Timer  [Esc] Back", COL_FG, COL_BG, font);
}

static void draw_cursor(int x, int y) {
    if (x < 0 || y < 0) return;
    bb_fill_rect((u64)x - 1, (u64)y - 8, 3, 17, COL_FG);
    bb_fill_rect((u64)x - 8, (u64)y - 1, 17, 3, COL_FG);
}

static void blit(void) {
    if (!g_dws || !g_dws->blit_scaled) return;
    g_dws->blit_scaled(0, 0, g_screen_w, g_screen_h,
                       g_backbuf, g_backbuf_w, g_backbuf_h);
}

static void redraw_all(void) {
    if      (g_screen == SCREEN_MAIN)       draw_main_screen();
    else if (g_screen == SCREEN_HWMAP)      draw_hwmap_screen();
    else if (g_screen == SCREEN_PCI)        draw_pci_screen();
    else if (g_screen == SCREEN_MANAGEMENT) draw_mgmt_screen();
    else if (g_screen == SCREEN_TIMER)      draw_timer_screen();
    draw_cursor(g_mouse_x, g_mouse_y);
    blit();
    g_dirty = 0;
    g_drawn = 1;
    g_prev_mouse_x = g_mouse_x;
    g_prev_mouse_y = g_mouse_y;
}

static void confirm_power(void) {
    if (g_power_confirmation == POWER_ACTION_SHUTDOWN)
        g_power_action_status = (g_acpi && g_acpi->shutdown)
            ? g_acpi->shutdown() : DEPRESSURE_ACPI_POWER_UNAVAILABLE;
    else if (g_power_confirmation == POWER_ACTION_REBOOT)
        g_power_action_status = (g_acpi && g_acpi->reboot)
            ? g_acpi->reboot() : DEPRESSURE_ACPI_RESET_UNAVAILABLE;
    g_power_confirmation = POWER_ACTION_NONE;
    g_dirty = 1;
}

static void timer_select(u32 src) {
    if (!g_api || !g_api->timer_set_source) return;
    dstatus_t s = g_api->timer_set_source(src);
    if (g_uart) {
        g_uart->puts("[DepMgr] timer -> ");
        g_uart->puts(ts_name(src));
        g_uart->puts(" st=");
        g_uart->put_hex(s);
        g_uart->puts("\r\n");
    }
    g_dirty = 1;
}

static void handle_click(int x, int y) {
    if (g_screen == SCREEN_MAIN) {
        if ((u64)x >= g_management_x && (u64)x < g_management_x + g_management_w &&
            (u64)y >= g_management_y && (u64)y < g_management_y + g_management_h) {
            g_screen = SCREEN_MANAGEMENT; g_dirty = 1;
        } else if ((u64)x >= g_icon_x && (u64)x < g_icon_x + g_icon_w &&
                   (u64)y >= g_icon_y && (u64)y < g_icon_y + g_icon_h) {
            g_screen = SCREEN_HWMAP; g_dirty = 1;
        } else if ((u64)x >= g_timer_icon_x && (u64)x < g_timer_icon_x + g_timer_icon_w &&
                   (u64)y >= g_timer_icon_y && (u64)y < g_timer_icon_y + g_timer_icon_h) {
            g_screen = SCREEN_TIMER; g_dirty = 1;
        }
    } else if (g_screen == SCREEN_TIMER) {
        for (u32 i = 0; i < DTIMER_SOURCE_COUNT; i++)
            if ((u64)x >= g_timer_btn_x[i] && (u64)x < g_timer_btn_x[i] + g_timer_btn_w &&
                (u64)y >= g_timer_btn_y && (u64)y < g_timer_btn_y + g_timer_btn_h) {
                timer_select(i); return;
            }
    } else if (g_screen == SCREEN_MANAGEMENT) {
        if (g_power_confirmation != POWER_ACTION_NONE) {
            if ((u64)x >= g_confirm_x && (u64)x < g_confirm_x + 200 &&
                (u64)y >= g_confirm_y && (u64)y < g_confirm_y + 40) confirm_power();
            else if ((u64)x >= g_cancel_x && (u64)x < g_cancel_x + 200 &&
                     (u64)y >= g_cancel_y && (u64)y < g_cancel_y + 40) {
                g_power_confirmation = POWER_ACTION_NONE; g_dirty = 1;
            }
        } else if ((u64)x >= g_shutdown_x && (u64)x < g_shutdown_x + 260 &&
                   (u64)y >= g_shutdown_y && (u64)y < g_shutdown_y + 40) {
            g_power_confirmation = POWER_ACTION_SHUTDOWN;
            g_power_action_status = DEPRESSURE_STATUS_OK; g_dirty = 1;
        } else if ((u64)x >= g_reboot_x && (u64)x < g_reboot_x + 260 &&
                   (u64)y >= g_reboot_y && (u64)y < g_reboot_y + 40) {
            g_power_confirmation = POWER_ACTION_REBOOT;
            g_power_action_status = DEPRESSURE_STATUS_OK; g_dirty = 1;
        }
    }
}

static void handle_key(char c) {
    if (c == 27) {
        if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation != POWER_ACTION_NONE) {
            g_power_confirmation = POWER_ACTION_NONE; g_dirty = 1;
        } else if (g_screen != SCREEN_MAIN) {
            g_screen = SCREEN_MAIN; g_dirty = 1;
        }
    } else if (g_screen == SCREEN_MAIN && (c == 'm' || c == 'M')) {
        g_screen = SCREEN_MANAGEMENT; g_dirty = 1;
    } else if (g_screen == SCREEN_MAIN && (c == 't' || c == 'T')) {
        g_screen = SCREEN_TIMER; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation != POWER_ACTION_NONE &&
               (c == 'y' || c == 'Y')) {
        confirm_power();
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation != POWER_ACTION_NONE &&
               (c == 'n' || c == 'N')) {
        g_power_confirmation = POWER_ACTION_NONE; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation == POWER_ACTION_NONE &&
               (c == 's' || c == 'S')) {
        g_power_confirmation = POWER_ACTION_SHUTDOWN; g_dirty = 1;
    } else if (g_screen == SCREEN_MANAGEMENT && g_power_confirmation == POWER_ACTION_NONE &&
               (c == 'r' || c == 'R')) {
        g_power_confirmation = POWER_ACTION_REBOOT; g_dirty = 1;
    } else if (g_screen == SCREEN_HWMAP && (c == 'p' || c == 'P')) {
        g_pci_page = 0; g_screen = SCREEN_PCI; g_dirty = 1;
    } else if (g_screen == SCREEN_HWMAP && (c == 't' || c == 'T')) {
        g_screen = SCREEN_TIMER; g_dirty = 1;
    } else if (g_screen == SCREEN_TIMER) {
        if (c == '1') timer_select(DTIMER_SOURCE_PIT);
        else if (c == '2') timer_select(DTIMER_SOURCE_HPET);
        else if (c == '3') timer_select(DTIMER_SOURCE_LAPIC);
    } else if (g_screen == SCREEN_PCI) {
        u32 count = 0, pages = 1;
        if (g_pci && g_pci->get_count && DSTATUS_IS_OK(g_pci->get_count(&count))) {
            pages = (count + PCI_PAGE_SIZE - 1)/PCI_PAGE_SIZE;
            if (pages == 0) pages = 1;
        }
        if (c == 'n' || c == 'N') { if (g_pci_page + 1 < pages) g_pci_page++; g_dirty = 1; }
        else if (c == 'b' || c == 'B') { if (g_pci_page > 0) g_pci_page--; g_dirty = 1; }
        else if (c == 'h' || c == 'H') { g_screen = SCREEN_HWMAP; g_dirty = 1; }
    }
}

void depmgr_poll(void) {
    int mx = g_mouse_x, my = g_mouse_y, clicked = 0;
    if (g_mouse && g_mouse->get_x && g_mouse->get_y && g_mouse->get_buttons) {
        mx = g_mouse->get_x();
        my = g_mouse->get_y();
        int b = g_mouse->get_buttons();
        clicked = (b & 1) && !(g_last_buttons & 1);
        g_mouse_x = mx; g_mouse_y = my; g_last_buttons = b;
        if (mx != g_prev_mouse_x || my != g_prev_mouse_y) g_dirty = 1;
    }

    if (g_api && g_api->smp_get_load) {
        u32 limit = g_ncpu < 64 ? g_ncpu : 64;
        for (u32 i = 0; i < limit; i++) {
            u32 v = g_api->smp_get_load(i);
            if (v != g_prev_core_load[i]) {
                g_prev_core_load[i] = v;
                g_core_load[i] = v;
                g_dirty = 1;
            }
        }
    }

    if (g_dirty) redraw_all();

    if (clicked) handle_click(mx, my);

    if (g_kbd && g_kbd->has_key && g_kbd->has_key())
        handle_key(g_kbd->get_key());

    if (!g_dirty && g_drawn) return;

    if (g_dirty) redraw_all();
}

void *depmgr_main(dkernel_api_t *api, void *fb_info,
                  uart_api_t *uart, dws_api_t *dws, dkbd_api_t *kbd,
                  dmouse_api_t *mouse, int boot_mode, u32 ncpu) {
    (void)boot_mode;
    g_uart = uart; g_dws = dws; g_kbd = kbd; g_api = api; g_mouse = mouse;

    if (g_api && g_api->DFindAndGetDriver) {
        ddriver_t *pd = g_api->DFindAndGetDriver("pci");
        ddriver_t *ad = g_api->DFindAndGetDriver("acpi");
        if (pd && pd->api) g_pci = (pci_api_t *)pd->api;
        if (ad && ad->api) g_acpi = (acpi_api_t *)ad->api;
    }
    if (ncpu > 0) g_ncpu = ncpu;

    if (fb_info) {
        typedef struct { u64 base, width, height, pitch, format, size; } fb_info_t;
        fb_info_t *fb = (fb_info_t *)fb_info;
        g_screen_w = fb->width;
        g_screen_h = fb->height;
    }
    if (g_screen_w > BACKBUF_MAX_W) g_screen_w = BACKBUF_MAX_W;
    if (g_screen_h > BACKBUF_MAX_H) g_screen_h = BACKBUF_MAX_H;
    if (g_screen_w == 0) g_screen_w = 800;
    if (g_screen_h == 0) g_screen_h = 600;

    g_backbuf_w = (u32)g_screen_w;
    g_backbuf_h = (u32)g_screen_h;
    g_backbuf = (u32 *)BACKBUF_ADDR;

    if (!g_dws) {
        if (g_uart) g_uart->puts("[DepMgr] No DWS!\r\n");
        return 0;
    }
    if (g_uart) g_uart->puts("[DepMgr] Starting GUI...\r\n");

    cpu_detect();
    if (g_api && g_api->smp_get_ncpu) {
        u32 n = g_api->smp_get_ncpu();
        if (n > 0) g_ncpu = n;
    }
    if (g_api && g_api->smp_get_load) {
        u32 limit = g_ncpu < 64 ? g_ncpu : 64;
        for (u32 i = 0; i < limit; i++) {
            g_core_load[i] = g_api->smp_get_load(i);
            g_prev_core_load[i] = g_core_load[i];
        }
    }

    redraw_all();
    if (api && api->task_add) api->task_add("DepMgr", depmgr_poll, 1);
    if (g_uart) g_uart->puts("[DepMgr] GUI ready.\r\n");
    return (void *)depmgr_poll;
}