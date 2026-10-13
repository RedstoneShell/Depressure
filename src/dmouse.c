// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "ddriver.h"
#include "dkrnl.h"

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

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_CMD_ENABLE_AUX    0xA8
#define PS2_CMD_SEND_AUX      0xD4

#define MOUSE_CMD_RESET       0xFF
#define MOUSE_CMD_DEFAULTS    0xF6
#define MOUSE_CMD_ENABLE      0xF4
#define MOUSE_CMD_DISABLE     0xF5

static int g_mouse_x = 400;
static int g_mouse_y = 300;
static int g_mouse_buttons = 0;

static u8  g_packet[3];
static int g_packet_idx = 0;

static int g_last_dx = 0;
static int g_last_dy = 0;
static int g_last_buttons = 0;
static int g_has_new_event = 0;

static inline void outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS) & 0x02) == 0) return;
    }
}

static int ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01) return 1;
    }
    return 0;
}

static void ps2_send_cmd(u8 cmd) {
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

static void ps2_send_data(u8 data) {
    ps2_wait_write();
    outb(PS2_DATA, data);
}

static void mouse_send_cmd(u8 cmd) {
    ps2_send_cmd(PS2_CMD_SEND_AUX);
    ps2_send_data(cmd);
}

static u8 mouse_read_byte(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

static void mouse_init(void) {
    ps2_send_cmd(PS2_CMD_ENABLE_AUX);

    ps2_send_cmd(PS2_CMD_READ_CONFIG);
    u8 config = mouse_read_byte();
    config |= 0x03;
    config &= ~0x30;
    ps2_send_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    mouse_send_cmd(MOUSE_CMD_RESET);
    mouse_read_byte();
    mouse_read_byte();
    mouse_read_byte();

    mouse_send_cmd(MOUSE_CMD_DEFAULTS);
    mouse_read_byte();

    mouse_send_cmd(MOUSE_CMD_ENABLE);
    mouse_read_byte();

    while (inb(PS2_STATUS) & 0x01) {
        inb(PS2_DATA);
    }
}

static void mouse_process_packet(void) {
    u8 b0 = g_packet[0];
    u8 b1 = g_packet[1];
    u8 b2 = g_packet[2];

    int buttons = b0 & 0x07;

    int dx = (int)b1;
    if (b0 & 0x10) dx |= 0xFFFFFF00;

    int dy = (int)b2;
    if (b0 & 0x20) dy |= 0xFFFFFF00;
    dy = -dy;

    if (b0 & 0x40) dx = 0;
    if (b0 & 0x80) dy = 0;

    g_mouse_x += dx;
    g_mouse_y += dy;

    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x > 799) g_mouse_x = 799;
    if (g_mouse_y > 599) g_mouse_y = 599;

    g_last_dx = dx;
    g_last_dy = dy;
    g_last_buttons = buttons;
    g_mouse_buttons = buttons;
    g_has_new_event = 1;
}

static void mouse_poll(void) {
    while (inb(PS2_STATUS) & 0x01) {
        u8 status = inb(PS2_STATUS);

        if ((status & 0x20) == 0) {
            break;
        }

        u8 data = inb(PS2_DATA);

        if (data == 0xFA || data == 0xAA) continue;

        if (g_packet_idx == 0) {
            if ((data & 0x08) == 0) {
                continue;
            }
        }

        g_packet[g_packet_idx++] = data;

        if (g_packet_idx >= 3) {
            mouse_process_packet();
            g_packet_idx = 0;
        }
    }
}

static int mouse_has_event(void) {
    return g_has_new_event;
}

static void mouse_get_state(int *x, int *y, int *buttons) {
    if (x) *x = g_last_dx;
    if (y) *y = g_last_dy;
    if (buttons) *buttons = g_last_buttons;
    g_has_new_event = 0;
}

static int mouse_get_x(void) {
    return g_mouse_x;
}

static int mouse_get_y(void) {
    return g_mouse_y;
}

static int mouse_get_buttons(void) {
    return g_mouse_buttons;
}

static void mouse_set_position(int x, int y) {
    g_mouse_x = x;
    g_mouse_y = y;
}

static dmouse_api_t g_mouse_api;

static ddriver_t mouse_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_INPUT,
    .name    = "dmouse",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void dmouse_main(dkernel_api_t *api, void *fb_info) {
    (void)fb_info;

    mouse_init();

    g_mouse_api.version       = 0x00010000;
    g_mouse_api._pad          = 0;
    g_mouse_api.has_event     = mouse_has_event;
    g_mouse_api.get_state     = mouse_get_state;
    g_mouse_api.get_x         = mouse_get_x;
    g_mouse_api.get_y         = mouse_get_y;
    g_mouse_api.get_buttons   = mouse_get_buttons;
    g_mouse_api.set_position  = mouse_set_position;

    mouse_driver.entry = (u64)mouse_poll;

    if (api && api->register_driver) {
        mouse_driver.api = (u64)&g_mouse_api;
        api->register_driver(&mouse_driver);
    }

    if (api && api->task_add) {
        api->task_add("dmouse", mouse_poll, 1);
    }
}