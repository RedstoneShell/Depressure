// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dboot.h"
#include "duefi.h"

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++; prefix++;
    }
    return 1;
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

int dboot_parse(const char *buf, u64 size, dboot_config_t *out) {
    if (!buf || !out) return -1;

    /* Defaults */
    out->boot_mode = 0;
    out->log_level = 1;
    out->init[0] = 0;

    u64 i = 0;
    while (i < size && buf[i]) {
        while (i < size && (buf[i] == ' ' || buf[i] == '\t')) i++;

        if (i >= size || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '#' || buf[i] == ';') {
            while (i < size && buf[i] != '\n') i++;
            if (i < size) i++;
            continue;
        }

        u64 line_start = i;
        while (i < size && buf[i] != '\n' && buf[i] != '\r') i++;
        u64 line_end = i;

        const char *line = buf + line_start;
        u64 line_len = line_end - line_start;

        u64 eq = 0;
        while (eq < line_len && line[eq] != '=') eq++;

        if (eq == line_len) {
            if (i < size) i++;
            continue;
        }

        u64 key_end = eq;
        while (key_end > 0 && (line[key_end-1] == ' ' || line[key_end-1] == '\t')) key_end--;

        u64 val_start = eq + 1;
        while (val_start < line_len && (line[val_start] == ' ' || line[val_start] == '\t')) val_start++;

        u64 val_end = line_len;
        while (val_end > val_start && (line[val_end-1] == ' ' || line[val_end-1] == '\t')) val_end--;

        u64 key_len = key_end;
        u64 val_len = val_end - val_start;

        char key[64];
        char val[128];

        if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
        if (val_len >= sizeof(val)) val_len = sizeof(val) - 1;

        for (u64 k = 0; k < key_len; k++) key[k] = line[k];
        key[key_len] = 0;

        for (u64 k = 0; k < val_len; k++) val[k] = line[val_start + k];
        val[val_len] = 0;

        if (str_eq(key, "bootMode")) {
            int v;
            if (parse_int(val, &v) == 0) out->boot_mode = v;
        } else if (str_eq(key, "logLevel")) {
            int v;
            if (parse_int(val, &v) == 0) out->log_level = v;
        } else if (str_eq(key, "init")) {
            u64 k = 0;
            while (k < val_len && k < sizeof(out->init) - 1) {
                out->init[k] = val[k];
                k++;
            }
            out->init[k] = 0;
        }

        while (i < size && (buf[i] == '\n' || buf[i] == '\r')) i++;
    }

    return 0;
}

int dboot_load(dboot_config_t *out) {
    static u8 buf[4096];
    static const u16 path[] = {
        '\\','D','e','p','r','e','s','s','u','r','e','\\',
        'd','b','o','o','t','.','i','n','i', 0
    };

    u64 bytes = uefi_load_file(path, buf, sizeof(buf));
    if (bytes == 0) {
        out->boot_mode = 0;
        out->log_level = 1;
        out->init[0] = 0;
        return -1;
    }

    return dboot_parse((const char *)buf, bytes, out);
}