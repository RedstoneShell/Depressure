// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "acpi.h"
#include "hpet.h"

#define HPET_TABLE_MIN_SIZE 56U
#define HPET_REGISTER_CAPABILITIES 0x000U
#define HPET_REGISTER_CONFIGURATION 0x010U
#define HPET_REGISTER_MAIN_COUNTER 0x0F0U
#define HPET_CONFIGURATION_ENABLE 0x001ULL
#define HPET_CONFIGURATION_LEGACY_REPLACEMENT 0x002ULL
#define HPET_COUNTER_PERIOD_FS_LIMIT 100000000ULL

typedef struct __attribute__((packed)) hpet_gas {
    u8 address_space_id;
    u8 register_bit_width;
    u8 register_bit_offset;
    u8 access_size;
    u64 address;
} hpet_gas_t;

typedef struct __attribute__((packed)) hpet_table {
    u8 header[36];
    u32 event_timer_block_id;
    hpet_gas_t base_address;
    u8 hpet_number;
    u16 minimum_tick;
    u8 page_protection;
} hpet_table_t;

volatile hpet_system_info_t g_hpet_info = {
    .status = DEPRESSURE_HPET_NOT_INITIALIZED
};

static int signature_is_hpet(const char signature[5]) {
    return signature[0] == 'H' &&
           signature[1] == 'P' &&
           signature[2] == 'E' &&
           signature[3] == 'T';
}

static dstatus_t find_hpet_table(const acpi_api_t *acpi,
                                 const hpet_table_t **out_table) {
    u32 table_count = 0;
    dstatus_t status;

    if (!acpi || acpi->version != ACPI_API_VERSION ||
        !acpi->get_table_count || !acpi->get_table || !out_table) {
        return DEPRESSURE_HPET_NOT_FOUND;
    }

    status = acpi->get_table_count(&table_count);
    if (DSTATUS_IS_ERR(status)) return status;

    for (u32 i = 0; i < table_count; i++) {
        const acpi_table_info_t *info = 0;

        status = acpi->get_table(i, &info);
        if (DSTATUS_IS_ERR(status)) return status;
        if (!info || !signature_is_hpet(info->signature)) continue;
        if (info->length < HPET_TABLE_MIN_SIZE || !info->address) {
            return DEPRESSURE_HPET_INVALID_TABLE;
        }
        *out_table = (const hpet_table_t *)info->address;
        return DEPRESSURE_STATUS_OK;
    }

    return DEPRESSURE_HPET_NOT_FOUND;
}

dstatus_t dhpet_init(const struct acpi_api *acpi) {
    const hpet_table_t *table = 0;
    volatile u64 *registers;
    u64 capabilities;
    u64 period_femtoseconds;
    u64 configuration;
    u64 address;
    dstatus_t status;

    if (g_hpet_info.enabled) return DEPRESSURE_ALREADY_INITIALIZED;

    g_hpet_info.status = DEPRESSURE_HPET_NOT_INITIALIZED;
    status = find_hpet_table((const acpi_api_t *)acpi, &table);
    if (DSTATUS_IS_ERR(status)) {
        g_hpet_info.status = status == DEPRESSURE_NOT_FOUND ?
            DEPRESSURE_HPET_NOT_FOUND : status;
        return g_hpet_info.status;
    }

    g_hpet_info.table_address = (u64)table;
    g_hpet_info.address_space_id = table->base_address.address_space_id;
    g_hpet_info.register_bit_width = table->base_address.register_bit_width;
    g_hpet_info.register_bit_offset = table->base_address.register_bit_offset;
    g_hpet_info.mmio_base = table->base_address.address;
    g_hpet_info.minimum_tick = table->minimum_tick;

    if (table->base_address.address_space_id != 0 ||
        table->base_address.register_bit_offset != 0 ||
        (table->base_address.register_bit_width != 0 &&
         table->base_address.register_bit_width < 64)) {
        g_hpet_info.status = DEPRESSURE_HPET_NOT_SUPPORTED;
        return g_hpet_info.status;
    }

    address = table->base_address.address;
    if (!address || (address & 7) ||
        address > 0x00007FFFFFFFFFFFULL - HPET_REGISTER_MAIN_COUNTER) {
        g_hpet_info.status = DEPRESSURE_HPET_INVALID_ADDRESS;
        return g_hpet_info.status;
    }

    registers = (volatile u64 *)address;
    capabilities = registers[HPET_REGISTER_CAPABILITIES / sizeof(u64)];
    period_femtoseconds = capabilities >> 32;
    if (!period_femtoseconds ||
        period_femtoseconds > HPET_COUNTER_PERIOD_FS_LIMIT) {
        g_hpet_info.status = DEPRESSURE_HPET_INVALID_PERIOD;
        return g_hpet_info.status;
    }

    g_hpet_info.capabilities = capabilities;
    g_hpet_info.period_femtoseconds = period_femtoseconds;
    g_hpet_info.frequency_hz = 1000000000000000ULL / period_femtoseconds;
    g_hpet_info.revision_id = (u8)(capabilities & 0xFF);
    g_hpet_info.timer_count = (u32)(((capabilities >> 8) & 0x1F) + 1);
    g_hpet_info.counter_64bit = (u8)((capabilities >> 13) & 1);
    g_hpet_info.legacy_replacement_capable = (u8)((capabilities >> 15) & 1);
    g_hpet_info.vendor_id = (u32)((capabilities >> 16) & 0xFFFF);

    configuration = registers[HPET_REGISTER_CONFIGURATION / sizeof(u64)];
    configuration |= HPET_CONFIGURATION_ENABLE;
    registers[HPET_REGISTER_CONFIGURATION / sizeof(u64)] = configuration;
    __asm__ volatile ("mfence" : : : "memory");

    configuration = registers[HPET_REGISTER_CONFIGURATION / sizeof(u64)];
    g_hpet_info.configuration = configuration;
    if (!(configuration & HPET_CONFIGURATION_ENABLE)) {
        g_hpet_info.status = DEPRESSURE_HPET_ENABLE_FAILED;
        return g_hpet_info.status;
    }

    g_hpet_info.enabled = 1;
    g_hpet_info.legacy_replacement_enabled =
        (u8)((configuration & HPET_CONFIGURATION_LEGACY_REPLACEMENT) != 0);
    g_hpet_info.counter_value =
        registers[HPET_REGISTER_MAIN_COUNTER / sizeof(u64)];
    g_hpet_info.status = DEPRESSURE_STATUS_OK;
    return DEPRESSURE_STATUS_OK;
}

void dhpet_update(void) {
    u64 address;

    if (!g_hpet_info.enabled) return;
    address = g_hpet_info.mmio_base;
    if (!address) return;

    g_hpet_info.counter_value =
        ((volatile u64 *)address)[HPET_REGISTER_MAIN_COUNTER / sizeof(u64)];
}
