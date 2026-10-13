// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef HPET_H
#define HPET_H

#include "dstat.h"
#include "dtypes.h"

typedef struct hpet_system_info {
    dstatus_t status;
    u64 table_address;
    u64 mmio_base;
    u64 frequency_hz;
    u64 period_femtoseconds;
    u64 counter_value;
    u64 capabilities;
    u64 configuration;
    u32 vendor_id;
    u32 timer_count;
    u32 minimum_tick;
    u8 revision_id;
    u8 counter_64bit;
    u8 legacy_replacement_capable;
    u8 legacy_replacement_enabled;
    u8 enabled;
    u8 address_space_id;
    u8 register_bit_width;
    u8 register_bit_offset;
} hpet_system_info_t;

struct acpi_api;

extern volatile hpet_system_info_t g_hpet_info;

dstatus_t dhpet_init(const struct acpi_api *acpi);
void dhpet_update(void);
dstatus_t dhpet_set_periodic_10us(void);

#endif
