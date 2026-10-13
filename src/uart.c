// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "ddriver.h"

typedef struct dkernel_api {
    u32 version;
    u32 _pad;
    int (*register_driver)(ddriver_t *drv);
} dkernel_api_t;

typedef struct uart_api {
    u32 version;
    u32 _pad;
    void (*putc)(char c);
    void (*puts)(const char *str);
    void (*put_hex)(u64 value);
    int  (*has_data)(void);
    char (*getc)(void);
} uart_api_t;

#define UART_PORT   0x3F8

static inline void outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void uart_init(void) {
    outb(UART_PORT + 1, 0x00);
    outb(UART_PORT + 3, 0x80);
    outb(UART_PORT + 0, 0x03);
    outb(UART_PORT + 1, 0x00);
    outb(UART_PORT + 3, 0x03);
    outb(UART_PORT + 2, 0xC7);
    outb(UART_PORT + 4, 0x0B);
}

static void uart_putc(char c) {
    while ((inb(UART_PORT + 5) & 0x20) == 0) {
        /* busy wait */
    }
    outb(UART_PORT, (u8)c);
}

static void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

static void uart_put_hex(u64 value) {
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(value >> i) & 0xF]);
    }
}

static int uart_has_data(void) {
    return (inb(UART_PORT + 5) & 0x01) != 0;
}

static char uart_getc(void) {
    while (!uart_has_data()) {
        /* busy wait */
    }
    return (char)inb(UART_PORT);
}

static uart_api_t g_uart_api;

static ddriver_t uart_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_OTHER,
    .name    = "uart",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void uart_main(dkernel_api_t *api, void *fb_info) {
    (void)fb_info;
    uart_init();
    g_uart_api.version  = 0x00010000;
    g_uart_api._pad     = 0;
    g_uart_api.putc     = uart_putc;
    g_uart_api.puts     = uart_puts;
    g_uart_api.put_hex  = uart_put_hex;
    g_uart_api.has_data = uart_has_data;
    g_uart_api.getc     = uart_getc;
    if (api && api->register_driver) {
        uart_driver.api = (u64)&g_uart_api;
        api->register_driver(&uart_driver);
    }
    uart_puts("UART Initialized by Depressure OS\r\n");
    uart_put_hex((u64)&g_uart_api);
    uart_puts("\r\n");
}