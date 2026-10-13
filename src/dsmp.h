// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DSMP_H
#define DSMP_H

#include "dtypes.h"

typedef void *EFI_BOOT_SERVICES;
typedef void *EFI_EVENT;
typedef void *EFI_MP_SERVICES_PROTOCOL;
typedef void (*EFI_AP_PROCEDURE)(void *ProcedureArgument);

typedef struct core_metrics {
    u64 total_cycles;
    u64 active_cycles;
    u64 last_tsc;
    u32 load_percent;
} core_metrics_t;

extern void ap_worker_asm(void);
extern u64 ap_started_count;
extern u32 g_ncpu;

EFI_MP_SERVICES_PROTOCOL *uefi_locate_mp_services(void *BootServices);
u64 uefi_mp_get_num_processors(EFI_MP_SERVICES_PROTOCOL *mp,
                               u64 *num_cpus,
                               u64 *num_enabled);
u64 uefi_mp_startup_all_aps(EFI_MP_SERVICES_PROTOCOL *mp,
                            EFI_AP_PROCEDURE procedure,
                            void *procedure_arg,
                            u64 single_thread,
                            u64 timeout_us,
                            void *wait_event);
u64 uefi_mp_startup_this_ap(EFI_MP_SERVICES_PROTOCOL *mp,
                            EFI_AP_PROCEDURE procedure,
                            void *procedure_arg,
                            u64 processor_number,
                            void *wait_event,
                            u64 timeout_us);
u64 uefi_mp_whoami(EFI_MP_SERVICES_PROTOCOL *mp, u64 *cpu_num);
u64 uefi_create_event(EFI_BOOT_SERVICES *bs, EFI_EVENT *out_event);

void dsmp_set_umcas_submit(int (*fn)(u32, u64, u64, u64, u64));
void dsmp_add_metrics(u32 core_id, u64 active, u64 total);
void dsmp_update_loads(void);
void dsmp_poll_workers(void);

u32 dsmp_get_ncpu(void);
u32 dsmp_get_load(u32 core_id);
u32 dsmp_get_lapic_id(void);
u32 dsmp_get_lapic_count(void);
u32 dsmp_get_bsp_lapic_id(void);
u32 dsmp_lapic_to_core(u32 lapic_id);

int dsmp_init(void *BootServices);
int dsmp_start_workers(void *bs);

u64 dsmp_get_workers_started(void);
u64 dsmp_get_tasks_done(void);
u64 dsmp_rdtsc(void);

#endif