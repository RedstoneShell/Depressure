// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "dap.h"

#define LAPIC_BASE 0xFEE00000ULL

static inline u64 rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

static u32 lapic_id(void) {
    return *(volatile u32 *)(LAPIC_BASE + 0x20) >> 24;
}

void d_ap_worker_c(void *arg) {
    (void)arg;

    d_ap_shared_t *sh = d_ap_get_shared();
    if (!sh->ready) return;

    u32 lid = lapic_id();
    u32 core = 0;
    for (u32 i = 0; i < D_AP_MAX_CORES; i++) {
        if (sh->stats[i].core_id == lid) { core = i; break; }
    }

    d_ap_stats_t *st = &sh->stats[core];
    st->online = 1;

    u64 t_start = rdtsc();

    u32 head = sh->task_head[core];
    u32 tail = sh->task_tail[core];

    if (head == tail) {
        st->idle_cycles += rdtsc() - t_start;
        return;
    }

    d_ap_task_t *task = &sh->tasks[core][tail];

    if (task->opcode == D_AP_TASK_EXEC && task->code_addr && task->code_size) {
        typedef u64 (*ap_fn_t)(u64);
        ap_fn_t fn = (ap_fn_t)task->code_addr;
        task->result = fn(task->arg);
        task->done = 1;
        st->tasks_done++;
    } else if (task->opcode == D_AP_TASK_STOP) {
        task->done = 1;
    }

    sh->task_tail[core] = (tail + 1) % D_AP_TASK_QUEUE;
    st->active_cycles += rdtsc() - t_start;
}