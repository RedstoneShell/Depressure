#include "dtypes.h"
#include "dfont8x8.h"
#include "dkrnl.h"
#include "pci.h"
#include "acpi.h"
#include "hpet.h"

typedef struct uart_api {
    u32 version;
    u32 _pad;
    void (*putc)(char c);
    void (*puts)(const char *str);
    void (*put_hex)(u64 value);
    int  (*has_data)(void);
    char (*getc)(void);
} uart_api_t;

typedef struct dkbd_api {
    u32 version;
    u32 _pad;
    int  (*has_key)(void);
    char (*get_key)(void);
    u8   (*get_scancode)(void);
} dkbd_api_t;

typedef struct dmouse_api {
    u32 version;
    u32 _pad;
    int  (*has_event)(void);
    void (*get_state)(int *x, int *y, int *buttons);
    int  (*get_x)(void);
    int  (*get_y)(void);
    int  (*get_buttons)(void);
    void (*set_position)(int x, int y);
} dmouse_api_t;

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

static uart_api_t  *g_uart  = 0;
static dws_api_t   *g_dws   = 0;
static dkbd_api_t  *g_kbd   = 0;
static dkernel_api_t *g_api = 0;
static dmouse_api_t *g_mouse = 0;
static acpi_api_t *g_acpi = 0;

static u64 g_screen_w = 800;
static u64 g_screen_h = 600;

#define COL_BG      0x00990000
#define COL_FG      0x00FFFFFF
#define COL_ACCENT  0x00FFCC00
#define COL_DARK    0x00660000
#define COL_CORE_BG 0x00CC0000
#define COL_CORE_ON 0x0000FF00
#define COL_CORE_OFF 0x00880000

#define CHAR_W  16
#define CHAR_H  16

#define SCREEN_MAIN   0
#define SCREEN_HWMAP  1
#define SCREEN_PCI    2
#define SCREEN_MANAGEMENT 3
#define PCI_PAGE_SIZE 10
#define POWER_ACTION_NONE 0
#define POWER_ACTION_SHUTDOWN 1
#define POWER_ACTION_REBOOT 2

#define CORES_PER_ROW  5

#define BACKBUF_MAX_W  1024
#define BACKBUF_MAX_H  768

#define BACKBUF_ADDR   0x02000000ULL
#define BACKBUF_SIZE   ((u64)BACKBUF_MAX_W * BACKBUF_MAX_H * sizeof(u32))

static int g_screen = SCREEN_MAIN;
static u32 g_pci_page = 0;
static pci_api_t *g_pci = 0;
static u64 g_management_x = 0;
static u64 g_management_y = 0;
static u64 g_management_w = 0;
static u64 g_management_h = 0;
static u64 g_shutdown_x = 0;
static u64 g_shutdown_y = 0;
static u64 g_reboot_x = 0;
static u64 g_reboot_y = 0;
static u64 g_confirm_x = 0;
static u64 g_confirm_y = 0;
static u64 g_cancel_x = 0;
static u64 g_cancel_y = 0;
static int g_power_confirmation = POWER_ACTION_NONE;
static dstatus_t g_power_action_status = DEPRESSURE_STATUS_OK;

static u64 g_icon_x = 0;
static u64 g_icon_y = 0;
static u64 g_icon_w = 0;
static u64 g_icon_h = 0;

static int g_last_buttons = 0;
static int g_mouse_x = 400;
static int g_mouse_y = 300;

static u32 g_ncpu = 1;
static u32 g_ncpu_cpuid = 1;
static char g_cpu_vendor[13] = {0};
static char g_cpu_name[49] = {0};
static u32 g_core_load[64] = {0};
static u32 *g_backbuf = (u32 *)BACKBUF_ADDR;
static u32 *g_presented_buf = (u32 *)(BACKBUF_ADDR + BACKBUF_SIZE);
static u32 g_backbuf_w = 0;
static u32 g_backbuf_h = 0;
static int g_presented_valid = 0;

static void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

static void cpu_detect(void) {
    u32 eax, ebx, ecx, edx;

    cpuid(0, &eax, &ebx, &ecx, &edx);
    *(u32 *)&g_cpu_vendor[0] = ebx;
    *(u32 *)&g_cpu_vendor[4] = edx;
    *(u32 *)&g_cpu_vendor[8] = ecx;
    g_cpu_vendor[12] = 0;

    cpuid(1, &eax, &ebx, &ecx, &edx);

    u32 family   = (eax >> 8) & 0xF;
    u32 model    = (eax >> 4) & 0xF;
    u32 ncpu     = (ebx >> 16) & 0xFF;

    if (ncpu > 0) g_ncpu_cpuid = ncpu;

    const char *vendor = g_cpu_vendor;
    int i = 0;
    while (vendor[i] && i < 48) {
        g_cpu_name[i] = vendor[i];
        i++;
    }

    g_cpu_name[i++] = ' ';
    g_cpu_name[i++] = 'F';
    g_cpu_name[i++] = '0' + (family % 10);
    g_cpu_name[i++] = 'M';
    g_cpu_name[i++] = '0' + (model % 10);
    g_cpu_name[i] = 0;
}

static void bb_clear(u32 rgb) {
    u32 n = g_backbuf_w * g_backbuf_h;
    for (u32 i = 0; i < n; i++) {
        g_backbuf[i] = rgb;
    }
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
        for (u64 dx = 0; dx < w; dx++) {
            row[dx] = rgb;
        }
    }
}

static void bb_draw_char(u64 x, u64 y, char ch, u32 fg, u32 bg, const u8 *font) {
    if (ch < 32 || ch > 126) ch = '?';
    const u8 *glyph = font + (u8)(ch - 32) * 8;

    for (u64 row = 0; row < 8; row++) {
        u8 bits = glyph[row];
        for (u64 col = 0; col < 8; col++) {
            u32 color = (bits & (1 << col)) ? fg : bg;
            bb_fill_rect(x + col*2, y + row*2, 2, 2, color);
        }
    }
}

static void bb_puts_at(u64 x, u64 y, const char *str, u32 fg, u32 bg, const u8 *font) {
    u64 cx = x;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += CHAR_H;
        } else {
            bb_draw_char(cx, y, *str, fg, bg, font);
            cx += CHAR_W;
        }
        str++;
    }
}

static void bb_put_u32_at(u64 x, u64 y, u32 val, u32 fg, u32 bg, const u8 *font) {
    char buf[16];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    buf[i] = 0;
    for (int k = 0; k < i/2; k++) {
        char t = buf[k];
        buf[k] = buf[i-1-k];
        buf[i-1-k] = t;
    }
    bb_puts_at(x, y, buf, fg, bg, font);
}

static void bb_put_hex16_at(u64 x, u64 y, u16 value,
                            u32 fg, u32 bg, const u8 *font) {
    static const char hex[] = "0123456789ABCDEF";
    char text[5];

    for (u32 i = 0; i < 4; i++) {
        text[i] = hex[(value >> (12 - i * 4)) & 0xF];
    }
    text[4] = 0;
    bb_puts_at(x, y, text, fg, bg, font);
}

static const char *pci_class_label(u8 class_code) {
    switch (class_code) {
        case 0x01: return "STORAGE";
        case 0x02: return "NETWORK";
        case 0x03: return "DISPLAY";
        case 0x04: return "MEDIA";
        case 0x06: return "BRIDGE";
        case 0x07: return "SERIAL";
        case 0x08: return "SYSTEM";
        case 0x09: return "INPUT";
        case 0x0C: return "BUS";
        default:   return "OTHER";
    }
}

static void draw_pci_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    u32 count = 0;
    u32 truncated = 0;
    dstatus_t status;

    bb_clear(COL_BG);
    bb_puts_at(40, 20, "=== PCI DEVICES ===", COL_ACCENT, COL_BG, font);

    if (!g_pci || !g_pci->get_count || !g_pci->get_device) {
        bb_puts_at(40, 64, "PCI driver/API is not available",
                   COL_FG, COL_BG, font);
        bb_puts_at(40, g_screen_h - CHAR_H * 2,
                   "[H] Hardware Map  [Esc] Main",
                   COL_FG, COL_BG, font);
        return;
    }

    status = g_pci->get_count(&count);
    if (DSTATUS_IS_ERR(status)) {
        bb_puts_at(40, 64, "PCI device query failed",
                   COL_FG, COL_BG, font);
        return;
    }

    if (g_pci->get_scan_truncated) {
        status = g_pci->get_scan_truncated(&truncated);
        if (DSTATUS_IS_ERR(status)) truncated = 1;
    }

    bb_puts_at(40, 44, "Found:", COL_FG, COL_BG, font);
    bb_put_u32_at(40 + 7 * CHAR_W, 44, count, COL_FG, COL_BG, font);
    if (truncated) {
        bb_puts_at(40 + 14 * CHAR_W, 44, "SCAN LIMIT REACHED",
                   COL_ACCENT, COL_BG, font);
    }

    u32 page_count = (count + PCI_PAGE_SIZE - 1) / PCI_PAGE_SIZE;
    if (page_count == 0) page_count = 1;
    if (g_pci_page >= page_count) g_pci_page = page_count - 1;

    bb_puts_at(40, 64, "BDF      Vendor:Device  Class",
               COL_ACCENT, COL_BG, font);

    u32 first = g_pci_page * PCI_PAGE_SIZE;
    u32 limit = first + PCI_PAGE_SIZE;
    if (limit > count) limit = count;

    for (u32 index = first; index < limit; index++) {
        const pci_device_t *device = 0;
        u64 y = 88 + (index - first) * 44;

        status = g_pci->get_device(index, &device);
        if (DSTATUS_IS_ERR(status) || !device) {
            bb_puts_at(40, y, "PCI entry read failed",
                       COL_ACCENT, COL_BG, font);
            continue;
        }

        bb_puts_at(40, y, "B", COL_FG, COL_BG, font);
        bb_put_u32_at(40 + CHAR_W, y, device->bus, COL_FG, COL_BG, font);
        bb_puts_at(40 + 4 * CHAR_W, y, "S", COL_FG, COL_BG, font);
        bb_put_u32_at(40 + 5 * CHAR_W, y, device->slot, COL_FG, COL_BG, font);
        bb_puts_at(40 + 8 * CHAR_W, y, "F", COL_FG, COL_BG, font);
        bb_put_u32_at(40 + 9 * CHAR_W, y, device->func, COL_FG, COL_BG, font);

        bb_put_hex16_at(40 + 12 * CHAR_W, y, device->vendor_id,
                        COL_FG, COL_BG, font);
        bb_puts_at(40 + 16 * CHAR_W, y, ":", COL_FG, COL_BG, font);
        bb_put_hex16_at(40 + 17 * CHAR_W, y, device->device_id,
                        COL_FG, COL_BG, font);
        bb_puts_at(40 + 22 * CHAR_W, y,
                   pci_class_label(device->class_code),
                   COL_FG, COL_BG, font);

        if (device->vendor_id == 0x8086 && device->device_id == 0x100E) {
            ddriver_t *network_driver = g_api->DFindAndGetDriver ?
                g_api->DFindAndGetDriver("de1000") : 0;
            bb_puts_at(56, y + 14,
                       network_driver ? "DRIVER: DE1000 ACTIVE" :
                                        "DRIVER: DE1000 NOT LOADED",
                       network_driver ? COL_CORE_ON : COL_ACCENT,
                       COL_BG, font);
        } else if (device->vendor_id == 0x8086 &&
                   device->device_id == 0x7113) {
            ddriver_t *acpi_driver = g_api && g_api->DFindAndGetDriver ?
                g_api->DFindAndGetDriver("acpi") : 0;
            acpi_system_info_t acpi_info;
            int has_info = 0;
            if (acpi_driver && acpi_driver->api) {
                g_acpi = (acpi_api_t *)acpi_driver->api;
                has_info = g_acpi->version == ACPI_API_VERSION &&
                    g_acpi->get_info &&
                    DSTATUS_IS_OK(g_acpi->get_info(&acpi_info));
            }
            bb_puts_at(56, y + 14,
                       has_info ? "DRIVER: PIIX4 ACPI ACTIVE" :
                                  "DRIVER: PIIX4 ACPI NOT READY",
                       has_info ? COL_CORE_ON : COL_ACCENT,
                       COL_BG, font);
            if (has_info) {
                bb_puts_at(56, y + 36,
                           acpi_info.root_is_xsdt ? "ACPI: XSDT" :
                                                    "ACPI: RSDT",
                           COL_FG, COL_BG, font);
                bb_puts_at(56 + 10 * CHAR_W, y + 36, "TABLES:",
                           COL_FG, COL_BG, font);
                bb_put_u32_at(56 + 17 * CHAR_W, y + 36,
                              acpi_info.table_count, COL_FG, COL_BG, font);
                if (acpi_info.madt_address) {
                    bb_puts_at(56 + 20 * CHAR_W, y + 36, "MADT",
                               COL_FG, COL_BG, font);
                }
            }
        } else {
            bb_puts_at(56, y + 14, "DRIVER: NOT BOUND",
                       COL_ACCENT, COL_BG, font);
        }

        bb_puts_at(56, y + 28,
                   (device->command & 0x1) ? "IO:ON" : "IO:OFF",
                   COL_FG, COL_BG, font);
        bb_puts_at(40 + 10 * CHAR_W, y + 28,
                   (device->command & 0x2) ? "MEM:ON" : "MEM:OFF",
                   COL_FG, COL_BG, font);
        bb_puts_at(40 + 22 * CHAR_W, y + 28,
                   (device->command & 0x4) ? "MASTER:ON" : "MASTER:OFF",
                   COL_FG, COL_BG, font);
    }

    bb_puts_at(40, g_screen_h - CHAR_H * 2,
               "[N] Next  [B] Previous  [H] Hardware  [Esc] Main",
               COL_FG, COL_BG, font);
}

static u64 bb_put_u64_dec_at(u64 x, u64 y, u64 val, u32 fg, u32 bg, const u8 *font) {
    char buf[24];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    buf[i] = 0;
    for (int k = 0; k < i/2; k++) {
        char t = buf[k];
        buf[k] = buf[i-1-k];
        buf[i-1-k] = t;
    }
    bb_puts_at(x, y, buf, fg, bg, font);
    return x + (u64)i * CHAR_W;
}

static u64 bb_put_hex64_at(u64 x, u64 y, u64 value,
                           u32 fg, u32 bg, const u8 *font) {
    static const char hex[] = "0123456789ABCDEF";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (u32 i = 0; i < 16; i++) {
        text[2 + i] = hex[(value >> (60 - i * 4)) & 0xF];
    }
    text[18] = 0;
    bb_puts_at(x, y, text, fg, bg, font);
    return x + 18 * CHAR_W;
}

static const char *hpet_status_name(dstatus_t status) {
    switch (status) {
        case DEPRESSURE_STATUS_OK: return "ENABLED";
        case DEPRESSURE_HPET_NOT_FOUND: return "NOT FOUND";
        case DEPRESSURE_HPET_NOT_SUPPORTED: return "NOT SUPPORTED";
        case DEPRESSURE_HPET_INVALID_TABLE: return "INVALID TABLE";
        case DEPRESSURE_HPET_INVALID_ADDRESS: return "INVALID ADDRESS";
        case DEPRESSURE_HPET_INVALID_PERIOD: return "INVALID PERIOD";
        case DEPRESSURE_HPET_ENABLE_FAILED: return "ENABLE FAILED";
        case DEPRESSURE_HPET_NOT_INITIALIZED: return "NOT INITIALIZED";
        case DEPRESSURE_NOT_FOUND: return "ACPI NOT AVAILABLE";
        case DEPRESSURE_CHECKSUM_ERROR: return "ACPI CHECKSUM ERROR";
        case DEPRESSURE_CORRUPTED_DATA: return "ACPI DATA CORRUPTED";
        default: return "INITIALIZATION ERROR";
    }
}

static void bb_put_hex32_at(u64 x, u64 y, u32 value,
                            u32 fg, u32 bg, const u8 *font) {
    static const char hex[] = "0123456789ABCDEF";
    char text[11];
    text[0] = '0';
    text[1] = 'x';
    for (u32 i = 0; i < 8; i++) {
        text[2 + i] = hex[(value >> (28 - i * 4)) & 0xF];
    }
    text[10] = 0;
    bb_puts_at(x, y, text, fg, bg, font);
}

static const char *fadt_status_name(dstatus_t status) {
    switch (status) {
        case DEPRESSURE_STATUS_OK: return "READY";
        case DEPRESSURE_ACPI_FADT_NOT_FOUND: return "FADT NOT FOUND";
        case DEPRESSURE_ACPI_FADT_INVALID: return "FADT INVALID";
        case DEPRESSURE_ACPI_S5_NOT_FOUND: return "S5 NOT FOUND";
        case DEPRESSURE_ACPI_POWER_UNAVAILABLE: return "POWER UNAVAILABLE";
        case DEPRESSURE_ACPI_RESET_UNAVAILABLE: return "RESET UNAVAILABLE";
        default: return "ERROR";
    }
}

static void draw_management_button(u64 x, u64 y, u64 width,
                                   const char *label, u32 color,
                                   const u8 *font) {
    bb_fill_rect(x, y, width, 40, COL_DARK);
    bb_fill_rect(x, y, width, 2, color);
    bb_fill_rect(x, y + 38, width, 2, color);
    bb_fill_rect(x, y, 2, 40, color);
    bb_fill_rect(x + width - 2, y, 2, 40, color);
    bb_puts_at(x + 24, y + 12, label, COL_FG, COL_DARK, font);
}

static void draw_management_screen(void) {
    const u8 *font = (const u8 *)font8x8;
    const volatile hpet_system_info_t *hpet =
        g_api ? g_api->hpet_info : 0;
    acpi_system_info_t acpi_info;
    int acpi_info_valid = 0;
    u64 y = 84;

    bb_clear(COL_BG);
    bb_puts_at(40, 20, "=== MANAGEMENT ===", COL_ACCENT, COL_BG, font);
    bb_puts_at(40, 56, "HPET STATUS", COL_ACCENT, COL_BG, font);

    if (g_acpi && g_acpi->version == ACPI_API_VERSION &&
        g_acpi->get_info &&
        DSTATUS_IS_OK(g_acpi->get_info(&acpi_info))) {
        acpi_info_valid = 1;
    }

    if (!hpet) {
        bb_puts_at(56, y, "HPET state unavailable", COL_ACCENT, COL_BG, font);
    } else {
        bb_puts_at(56, y, "State:", COL_FG, COL_BG, font);
        bb_puts_at(56 + 8 * CHAR_W, y, hpet_status_name(hpet->status),
                   hpet->enabled ? COL_CORE_ON : COL_ACCENT, COL_BG, font);
        bb_puts_at(360, y, "MMIO:", COL_FG, COL_BG, font);
        bb_put_hex64_at(360 + 7 * CHAR_W, y, hpet->mmio_base,
                        COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "Frequency:", COL_FG, COL_BG, font);
        bb_put_u64_dec_at(56 + 12 * CHAR_W, y, hpet->frequency_hz,
                          COL_FG, COL_BG, font);
        bb_puts_at(56 + 23 * CHAR_W, y, "Hz", COL_FG, COL_BG, font);
        bb_puts_at(360, y, "Counter:", COL_FG, COL_BG, font);
        bb_put_hex64_at(360 + 10 * CHAR_W, y, hpet->counter_value,
                        COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "Timers:", COL_FG, COL_BG, font);
        bb_put_u32_at(56 + 9 * CHAR_W, y, hpet->timer_count,
                      COL_FG, COL_BG, font);
        bb_puts_at(240, y, "Vendor:", COL_FG, COL_BG, font);
        bb_put_hex16_at(240 + 8 * CHAR_W, y, (u16)hpet->vendor_id,
                        COL_FG, COL_BG, font);
        bb_puts_at(360, y, "Period fs:", COL_FG, COL_BG, font);
        bb_put_u64_dec_at(360 + 12 * CHAR_W, y,
                          hpet->period_femtoseconds, COL_FG, COL_BG, font);
    }

    y += 24;
    bb_puts_at(40, y, "FACP / LEGACY HARDWARE", COL_ACCENT, COL_BG, font);
    y += 22;
    if (!acpi_info_valid) {
        bb_puts_at(56, y, "ACPI driver data unavailable",
                   COL_ACCENT, COL_BG, font);
    } else {
        bb_puts_at(56, y, "FADT:", COL_FG, COL_BG, font);
        bb_puts_at(56 + 7 * CHAR_W, y,
                   fadt_status_name(acpi_info.fadt_status),
                   DSTATUS_IS_OK(acpi_info.fadt_status) ?
                       COL_CORE_ON : COL_ACCENT,
                   COL_BG, font);
        bb_puts_at(360, y, "SCI:", COL_FG, COL_BG, font);
        bb_put_u32_at(360 + 5 * CHAR_W, y, acpi_info.sci_interrupt,
                      COL_FG, COL_BG, font);
        bb_puts_at(480, y, "SCI_EN:", COL_FG, COL_BG, font);
        bb_puts_at(480 + 9 * CHAR_W, y,
                   acpi_info.acpi_enabled ? "ON" : "OFF",
                   acpi_info.acpi_enabled ? COL_CORE_ON : COL_ACCENT,
                   COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "PM1 CNT A:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 12 * CHAR_W, y,
                        (u16)acpi_info.pm1a_control_port, COL_FG, COL_BG, font);
        bb_puts_at(280, y, "B:", COL_FG, COL_BG, font);
        bb_put_hex16_at(280 + 3 * CHAR_W, y,
                        (u16)acpi_info.pm1b_control_port, COL_FG, COL_BG, font);
        bb_puts_at(440, y, "len:", COL_FG, COL_BG, font);
        bb_put_u32_at(440 + 5 * CHAR_W, y,
                      acpi_info.pm1_control_length, COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "CNT VAL A:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 12 * CHAR_W, y,
                        acpi_info.pm1a_control_value, COL_FG, COL_BG, font);
        bb_puts_at(280, y, "B:", COL_FG, COL_BG, font);
        bb_put_hex16_at(280 + 3 * CHAR_W, y,
                        acpi_info.pm1b_control_value, COL_FG, COL_BG, font);
        bb_puts_at(440, y, "SLP_EN:", COL_FG, COL_BG, font);
        bb_puts_at(440 + 9 * CHAR_W, y,
                   (acpi_info.pm1a_control_value & 0x2000) ? "ON" : "OFF",
                   COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "PM1 EVT A:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 12 * CHAR_W, y,
                        (u16)acpi_info.pm1a_event_port, COL_FG, COL_BG, font);
        bb_puts_at(280, y, "B:", COL_FG, COL_BG, font);
        bb_put_hex16_at(280 + 3 * CHAR_W, y,
                        (u16)acpi_info.pm1b_event_port, COL_FG, COL_BG, font);
        bb_puts_at(440, y, "len:", COL_FG, COL_BG, font);
        bb_put_u32_at(440 + 5 * CHAR_W, y,
                      acpi_info.pm1_event_length, COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "EVT STAT A:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 12 * CHAR_W, y,
                        acpi_info.pm1a_event_status, COL_FG, COL_BG, font);
        bb_puts_at(280, y, "B:", COL_FG, COL_BG, font);
        bb_put_hex16_at(280 + 3 * CHAR_W, y,
                        acpi_info.pm1b_event_status, COL_FG, COL_BG, font);
        bb_puts_at(440, y, "EVT EN A:", COL_FG, COL_BG, font);
        bb_put_hex16_at(440 + 11 * CHAR_W, y,
                        acpi_info.pm1a_event_enable, COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "EVT EN B:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 11 * CHAR_W, y,
                        acpi_info.pm1b_event_enable, COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "PM2:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 6 * CHAR_W, y,
                        (u16)acpi_info.pm2_control_port, COL_FG, COL_BG, font);
        bb_puts_at(200, y, "PM TMR:", COL_FG, COL_BG, font);
        bb_put_hex16_at(200 + 9 * CHAR_W, y,
                        (u16)acpi_info.pm_timer_port, COL_FG, COL_BG, font);
        bb_puts_at(440, y, "GPE0:", COL_FG, COL_BG, font);
        bb_put_hex16_at(440 + 7 * CHAR_W, y,
                        (u16)acpi_info.gpe0_port, COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "PM2 VALUE:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 12 * CHAR_W, y,
                        acpi_info.pm2_control_value, COL_FG, COL_BG, font);
        bb_puts_at(240, y, "PM TMR NOW:", COL_FG, COL_BG, font);
        bb_put_hex32_at(240 + 13 * CHAR_W, y,
                        acpi_info.fadt_pm_timer_value, COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "GPE1:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 7 * CHAR_W, y,
                        (u16)acpi_info.gpe1_port, COL_FG, COL_BG, font);
        bb_puts_at(240, y, "SMI CMD:", COL_FG, COL_BG, font);
        bb_put_hex16_at(240 + 10 * CHAR_W, y,
                        (u16)acpi_info.smi_command_port, COL_FG, COL_BG, font);
        bb_puts_at(440, y, "ACPI EN/DIS:", COL_FG, COL_BG, font);
        bb_put_hex16_at(440 + 13 * CHAR_W, y,
                        (u16)(((u16)acpi_info.acpi_enable_value << 8) |
                              acpi_info.acpi_disable_value),
                        COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "GPE0 STAT/EN:", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 14 * CHAR_W, y, acpi_info.gpe0_status,
                        COL_FG, COL_BG, font);
        bb_puts_at(56 + 19 * CHAR_W, y, "/", COL_FG, COL_BG, font);
        bb_put_hex16_at(56 + 21 * CHAR_W, y, acpi_info.gpe0_enable,
                        COL_FG, COL_BG, font);
        bb_puts_at(400, y, "GPE1:", COL_FG, COL_BG, font);
        bb_put_hex16_at(400 + 7 * CHAR_W, y, acpi_info.gpe1_status,
                        COL_FG, COL_BG, font);
        bb_puts_at(400 + 12 * CHAR_W, y, "/", COL_FG, COL_BG, font);
        bb_put_hex16_at(400 + 14 * CHAR_W, y, acpi_info.gpe1_enable,
                        COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "Legacy:", COL_FG, COL_BG, font);
        bb_puts_at(56 + 8 * CHAR_W, y,
                   (acpi_info.boot_architecture_flags & 1) ? "YES" : "NO",
                   COL_FG, COL_BG, font);
        bb_puts_at(160, y, "8042:", COL_FG, COL_BG, font);
        bb_puts_at(160 + 6 * CHAR_W, y,
                   (acpi_info.boot_architecture_flags & 2) ? "YES" : "NO",
                   COL_FG, COL_BG, font);
        bb_puts_at(280, y, "VGA absent:", COL_FG, COL_BG, font);
        bb_puts_at(280 + 13 * CHAR_W, y,
                   (acpi_info.boot_architecture_flags & 4) ? "YES" : "NO",
                   COL_FG, COL_BG, font);
        bb_puts_at(440, y, "FADT flags:", COL_FG, COL_BG, font);
        bb_put_hex32_at(440 + 12 * CHAR_W, y, acpi_info.fadt_flags,
                        COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "Reset GAS:", COL_FG, COL_BG, font);
        bb_puts_at(56 + 12 * CHAR_W, y,
                   acpi_info.reset_address_space == 1 ? "IO" :
                   acpi_info.reset_address_space == 0 ? "MEM" : "OTHER",
                   COL_FG, COL_BG, font);
        bb_put_hex64_at(56 + 16 * CHAR_W, y,
                        acpi_info.reset_register_address, COL_FG, COL_BG, font);
        bb_puts_at(600, y, "value:", COL_FG, COL_BG, font);
        bb_put_hex16_at(600 + 7 * CHAR_W, y, acpi_info.reset_value,
                        COL_FG, COL_BG, font);
        y += 20;

        bb_puts_at(56, y, "S5:", COL_FG, COL_BG, font);
        bb_puts_at(56 + 5 * CHAR_W, y,
                   acpi_info.s5_available ? "AVAILABLE" : "UNAVAILABLE",
                   acpi_info.s5_available ? COL_CORE_ON : COL_ACCENT,
                   COL_BG, font);
        bb_puts_at(240, y, "Sleep types A/B:", COL_FG, COL_BG, font);
        bb_put_u32_at(240 + 17 * CHAR_W, y, acpi_info.s5_type_a,
                      COL_FG, COL_BG, font);
        bb_puts_at(240 + 20 * CHAR_W, y, "/", COL_FG, COL_BG, font);
        bb_put_u32_at(240 + 22 * CHAR_W, y, acpi_info.s5_type_b,
                      COL_FG, COL_BG, font);
    }

    g_shutdown_x = 80;
    g_reboot_x = 420;
    g_shutdown_y = g_screen_h - 100;
    g_reboot_y = g_shutdown_y;
    draw_management_button(g_shutdown_x, g_shutdown_y, 260,
                           "Shutdown", COL_ACCENT, font);
    draw_management_button(g_reboot_x, g_reboot_y, 260,
                           "Reboot", COL_ACCENT, font);

    if (g_power_confirmation != POWER_ACTION_NONE) {
        const char *prompt = g_power_confirmation == POWER_ACTION_SHUTDOWN ?
            "Confirm shutdown? Y/N" : "Confirm reboot? Y/N";
        u64 modal_y = g_shutdown_y - 82;
        bb_fill_rect(120, modal_y, 560, 72, COL_DARK);
        bb_puts_at(144, modal_y + 6, prompt, COL_ACCENT, COL_DARK, font);
        g_confirm_x = 144;
        g_confirm_y = modal_y + 30;
        g_cancel_x = 424;
        g_cancel_y = g_confirm_y;
        draw_management_button(g_confirm_x, g_confirm_y, 200,
                               "Confirm", COL_ACCENT, font);
        draw_management_button(g_cancel_x, g_cancel_y, 200,
                               "Cancel", COL_ACCENT, font);
    } else if (DSTATUS_IS_ERR(g_power_action_status)) {
        bb_puts_at(40, g_shutdown_y - 24,
                   g_power_action_status == DEPRESSURE_TIMEOUT ?
                       "Power command issued; system remains running" :
                   g_power_action_status == DEPRESSURE_ACPI_RESET_UNAVAILABLE ?
                       "ACPI reset register unavailable" :
                       "ACPI power action unavailable",
                   COL_ACCENT, COL_BG, font);
    }

    bb_puts_at(40, g_screen_h - CHAR_H * 2,
               "[S] Shutdown  [R] Reboot  [Esc] Main Menu",
               COL_FG, COL_BG, font);
}

static void draw_gear_icon(u64 cx, u64 cy, u64 size) {
    u64 r_outer = size / 2;
    u64 r_inner = r_outer * 6 / 10;
    u64 r_hole  = r_outer * 3 / 10;

    for (int i = 0; i < 8; i++) {
        i64 ax = 0, ay = 0;
        switch (i) {
            case 0: ax = 0;  ay = -1; break;
            case 1: ax = 1;  ay = -1; break;
            case 2: ax = 1;  ay = 0;  break;
            case 3: ax = 1;  ay = 1;  break;
            case 4: ax = 0;  ay = 1;  break;
            case 5: ax = -1; ay = 1;  break;
            case 6: ax = -1; ay = 0;  break;
            case 7: ax = -1; ay = -1; break;
        }

        i64 tx = (i64)cx + ax * (i64)(r_inner + (r_outer - r_inner) / 2);
        i64 ty = (i64)cy + ay * (i64)(r_inner + (r_outer - r_inner) / 2);

        u64 tw = size / 5;
        u64 th = size / 5;

        bb_fill_rect((u64)(tx - (i64)tw / 2),
                     (u64)(ty - (i64)th / 2),
                     tw, th, COL_ACCENT);
    }

    for (i64 dy = -(i64)r_outer; dy <= (i64)r_outer; dy++) {
        for (i64 dx = -(i64)r_outer; dx <= (i64)r_outer; dx++) {
            i64 d2 = dx*dx + dy*dy;
            if (d2 <= (i64)(r_outer * r_outer) &&
                d2 >= (i64)(r_inner * r_inner)) {
                bb_pixel((u64)((i64)cx + dx), (u64)((i64)cy + dy), COL_ACCENT);
            }
        }
    }

    for (i64 dy = -(i64)r_hole; dy <= (i64)r_hole; dy++) {
        for (i64 dx = -(i64)r_hole; dx <= (i64)r_hole; dx++) {
            if (dx*dx + dy*dy <= (i64)(r_hole * r_hole)) {
                bb_pixel((u64)((i64)cx + dx), (u64)((i64)cy + dy), COL_BG);
            }
        }
    }
}

static void draw_main_screen(void) {
    bb_clear(COL_BG);

    const char *title = "Depressure Manager";
    u64 tl = 0;
    while (title[tl]) tl++;

    u64 tx = (g_screen_w - tl * CHAR_W) / 2;
    bb_puts_at(tx, 40, title, COL_ACCENT, COL_BG, (const u8 *)font8x8);

    u64 icon_size = 160;
    g_icon_w = icon_size;
    g_icon_h = icon_size;
    g_icon_x = (g_screen_w - icon_size) / 2;
    g_icon_y = (g_screen_h - icon_size) / 2 - 40;

    draw_gear_icon(g_icon_x + icon_size/2, g_icon_y + icon_size/2, icon_size);

    const char *label = "Hardware Map";
    u64 ll = 0;
    while (label[ll]) ll++;

    u64 lx = (g_screen_w - ll * CHAR_W) / 2;
    u64 ly = g_icon_y + icon_size + 20;

    bb_puts_at(lx, ly, label, COL_FG, COL_BG, (const u8 *)font8x8);

    g_management_w = 240;
    g_management_h = 44;
    g_management_x = (g_screen_w - g_management_w) / 2;
    g_management_y = ly + 40;
    bb_fill_rect(g_management_x, g_management_y,
                 g_management_w, g_management_h, COL_DARK);
    bb_fill_rect(g_management_x, g_management_y,
                 g_management_w, 2, COL_ACCENT);
    bb_fill_rect(g_management_x, g_management_y + g_management_h - 2,
                 g_management_w, 2, COL_ACCENT);
    bb_fill_rect(g_management_x, g_management_y, 2,
                 g_management_h, COL_ACCENT);
    bb_fill_rect(g_management_x + g_management_w - 2, g_management_y,
                 2, g_management_h, COL_ACCENT);
    bb_puts_at(g_management_x + 40, g_management_y + 14,
               "Management", COL_FG, COL_DARK, (const u8 *)font8x8);

    const char *footer = "Gear: Hardware Map | M: Management";
    u64 fl = 0;
    while (footer[fl]) fl++;

    u64 fx = (g_screen_w - fl * CHAR_W) / 2;
    u64 fy = g_screen_h - CHAR_H * 2;

    bb_puts_at(fx, fy, footer, COL_FG, COL_BG, (const u8 *)font8x8);
}

static void bb_put_load(u64 x, u64 y, u32 load, u32 fg, u32 bg, const u8 *font) {
    char load_line[32];
    int li = 0;
    const char *lp = "L:";
    while (*lp) load_line[li++] = *lp++;

    u32 v = load;
    if (v == 0) {
        load_line[li++] = '0';
    } else {
        char tmp[16];
        int ti = 0;
        while (v > 0) {
            tmp[ti++] = '0' + (v % 10);
            v /= 10;
        }
        for (int k = ti - 1; k >= 0; k--) {
            load_line[li++] = tmp[k];
        }
    }
    load_line[li++] = '%';
    load_line[li] = 0;

    bb_puts_at(x, y, load_line, fg, bg, font);
}

static void draw_core_box(u64 x, u64 y, u64 w, u64 h, u32 idx, u32 load) {
    bb_fill_rect(x, y, w, h, COL_CORE_BG);

    bb_fill_rect(x, y, w, 2, COL_ACCENT);
    bb_fill_rect(x, y + h - 2, w, 2, COL_ACCENT);
    bb_fill_rect(x, y, 2, h, COL_ACCENT);
    bb_fill_rect(x + w - 2, y, 2, h, COL_ACCENT);

    char label[16];
    label[0] = 'C';
    label[1] = 'O';
    label[2] = 'R';
    label[3] = 'E';
    label[4] = ' ';
    label[5] = '0' + ((idx / 10) % 10);
    label[6] = '0' + (idx % 10);
    label[7] = 0;

    bb_puts_at(x + 8, y + 6, label, COL_FG, COL_CORE_BG, (const u8 *)font8x8);

    bb_puts_at(x + 8, y + 26, "ONLINE", COL_CORE_ON, COL_CORE_BG, (const u8 *)font8x8);

    bb_put_load(x + 8, y + 44, load, COL_FG, COL_CORE_BG, (const u8 *)font8x8);

    u64 bar_w = w - 16;
    u64 fill = (bar_w * (load > 100 ? 100 : load)) / 100;
    bb_fill_rect(x + 8, y + h - 10, bar_w, 4, COL_CORE_OFF);
    bb_fill_rect(x + 8, y + h - 10, fill, 4, COL_CORE_ON);
}

static void draw_hwmap_screen(void) {
    bb_clear(COL_BG);

    const u8 *font = (const u8 *)font8x8;

    bb_puts_at(40, 20, "=== HARDWARE MAP ===", COL_ACCENT, COL_BG, font);

    {
        char cpu_line[80];
        int ci = 0;
        const char *cp = "CPU: ";
        while (*cp) cpu_line[ci++] = *cp++;
        cp = g_cpu_name;
        while (*cp) cpu_line[ci++] = *cp++;
        cpu_line[ci] = 0;
        bb_puts_at(40, 44, cpu_line, COL_ACCENT, COL_BG, font);
    }

    {
        char n_line[64];
        int ni = 0;
        const char *np = "CORES: ";
        while (*np) n_line[ni++] = *np++;
        u32 v = g_ncpu;
        char tmp[8];
        int ti = 0;
        if (v == 0) tmp[ti++] = '0';
        while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
        for (int k = ti - 1; k >= 0; k--) n_line[ni++] = tmp[k];
        const char *suffix = " (UEFI MP)";
        while (*suffix) n_line[ni++] = *suffix++;
        n_line[ni] = 0;
        bb_puts_at(40, 60, n_line, COL_FG, COL_BG, font);
    }

    u64 core_y = 90;
    u64 core_w = 136;
    u64 core_h = 70;
    u64 core_gap = 10;
    u64 row_h = core_h + core_gap;

    u32 drawn = 0;
    for (u32 i = 0; i < g_ncpu && i < 64; i++) {
        u32 row = i / CORES_PER_ROW;
        u32 col = i % CORES_PER_ROW;

        u64 core_x = 40 + col * (core_w + core_gap);
        u64 cy = core_y + row * row_h;

        if (cy + core_h > g_screen_h - 200) break;

        draw_core_box(core_x, cy, core_w, core_h, i, g_core_load[i]);
        drawn++;
    }

    u64 info_y = core_y + ((drawn + CORES_PER_ROW - 1) / CORES_PER_ROW) * row_h + 20;

    bb_puts_at(40, info_y, "RAM:", COL_ACCENT, COL_BG, font);

    u64 total_ram = 0;
    if (g_api && g_api->get_total_ram) {
        total_ram = g_api->get_total_ram();
    }

    u64 total_mb = total_ram / (1024 * 1024);
    u64 total_gb = total_mb / 1024;

    {
        char ram_line[64];
        int ri = 0;
        const char *rp = "  TOTAL:  ";
        while (*rp) ram_line[ri++] = *rp++;

        if (total_gb > 0) {
            u64 v = total_gb;
            char tmp[24];
            int ti = 0;
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            for (int k = ti - 1; k >= 0; k--) ram_line[ri++] = tmp[k];
            const char *suffix = " GB";
            while (*suffix) ram_line[ri++] = *suffix++;
        } else {
            u64 v = total_mb;
            char tmp[24];
            int ti = 0;
            if (v == 0) tmp[ti++] = '0';
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            for (int k = ti - 1; k >= 0; k--) ram_line[ri++] = tmp[k];
            const char *suffix = " MB";
            while (*suffix) ram_line[ri++] = *suffix++;
        }
        ram_line[ri] = 0;

        bb_puts_at(40, info_y + 20, ram_line, COL_FG, COL_BG, font);
    }

    u64 stor_y = info_y + 50;
    bb_puts_at(40, stor_y, "STORAGE:", COL_ACCENT, COL_BG, font);

    u64 storage_bytes = 0;
    u32 storage_count = 0;

    if (g_api) {
        if (g_api->get_storage_bytes) storage_bytes = g_api->get_storage_bytes();
        if (g_api->get_storage_count) storage_count = g_api->get_storage_count();
    }

    if (storage_count == 0) {
        bb_puts_at(40, stor_y + 20, "  No storage devices found", COL_FG, COL_BG, font);
    } else {
        bb_puts_at(40, stor_y + 20, "  Devices: ", COL_FG, COL_BG, font);
        bb_put_u32_at(40 + 11 * CHAR_W, stor_y + 20, storage_count, COL_FG, COL_BG, font);

        u64 total_gb_st = storage_bytes / (1024ULL * 1024ULL * 1024ULL);
        u64 total_mb_st = storage_bytes / (1024ULL * 1024ULL);

        bb_puts_at(40, stor_y + 36, "  Total: ", COL_FG, COL_BG, font);
        u64 unit_x;
        if (total_gb_st > 0) {
            unit_x = bb_put_u64_dec_at(40 + 9 * CHAR_W, stor_y + 36, total_gb_st, COL_FG, COL_BG, font);
            bb_puts_at(unit_x, stor_y + 36, " GB", COL_FG, COL_BG, font);
        } else {
            unit_x = bb_put_u64_dec_at(40 + 9 * CHAR_W, stor_y + 36, total_mb_st, COL_FG, COL_BG, font);
            bb_puts_at(unit_x, stor_y + 36, " MB", COL_FG, COL_BG, font);
        }

        for (u32 i = 0; i < storage_count && i < 4; i++) {
            u64 dev_size = 0;
            if (g_api->get_storage_device_size) {
                dev_size = g_api->get_storage_device_size(i);
            }

            u64 dev_mb = dev_size / (1024ULL * 1024ULL);
            u64 dev_gb = dev_mb / 1024;

            u64 y = stor_y + 56 + i * 16;
            bb_puts_at(40, y, "  [", COL_FG, COL_BG, font);
            bb_put_u32_at(40 + 3 * CHAR_W, y, i % 10, COL_FG, COL_BG, font);
            bb_puts_at(40 + 4 * CHAR_W, y, "] ", COL_FG, COL_BG, font);

            if (dev_gb > 0) {
                unit_x = bb_put_u64_dec_at(40 + 6 * CHAR_W, y, dev_gb, COL_FG, COL_BG, font);
                bb_puts_at(unit_x, y, " GB", COL_FG, COL_BG, font);
            } else {
                unit_x = bb_put_u64_dec_at(40 + 6 * CHAR_W, y, dev_mb, COL_FG, COL_BG, font);
                bb_puts_at(unit_x, y, " MB", COL_FG, COL_BG, font);
            }
        }
    }

    bb_puts_at(40, g_screen_h - CHAR_H * 2,
               "[P] PCI Devices  [Esc] Back", COL_FG, COL_BG, font);
}

static void draw_cursor_bb(int x, int y) {
    if (x < 0 || y < 0) return;

    bb_fill_rect((u64)x - 1, (u64)y - 8, 3, 17, COL_FG);
    bb_fill_rect((u64)x - 8, (u64)y - 1, 17, 3, COL_FG);
}

static void blit_backbuffer(void) {
    u32 pixel_count = g_backbuf_w * g_backbuf_h;
    int changed = !g_presented_valid;

    if (!g_dws || !g_dws->blit_scaled) return;

    if (!changed) {
        for (u32 i = 0; i < pixel_count; i++) {
            if (g_backbuf[i] != g_presented_buf[i]) {
                changed = 1;
                break;
            }
        }
    }

    if (changed) {
        g_dws->blit_scaled(0, 0, g_screen_w, g_screen_h,
                           g_backbuf, g_backbuf_w, g_backbuf_h);
    }

    {
        u32 *previous_frame = g_presented_buf;
        g_presented_buf = g_backbuf;
        g_backbuf = previous_frame;
    }
    g_presented_valid = 1;
}

static void redraw(void) {
    if (g_screen == SCREEN_MAIN) {
        draw_main_screen();
    } else if (g_screen == SCREEN_HWMAP) {
        draw_hwmap_screen();
    } else if (g_screen == SCREEN_PCI) {
        draw_pci_screen();
    } else if (g_screen == SCREEN_MANAGEMENT) {
        draw_management_screen();
    }
    draw_cursor_bb(g_mouse_x, g_mouse_y);
    blit_backbuffer();
}

static void confirm_power_action(void) {
    if (g_power_confirmation == POWER_ACTION_SHUTDOWN) {
        g_power_action_status = g_acpi && g_acpi->shutdown ?
            g_acpi->shutdown() : DEPRESSURE_ACPI_POWER_UNAVAILABLE;
    } else if (g_power_confirmation == POWER_ACTION_REBOOT) {
        g_power_action_status = g_acpi && g_acpi->reboot ?
            g_acpi->reboot() : DEPRESSURE_ACPI_RESET_UNAVAILABLE;
    }
    g_power_confirmation = POWER_ACTION_NONE;
    redraw();
}

static void handle_click(int x, int y) {
    if (g_screen == SCREEN_MAIN) {
        if ((u64)x >= g_management_x &&
            (u64)x < g_management_x + g_management_w &&
            (u64)y >= g_management_y &&
            (u64)y < g_management_y + g_management_h) {
            g_screen = SCREEN_MANAGEMENT;
            redraw();
            if (g_uart) g_uart->puts("[DepMgr] Open Management\r\n");
        } else if ((u64)x >= g_icon_x && (u64)x < g_icon_x + g_icon_w &&
            (u64)y >= g_icon_y && (u64)y < g_icon_y + g_icon_h) {
            g_screen = SCREEN_HWMAP;
            redraw();
            if (g_uart) g_uart->puts("[DepMgr] Open Hardware Map\r\n");
        }
    } else if (g_screen == SCREEN_MANAGEMENT) {
        if (g_power_confirmation != POWER_ACTION_NONE) {
            if ((u64)x >= g_confirm_x && (u64)x < g_confirm_x + 200 &&
                (u64)y >= g_confirm_y && (u64)y < g_confirm_y + 40) {
                confirm_power_action();
            } else if ((u64)x >= g_cancel_x && (u64)x < g_cancel_x + 200 &&
                       (u64)y >= g_cancel_y && (u64)y < g_cancel_y + 40) {
                g_power_confirmation = POWER_ACTION_NONE;
                redraw();
            }
        } else if ((u64)x >= g_shutdown_x && (u64)x < g_shutdown_x + 260 &&
                   (u64)y >= g_shutdown_y && (u64)y < g_shutdown_y + 40) {
            g_power_confirmation = POWER_ACTION_SHUTDOWN;
            g_power_action_status = DEPRESSURE_STATUS_OK;
            redraw();
        } else if ((u64)x >= g_reboot_x && (u64)x < g_reboot_x + 260 &&
                   (u64)y >= g_reboot_y && (u64)y < g_reboot_y + 40) {
            g_power_confirmation = POWER_ACTION_REBOOT;
            g_power_action_status = DEPRESSURE_STATUS_OK;
            redraw();
        }
    }
}

void depmgr_poll(void) {
    if (g_api) {
        if (g_api->smp_get_ncpu) {
            u32 n = g_api->smp_get_ncpu();
            if (n > 0) g_ncpu = n;
        }
        if (g_api->smp_get_load) {
            u32 limit = g_ncpu < 64 ? g_ncpu : 64;
            for (u32 i = 0; i < limit; i++) {
                g_core_load[i] = g_api->smp_get_load(i);
            }
        }
    }

    int mx = g_mouse_x;
    int my = g_mouse_y;
    int clicked = 0;

    if (g_mouse && g_mouse->get_x && g_mouse->get_y &&
        g_mouse->get_buttons) {
        mx = g_mouse->get_x();
        my = g_mouse->get_y();
        int buttons = g_mouse->get_buttons();

        clicked = (buttons & 0x01) && !(g_last_buttons & 0x01);

        g_mouse_x = mx;
        g_mouse_y = my;
        g_last_buttons = buttons;
    }

    redraw();

    blit_backbuffer();

    if (clicked) {
        handle_click(mx, my);
    }

    if (g_kbd && g_kbd->has_key && g_kbd->has_key()) {
        char c = g_kbd->get_key();
        if (c == 27) {
            if (g_screen == SCREEN_MANAGEMENT &&
                g_power_confirmation != POWER_ACTION_NONE) {
                g_power_confirmation = POWER_ACTION_NONE;
                redraw();
            } else if (g_screen == SCREEN_HWMAP || g_screen == SCREEN_PCI ||
                       g_screen == SCREEN_MANAGEMENT) {
                g_screen = SCREEN_MAIN;
                redraw();
            }
        } else if (g_screen == SCREEN_MAIN && (c == 'm' || c == 'M')) {
            g_screen = SCREEN_MANAGEMENT;
            redraw();
        } else if (g_screen == SCREEN_MANAGEMENT &&
                   g_power_confirmation != POWER_ACTION_NONE &&
                   (c == 'y' || c == 'Y')) {
            confirm_power_action();
        } else if (g_screen == SCREEN_MANAGEMENT &&
                   g_power_confirmation != POWER_ACTION_NONE &&
                   (c == 'n' || c == 'N')) {
            g_power_confirmation = POWER_ACTION_NONE;
            redraw();
        } else if (g_screen == SCREEN_MANAGEMENT &&
                   g_power_confirmation == POWER_ACTION_NONE &&
                   (c == 's' || c == 'S')) {
            g_power_confirmation = POWER_ACTION_SHUTDOWN;
            redraw();
        } else if (g_screen == SCREEN_MANAGEMENT &&
                   g_power_confirmation == POWER_ACTION_NONE &&
                   (c == 'r' || c == 'R')) {
            g_power_confirmation = POWER_ACTION_REBOOT;
            redraw();
        } else if (g_screen == SCREEN_HWMAP && (c == 'p' || c == 'P')) {
            g_pci_page = 0;
            g_screen = SCREEN_PCI;
            redraw();
        } else if (g_screen == SCREEN_PCI) {
            u32 count = 0;
            u32 page_count = 1;
            if (g_pci && g_pci->get_count &&
                DSTATUS_IS_OK(g_pci->get_count(&count))) {
                page_count = (count + PCI_PAGE_SIZE - 1) / PCI_PAGE_SIZE;
                if (page_count == 0) page_count = 1;
            }

            if (c == 'n' || c == 'N') {
                if (g_pci_page + 1 < page_count) g_pci_page++;
                redraw();
            } else if (c == 'b' || c == 'B') {
                if (g_pci_page > 0) g_pci_page--;
                redraw();
            } else if (c == 'h' || c == 'H') {
                g_screen = SCREEN_HWMAP;
                redraw();
            }
        }
    }
}

void *depmgr_main(dkernel_api_t *api, void *fb_info,
                  uart_api_t *uart, dws_api_t *dws, dkbd_api_t *kbd,
                  dmouse_api_t *mouse, int boot_mode,
                  u32 ncpu) {
    (void)boot_mode;

    g_uart  = uart;
    g_dws   = dws;
    g_kbd   = kbd;
    g_api   = api;
    g_mouse = mouse;
    g_pci = 0;
    g_acpi = 0;
    if (g_api && g_api->DFindAndGetDriver) {
        ddriver_t *pci_driver = g_api->DFindAndGetDriver("pci");
        ddriver_t *acpi_driver = g_api->DFindAndGetDriver("acpi");
        if (pci_driver && pci_driver->api) {
            g_pci = (pci_api_t *)pci_driver->api;
        }
        if (acpi_driver && acpi_driver->api) {
            g_acpi = (acpi_api_t *)acpi_driver->api;
        }
    }

    if (ncpu > 0) g_ncpu = ncpu;

    if (fb_info) {
        typedef struct {
            u64 base;
            u64 width;
            u64 height;
            u64 pitch;
            u64 format;
            u64 size;
        } fb_info_t;
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
    g_presented_buf = (u32 *)(BACKBUF_ADDR + BACKBUF_SIZE);
    g_presented_valid = 0;

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
        }
    }

    redraw();

    if (api && api->task_add) {
        api->task_add("DepMgr", depmgr_poll, 1);
    }

    if (g_uart) g_uart->puts("[DepMgr] GUI ready.\r\n");

    return (void *)depmgr_poll;
}