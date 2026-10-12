#ifndef ACPI_H
#define ACPI_H

#include "dtypes.h"
#include "dstat.h"

#define ACPI_API_VERSION 0x00010002
#define ACPI_MAX_TABLES  64
#define ACPI_MAX_IOAPICS 4

typedef struct acpi_table_info {
    char signature[5];
    u32 length;
    u8 revision;
    u8 checksum;
    u64 address;
} acpi_table_info_t;

typedef struct acpi_system_info {
    dstatus_t status;
    u64 rsdp_address;
    u64 root_address;
    u64 fadt_address;
    u64 madt_address;
    u64 dsdt_address;
    u64 local_apic_address;
    u32 root_is_xsdt;
    u32 table_count;
    u32 processor_count;
    u32 ioapic_count;
    u64 ioapic_address[ACPI_MAX_IOAPICS];
    u32 ioapic_gsi_base[ACPI_MAX_IOAPICS];
    u32 piix4_found;
    u8  piix4_bus;
    u8  piix4_slot;
    u8  piix4_func;
    u8  piix4_pm_io_enabled;
    u16 piix4_command;
    u16 piix4_pm_base;
    u32 piix4_pm_timer;
    dstatus_t fadt_status;
    u32 smi_command_port;
    u32 pm1a_event_port;
    u32 pm1b_event_port;
    u32 pm1a_control_port;
    u32 pm1b_control_port;
    u32 pm2_control_port;
    u32 pm_timer_port;
    u32 gpe0_port;
    u32 gpe1_port;
    u32 fadt_flags;
    u16 sci_interrupt;
    u16 boot_architecture_flags;
    u8  pm1_event_length;
    u8  pm1_control_length;
    u8  pm2_control_length;
    u8  pm_timer_length;
    u8  gpe0_length;
    u8  gpe1_length;
    u8  acpi_enable_value;
    u8  acpi_disable_value;
    u8  acpi_enabled;
    u8  s5_available;
    u8  s5_type_a;
    u8  s5_type_b;
    u8  reset_address_space;
    u8  reset_register_width;
    u8  reset_register_offset;
    u8  reset_access_size;
    u64 reset_register_address;
    u8  reset_value;
    u8  _reserved_fadt[7];
    u16 pm1a_control_value;
    u16 pm1b_control_value;
    u16 pm1a_event_status;
    u16 pm1a_event_enable;
    u16 pm1b_event_status;
    u16 pm1b_event_enable;
    u16 gpe0_status;
    u16 gpe0_enable;
    u16 gpe1_status;
    u16 gpe1_enable;
    u8  pm2_control_value;
    u8  _reserved_fadt_runtime[3];
    u32 fadt_pm_timer_value;
} acpi_system_info_t;

typedef struct acpi_api {
    u32 version;
    u32 _pad;
    dstatus_t (*get_info)(acpi_system_info_t *out);
    dstatus_t (*get_table_count)(u32 *out_count);
    dstatus_t (*get_table)(u32 index, const acpi_table_info_t **out);
    dstatus_t (*get_ioapic)(u32 index, u64 *out_addr, u32 *out_gsi_base);
    dstatus_t (*shutdown)(void);
    dstatus_t (*reboot)(void);
} acpi_api_t;

void acpi_main(void *api, void *fb_info);

#endif