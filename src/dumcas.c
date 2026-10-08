#include "dtypes.h"
#include "ddriver.h"
#include "dkrnl.h"
#include "dumcas.h"

static umcas_ring_t g_master;
static umcas_task_t g_master_tasks[UMCAS_RING_SIZE];

static volatile u64 g_submitted = 0;
static volatile u64 g_executed = 0;

static dkernel_api_t *g_api = 0;

static int ring_push(umcas_ring_t *r, const umcas_task_t *task) {
    u32 head = r->head;
    u32 tail = r->tail;
    u32 next = (head + 1) % r->capacity;

    if (next == tail) return -1;

    r->tasks[head] = *task;
    __asm__ volatile ("" ::: "memory");
    r->head = next;
    return 0;
}

static int ring_pop(umcas_ring_t *r, umcas_task_t *out) {
    u32 head = r->head;
    u32 tail = r->tail;

    if (head == tail) return -1;

    *out = r->tasks[tail];
    __asm__ volatile ("" ::: "memory");
    r->tail = (tail + 1) % r->capacity;
    return 0;
}

static void umcas_execute(const umcas_task_t *t) {
    switch (t->opcode) {
        case UMCAS_OP_METRICS:
            if (g_api && g_api->smp_add_metrics) {
                g_api->smp_add_metrics((u32)t->core_id,
                                       t->arg0, t->arg1);
            }
            break;

        case UMCAS_OP_CUSTOM:
            if (t->arg0) {
                void (*fn)(u64) = (void (*)(u64))t->arg0;
                fn(t->arg1);
            }
            break;

        default:
            break;
    }

    g_executed++;
}

static int umcas_submit(u32 opcode, u64 a0, u64 a1, u64 a2, u64 a3) {
    umcas_task_t t;
    t.opcode = opcode;
    t.core_id = 0;
    t.arg0 = a0;
    t.arg1 = a1;
    t.arg2 = a2;
    t.arg3 = a3;

    if (ring_push(&g_master, &t) != 0) {
        return -1;
    }

    g_submitted++;
    return 0;
}

static void umcas_drain(void) {
    umcas_task_t t;
    int count = 0;

    while (ring_pop(&g_master, &t) == 0) {
        umcas_execute(&t);
        count++;
        if (count >= 64) break;
    }
}

static u64 umcas_get_submitted(void) { return g_submitted; }
static u64 umcas_get_executed(void)  { return g_executed; }

static u32 umcas_get_pending(void) {
    u32 head = g_master.head;
    u32 tail = g_master.tail;
    if (head >= tail) return head - tail;
    return g_master.capacity - tail + head;
}

static dumcas_api_t g_umcas_api;

static ddriver_t umcas_driver = {
    .pattern = DDRIVER_PATTERN,
    .version = DDRIVER_VERSION,
    .next    = 0,
    .type    = DDRIVER_TYPE_OTHER,
    .name    = "umcas",
    .entry   = 0,
    .api     = 0,
    .data    = 0,
};

void dumcas_main(dkernel_api_t *api, void *fb_info) {
    (void)fb_info;

    g_api = api;

    g_master.head = 0;
    g_master.tail = 0;
    g_master.capacity = UMCAS_RING_SIZE;
    g_master.tasks = g_master_tasks;

    g_umcas_api.version        = 0x00010000;
    g_umcas_api._pad           = 0;
    g_umcas_api.submit         = umcas_submit;
    g_umcas_api.drain          = umcas_drain;
    g_umcas_api.get_submitted  = umcas_get_submitted;
    g_umcas_api.get_executed   = umcas_get_executed;
    g_umcas_api.get_pending    = umcas_get_pending;

    if (api && api->register_driver) {
        umcas_driver.api = (u64)&g_umcas_api;
        api->register_driver(&umcas_driver);
    }

    if (api && api->task_add) {
        api->task_add("umcas_drain", umcas_drain, 1);
    }
}