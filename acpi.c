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

static dkernel_api_t *g_kernel_api;
static acpi_system_info_t g_info;
static acpi_table_info_t g_tables[ACPI_MAX_TABLES];
static acpi_api_t g_acpi_api;

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
    for (int shift = 60; shift >= 0; shift -= 4) {
        acpi_log_char(hex[(value >> shift) & 0xF]);
    }
}

static void acpi_log_signature(const char signature[4]) {
    for (u32 i = 0; i < 4; i++) {
        acpi_log_char(signature[i]);
    }
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
    for (u32 i = 0; i < count; i++) {
        if (left[i] != right[i]) return 0;
    }
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
    return (u32)data[0] |
           ((u32)data[1] << 8) |
           ((u32)data[2] << 16) |
           ((u32)data[3] << 24);
}

static u64 read_u64(const u8 *data) {
    return (u64)read_u32(data) | ((u64)read_u32(data + 4) << 32);
}

static int parse_aml_integer(const u8 *aml, u32 end, u32 *offset, u64 *value) {
    u8 opcode;
    u32 width;

    if (*offset >= end) return 0;
    opcode = aml[(*offset)++];

    if (opcode == 0x00) {
        *value = 0;
        return 1;
    }
    if (opcode == 0x01) {
        *value = 1;
        return 1;
    }
    if (opcode == 0xFF) {
        *value = ~(u64)0;
        return 1;
    }

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

static int aml_package_length(const u8 *aml, u32 end, u32 *offset,
                              u32 *package_end) {
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
        for (u32 i = 0; i < following; i++) {
            length |= (u32)aml[(*offset)++] << (4 + 8 * i);
        }
    }

    if (length < *offset - length_start) return 0;
    if (length > end - length_start) return 0;
    *package_end = length_start + length;
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
        if (!aml_package_length(aml, aml_length, &cursor, &package_end) ||
            package_end > aml_length || cursor >= package_end) continue;
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

static int gas_io_port(const u8 *fadt, u32 length, u32 offset,
                       u32 *out_port) {
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
        if (extended_dsdt > 0xFFFFFFFFULL) {
            g_info.dsdt_address = extended_dsdt;
        }
    }
    if (!g_info.dsdt_address) g_info.dsdt_address = dsdt_address;

    if (length >= 129 &&
        (g_info.fadt_flags & ACPI_FADT_RESET_SUPPORTED)) {
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
            (io_inw((u16)g_info.pm1a_control_port) &
             ACPI_PM1_SCI_ENABLE) != 0;
    }

    g_info.s5_available = find_s5_sleep_types(g_info.dsdt_address,
                                               &g_info.s5_type_a,
                                               &g_info.s5_type_b);
    if (!g_info.s5_available) {
        g_info.fadt_status = DEPRESSURE_ACPI_S5_NOT_FOUND;
    } else if (!g_info.pm1a_control_port ||
               g_info.pm1_control_length < 2 || !g_info.acpi_enabled) {
        g_info.fadt_status = DEPRESSURE_ACPI_POWER_UNAVAILABLE;
    } else {
        g_info.fadt_status = DEPRESSURE_STATUS_OK;
    }

    acpi_log("[acpi] FADT PM1a control=");
    acpi_log_hex(g_info.pm1a_control_port);
    acpi_log(" PM1b control=");
    acpi_log_hex(g_info.pm1b_control_port);
    acpi_log(" SCI_EN=");
    acpi_log_hex(g_info.acpi_enabled);
    acpi_log(" S5=");
    acpi_log_hex(g_info.s5_available);
    acpi_log(" typeA=");
    acpi_log_hex(g_info.s5_type_a);
    acpi_log(" typeB=");
    acpi_log_hex(g_info.s5_type_b);
    acpi_log(" status=");
    acpi_log_hex(g_info.fadt_status);
    acpi_log("\n");
}

static dstatus_t acpi_shutdown(void) {
    u16 value;

    if (!g_info.s5_available || !g_info.pm1a_control_port ||
        g_info.pm1_control_length < 2 || !g_info.acpi_enabled) {
        return DEPRESSURE_ACPI_POWER_UNAVAILABLE;
    }

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
    u32 port;
    u32 bytes;

    if (!(g_info.fadt_flags & ACPI_FADT_RESET_SUPPORTED) ||
        g_info.reset_address_space != ACPI_GAS_SYSTEM_IO ||
        g_info.reset_register_offset != 0 ||
        !g_info.reset_register_address ||
        (g_info.reset_register_width != 8 &&
         g_info.reset_register_width != 16 &&
         g_info.reset_register_width != 32)) {
        if (!(g_info.boot_architecture_flags & 2)) {
            return DEPRESSURE_ACPI_RESET_UNAVAILABLE;
        }
        for (u32 wait = 0; wait < 1000000; wait++) {
            if (!(io_inb(0x64) & 2)) {
                io_outb(0x64, 0xFE);
                return DEPRESSURE_TIMEOUT;
            }
        }
        return DEPRESSURE_TIMEOUT;
    }

    bytes = g_info.reset_register_width / 8;
    if (g_info.reset_register_address > 0x10000 - bytes) {
        return DEPRESSURE_ACPI_RESET_UNAVAILABLE;
    }
    port = (u32)g_info.reset_register_address;
    if (g_info.reset_register_width == 8) {
        io_outb((u16)port, g_info.reset_value);
    } else if (g_info.reset_register_width == 16) {
        io_outw((u16)port, g_info.reset_value);
    } else {
        io_outl((u16)port, g_info.reset_value);
    }
    return DEPRESSURE_TIMEOUT;
}

static void sample_fadt_registers(void) {
    if (g_info.pm1a_control_port && g_info.pm1_control_length >= 2) {
        g_info.pm1a_control_value =
            io_inw((u16)g_info.pm1a_control_port);
    }
    if (g_info.pm1b_control_port && g_info.pm1_control_length >= 2) {
        g_info.pm1b_control_value =
            io_inw((u16)g_info.pm1b_control_port);
    }
    if (g_info.pm1a_event_port && g_info.pm1_event_length >= 4) {
        g_info.pm1a_event_status = io_inw((u16)g_info.pm1a_event_port);
        g_info.pm1a_event_enable =
            io_inw((u16)(g_info.pm1a_event_port + 2));
    }
    if (g_info.pm1b_event_port && g_info.pm1_event_length >= 4) {
        g_info.pm1b_event_status = io_inw((u16)g_info.pm1b_event_port);
        g_info.pm1b_event_enable =
            io_inw((u16)(g_info.pm1b_event_port + 2));
    }
    if (g_info.pm2_control_port && g_info.pm2_control_length) {
        g_info.pm2_control_value = io_inb((u16)g_info.pm2_control_port);
    }
    if (g_info.pm_timer_port && g_info.pm_timer_length >= 4) {
        g_info.fadt_pm_timer_value = io_inl((u16)g_info.pm_timer_port);
    }
    if (g_info.gpe0_port && g_info.gpe0_length >= 2) {
        u32 enable_port = g_info.gpe0_port + g_info.gpe0_length / 2;
        g_info.gpe0_status = io_inw((u16)g_info.gpe0_port);
        if (enable_port <= 0xFFFE) {
            g_info.gpe0_enable = io_inw((u16)enable_port);
        }
    }
    if (g_info.gpe1_port && g_info.gpe1_length >= 2) {
        u32 enable_port = g_info.gpe1_port + g_info.gpe1_length / 2;
        g_info.gpe1_status = io_inw((u16)g_info.gpe1_port);
        if (enable_port <= 0xFFFE) {
            g_info.gpe1_enable = io_inw((u16)enable_port);
        }
    }
}

static void add_table(const acpi_header_t *header) {
    acpi_table_info_t *entry;

    acpi_log("[acpi] validate table @");
    acpi_log_hex((u64)header);
    acpi_log("\n");
    if (!table_valid(header)) {
        acpi_log("[acpi] invalid table header/checksum\n");
        return;
    }
    acpi_log("[acpi] table ");
    acpi_log_signature(header->signature);
    acpi_log(" length=");
    acpi_log_hex(header->length);
    acpi_log("\n");

    for (u32 i = 0; i < g_info.table_count; i++) {
        if (g_tables[i].address == (u64)header) return;
    }
    if (g_info.table_count >= ACPI_MAX_TABLES) return;

    entry = &g_tables[g_info.table_count++];
    for (u32 i = 0; i < 4; i++) entry->signature[i] = header->signature[i];
    entry->signature[4] = 0;
    entry->length = header->length;
    entry->revision = header->revision;
    entry->checksum = header->checksum;
    entry->address = (u64)header;

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
                u8 entry_length = madt[offset + 1];
                if (entry_length < 2 ||
                    entry_length > header->length - offset) break;
                if (type == 0 && entry_length >= 8) {
                    g_info.processor_count++;
                } else if (type == 1 && entry_length >= 12) {
                    g_info.ioapic_count++;
                } else if (type == 9 && entry_length >= 16) {
                    g_info.processor_count++;
                }
                offset += entry_length;
            }
        }
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
    u32 payload_size;
    u32 count;
    const u8 *entries;

    acpi_log("[acpi] scan ");
    acpi_log(xsdt ? "XSDT @ " : "RSDT @ ");
    acpi_log_hex(address);
    acpi_log("\n");

    if (!table_valid(root)) {
        acpi_log("[acpi] root checksum/header invalid\n");
        return;
    }
    if (!bytes_equal(root->signature, xsdt ? "XSDT" : "RSDT", 4)) {
        acpi_log("[acpi] root signature mismatch\n");
        return;
    }
    if (root->length < sizeof(acpi_header_t)) {
        acpi_log("[acpi] root length too small\n");
        return;
    }

    payload_size = root->length - sizeof(acpi_header_t);
    if (payload_size % entry_size != 0) {
        acpi_log("[acpi] malformed root entry array\n");
        return;
    }
    count = payload_size / entry_size;
    if (count > ACPI_MAX_TABLES) {
        acpi_log("[acpi] root entry count exceeds limit: ");
        acpi_log_hex(count);
        acpi_log("\n");
        return;
    }

    g_info.root_address = address;
    g_info.root_is_xsdt = xsdt != 0;
    acpi_log("[acpi] root entries=");
    acpi_log_hex(count);
    acpi_log("\n");
    add_table(root);
    entries = (const u8 *)root + sizeof(acpi_header_t);

    for (u32 i = 0; i < count; i++) {
        u64 table_address = xsdt ?
            *(const u64 *)(entries + i * entry_size) :
            *(const u32 *)(entries + i * entry_size);
        if (table_address) add_table((const acpi_header_t *)table_address);
    }

    if (g_info.dsdt_address) {
        add_table((const acpi_header_t *)g_info.dsdt_address);
    }
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
        const pci_device_t *device = 0;
        if (DSTATUS_IS_ERR(pci->get_device(i, &device)) || !device) continue;
        if (device->vendor_id == 0x8086 && device->device_id == 0x7113) {
            u32 pm_base_reg = 0;
            u32 pm_misc_reg = 0;
            g_info.piix4_found = 1;
            g_info.piix4_bus = device->bus;
            g_info.piix4_slot = device->slot;
            g_info.piix4_func = device->func;
            g_info.piix4_command = device->command;
            if (pci->read_config32 &&
                DSTATUS_IS_OK(pci->read_config32(device->bus, device->slot,
                                                 device->func, 0x40,
                                                 &pm_base_reg)) &&
                DSTATUS_IS_OK(pci->read_config32(device->bus, device->slot,
                                                 device->func, 0x80,
                                                 &pm_misc_reg))) {
                g_info.piix4_pm_base = (u16)(pm_base_reg & PIIX4_PM_BASE_MASK);
                g_info.piix4_pm_io_enabled =
                    (u8)((pm_misc_reg & 1) && (device->command & 1));
                if (g_info.piix4_pm_io_enabled && g_info.piix4_pm_base >= 0x100) {
                    g_info.piix4_pm_timer =
                        io_inl((u16)(g_info.piix4_pm_base + 0x08));
                }
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

    acpi_log("[acpi] locating UEFI RSDP\n");
    status = uefi_get_acpi_rsdp(&rsdp_pointer);
    acpi_log("[acpi] RSDP lookup status=");
    acpi_log_hex(status);
    acpi_log(" pointer=");
    acpi_log_hex((u64)rsdp_pointer);
    acpi_log("\n");
    if (status == 0 && valid_rsdp((acpi_rsdp_t *)rsdp_pointer)) {
        rsdp = (acpi_rsdp_t *)rsdp_pointer;
        g_info.rsdp_address = (u64)rsdp;
        acpi_log("[acpi] RSDP valid revision=");
        acpi_log_hex(rsdp->revision);
        acpi_log("\n");
        if (rsdp->revision >= 2 && rsdp->xsdt_address) {
            acpi_log("[acpi] XSDT address=");
            acpi_log_hex(rsdp->xsdt_address);
            acpi_log("\n");
            scan_root_table(rsdp->xsdt_address, 1);
        }
        if (!g_info.root_address && rsdp->rsdt_address) {
            acpi_log("[acpi] fallback RSDT address=");
            acpi_log_hex(rsdp->rsdt_address);
            acpi_log("\n");
            scan_root_table(rsdp->rsdt_address, 0);
        }
        g_info.status = g_info.root_address ?
            DEPRESSURE_STATUS_OK : DEPRESSURE_CORRUPTED_DATA;
    } else if (status != 0) {
        acpi_log("[acpi] RSDP lookup failed\n");
        g_info.status = status == 14 ?
            DEPRESSURE_NOT_FOUND : DEPRESSURE_IO_ERROR;
    } else {
        acpi_log("[acpi] RSDP signature/checksum invalid\n");
        g_info.status = DEPRESSURE_CHECKSUM_ERROR;
    }

    acpi_log("[acpi] refreshing PIIX4 PCI state\n");
    refresh_piix4();
    acpi_log("[acpi] PIIX4 found=");
    acpi_log_hex(g_info.piix4_found);
    acpi_log(" BDF=");
    acpi_log_hex(g_info.piix4_bus);
    acpi_log_char(':');
    acpi_log_hex(g_info.piix4_slot);
    acpi_log_char('.');
    acpi_log_hex(g_info.piix4_func);
    acpi_log(" PMBASE=");
    acpi_log_hex(g_info.piix4_pm_base);
    acpi_log("\n");
    g_acpi_api.version = ACPI_API_VERSION;
    g_acpi_api._pad = 0;
    g_acpi_api.get_info = api_get_info;
    g_acpi_api.get_table_count = api_get_table_count;
    g_acpi_api.get_table = api_get_table;
    g_acpi_api.shutdown = acpi_shutdown;
    g_acpi_api.reboot = acpi_reboot;
    g_acpi_driver.api = (u64)&g_acpi_api;

    if (g_kernel_api && g_kernel_api->register_driver) {
        int result = g_kernel_api->register_driver(&g_acpi_driver);
        acpi_log("[acpi] driver registration result=");
        acpi_log_hex((u64)(u32)result);
        acpi_log("\n");
    } else {
        acpi_log("[acpi] kernel register_driver unavailable\n");
    }
    acpi_log("[acpi] initialization complete status=");
    acpi_log_hex(g_info.status);
    acpi_log(" tables=");
    acpi_log_hex(g_info.table_count);
    acpi_log("\n");
}
