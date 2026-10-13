// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DAP_H
#define DAP_H

#include "dtypes.h"

#define D_AP_MAX_CORES  64
#define D_AP_TASK_QUEUE 32
#define D_AP_TASK_CODE_MAX 65535

typedef enum {
    D_AP_TASK_NONE = 0,
    D_AP_TASK_EXEC,
    D_AP_TASK_STOP,
} d_ap_opcode_t;

typedef struct d_ap_task {
    volatile u32 opcode;
    volatile u32 core_id;
    volatile u64 code_addr;
    volatile u64 code_size;
    volatile u64 arg;
    volatile u64 result;
    volatile u32 done;
    u32 _pad;
} d_ap_task_t;

typedef struct d_ap_stats {
    volatile u64 idle_cycles;
    volatile u64 active_cycles;
    volatile u64 tasks_done;
    volatile u32 core_id;
    volatile u32 online;
    u64 _pad[2];
} d_ap_stats_t;

typedef struct d_ap_shared {
    volatile u32 ready;

    d_ap_task_t tasks[D_AP_MAX_CORES][D_AP_TASK_QUEUE];
    volatile u32 task_head[D_AP_MAX_CORES];
    volatile u32 task_tail[D_AP_MAX_CORES];

    d_ap_stats_t stats[D_AP_MAX_CORES];
} d_ap_shared_t;

#define D_AP_SHARED_ADDR  0x04000000ULL

static inline d_ap_shared_t *d_ap_get_shared(void) {
    return (d_ap_shared_t *)D_AP_SHARED_ADDR;
}

void d_ap_init_shared(void);
int  d_ap_submit(u32 core, u32 opcode, u64 code_addr, u64 code_size, u64 arg);
int  d_ap_wait(u32 core, u64 timeout_cycles);
u64  d_ap_get_result(u32 core);
void d_ap_get_stats(u32 core, u64 *idle, u64 *active, u64 *tasks);

#endif