// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "ddriver.h"
#include "dkrnl.h"

typedef struct dkbd_api {
    u32 version;
    u32 _pad;
    int  (*has_key)(void);
    char (*get_key)(void);
    u8   (*get_scancode)(void);
    int  (*has_arrow)(void);
    int  (*get_arrow)(void);
} dkbd_api_t;

#define PS2_DATA    0x60
#define PS2_STATUS  0x64

#define ARROW_NONE   -1
#define ARROW_UP      0
#define ARROW_DOWN    1
#define ARROW_LEFT    2
#define ARROW_RIGHT   3

static const char scancode_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',
    0,  '*', 0,  ' ',
};

static const char scancode_to_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',
    0,  '*', 0,  ' ',
};

static int g_shift = 0;
static int g_ctrl = 0;
static int g_alt = 0;
static int g_extended = 0;

static char g_last_char = 0;
static u8   g_last_scancode = 0;

static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int g_pending_arrow = ARROW_NONE;

static int kbd_has_arrow(void) {
    kbd_has_key();
    return g_pending_arrow != ARROW_NONE;
}

static int kbd_get_arrow(void) {
    int a = g_pending_arrow;
    g_pending_arrow = ARROW_NONE;
    return a;
}

static void kbd_process_scancode(u8 sc) {
    g_last_scancode = sc;

    if (sc == 0xE0) {
        g_extended = 1;
        return;
    }

    if (sc & 0x80) {
        /* break code */
        u8 make = sc & 0x7F;

        if (make == 0x2A || make == 0x36) g_shift = 0;
        if (make == 0x1D) g_ctrl = 0;
        if (make == 0x38) g_alt = 0;

        g_extended = 0;
        return;
    }

    u8 make = sc;

    /* shift/ctrl/alt */
    if (make == 0x2A || make == 0x36) { g_shift = 1; g_extended = 0; return; }
    if (make == 0x1D) { g_ctrl = 1; g_extended = 0; return; }
    if (make == 0x38) { g_alt = 1; g_extended = 0; return; }

    /* extended: arrows */
    if (g_extended) {
        g_extended = 0;
        if (make == 0x48) { g_pending_arrow = ARROW_UP;    return; }
        if (make == 0x50) { g_pending_arrow = ARROW_DOWN;  return; }
        if (make == 0x4B) { g_pending_arrow = ARROW_LEFT;  return; }
        if (make == 0x4D) { g_pending_arrow = ARROW_RIGHT; return; }
        return;
    }

    char c = 0;
    if (make < 128) {
        c = g_shift ? scancode_to_ascii_shift[make] : scancode_to_ascii[make];
    }
    if (c) g_last_char = c;
}

int kbd_has_key(void) {
    u8 status = inb(PS2_STATUS);

    if ((status & 0x01) == 0) return 0;
    if ((status & 0x20) != 0) return 0;

    u8 sc = inb(PS2_DATA);
    kbd_process_scancode(sc);

    return g_last_char != 0;
}

static char kbd_get_key(void) {
    char c = g_last_char;
    g_last_char = 0;
    return c;
}

static u8 kbd_get_scancode(void) {
    u8 s = g_last_scancode;
    g_last_scancode = 0;
    return s;
}

static dkbd_api_t g_kbd_api;

static ddriver_t kbd_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_INPUT,
    .name    = "dkbd",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void dkbd_main(dkernel_api_t *api, void *fb_info) {
    (void)fb_info;

    while (inb(PS2_STATUS) & 0x01) {
        inb(PS2_DATA);
    }

    g_kbd_api.version       = 0x00010000;
    g_kbd_api._pad          = 0;
    g_kbd_api.has_key       = kbd_has_key;
    g_kbd_api.get_key       = kbd_get_key;
    g_kbd_api.get_scancode  = kbd_get_scancode;
    g_kbd_api.has_arrow = kbd_has_arrow;
    g_kbd_api.get_arrow = kbd_get_arrow;

    if (api && api->register_driver) {
        kbd_driver.api = (u64)&g_kbd_api;
        api->register_driver(&kbd_driver);
    }
}