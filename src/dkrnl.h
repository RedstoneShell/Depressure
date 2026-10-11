#ifndef DKRNL_H
#define DKRNL_H
#include "dtypes.h"
#include "ddriver.h"
#include "dmemory.h"
#include "hpet.h"

#define DTASK_MAX            16
#define DTASK_STACK_SIZE     0x8000
#define DTASK_SIMD_SLOTS     16   /* XMM0..XMM15 */

typedef struct dtask_saved {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
    u64 vector;
    u64 error_code;
    u64 rip, cs, rflags;
    u64 rsp;
    u64 simd[DTASK_SIMD_SLOTS * 2];
} dtask_saved_t;

typedef struct dtask {
    const char *name;
    void (*func)(void);
    u64 period;
    u64 next_tick;
    u32 active;
    u32 uses_simd;
    u64 stack_base;
    u64 stack_size;
    u64 stack_top;
    dtask_saved_t saved;
} dtask_t;

typedef struct dkernel_api {
    u32 version;
    u32 _pad;
    int (*register_driver)(void *drv);
    int (*task_add)(const char *name, void (*func)(void), u64 period);
    int (*task_add_simd)(const char *name, void (*func)(void), u64 period);
    int (*task_remove)(const char *name);
    u64 (*write_file)(const u16 *path, const void *buf, u64 size);
    u64 (*read_file)(const u16 *path, void *buf, u64 size);
    u32 (*smp_get_ncpu)(void);
    u32 (*smp_get_load)(u32 core_id);
    u64 (*get_total_ram)(void);
    u64 (*get_storage_bytes)(void);
    u32 (*get_storage_count)(void);
    u64 (*get_storage_device_size)(u32 index);
    void (*smp_add_metrics)(u32 core_id, u64 active, u64 total);
    ddriver_t *(*DFindAndGetDriver)(const char *name);
    volatile hpet_system_info_t *hpet_info;
    D_HEAP_POINTER (*DCreateHeap)(const char *source);
    dstatus_t (*DWriteHeap)(D_HEAP_POINTER h, u64 offset,
                            const void *data, u64 size);
    dstatus_t (*DReadHeap)(D_HEAP_POINTER h, u64 offset,
                           void *out, u64 size);
    dstatus_t (*DCloseHeap)(D_HEAP_POINTER h);
} dkernel_api_t;

#define DKRNL_API_VERSION   0x00010001u

ddriver_t *DFindAndGetDriver(const char *name);
int dkrnl_task_add(const char *name, void (*func)(void), u64 period);
int dkrnl_task_add_simd(const char *name, void (*func)(void), u64 period);
int dkrnl_task_remove(const char *name);
void dkrnl_task_yield(void);
void pit_init(u32 hz);

#endif /* DKRNL_H */