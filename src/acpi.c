// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "acpi.h"
#include "ddriver.h"
#include "dkrnl.h"
#include "duefi.h"
#include "pci.h"

#define ACPI_TABLE_MAX_SIZE 0x100000U
#define PIIX4_PM_BASE_MASK  0xFFC0U

typedef struct __attribute__((packed)) acpi_rsdp {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} acpi_rsdp_t;

typedef struct __attribute__((packed)) acpi_header {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} acpi_header_t;

typedef struct __attribute__((packed)) acpi_gas {
    u8 address_space_id;
    u8 register_bit_width;
    u8 register_bit_offset;
    u8 access_size;
    u64 address;
} acpi_gas_t;

#define ACPI_PM1_SCI_ENABLE        0x0001U
#define ACPI_PM1_SLEEP_TYPE_MASK   0x1C00U
#define ACPI_PM1_SLEEP_ENABLE      0x2000U
#define ACPI_GAS_SYSTEM_IO         1U
#define ACPI_FADT_RESET_SUPPORTED  (1U << 10)

#define AML_ZERO_OP      0x00
#define AML_ONE_OP       0x01
#define AML_ALIAS_OP     0x06
#define AML_NAME_OP      0x08
#define AML_BYTE_PREFIX  0x0A
#define AML_WORD_PREFIX  0x0B
#define AML_DWORD_PREFIX 0x0C
#define AML_STRING_PREFIX 0x0D
#define AML_QWORD_PREFIX 0x0E
#define AML_SCOPE_OP     0x10
#define AML_BUFFER_OP    0x11
#define AML_PACKAGE_OP   0x12
#define AML_VAR_PACKAGE_OP 0x13
#define AML_METHOD_OP    0x14
#define AML_EXTERNAL_OP  0x15
#define AML_LOCAL0_OP    0x60
#define AML_LOCAL1_OP    0x61
#define AML_LOCAL2_OP    0x62
#define AML_LOCAL3_OP    0x63
#define AML_LOCAL4_OP    0x64
#define AML_LOCAL5_OP    0x65
#define AML_LOCAL6_OP    0x66
#define AML_LOCAL7_OP    0x67
#define AML_ARG0_OP      0x68
#define AML_ARG1_OP      0x69
#define AML_ARG2_OP      0x6A
#define AML_ARG3_OP      0x6B
#define AML_ARG4_OP      0x6C
#define AML_ARG5_OP      0x6D
#define AML_ARG6_OP      0x6E
#define AML_STORE_OP     0x70
#define AML_REF_OF_OP    0x71
#define AML_ADD_OP       0x72
#define AML_CONCAT_OP    0x73
#define AML_SUBTRACT_OP  0x74
#define AML_INCREMENT_OP 0x75
#define AML_DECREMENT_OP 0x76
#define AML_MULTIPLY_OP  0x77
#define AML_DIVIDE_OP    0x78
#define AML_SHIFT_LEFT_OP 0x79
#define AML_SHIFT_RIGHT_OP 0x7A
#define AML_AND_OP       0x7B
#define AML_NAND_OP      0x7C
#define AML_OR_OP        0x7D
#define AML_NOR_OP       0x7E
#define AML_XOR_OP       0x7F
#define AML_NOT_OP       0x80
#define AML_FIND_SET_LEFT_BIT_OP 0x81
#define AML_FIND_SET_RIGHT_BIT_OP 0x82
#define AML_DEREF_OF_OP  0x83
#define AML_CONCAT_RES_OP 0x84
#define AML_MOD_OP       0x85
#define AML_NOTIFY_OP    0x86
#define AML_SIZE_OF_OP   0x87
#define AML_INDEX_OP     0x88
#define AML_MATCH_OP     0x89
#define AML_CREATE_DWORD_FIELD_OP 0x8A
#define AML_CREATE_WORD_FIELD_OP  0x8B
#define AML_CREATE_BYTE_FIELD_OP  0x8C
#define AML_CREATE_BIT_FIELD_OP   0x8D
#define AML_OBJECT_TYPE_OP 0x8E
#define AML_CREATE_QWORD_FIELD_OP 0x8F
#define AML_LAND_OP      0x90
#define AML_LOR_OP       0x91
#define AML_LNOT_OP      0x92
#define AML_LEQUAL_OP    0x93
#define AML_LGREATER_OP  0x94
#define AML_LLESS_OP     0x95
#define AML_TO_BUFFER_OP 0x96
#define AML_TO_DEC_STRING_OP 0x97
#define AML_TO_HEX_STRING_OP 0x98
#define AML_TO_INTEGER_OP 0x99
#define AML_TO_STRING_OP 0x9C
#define AML_COPY_OBJECT_OP 0x9D
#define AML_MID_OP       0x9E
#define AML_CONTINUE_OP  0x9F
#define AML_IF_OP        0xA0
#define AML_ELSE_OP      0xA1
#define AML_WHILE_OP     0xA2
#define AML_NOOP_OP      0xA3
#define AML_RETURN_OP    0xA4
#define AML_BREAK_OP     0xA5
#define AML_BREAK_POINT_OP 0xA5
#define AML_ONES_OP      0xFF
#define AML_REVISION_OP  0x5B

#define AML_EXT_OP_PREFIX 0x5B
#define AML_MUTEX_OP      0x01
#define AML_EVENT_OP      0x02
#define AML_COND_REF_OF_OP 0x12
#define AML_CREATE_FIELD_OP 0x13
#define AML_LOAD_TABLE_OP  0x1F
#define AML_LOAD_OP        0x20
#define AML_STALL_OP       0x21
#define AML_SLEEP_OP       0x22
#define AML_ACQUIRE_OP     0x23
#define AML_SIGNAL_OP      0x24
#define AML_WAIT_OP        0x25
#define AML_RESET_OP       0x26
#define AML_RELEASE_OP     0x27
#define AML_FROM_BCD_OP    0x28
#define AML_TO_BCD_OP      0x29
#define AML_UNLOAD_OP      0x2A
#define AML_REVISION_OP2   0x30
#define AML_DEBUG_OP       0x31
#define AML_FATAL_OP       0x32
#define AML_TIMER_OP       0x33
#define AML_OP_REGION_OP   0x80
#define AML_FIELD_OP       0x81
#define AML_DEVICE_OP      0x82
#define AML_PROCESSOR_OP   0x83
#define AML_POWER_RES_OP   0x84
#define AML_THERMAL_ZONE_OP 0x85
#define AML_INDEX_FIELD_OP 0x86
#define AML_BANK_FIELD_OP  0x87
#define AML_DATA_REGION_OP 0x88

#define MAX_NS_NODES 256

typedef struct aml_ns_node {
    char     name[5];
    u32      offset;
    u32      parent;
    u32      type;
    u32      arg_count;
    int      valid;
} aml_ns_node_t;

typedef struct aml_ctx {
    const u8 *aml;
    u32       aml_len;
    u32       pos;

    u32       locals[8];
    u32       args[7];
    u64       ret_value;
    int       returned;

    aml_ns_node_t ns[MAX_NS_NODES];
    u32           ns_count;
} aml_ctx_t;

static dkernel_api_t *g_kernel_api;
static acpi_system_info_t g_info;
static acpi_table_info_t g_tables[ACPI_MAX_TABLES];
static acpi_api_t g_acpi_api;

static aml_ctx_t g_aml_ctx;
static u32       g_ac0_offset = 0;
static u32       g_ac0_scope  = 0xFFFFFFFF;
static int       g_ac0_found  = 0;

static inline void io_outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline u8 io_inb(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline u32 io_inl(u16 port) {
    u32 value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline u16 io_inw(u16 port) {
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void io_outw(u16 port, u16 value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline void io_outl(u16 port, u32 value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void acpi_log_char(char c) {
    u32 timeout = 100000;
    while (timeout-- && !(io_inb(0x3FD) & 0x20)) {}
    io_outb(0x3F8, (u8)c);
}
static void acpi_log(const char *message) {
    while (*message) {
        if (*message == '\n') acpi_log_char('\r');
        acpi_log_char(*message++);
    }
}
static void acpi_log_hex(u64 value) {
    static const char hex[] = "0123456789ABCDEF";
    acpi_log("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        acpi_log_char(hex[(value >> shift) & 0xF]);
}
static void acpi_log_signature(const char signature[4]) {
    for (u32 i = 0; i < 4; i++) acpi_log_char(signature[i]);
}
static void acpi_log_dec(u64 v) {
    char b[24]; int i = 0;
    if (v == 0) { acpi_log_char('0'); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i) acpi_log_char(b[--i]);
}

static void acpi_uart_init(void) {
    io_outb(0x3F9, 0x00);
    io_outb(0x3FB, 0x80);
    io_outb(0x3F8, 0x03);
    io_outb(0x3F9, 0x00);
    io_outb(0x3FB, 0x03);
    io_outb(0x3FA, 0xC7);
    io_outb(0x3FC, 0x0B);
}

static ddriver_t g_acpi_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next = 0,
    .type = DDRIVER_TYPE_OTHER,
    .name = "acpi",
    .entry = 0,
    .api = 0,
    .data = 0,
};

static int bytes_equal(const char *left, const char *right, u32 count) {
    for (u32 i = 0; i < count; i++)
        if (left[i] != right[i]) return 0;
    return 1;
}
static int checksum_valid(const void *data, u32 length) {
    const u8 *bytes = (const u8 *)data;
    u8 sum = 0;
    for (u32 i = 0; i < length; i++) sum = (u8)(sum + bytes[i]);
    return sum == 0;
}
static int table_valid(const acpi_header_t *header) {
    if (!header || header->length < sizeof(acpi_header_t) ||
        header->length > ACPI_TABLE_MAX_SIZE) return 0;
    return checksum_valid(header, header->length);
}
static u16 read_u16(const u8 *data) {
    return (u16)data[0] | ((u16)data[1] << 8);
}
static u32 read_u32(const u8 *data) {
    return (u32)data[0] | ((u32)data[1] << 8) |
           ((u32)data[2] << 16) | ((u32)data[3] << 24);
}
static u64 read_u64(const u8 *data) {
    return (u64)read_u32(data) | ((u64)read_u32(data + 4) << 32);
}

static u32 aml_pkg_length(const u8 *aml, u32 end, u32 *pos) {
    if (*pos >= end) return 0;
    u8 lead = aml[(*pos)++];
    u32 following = lead >> 6;
    u32 length = lead & 0x3F;

    if (following == 0) return length;

    length = lead & 0x0F;
    for (u32 i = 0; i < following; i++) {
        if (*pos >= end) return 0;
        length |= (u32)aml[(*pos)++] << (4 + 8 * i);
    }
    return length;
}

static void aml_read_name(const u8 *aml, u32 end, u32 *pos, char *out) {
    u32 i = 0;
    for (; i < 4; i++) {
        if (*pos >= end) { out[i] = 0; return; }
        char c = (char)aml[(*pos)++];
        out[i] = c ? c : '_';
    }
    out[4] = 0;
}

static void aml_skip_name_string(const u8 *aml, u32 end, u32 *pos) {
    if (*pos >= end) return;
    u8 c = aml[*pos];

    if (c == '\\') { (*pos)++; }
    else if (c == '^') {
        while (*pos < end && aml[*pos] == '^') (*pos)++;
    }
    else if ((c & 0xE0) == 0x00 || (c & 0xF0) == 0x40) {
        /* NullName / RootChar / ParentPrefixChar - already handled */
    }

    aml_read_name(aml, end, pos, (char[5]){0});
}

static u64 aml_read_integer_operand(const u8 *aml, u32 end, u32 *pos) {
    if (*pos >= end) return 0;
    u8 op = aml[(*pos)++];
    u64 v = 0;
    switch (op) {
        case AML_ZERO_OP:  return 0;
        case AML_ONE_OP:   return 1;
        case AML_ONES_OP:  return ~(u64)0;
        case AML_BYTE_PREFIX:
            if (*pos >= end) return 0;
            return aml[(*pos)++];
        case AML_WORD_PREFIX:
            if (*pos + 2 > end) return 0;
            v = read_u16(aml + *pos); *pos += 2; return v;
        case AML_DWORD_PREFIX:
            if (*pos + 4 > end) return 0;
            v = read_u32(aml + *pos); *pos += 4; return v;
        case AML_QWORD_PREFIX:
            if (*pos + 8 > end) return 0;
            v = read_u64(aml + *pos); *pos += 8; return v;
        default:
            (*pos)--;
            return 0;
    }
}

static int aml_ns_add(aml_ctx_t *c, const char *name, u32 offset,
                       u32 parent, u32 type, u32 arg_count) {
    if (c->ns_count >= MAX_NS_NODES) return -1;
    aml_ns_node_t *n = &c->ns[c->ns_count++];
    for (u32 i = 0; i < 5; i++) n->name[i] = name[i];
    n->offset    = offset;
    n->parent    = parent;
    n->type      = type;
    n->arg_count = arg_count;
    n->valid     = 1;
    return (int)(c->ns_count - 1);
}

static int aml_ns_find(aml_ctx_t *c, const char *name) {
    for (u32 i = 0; i < c->ns_count; i++) {
        if (!c->ns[i].valid) continue;
        if (c->ns[i].name[0] == name[0] &&
            c->ns[i].name[1] == name[1] &&
            c->ns[i].name[2] == name[2] &&
            c->ns[i].name[3] == name[3]) return (int)i;
    }
    return -1;
}

static void aml_scan_scope(aml_ctx_t *c, u32 start, u32 end, u32 parent) {
    u32 pos = start;
    while (pos < end) {
        u32 obj_start = pos;
        u8 op = c->aml[pos++];

        if (op == AML_NAME_OP) {
            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            aml_ns_add(c, name, obj_start, parent, AML_NAME_OP, 0);
            if (pos < end) {
                u32 save = pos;
                u32 pl = aml_pkg_length(c->aml, end, &pos);
                if (pl > 0 && pos + pl <= end) {
                    aml_scan_scope(c, pos, pos + pl, (u32)parent);
                    pos = save;
                }
            }
            continue;
        }

        if (op == AML_METHOD_OP) {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 mstart = pl_pos;
            u32 mend   = mstart + pl;
            if (mend > end) mend = end;

            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            u32 arg_count = (pos < end) ? c->aml[pos++] : 0;
            aml_ns_add(c, name, pos, parent, AML_METHOD_OP, arg_count);

            pos = mend;
            continue;
        }

        if (op == AML_SCOPE_OP) {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 sstart = pl_pos;
            u32 send   = sstart + pl;
            if (send > end) send = end;

            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            int idx = aml_ns_add(c, name, pos, parent, AML_SCOPE_OP, 0);
            aml_scan_scope(c, pos, send, (u32)idx);
            pos = send;
            continue;
        }

        if (op == AML_THERMAL_ZONE_OP) {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 tstart = pl_pos;
            u32 tend   = tstart + pl;
            if (tend > end) tend = end;

            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            int idx = aml_ns_add(c, name, pos, parent, AML_THERMAL_ZONE_OP, 0);
            aml_scan_scope(c, pos, tend, (u32)idx);
            pos = tend;
            continue;
        }

        if (op == AML_PROCESSOR_OP) {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 pstart = pl_pos;
            u32 pend   = pstart + pl;
            if (pend > end) pend = end;

            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            pos += 6; /* ProcessorID + PblkAddr + PblkLen */
            int idx = aml_ns_add(c, name, pos, parent, AML_PROCESSOR_OP, 0);
            aml_scan_scope(c, pos, pend, (u32)idx);
            pos = pend;
            continue;
        }

        if (op == AML_DEVICE_OP) {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 dstart = pl_pos;
            u32 dend   = dstart + pl;
            if (dend > end) dend = end;

            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            int idx = aml_ns_add(c, name, pos, parent, AML_DEVICE_OP, 0);
            aml_scan_scope(c, pos, dend, (u32)idx);
            pos = dend;
            continue;
        }

        if (op == AML_POWER_RES_OP) {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 rstart = pl_pos;
            u32 rend   = rstart + pl;
            if (rend > end) rend = end;

            char name[5];
            aml_read_name(c->aml, end, &pos, name);
            pos++; /* SystemLevel */
            pos += 2; /* ResourceOrder */
            int idx = aml_ns_add(c, name, pos, parent, AML_POWER_RES_OP, 0);
            aml_scan_scope(c, pos, rend, (u32)idx);
            pos = rend;
            continue;
        }

        if (op == AML_EXT_OP_PREFIX) {
            if (pos >= end) break;
            u8 ext = c->aml[pos++];
            if (ext == AML_OP_REGION_OP || ext == AML_FIELD_OP ||
                ext == AML_INDEX_FIELD_OP || ext == AML_BANK_FIELD_OP) {
                u32 pl_pos = pos;
                u32 pl = aml_pkg_length(c->aml, end, &pos);
                u32 rstart = pl_pos;
                u32 rend   = rstart + pl;
                if (rend > end) rend = end;
                pos = rend;
                continue;
            }
            continue;
        }
    }
}

static u64 aml_exec(aml_ctx_t *c, u32 start, u32 end);

static u64 aml_read_target(aml_ctx_t *c, u32 *pos, u32 end) {
    if (*pos >= end) return 0;
    u8 op = c->aml[(*pos)++];

    if (op >= AML_LOCAL0_OP && op <= AML_LOCAL7_OP) {
        return c->locals[op - AML_LOCAL0_OP];
    }
    if (op >= AML_ARG0_OP && op <= AML_ARG6_OP) {
        return c->args[op - AML_ARG0_OP];
    }
    if (op == AML_ZERO_OP) return 0;
    if (op == AML_ONE_OP)  return 1;
    if (op == AML_ONES_OP) return ~(u64)0;
    if (op == AML_BYTE_PREFIX) {
        if (*pos >= end) return 0;
        return c->aml[(*pos)++];
    }
    if (op == AML_WORD_PREFIX) {
        if (*pos + 2 > end) return 0;
        u64 v = read_u16(c->aml + *pos); *pos += 2; return v;
    }
    if (op == AML_DWORD_PREFIX) {
        if (*pos + 4 > end) return 0;
        u64 v = read_u32(c->aml + *pos); *pos += 4; return v;
    }
    if (op == AML_QWORD_PREFIX) {
        if (*pos + 8 > end) return 0;
        u64 v = read_u64(c->aml + *pos); *pos += 8; return v;
    }
    (*pos)--;
    return 0;
}

static void aml_write_target(aml_ctx_t *c, u32 *pos, u32 end, u64 value) {
    if (*pos >= end) return;
    u8 op = c->aml[(*pos)++];

    if (op >= AML_LOCAL0_OP && op <= AML_LOCAL7_OP) {
        c->locals[op - AML_LOCAL0_OP] = (u32)value;
        return;
    }
    if (op >= AML_ARG0_OP && op <= AML_ARG6_OP) {
        c->args[op - AML_ARG0_OP] = (u32)value;
        return;
    }
}

static u64 aml_exec(aml_ctx_t *c, u32 start, u32 end) {
    u32 pos = start;
    u64 last = 0;

    while (pos < end && !c->returned) {
        u8 op = c->aml[pos++];

        switch (op) {

        case AML_ZERO_OP: last = 0; break;
        case AML_ONE_OP:  last = 1; break;
        case AML_ONES_OP: last = ~(u64)0; break;

        case AML_BYTE_PREFIX:
            if (pos >= end) return last;
            last = c->aml[pos++];
            break;
        case AML_WORD_PREFIX:
            if (pos + 2 > end) return last;
            last = read_u16(c->aml + pos); pos += 2;
            break;
        case AML_DWORD_PREFIX:
            if (pos + 4 > end) return last;
            last = read_u32(c->aml + pos); pos += 4;
            break;
        case AML_QWORD_PREFIX:
            if (pos + 8 > end) return last;
            last = read_u64(c->aml + pos); pos += 8;
            break;
        case AML_STRING_PREFIX:
            while (pos < end && c->aml[pos]) pos++;
            pos++;
            break;

        case AML_LOCAL0_OP: case AML_LOCAL1_OP: case AML_LOCAL2_OP:
        case AML_LOCAL3_OP: case AML_LOCAL4_OP: case AML_LOCAL5_OP:
        case AML_LOCAL6_OP: case AML_LOCAL7_OP:
            last = c->locals[op - AML_LOCAL0_OP];
            break;

        case AML_ARG0_OP: case AML_ARG1_OP: case AML_ARG2_OP:
        case AML_ARG3_OP: case AML_ARG4_OP: case AML_ARG5_OP:
        case AML_ARG6_OP:
            last = c->args[op - AML_ARG0_OP];
            break;

        case AML_NOOP_OP:
            break;

        case AML_RETURN_OP:
            last = aml_read_target(c, &pos, end);
            c->ret_value = last;
            c->returned = 1;
            break;

        case AML_STORE_OP: {
            u64 value = aml_read_target(c, &pos, end);
            aml_write_target(c, &pos, end, value);
            last = value;
            break;
        }

        case AML_ADD_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            u32 tgt = pos;
            (void)tgt;
            aml_write_target(c, &pos, end, a + b);
            last = a + b;
            break;
        }
        case AML_SUBTRACT_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            aml_write_target(c, &pos, end, a - b);
            last = a - b;
            break;
        }
        case AML_AND_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            aml_write_target(c, &pos, end, a & b);
            last = a & b;
            break;
        }
        case AML_OR_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            aml_write_target(c, &pos, end, a | b);
            last = a | b;
            break;
        }
        case AML_XOR_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            aml_write_target(c, &pos, end, a ^ b);
            last = a ^ b;
            break;
        }

        case AML_LNOT_OP: {
            u64 a = aml_read_target(c, &pos, end);
            last = (a == 0) ? 1 : 0;
            break;
        }
        case AML_LEQUAL_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            last = (a == b) ? 1 : 0;
            break;
        }
        case AML_LGREATER_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            last = (a > b) ? 1 : 0;
            break;
        }
        case AML_LLESS_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            last = (a < b) ? 1 : 0;
            break;
        }
        case AML_LAND_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            last = (a && b) ? 1 : 0;
            break;
        }
        case AML_LOR_OP: {
            u64 a = aml_read_target(c, &pos, end);
            u64 b = aml_read_target(c, &pos, end);
            last = (a || b) ? 1 : 0;
            break;
        }

        case AML_NOTIFY_OP: {
            (void)aml_read_target(c, &pos, end);
            (void)aml_read_target(c, &pos, end);
            break;
        }

        case AML_IF_OP: {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 if_end = pl_pos + pl;
            if (if_end > end) if_end = end;

            u64 cond = aml_read_target(c, &pos, end);

            if (cond) {
                aml_exec(c, pos, if_end);
                pos = if_end;
                if (pos < end && c->aml[pos] == AML_ELSE_OP) {
                    pos++;
                    u32 pl2_pos = pos;
                    u32 pl2 = aml_pkg_length(c->aml, end, &pos);
                    u32 else_end = pl2_pos + pl2;
                    if (else_end > end) else_end = end;
                    pos = else_end;
                }
            } else {
                pos = if_end;
                if (pos < end && c->aml[pos] == AML_ELSE_OP) {
                    pos++;
                    u32 pl2_pos = pos;
                    u32 pl2 = aml_pkg_length(c->aml, end, &pos);
                    u32 else_end = pl2_pos + pl2;
                    if (else_end > end) else_end = end;
                    aml_exec(c, pos, else_end);
                    pos = else_end;
                }
            }
            break;
        }
        case AML_ELSE_OP: {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 else_end = pl_pos + pl;
            if (else_end > end) else_end = end;
            pos = else_end;
            break;
        }

        case AML_METHOD_OP: {
            u32 pl_pos = pos;
            u32 pl = aml_pkg_length(c->aml, end, &pos);
            u32 mend = pl_pos + pl;
            if (mend > end) mend = end;
            pos = mend;
            break;
        }

        case AML_EXT_OP_PREFIX: {
            if (pos >= end) return last;
            u8 ext = c->aml[pos++];
            switch (ext) {
                case AML_ACQUIRE_OP: {
                    (void)aml_read_target(c, &pos, end);
                    if (pos < end) pos += 2; /* Timeout WORD */
                    break;
                }
                case AML_RELEASE_OP: {
                    (void)aml_read_target(c, &pos, end);
                    break;
                }
                case AML_STALL_OP: {
                    (void)aml_read_target(c, &pos, end);
                    break;
                }
                case AML_SLEEP_OP: {
                    (void)aml_read_target(c, &pos, end);
                    break;
                }
                default:
                    break;
            }
            break;
        }

        default:
            break;
        }
    }

    return last;
}

static void aml_run_method(aml_ctx_t *c, u32 method_body_start, u32 method_end) {
    c->returned = 0;
    c->ret_value = 0;
    for (u32 i = 0; i < 8; i++) c->locals[i] = 0;
    for (u32 i = 0; i < 7; i++) c->args[i] = 0;

    aml_exec(c, method_body_start, method_end);
}

static void acpi_call_ac0(void) {
    if (!g_ac0_found) {
        acpi_log("[acpi] no _AC0 found, cannot call\n");
        return;
    }
    acpi_log("[acpi] calling _AC0 at offset ");
    acpi_log_hex(g_ac0_offset);
    acpi_log("\n");

    aml_ctx_t *c = &g_aml_ctx;
    u32 start = g_ac0_offset;
    u32 end   = c->aml_len;

    aml_run_method(c, start, end);

    acpi_log("[acpi] _AC0 returned ");
    acpi_log_hex(c->ret_value);
    acpi_log("\n");
}

static void scan_dsdt_for_ac_methods(void) {
    if (!g_info.dsdt_address) {
        acpi_log("[acpi] no DSDT\n");
        return;
    }

    const acpi_header_t *dsdt = (const acpi_header_t *)g_info.dsdt_address;
    if (!table_valid(dsdt) || !bytes_equal(dsdt->signature, "DSDT", 4)) {
        acpi_log("[acpi] DSDT invalid\n");
        return;
    }

    const u8 *aml = (const u8 *)dsdt + sizeof(acpi_header_t);
    u32 aml_len = dsdt->length - sizeof(acpi_header_t);

    g_aml_ctx.aml     = aml;
    g_aml_ctx.aml_len = aml_len;
    g_aml_ctx.ns_count = 0;
    g_ac0_found = 0;

    acpi_log("[acpi] scan DSDT, len=");
    acpi_log_hex(aml_len);
    acpi_log("\n");

    aml_scan_scope(&g_aml_ctx, 0, aml_len, 0xFFFFFFFF);

    acpi_log("[acpi] namespace nodes=");
    acpi_log_dec(g_aml_ctx.ns_count);
    acpi_log("\n");

    for (u32 i = 0; i < g_aml_ctx.ns_count; i++) {
        aml_ns_node_t *n = &g_aml_ctx.ns[i];
        if (!n->valid) continue;
        if (n->type == AML_THERMAL_ZONE_OP) {
            acpi_log("[acpi] ThermalZone '");
            acpi_log(n->name);
            acpi_log("'\n");
        }
        if (n->name[0] == '_' && n->name[1] == 'A' &&
            n->name[2] == 'C' && n->name[3] >= '0' && n->name[3] <= '9') {
            acpi_log("[acpi] found ");
            acpi_log(n->name);
            acpi_log(" at offset ");
            acpi_log_hex(n->offset);
            acpi_log(" type=");
            acpi_log_hex(n->type);
            acpi_log("\n");
            if (n->name[3] == '0' && n->type == AML_METHOD_OP) {
                g_ac0_offset = n->offset;
                g_ac0_scope  = n->parent;
                g_ac0_found  = 1;
            }
        }
    }

    if (g_ac0_found) {
        acpi_log("[acpi] _AC0 offset=");
        acpi_log_hex(g_ac0_offset);
        acpi_log(" scope=");
        acpi_log_hex(g_ac0_scope);
        acpi_log("\n");
    } else {
        acpi_log("[acpi] _AC0 not found\n");
    }
}

static int parse_aml_integer(const u8 *aml, u32 end, u32 *offset, u64 *value) {
    u8 opcode;
    u32 width;
    if (*offset >= end) return 0;
    opcode = aml[(*offset)++];

    if (opcode == 0x00) { *value = 0; return 1; }
    if (opcode == 0x01) { *value = 1; return 1; }
    if (opcode == 0xFF) { *value = ~(u64)0; return 1; }

    switch (opcode) {
        case 0x0A: width = 1; break;
        case 0x0B: width = 2; break;
        case 0x0C: width = 4; break;
        case 0x0E: width = 8; break;
        default: return 0;
    }
    if (width > end - *offset) return 0;

    *value = width == 1 ? aml[*offset] :
             width == 2 ? read_u16(aml + *offset) :
             width == 4 ? read_u32(aml + *offset) :
                          read_u64(aml + *offset);
    *offset += width;
    return 1;
}

static int find_s5_sleep_types(u64 dsdt_address, u8 *type_a, u8 *type_b) {
    const acpi_header_t *dsdt = (const acpi_header_t *)dsdt_address;
    const u8 *aml;
    u32 aml_length;

    if (!dsdt || !table_valid(dsdt) ||
        !bytes_equal(dsdt->signature, "DSDT", 4)) return 0;

    aml = (const u8 *)dsdt + sizeof(acpi_header_t);
    aml_length = dsdt->length - sizeof(acpi_header_t);

    for (u32 i = 0; i + 5 < aml_length; i++) {
        u32 cursor;
        u32 package_end;
        u8 element_count;
        u64 first;
        u64 second;

        if (aml[i] != '_' || aml[i + 1] != 'S' ||
            aml[i + 2] != '5' || aml[i + 3] != '_' ||
            aml[i + 4] != 0x12) continue;

        cursor = i + 5;
        if (!aml_pkg_length_ok(aml, aml_length, &cursor, &package_end)) continue;
        element_count = aml[cursor++];
        if (element_count < 2) continue;
        if (!parse_aml_integer(aml, package_end, &cursor, &first) ||
            !parse_aml_integer(aml, package_end, &cursor, &second)) continue;

        *type_a = (u8)(first & 7);
        *type_b = (u8)(second & 7);
        return 1;
    }
    return 0;
}

int aml_pkg_length_ok(const u8 *aml, u32 end, u32 *offset, u32 *package_end) {
    u8 lead;
    u32 following;
    u32 length;
    u32 length_start = *offset;

    if (*offset >= end) return 0;
    lead = aml[(*offset)++];
    following = lead >> 6;
    if (following > end - *offset) return 0;

    if (following == 0) {
        length = lead & 0x3F;
    } else {
        length = lead & 0x0F;
        for (u32 i = 0; i < following; i++)
            length |= (u32)aml[(*offset)++] << (4 + 8 * i);
    }

    if (length < *offset - length_start) return 0;
    if (length > end - length_start) return 0;
    *package_end = length_start + length;
    return 1;
}

static int gas_io_port(const u8 *fadt, u32 length, u32 offset, u32 *out_port) {
    const acpi_gas_t *gas;
    if (offset > length || sizeof(acpi_gas_t) > length - offset) return 0;
    gas = (const acpi_gas_t *)(fadt + offset);
    if (gas->address_space_id != ACPI_GAS_SYSTEM_IO ||
        gas->register_bit_offset != 0 ||
        gas->address == 0 || gas->address > 0xFFFF) return 0;
    *out_port = (u32)gas->address;
    return 1;
}

static void parse_fadt(const acpi_header_t *header) {
    const u8 *fadt = (const u8 *)header;
    u32 length = header->length;
    u32 dsdt_address = 0;

    g_info.fadt_status = DEPRESSURE_ACPI_FADT_INVALID;
    if (length < 116) return;

    g_info.sci_interrupt = read_u16(fadt + 46);
    g_info.smi_command_port = read_u32(fadt + 48);
    g_info.acpi_enable_value = fadt[52];
    g_info.acpi_disable_value = fadt[53];
    g_info.pm1a_event_port = read_u32(fadt + 56);
    g_info.pm1b_event_port = read_u32(fadt + 60);
    g_info.pm1a_control_port = read_u32(fadt + 64);
    g_info.pm1b_control_port = read_u32(fadt + 68);
    g_info.pm2_control_port = read_u32(fadt + 72);
    g_info.pm_timer_port = read_u32(fadt + 76);
    g_info.gpe0_port = read_u32(fadt + 80);
    g_info.gpe1_port = read_u32(fadt + 84);
    g_info.pm1_event_length = fadt[88];
    g_info.pm1_control_length = fadt[89];
    g_info.pm2_control_length = fadt[90];
    g_info.pm_timer_length = fadt[91];
    g_info.gpe0_length = fadt[92];
    g_info.gpe1_length = fadt[93];
    g_info.boot_architecture_flags = length >= 111 ? read_u16(fadt + 109) : 0;
    g_info.fadt_flags = read_u32(fadt + 112);
    dsdt_address = read_u32(fadt + 40);
    if (length >= 148) {
        u64 extended_dsdt = read_u64(fadt + 140);
        if (extended_dsdt) dsdt_address = (u32)extended_dsdt;
        if (extended_dsdt > 0xFFFFFFFFULL) g_info.dsdt_address = extended_dsdt;
    }
    if (!g_info.dsdt_address) g_info.dsdt_address = dsdt_address;

    if (length >= 129 && (g_info.fadt_flags & ACPI_FADT_RESET_SUPPORTED)) {
        const acpi_gas_t *reset = (const acpi_gas_t *)(fadt + 116);
        g_info.reset_address_space = reset->address_space_id;
        g_info.reset_register_width = reset->register_bit_width;
        g_info.reset_register_offset = reset->register_bit_offset;
        g_info.reset_access_size = reset->access_size;
        g_info.reset_register_address = reset->address;
        g_info.reset_value = fadt[128];
    }

    if (length >= 148) {
        u32 port;
        if (gas_io_port(fadt, length, 148, &port)) g_info.pm1a_event_port = port;
        if (gas_io_port(fadt, length, 160, &port)) g_info.pm1b_event_port = port;
        if (gas_io_port(fadt, length, 172, &port)) g_info.pm1a_control_port = port;
        if (gas_io_port(fadt, length, 184, &port)) g_info.pm1b_control_port = port;
        if (gas_io_port(fadt, length, 196, &port)) g_info.pm2_control_port = port;
        if (gas_io_port(fadt, length, 208, &port)) g_info.pm_timer_port = port;
        if (gas_io_port(fadt, length, 220, &port)) g_info.gpe0_port = port;
        if (gas_io_port(fadt, length, 232, &port)) g_info.gpe1_port = port;
    }

    if (g_info.smi_command_port > 0xFFFF) g_info.smi_command_port = 0;
    if (g_info.pm1a_control_port > 0xFFFE) g_info.pm1a_control_port = 0;
    if (g_info.pm1b_control_port > 0xFFFE) g_info.pm1b_control_port = 0;
    if (g_info.pm1a_event_port > 0xFFFC) g_info.pm1a_event_port = 0;
    if (g_info.pm1b_event_port > 0xFFFC) g_info.pm1b_event_port = 0;
    if (g_info.pm2_control_port > 0xFFFF) g_info.pm2_control_port = 0;
    if (g_info.pm_timer_port > 0xFFFC) g_info.pm_timer_port = 0;
    if (g_info.gpe0_port > 0xFFFE) g_info.gpe0_port = 0;
    if (g_info.gpe1_port > 0xFFFE) g_info.gpe1_port = 0;

    if (g_info.pm1a_control_port && g_info.pm1_control_length >= 2) {
        if (!(io_inw((u16)g_info.pm1a_control_port) & ACPI_PM1_SCI_ENABLE) &&
            g_info.smi_command_port && g_info.acpi_enable_value) {
            io_outb((u16)g_info.smi_command_port, g_info.acpi_enable_value);
            for (u32 wait = 0; wait < 1000000; wait++) {
                if (io_inw((u16)g_info.pm1a_control_port) &
                    ACPI_PM1_SCI_ENABLE) break;
            }
        }
        g_info.acpi_enabled =
            (io_inw((u16)g_info.pm1a_control_port) & ACPI_PM1_SCI_ENABLE) != 0;
    }

    g_info.s5_available = find_s5_sleep_types(g_info.dsdt_address,
                                               &g_info.s5_type_a,
                                               &g_info.s5_type_b);
    if (!g_info.s5_available)
        g_info.fadt_status = DEPRESSURE_ACPI_S5_NOT_FOUND;
    else if (!g_info.pm1a_control_port ||
             g_info.pm1_control_length < 2 || !g_info.acpi_enabled)
        g_info.fadt_status = DEPRESSURE_ACPI_POWER_UNAVAILABLE;
    else
        g_info.fadt_status = DEPRESSURE_STATUS_OK;
}

static dstatus_t acpi_shutdown(void) {
    u16 value;
    if (!g_info.s5_available || !g_info.pm1a_control_port ||
        g_info.pm1_control_length < 2 || !g_info.acpi_enabled)
        return DEPRESSURE_ACPI_POWER_UNAVAILABLE;

    value = io_inw((u16)g_info.pm1a_control_port);
    value &= (u16)~(ACPI_PM1_SLEEP_TYPE_MASK | ACPI_PM1_SLEEP_ENABLE);
    value |= (u16)((g_info.s5_type_a & 7) << 10);
    value |= ACPI_PM1_SLEEP_ENABLE;
    io_outw((u16)g_info.pm1a_control_port, value);

    if (g_info.pm1b_control_port) {
        value = io_inw((u16)g_info.pm1b_control_port);
        value &= (u16)~(ACPI_PM1_SLEEP_TYPE_MASK | ACPI_PM1_SLEEP_ENABLE);
        value |= (u16)((g_info.s5_type_b & 7) << 10);
        value |= ACPI_PM1_SLEEP_ENABLE;
        io_outw((u16)g_info.pm1b_control_port, value);
    }
    return DEPRESSURE_TIMEOUT;
}

static dstatus_t acpi_reboot(void) {
    u32 port, bytes;
    if (!(g_info.fadt_flags & ACPI_FADT_RESET_SUPPORTED) ||
        g_info.reset_address_space != ACPI_GAS_SYSTEM_IO ||
        g_info.reset_register_offset != 0 ||
        !g_info.reset_register_address ||
        (g_info.reset_register_width != 8 &&
         g_info.reset_register_width != 16 &&
         g_info.reset_register_width != 32)) {
        if (!(g_info.boot_architecture_flags & 2))
            return DEPRESSURE_ACPI_RESET_UNAVAILABLE;
        for (u32 wait = 0; wait < 1000000; wait++) {
            if (!(io_inb(0x64) & 2)) {
                io_outb(0x64, 0xFE);
                return DEPRESSURE_TIMEOUT;
            }
        }
        return DEPRESSURE_TIMEOUT;
    }
    bytes = g_info.reset_register_width / 8;
    if (g_info.reset_register_address > 0x10000 - bytes)
        return DEPRESSURE_ACPI_RESET_UNAVAILABLE;
    port = (u32)g_info.reset_register_address;
    if (g_info.reset_register_width == 8) io_outb((u16)port, g_info.reset_value);
    else if (g_info.reset_register_width == 16) io_outw((u16)port, g_info.reset_value);
    else io_outl((u16)port, g_info.reset_value);
    return DEPRESSURE_TIMEOUT;
}

static void sample_fadt_registers(void) {
    if (g_info.pm1a_control_port && g_info.pm1_control_length >= 2)
        g_info.pm1a_control_value = io_inw((u16)g_info.pm1a_control_port);
    if (g_info.pm1b_control_port && g_info.pm1_control_length >= 2)
        g_info.pm1b_control_value = io_inw((u16)g_info.pm1b_control_port);
    if (g_info.pm1a_event_port && g_info.pm1_event_length >= 4) {
        g_info.pm1a_event_status = io_inw((u16)g_info.pm1a_event_port);
        g_info.pm1a_event_enable = io_inw((u16)(g_info.pm1a_event_port + 2));
    }
    if (g_info.pm1b_event_port && g_info.pm1_event_length >= 4) {
        g_info.pm1b_event_status = io_inw((u16)g_info.pm1b_event_port);
        g_info.pm1b_event_enable = io_inw((u16)(g_info.pm1b_event_port + 2));
    }
    if (g_info.pm2_control_port && g_info.pm2_control_length)
        g_info.pm2_control_value = io_inb((u16)g_info.pm2_control_port);
    if (g_info.pm_timer_port && g_info.pm_timer_length >= 4)
        g_info.fadt_pm_timer_value = io_inl((u16)g_info.pm_timer_port);
    if (g_info.gpe0_port && g_info.gpe0_length >= 2) {
        u32 ep = g_info.gpe0_port + g_info.gpe0_length / 2;
        g_info.gpe0_status = io_inw((u16)g_info.gpe0_port);
        if (ep <= 0xFFFE) g_info.gpe0_enable = io_inw((u16)ep);
    }
    if (g_info.gpe1_port && g_info.gpe1_length >= 2) {
        u32 ep = g_info.gpe1_port + g_info.gpe1_length / 2;
        g_info.gpe1_status = io_inw((u16)g_info.gpe1_port);
        if (ep <= 0xFFFE) g_info.gpe1_enable = io_inw((u16)ep);
    }
}

static void add_table(const acpi_header_t *header) {
    acpi_table_info_t *entry;
    if (!table_valid(header)) return;
    for (u32 i = 0; i < g_info.table_count; i++)
        if (g_tables[i].address == (u64)header) return;
    if (g_info.table_count >= ACPI_MAX_TABLES) return;

    entry = &g_tables[g_info.table_count++];
    for (u32 i = 0; i < 4; i++) entry->signature[i] = header->signature[i];
    entry->signature[4] = 0;
    entry->length   = header->length;
    entry->revision = header->revision;
    entry->checksum = header->checksum;
    entry->address  = (u64)header;

    if (bytes_equal(header->signature, "FACP", 4)) {
        g_info.fadt_address = (u64)header;
        parse_fadt(header);
    } else if (bytes_equal(header->signature, "APIC", 4)) {
        g_info.madt_address = (u64)header;
        if (header->length >= 44) {
            const u8 *madt = (const u8 *)header;
            u32 offset = 44;
            g_info.local_apic_address = *(const u32 *)(madt + 36);
            while (offset + 2 <= header->length) {
                u8 type = madt[offset];
                u8 elen = madt[offset + 1];
                if (elen < 2 || elen > header->length - offset) break;
                if (type == 0 && elen >= 8) g_info.processor_count++;
                else if (type == 1 && elen >= 12) {
                    if (g_info.ioapic_count < ACPI_MAX_IOAPICS) {
                        g_info.ioapic_address[g_info.ioapic_count] =
                            (u64)*(const u32 *)(madt + offset + 4);
                        g_info.ioapic_gsi_base[g_info.ioapic_count] =
                            *(const u32 *)(madt + offset + 8);
                    }
                    g_info.ioapic_count++;
                } else if (type == 9 && elen >= 16) g_info.processor_count++;
                offset += elen;
            }
        }
    } else if (bytes_equal(header->signature, "BGRT", 4)) {
        if (header->length < 56) return;
        const u8 *bgrt = (const u8 *)header;
        g_info.bgrt_address = (u64)header;
        g_info.bgrt_status        = bgrt[0x28];
        g_info.bgrt_image_type    = bgrt[0x29];
        g_info.bgrt_image_address = read_u64(bgrt + 0x30);
        g_info.bgrt_image_offset_x = read_u32(bgrt + 0x38);
        g_info.bgrt_image_offset_y = read_u32(bgrt + 0x3C);
        g_info.bgrt_image_width   = 0;
        g_info.bgrt_image_height  = 0;
        g_info.bgrt_available     = 1;
    }
}

static int valid_rsdp(acpi_rsdp_t *rsdp) {
    if (!rsdp || !bytes_equal(rsdp->signature, "RSD PTR ", 8) ||
        !checksum_valid(rsdp, 20)) return 0;
    if (rsdp->revision >= 2) {
        if (rsdp->length < 36 || rsdp->length > 4096 ||
            !checksum_valid(rsdp, rsdp->length)) return 0;
    }
    return 1;
}

static void scan_root_table(u64 address, int xsdt) {
    const acpi_header_t *root = (const acpi_header_t *)address;
    u32 entry_size = xsdt ? sizeof(u64) : sizeof(u32);
    u32 payload_size, count;
    const u8 *entries;

    if (!table_valid(root)) return;
    if (!bytes_equal(root->signature, xsdt ? "XSDT" : "RSDT", 4)) return;
    if (root->length < sizeof(acpi_header_t)) return;

    payload_size = root->length - sizeof(acpi_header_t);
    if (payload_size % entry_size != 0) return;
    count = payload_size / entry_size;
    if (count > ACPI_MAX_TABLES) return;

    g_info.root_address = address;
    g_info.root_is_xsdt = xsdt != 0;
    add_table(root);
    entries = (const u8 *)root + sizeof(acpi_header_t);

    for (u32 i = 0; i < count; i++) {
        u64 ta = xsdt ? *(const u64 *)(entries + i * entry_size)
                      : *(const u32 *)(entries + i * entry_size);
        if (ta) add_table((const acpi_header_t *)ta);
    }
    if (g_info.dsdt_address)
        add_table((const acpi_header_t *)g_info.dsdt_address);
}

static void refresh_piix4(void) {
    ddriver_t *pci_driver;
    pci_api_t *pci;
    u32 count;

    g_info.piix4_found = 0;
    g_info.piix4_command = 0;
    g_info.piix4_pm_io_enabled = 0;
    g_info.piix4_pm_base = 0;
    g_info.piix4_pm_timer = 0;
    if (!g_kernel_api || !g_kernel_api->DFindAndGetDriver) return;

    pci_driver = g_kernel_api->DFindAndGetDriver("pci");
    if (!pci_driver || !pci_driver->api) return;
    pci = (pci_api_t *)pci_driver->api;
    if (!pci->get_count || !pci->get_device ||
        DSTATUS_IS_ERR(pci->get_count(&count))) return;

    for (u32 i = 0; i < count; i++) {
        const pci_device_t *d = 0;
        if (DSTATUS_IS_ERR(pci->get_device(i, &d)) || !d) continue;
        if (d->vendor_id == 0x8086 && d->device_id == 0x7113) {
            u32 pm_base_reg = 0, pm_misc_reg = 0;
            g_info.piix4_found = 1;
            g_info.piix4_bus = d->bus;
            g_info.piix4_slot = d->slot;
            g_info.piix4_func = d->func;
            g_info.piix4_command = d->command;
            if (pci->read_config32 &&
                DSTATUS_IS_OK(pci->read_config32(d->bus, d->slot, d->func, 0x40, &pm_base_reg)) &&
                DSTATUS_IS_OK(pci->read_config32(d->bus, d->slot, d->func, 0x80, &pm_misc_reg))) {
                g_info.piix4_pm_base = (u16)(pm_base_reg & PIIX4_PM_BASE_MASK);
                g_info.piix4_pm_io_enabled = (u8)((pm_misc_reg & 1) && (d->command & 1));
                if (g_info.piix4_pm_io_enabled && g_info.piix4_pm_base >= 0x100)
                    g_info.piix4_pm_timer = io_inl((u16)(g_info.piix4_pm_base + 0x08));
            }
            return;
        }
    }
}

static dstatus_t api_get_info(acpi_system_info_t *out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    refresh_piix4();
    sample_fadt_registers();
    *out = g_info;
    return g_info.status;
}
static dstatus_t api_get_table_count(u32 *out_count) {
    if (!out_count) return DEPRESSURE_NULL_DATA;
    *out_count = g_info.table_count;
    return g_info.status;
}
static dstatus_t api_get_table(u32 index, const acpi_table_info_t **out) {
    if (!out) return DEPRESSURE_NULL_DATA;
    if (index >= g_info.table_count) return DEPRESSURE_OUT_OF_RANGE;
    *out = &g_tables[index];
    return DEPRESSURE_STATUS_OK;
}
static dstatus_t api_get_ioapic(u32 index, u64 *out_addr, u32 *out_gsi_base) {
    if (!out_addr || !out_gsi_base) return DEPRESSURE_NULL_DATA;
    if (index >= ACPI_MAX_IOAPICS || index >= g_info.ioapic_count)
        return DEPRESSURE_OUT_OF_RANGE;
    *out_addr     = g_info.ioapic_address[index];
    *out_gsi_base = g_info.ioapic_gsi_base[index];
    return DEPRESSURE_STATUS_OK;
}

/* Публічний API для виклику _AC0 з DepMgr (або з DevCon) */
int acpi_start_cooling(void) {
    acpi_call_ac0();
    return g_ac0_found ? 0 : -1;
}

void acpi_main(void *api, void *fb_info) {
    void *rsdp_pointer = 0;
    acpi_rsdp_t *rsdp;
    u64 status;

    (void)fb_info;
    acpi_uart_init();
    acpi_log("[acpi] driver entry\n");
    g_kernel_api = (dkernel_api_t *)api;
    g_info.status = DEPRESSURE_NOT_FOUND;
    g_info.fadt_status = DEPRESSURE_ACPI_FADT_NOT_FOUND;

    status = uefi_get_acpi_rsdp(&rsdp_pointer);
    acpi_log("[acpi] RSDP status=");
    acpi_log_hex(status);
    acpi_log(" ptr=");
    acpi_log_hex((u64)rsdp_pointer);
    acpi_log("\n");

    if (status == 0 && valid_rsdp((acpi_rsdp_t *)rsdp_pointer)) {
        rsdp = (acpi_rsdp_t *)rsdp_pointer;
        g_info.rsdp_address = (u64)rsdp;
        if (rsdp->revision >= 2 && rsdp->xsdt_address)
            scan_root_table(rsdp->xsdt_address, 1);
        if (!g_info.root_address && rsdp->rsdt_address)
            scan_root_table(rsdp->rsdt_address, 0);
        g_info.status = g_info.root_address ? DEPRESSURE_STATUS_OK : DEPRESSURE_CORRUPTED_DATA;
        scan_dsdt_for_ac_methods();
    } else if (status != 0) {
        g_info.status = status == 14 ? DEPRESSURE_NOT_FOUND : DEPRESSURE_IO_ERROR;
    } else {
        g_info.status = DEPRESSURE_CHECKSUM_ERROR;
    }

    refresh_piix4();

    g_acpi_api.version          = ACPI_API_VERSION;
    g_acpi_api._pad             = 0;
    g_acpi_api.get_info         = api_get_info;
    g_acpi_api.get_table_count  = api_get_table_count;
    g_acpi_api.get_table        = api_get_table;
    g_acpi_api.get_ioapic       = api_get_ioapic;
    g_acpi_api.shutdown         = acpi_shutdown;
    g_acpi_api.reboot           = acpi_reboot;
    g_acpi_driver.api           = (u64)&g_acpi_api;

    if (g_kernel_api && g_kernel_api->register_driver)
        g_kernel_api->register_driver(&g_acpi_driver);

    acpi_log("[acpi] init complete status=");
    acpi_log_hex(g_info.status);
    acpi_log(" tables=");
    acpi_log_hex(g_info.table_count);
    acpi_log("\n");
}