#include "dtypes.h"
#include "duefi.h"
#include "dkrnl.h"

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

static uart_api_t *g_uart = 0;
static dws_api_t  *g_dws  = 0;
static dkbd_api_t *g_kbd  = 0;
static int         g_boot_mode = 0;

static char g_cmd_buf[256];
static u64  g_cmd_len = 0;

static u64 g_cur_x = 0;
static u64 g_cur_y = 0;

static u64 g_screen_w = 800;
static u64 g_screen_h = 600;

#define COL_FG  0x00FFFFFF
#define COL_BG  0x00000000

#define CHAR_W  16
#define CHAR_H  16

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int parse_int(const char *s, int *out) {
    int val = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    if (*s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    *out = val * sign;
    return 0;
}

static inline void outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void devcon_newline(void) {
    g_cur_x = 0;
    g_cur_y += CHAR_H;

    if (g_cur_y + CHAR_H > g_screen_h) {
        if (g_dws && g_dws->fill) {
            g_dws->fill(COL_BG);
        }
        g_cur_y = 0;
    }
}

static void devcon_putc(char c) {
    if (g_uart && g_uart->putc) {
        if (c == '\n') g_uart->putc('\r');
        g_uart->putc(c);
    }

    if (!g_dws || !g_dws->draw_char) {
        if (c == '\n') devcon_newline();
        else if (c == '\r') g_cur_x = 0;
        else g_cur_x += CHAR_W;
        return;
    }

    if (c == '\n') {
        devcon_newline();
    } else if (c == '\r') {
        g_cur_x = 0;
    } else if (c == '\b') {
        if (g_cur_x >= CHAR_W) {
            g_cur_x -= CHAR_W;
            g_dws->draw_char(g_cur_x, g_cur_y, ' ', COL_FG, COL_BG);
        }
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            g_dws->draw_char(g_cur_x, g_cur_y, ' ', COL_FG, COL_BG);
            g_cur_x += CHAR_W;
            if (g_cur_x + CHAR_W > g_screen_w) devcon_newline();
        }
    } else if (c >= 0x20 && c < 0x7F) {
        g_dws->draw_char(g_cur_x, g_cur_y, c, COL_FG, COL_BG);
        g_cur_x += CHAR_W;
        if (g_cur_x + CHAR_W > g_screen_w) devcon_newline();
    }
}

static void devcon_puts(const char *str) {
    while (*str) devcon_putc(*str++);
}

static void devcon_put_hex(u64 value) {
    static const char hex[] = "0123456789ABCDEF";
    devcon_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        devcon_putc(hex[(value >> i) & 0xF]);
    }
}

static void devcon_put_dec(u64 value) {
    char buf[32];
    int i = 0;
    if (value == 0) {
        devcon_putc('0');
        return;
    }
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0) {
        devcon_putc(buf[--i]);
    }
}

static u8 bcd_to_bin(u8 bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static u8 cmos_read(u8 reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void cmd_cmos(void) {
    while (cmos_read(0x0A) & 0x80) {
        /* busy wait */
    }

    u8 sec   = cmos_read(0x00);
    u8 min   = cmos_read(0x02);
    u8 hour  = cmos_read(0x04);
    u8 day   = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year  = cmos_read(0x09);
    u8 regB  = cmos_read(0x0B);

    int is_bcd = !(regB & 0x04);
    if (is_bcd) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour & 0x7F);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }

    int is_12h = !(regB & 0x02);
    if (is_12h) {
        int pm = hour & 0x80;
        hour = hour & 0x7F;
        if (is_bcd) hour = bcd_to_bin(hour);
        if (pm && hour != 12) hour += 12;
        if (!pm && hour == 12) hour = 0;
    }

    devcon_puts("CMOS RTC:\r\n");
    devcon_puts("  Date: 20");
    if (year < 10) devcon_putc('0');
    devcon_put_dec(year);
    devcon_putc('-');
    if (month < 10) devcon_putc('0');
    devcon_put_dec(month);
    devcon_putc('-');
    if (day < 10) devcon_putc('0');
    devcon_put_dec(day);
    devcon_puts("\r\n");

    devcon_puts("  Time: ");
    if (hour < 10) devcon_putc('0');
    devcon_put_dec(hour);
    devcon_putc(':');
    if (min < 10) devcon_putc('0');
    devcon_put_dec(min);
    devcon_putc(':');
    if (sec < 10) devcon_putc('0');
    devcon_put_dec(sec);
    devcon_puts("\r\n");

    devcon_puts("  Mode: ");
    devcon_puts(is_bcd ? "BCD" : "binary");
    devcon_puts(", ");
    devcon_puts(is_12h ? "12h" : "24h");
    devcon_puts("\r\n");

    u8 mem_low  = cmos_read(0x15);
    u8 mem_high = cmos_read(0x16);
    u8 mem_ext  = cmos_read(0x17);

    devcon_puts("  Memory (low):  ");
    devcon_put_dec(mem_low);
    devcon_puts(" KB\r\n");
    devcon_puts("  Memory (high): ");
    devcon_put_dec(mem_high);
    devcon_puts(" KB\r\n");
    devcon_puts("  Memory (ext):  ");
    devcon_put_dec(mem_ext);
    devcon_puts(" KB\r\n");
}

static void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

static void cmd_fpu(void) {
    u32 eax, ebx, ecx, edx;

    cpuid(0, &eax, &ebx, &ecx, &edx);

    char vendor[13];
    *(u32 *)&vendor[0] = ebx;
    *(u32 *)&vendor[4] = edx;
    *(u32 *)&vendor[8] = ecx;
    vendor[12] = 0;

    devcon_puts("CPU Vendor: ");
    devcon_puts(vendor);
    devcon_puts("\r\n");

    cpuid(1, &eax, &ebx, &ecx, &edx);

    u32 family   = (eax >> 8) & 0xF;
    u32 model    = (eax >> 4) & 0xF;
    u32 stepping = eax & 0xF;

    devcon_puts("CPU Family: ");
    devcon_put_dec(family);
    devcon_puts(", Model: ");
    devcon_put_dec(model);
    devcon_puts(", Stepping: ");
    devcon_put_dec(stepping);
    devcon_puts("\r\n");

    devcon_puts("FPU: ");
    if (edx & (1 << 0)) {
        devcon_puts("present\r\n");
    } else {
        devcon_puts("NOT present\r\n");
    }

    devcon_puts("Features:\r\n");

    if (edx & (1 << 0))  devcon_puts("  - x87 FPU\r\n");
    if (edx & (1 << 23)) devcon_puts("  - MMX\r\n");
    if (edx & (1 << 25)) devcon_puts("  - SSE\r\n");
    if (edx & (1 << 26)) devcon_puts("  - SSE2\r\n");
    if (ecx & (1 << 0))  devcon_puts("  - SSE3\r\n");
    if (ecx & (1 << 19)) devcon_puts("  - SSE4.1\r\n");
    if (ecx & (1 << 20)) devcon_puts("  - SSE4.2\r\n");
    if (ecx & (1 << 28)) devcon_puts("  - AVX\r\n");
    if (ecx & (1 << 12)) devcon_puts("  - FMA\r\n");
    if (ecx & (1 << 30)) devcon_puts("  - RDRAND\r\n");
    if (edx & (1 << 4))  devcon_puts("  - TSC (Time Stamp Counter)\r\n");
    if (edx & (1 << 5))  devcon_puts("  - MSR\r\n");
    if (edx & (1 << 6))  devcon_puts("  - PAE\r\n");
    if (edx & (1 << 9))  devcon_puts("  - APIC\r\n");
}

static void cmd_bootm(const char *arg) {
    int mode;
    if (parse_int(arg, &mode) != 0) {
        devcon_puts("Usage: bootm [0|1]\r\n");
        devcon_puts("  0 = DevCon (shell)\r\n");
        devcon_puts("  1 = GUI\r\n");
        return;
    }

    if (mode != 0 && mode != 1) {
        devcon_puts("Invalid boot mode. Use 0 or 1.\r\n");
        return;
    }

    static char new_content[256];
    static const char prefix[] = "bootMode=";
    u64 pos = 0;
    for (int i = 0; prefix[i]; i++) new_content[pos++] = prefix[i];
    new_content[pos++] = '0' + mode;
    new_content[pos++] = '\n';
    new_content[pos++] = 0;

    static const u16 ini_path[] = {
        '\\','D','e','p','r','e','s','s','u','r','e','\\',
        'd','b','o','o','t','.','i','n','i', 0
    };

    u64 written = uefi_write_file(ini_path, new_content, pos);
    if (written > 0) {
        devcon_puts("Boot mode saved to dboot.ini: ");
        devcon_put_dec(mode);
        devcon_puts("\r\n");
        devcon_puts("Reboot to apply.\r\n");
    } else {
        devcon_puts("Failed to write dboot.ini\r\n");
        devcon_puts("(UEFI write error)\r\n");
    }

    g_boot_mode = mode;
}

static void devcon_prompt(void) {
    devcon_puts("DevCon> ");
}

static void cmd_help(void) {
    devcon_puts("Available commands:\r\n");
    devcon_puts("  help        - show this help\r\n");
    devcon_puts("  echo        - echo text\r\n");
    devcon_puts("  clear       - clear screen\r\n");
    devcon_puts("  bootm [0|1] - set boot mode (writes dboot.ini)\r\n");
    devcon_puts("  fpu         - show CPU/FPU info\r\n");
    devcon_puts("  cmos        - show CMOS/RTC info\r\n");
    devcon_puts("  reboot      - reboot system\r\n");
    devcon_puts("  exit        - exit DevCon\r\n");
}

static void cmd_echo(const char *arg) {
    devcon_puts(arg);
    devcon_puts("\r\n");
}

static void cmd_clear(void) {
    if (g_dws && g_dws->fill) {
        g_dws->fill(COL_BG);
    }
    g_cur_x = 0;
    g_cur_y = 0;

    if (g_uart && g_uart->puts) {
        g_uart->puts("\x1B[2J\x1B[0;0H");
    }
}

static void cmd_reboot(void) {
    devcon_puts("Rebooting...\r\n");
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_exit(void) {
    devcon_puts("Exiting DevCon...\r\n");
}

static void devcon_execute(const char *cmd) {
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == 0) return;

    const char *arg = cmd;
    while (*arg && *arg != ' ' && *arg != '\t') arg++;

    char name[64];
    u64 nlen = arg - cmd;
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    for (u64 i = 0; i < nlen; i++) name[i] = cmd[i];
    name[nlen] = 0;

    while (*arg == ' ' || *arg == '\t') arg++;

    if (str_eq(name, "help")) {
        cmd_help();
    } else if (str_eq(name, "echo")) {
        cmd_echo(arg);
    } else if (str_eq(name, "clear")) {
        cmd_clear();
    } else if (str_eq(name, "bootm")) {
        cmd_bootm(arg);
    } else if (str_eq(name, "fpu")) {
        cmd_fpu();
    } else if (str_eq(name, "cmos")) {
        cmd_cmos();
    } else if (str_eq(name, "reboot")) {
        cmd_reboot();
    } else if (str_eq(name, "exit")) {
        cmd_exit();
    } else {
        devcon_puts("Unknown command: ");
        devcon_puts(name);
        devcon_puts("\r\n");
        devcon_puts("Type 'help' for list of commands\r\n");
    }
}

void devcon_poll(void) {
    char c = 0;

    if (g_kbd && g_kbd->has_key) {
        if (g_kbd->has_key()) {
            c = g_kbd->get_key();
        }
    }

    if (!c && g_uart && g_uart->has_data) {
        if (g_uart->has_data()) {
            c = g_uart->getc();
        }
    }

    if (!c) return;

    if (c == '\r' || c == '\n') {
        devcon_puts("\r\n");
        g_cmd_buf[g_cmd_len] = 0;
        devcon_execute(g_cmd_buf);
        g_cmd_len = 0;
        devcon_prompt();
        return;
    }

    if (c == 0x08 || c == 0x7F) {
        if (g_cmd_len > 0) {
            g_cmd_len--;
            devcon_puts("\b \b");
        }
        return;
    }

    if (c >= 0x20 && c < 0x7F) {
        if (g_cmd_len < sizeof(g_cmd_buf) - 1) {
            g_cmd_buf[g_cmd_len++] = c;
            devcon_putc(c);
        }
    }
}

void *devcon_main(dkernel_api_t *api, void *fb_info,
                  uart_api_t *uart, dws_api_t *dws, dkbd_api_t *kbd,
                  int boot_mode) {
    (void)api;

    g_uart = uart;
    g_dws  = dws;
    g_kbd  = kbd;
    g_boot_mode = boot_mode;

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

    if (api && api->task_add) {
        api->task_add("DevCon", devcon_poll, 1);
    }

    devcon_puts("\r\n");
    devcon_puts("Depressure DevCon v0.1\r\n");
    devcon_puts("Type 'help' for list of commands\r\n");
    devcon_puts("\r\n");
    devcon_prompt();

    return (void *)devcon_poll;
}