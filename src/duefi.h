// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DUEFI_H
#define DUEFI_H

#include "dtypes.h"

typedef void *EFI_EVENT;
typedef void *EFI_BOOT_SERVICES;

typedef struct ddir_entry {
    u64 file_size;
    u64 name_len;
    u16 name[0xF8];
} ddir_entry_t;

u64 uefi_load_file(const u16 *path, void *buf, u64 size);
u64 uefi_list_dir(const u16 *path, ddir_entry_t *buf, u64 max);
u64 uefi_get_system_table(void **out_system_table);
u64 uefi_get_configuration_table(const u8 *guid, void **out_table);
u64 uefi_get_acpi_rsdp(void **out_rsdp);
void uefi_stall(u64 microseconds);
u64 uefi_create_timer_event(EFI_BOOT_SERVICES *bs, EFI_EVENT *out_event);
u64 uefi_set_timer_periodic(EFI_BOOT_SERVICES *bs, EFI_EVENT event, u64 period_100ns);
u64 uefi_wait_for_event(EFI_BOOT_SERVICES *bs, u64 count, EFI_EVENT *events);
u64 uefi_write_file(const u16 *path, const void *buf, u64 size);
u64 uefi_get_total_ram(void *BootServices);
u64 uefi_create_event(EFI_BOOT_SERVICES *bs, EFI_EVENT *out_event);
u64 uefi_locate_handle_blockio(void *BootServices, void *buffer, u64 *size);
u64 uefi_handle_protocol_blockio(void *BootServices, void *handle, void **iface);
u64 uefi_get_simple_pointer(void **out_sp);
u64 uefi_simple_pointer_get_state(void *sp, void *out_state);
u64 uefi_simple_pointer_reset(void *sp, u8 extended);
u64 uefi_con_out_string(void *con_out, const u16 *string);

#endif /* DUEFI_H */