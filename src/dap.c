#include "dtypes.h"
#include "dap.h"

u64 ap_started_count = 0;
static u32 g_ncpu = 0;

void d_ap_init_shared(void) {
    d_ap_shared_t *sh = d_ap_get_shared();

    sh->ready = 0;

    for (u32 i = 0; i < D_AP_MAX_CORES; i++) {
        sh->task_head[i] = 0;
        sh->task_tail[i] = 0;
        sh->stats[i].idle_cycles   = 0;
        sh->stats[i].active_cycles = 0;
        sh->stats[i].tasks_done    = 0;
        sh->stats[i].core_id       = 0;
        sh->stats[i].online        = 0;

        for (u32 j = 0; j < D_AP_TASK_QUEUE; j++) {
            sh->tasks[i][j].opcode = D_AP_TASK_NONE;
            sh->tasks[i][j].done = 0;
        }
    }

    sh->ready = 1;
}

void d_ap_set_ncpu(u32 n) { g_ncpu = n; }
u32  d_ap_get_ncpu(void)  { return g_ncpu; }

void d_ap_set_core_lapic(u32 core, u32 lapic) {
    d_ap_shared_t *sh = d_ap_get_shared();
    if (core < D_AP_MAX_CORES) sh->stats[core].core_id = lapic;
}

int d_ap_submit(u32 core, u32 opcode, u64 code_addr, u64 code_size, u64 arg) {
    if (core >= D_AP_MAX_CORES) return -1;

    d_ap_shared_t *sh = d_ap_get_shared();
    u32 head = sh->task_head[core];
    u32 tail = sh->task_tail[core];
    u32 next = (head + 1) % D_AP_TASK_QUEUE;

    if (next == tail) return -1;

    d_ap_task_t *t = &sh->tasks[core][head];
    t->opcode    = opcode;
    t->core_id   = core;
    t->code_addr = code_addr;
    t->code_size = code_size;
    t->arg       = arg;
    t->result    = 0;
    t->done      = 0;

    __asm__ volatile ("" ::: "memory");
    sh->task_head[core] = next;

    return 0;
}

int d_ap_wait(u32 core, u64 timeout_cycles) {
    if (core >= D_AP_MAX_CORES) return -1;

    d_ap_shared_t *sh = d_ap_get_shared();
    u64 start = 0;
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    start = ((u64)hi << 32) | lo;

    while (1) {
        u32 tail = sh->task_tail[core];
        u32 head = sh->task_head[core];
        if (tail == head) return 0;

        d_ap_task_t *t = &sh->tasks[core][tail];
        if (t->done) return 0;

        __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
        u64 now = ((u64)hi << 32) | lo;
        if (now - start > timeout_cycles) return -1;
    }
}

u64 d_ap_get_result(u32 core) {
    if (core >= D_AP_MAX_CORES) return 0;
    d_ap_shared_t *sh = d_ap_get_shared();
    u32 tail = sh->task_tail[core];
    return sh->tasks[core][tail].result;
}

void d_ap_get_stats(u32 core, u64 *idle, u64 *active, u64 *tasks) {
    if (core >= D_AP_MAX_CORES) {
        if (idle) *idle = 0;
        if (active) *active = 0;
        if (tasks) *tasks = 0;
        return;
    }
    d_ap_shared_t *sh = d_ap_get_shared();
    d_ap_stats_t *st = &sh->stats[core];
    if (idle)   *idle   = st->idle_cycles;
    if (active) *active = st->active_cycles;
    if (tasks)  *tasks  = st->tasks_done;
}