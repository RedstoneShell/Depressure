// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "dtypes.h"
#include "dboot.h"
#include "ddef.h"
#include "dbool.h"
#include "dumcas.h"
#include "dreg.h"
#include "ddriver.h"
#include "dstorage.h"
#include "duefi.h"
#include "delf.h"
#include "dsmp.h"
#include "dap.h"
#include "dbmp.h"
#include "dkrnl.h"
#include "dstat.h"
#include "interrupts.h"
#include "acpi.h"
#include "hpet.h"

#define MARK_ADDR     0x300000
#define DBG_ADDR      0x301000
#define FB_INFO_ADDR  0x200000
#define MAX_DRIVERS   16
#define MAX_REGISTERED_DRIVERS 256
#define DRIVER_LOAD_LIMIT 0x80000000ULL
#define DRIVER_LOAD_ALIGN 0x1000ULL
#define KERNEL_RESERVED_START 0x1000000ULL
#define KERNEL_RESERVED_END   0x2000000ULL
#define DBG_DRIVER_TABLE 120

#define UART_BUF_SIZE   (64 * 1024)

#define TASK_QUANTUM_US 1000
#define TASK_QUANTUM_HZ (1000000 / TASK_QUANTUM_US)

typedef struct uart_api {
    u32 version;
    u32 _pad;
    void (*putc)(char c);
    void (*puts)(const char *str);
    void (*put_hex)(u64 value);
    int  (*has_data)(void);
    char (*getc)(void);
} uart_api_t;

typedef struct dws_api {
    u32 version;
    u32 _pad;
    void (*put_pixel)(u64 x, u64 y, u32 rgb);
    void (*fill)(u32 rgb);
    void (*fill_rect)(u64 x, u64 y, u64 w, u64 h, u32 rgb);
    u32  (*convert_color)(u32 rgb);
    void (*blit_scaled)(u64 x, u64 y, u64 dst_w, u64 dst_h,
                        const u32 *src, u64 src_w, u64 src_h);
    void (*draw_char)(u64 x, u64 y, char ch, u32 fg, u32 bg);
    void (*draw_text)(u64 x, u64 y, const char *str, u32 fg, u32 bg);
} dws_api_t;

typedef struct dmouse_api {
    u32 version;
    u32 _pad;
    int  (*has_event)(void);
    void (*get_state)(int *x, int *y, int *buttons);
    int  (*get_x)(void);
    int  (*get_y)(void);
    int  (*get_buttons)(void);
    void (*set_position)(int x, int y);
} dmouse_api_t;

typedef struct dkbd_api {
    u32 version;
    u32 _pad;
    int  (*has_key)(void);
    char (*get_key)(void);
    u8   (*get_scancode)(void);
} dkbd_api_t;

static char   g_uart_buf[UART_BUF_SIZE];
static u64    g_uart_buf_len  = 0;
static int    g_uart_ready    = 0;
static uart_api_t *g_uart     = 0;

static u8 driver_buf[0x100000];
static ddir_entry_t entries[MAX_DRIVERS];
static volatile u16 full_path[256];

static const u16 driver_dir[] = {
    '\\','D','e','p','r','e','s','s','u','r','e','\\','B','O','O','T', 0
};

static const u16 path_prefix[17] = {
    '\\','D','e','p','r','e','s','s','u','r','e','\\','B','O','O','T','\\'
};

static u64 g_total_ram = 0;

static inline void port_outb(u16 port, u8 v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline u8 port_inb(u16 port) {
    u8 r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static void early_uart_putc(char c) {
    while ((port_inb(0x3FD) & 0x20) == 0) {}
    port_outb(0x3F8, (u8)c);
}

static void early_uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') early_uart_putc('\r');
        early_uart_putc(*s++);
    }
}

static void uart_flush(void) {
    if (!g_uart || !g_uart->putc) return;
    for (u64 i = 0; i < g_uart_buf_len; i++) {
        char c = g_uart_buf[i];
        if (c == '\n') g_uart->putc('\r');
        g_uart->putc(c);
    }
    g_uart_buf_len = 0;
    g_uart_ready = 1;
}

static void klog(const char *s) {
    if (g_uart_ready && g_uart && g_uart->putc) {
        while (*s) {
            if (*s == '\n') g_uart->putc('\r');
            g_uart->putc(*s++);
        }
    } else {
        while (*s && g_uart_buf_len < UART_BUF_SIZE - 1) {
            g_uart_buf[g_uart_buf_len++] = *s++;
        }
    }
}

static void klog_hex64(u64 v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[24];
    int i = 0;
    buf[i++] = '0'; buf[i++] = 'x';
    for (int k = 60; k >= 0; k -= 4) buf[i++] = hex[(v >> k) & 0xF];
    buf[i] = 0;
    klog(buf);
}

static void klog_dec(u64 v) {
    char buf[24];
    int i = 0;
    if (v == 0) { klog("0"); return; }
    while (v > 0) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) { char c[2] = { buf[--i], 0 }; klog(c); }
}

static void klog_status(dstatus_t s) {
    klog_hex64(s);
    klog(" (");
    switch (s) {
        case DEPRESSURE_STATUS_OK:            klog("OK"); break;
        case DEPRESSURE_TOO_LONG_DATA:        klog("TOO_LONG_DATA"); break;
        case DEPRESSURE_INCOMPATIBLE_TYPE:    klog("INCOMPATIBLE_TYPE"); break;
        case DEPRESSURE_NULL_DATA:            klog("NULL_DATA"); break;
        case DEPRESSURE_INVALID_POINTER:      klog("INVALID_POINTER"); break;
        case DEPRESSURE_NOT_SUPPORTED:        klog("NOT_SUPPORTED"); break;
        case DEPRESSURE_ALIGNMENT_ERROR:      klog("ALIGNMENT_ERROR"); break;
        case DEPRESSURE_BUFFER_TOO_SMALL:     klog("BUFFER_TOO_SMALL"); break;
        case DEPRESSURE_OUT_OF_MEMORY:        klog("OUT_OF_MEMORY"); break;
        case DEPRESSURE_ALREADY_INITIALIZED:  klog("ALREADY_INITIALIZED"); break;
        case DEPRESSURE_NOT_INITIALIZED:      klog("NOT_INITIALIZED"); break;
        case DEPRESSURE_IO_ERROR:             klog("IO_ERROR"); break;
        case DEPRESSURE_TIMEOUT:              klog("TIMEOUT"); break;
        case DEPRESSURE_BUSY:                 klog("BUSY"); break;
        case DEPRESSURE_NOT_FOUND:            klog("NOT_FOUND"); break;
        case DEPRESSURE_ALREADY_EXISTS:       klog("ALREADY_EXISTS"); break;
        case DEPRESSURE_PERMISSION_DENIED:    klog("PERMISSION_DENIED"); break;
        case DEPRESSURE_OUT_OF_RANGE:         klog("OUT_OF_RANGE"); break;
        case DEPRESSURE_CHECKSUM_ERROR:       klog("CHECKSUM_ERROR"); break;
        case DEPRESSURE_CORRUPTED_DATA:       klog("CORRUPTED_DATA"); break;
        case DEPRESSURE_OVERFLOW:             klog("OVERFLOW"); break;
        case DEPRESSURE_UNDERFLOW:            klog("UNDERFLOW"); break;
        case DEPRESSURE_DIVIDE_BY_ZERO:       klog("DIVIDE_BY_ZERO"); break;
        case DEPRESSURE_NOT_ALIGNED:          klog("NOT_ALIGNED"); break;
        case DEPRESSURE_HPET_NOT_FOUND:            klog("HPET_NOT_FOUND"); break;
        case DEPRESSURE_HPET_NOT_SUPPORTED:        klog("HPET_NOT_SUPPORTED"); break;
        case DEPRESSURE_HPET_INVALID_TABLE:        klog("HPET_INVALID_TABLE"); break;
        case DEPRESSURE_HPET_INVALID_ADDRESS:      klog("HPET_INVALID_ADDRESS"); break;
        case DEPRESSURE_HPET_INVALID_PERIOD:       klog("HPET_INVALID_PERIOD"); break;
        case DEPRESSURE_HPET_ENABLE_FAILED:        klog("HPET_ENABLE_FAILED"); break;
        case DEPRESSURE_HPET_NOT_INITIALIZED:      klog("HPET_NOT_INITIALIZED"); break;
        case DEPRESSURE_ACPI_FADT_NOT_FOUND:       klog("ACPI_FADT_NOT_FOUND"); break;
        case DEPRESSURE_ACPI_FADT_INVALID:         klog("ACPI_FADT_INVALID"); break;
        case DEPRESSURE_ACPI_S5_NOT_FOUND:         klog("ACPI_S5_NOT_FOUND"); break;
        case DEPRESSURE_ACPI_POWER_UNAVAILABLE:    klog("ACPI_POWER_UNAVAILABLE"); break;
        case DEPRESSURE_ACPI_RESET_UNAVAILABLE:    klog("ACPI_RESET_UNAVAILABLE"); break;
        default:                                   klog("UNKNOWN"); break;
    }
    klog(")");
}

static dtask_t g_tasks[DTASK_MAX];
static u32     g_task_count = 0;
static volatile u64 g_tick = 0;

static dtask_t *g_current_task = 0;
static u32      g_current_index = 0;
static volatile int g_need_resched = 0;

static u8 g_task_stacks[DTASK_MAX][DTASK_STACK_SIZE] __attribute__((aligned(16)));

static inline void dtask_save_xmm(u64 *out) {
    __asm__ volatile (
        "movdqu %%xmm0,   0(%0)\n"
        "movdqu %%xmm1,  16(%0)\n"
        "movdqu %%xmm2,  32(%0)\n"
        "movdqu %%xmm3,  48(%0)\n"
        "movdqu %%xmm4,  64(%0)\n"
        "movdqu %%xmm5,  80(%0)\n"
        "movdqu %%xmm6,  96(%0)\n"
        "movdqu %%xmm7, 112(%0)\n"
        "movdqu %%xmm8, 128(%0)\n"
        "movdqu %%xmm9, 144(%0)\n"
        "movdqu %%xmm10,160(%0)\n"
        "movdqu %%xmm11,176(%0)\n"
        "movdqu %%xmm12,192(%0)\n"
        "movdqu %%xmm13,208(%0)\n"
        "movdqu %%xmm14,224(%0)\n"
        "movdqu %%xmm15,240(%0)\n"
        : : "r"(out) : "memory"
    );
}

static inline void dtask_load_xmm(const u64 *in) {
    __asm__ volatile (
        "movdqu   0(%0), %%xmm0\n"
        "movdqu  16(%0), %%xmm1\n"
        "movdqu  32(%0), %%xmm2\n"
        "movdqu  48(%0), %%xmm3\n"
        "movdqu  64(%0), %%xmm4\n"
        "movdqu  80(%0), %%xmm5\n"
        "movdqu  96(%0), %%xmm6\n"
        "movdqu 112(%0), %%xmm7\n"
        "movdqu 128(%0), %%xmm8\n"
        "movdqu 144(%0), %%xmm9\n"
        "movdqu 160(%0), %%xmm10\n"
        "movdqu 176(%0), %%xmm11\n"
        "movdqu 192(%0), %%xmm12\n"
        "movdqu 208(%0), %%xmm13\n"
        "movdqu 224(%0), %%xmm14\n"
        "movdqu 240(%0), %%xmm15\n"
        : : "r"(in) : "memory"
    );
}

__attribute__((noreturn)) void dtask_task_exit(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static __attribute__((noreturn)) void dtask_task_entry(void) {
    for (;;) {
        dtask_t *task = g_current_task;
        if (!task || !task->active) {
            dtask_task_exit();
        }

        task->func();
        task->next_tick = g_tick + task->period;

        while (task->active && g_tick < task->next_tick) {
            __asm__ volatile ("sti; hlt" : : : "memory");
        }
    }
}

static void dtask_prepare_initial(dtask_t *t) {
    u16 cs = interrupts_kernel_cs();
    u16 ss = interrupts_kernel_ss();
    u64 *frame;
    u64 frame_bytes;
    frame_bytes = (15 + 2 + 5) * sizeof(u64);
    frame = (u64 *)((t->stack_top - frame_bytes) & ~0xFULL);
    frame[0]  = 0;   /* r15 */
    frame[1]  = 0;   /* r14 */
    frame[2]  = 0;   /* r13 */
    frame[3]  = 0;   /* r12 */
    frame[4]  = 0;   /* r11 */
    frame[5]  = 0;   /* r10 */
    frame[6]  = 0;   /* r9  */
    frame[7]  = 0;   /* r8  */
    frame[8]  = 0;   /* rdi */
    frame[9]  = 0;   /* rsi */
    frame[10] = 0;   /* rbp */
    frame[11] = 0;   /* rdx */
    frame[12] = 0;   /* rcx */
    frame[13] = 0;   /* rbx */
    frame[14] = 0;   /* rax */
    frame[15] = 0;                     /* vector */
    frame[16] = 0;                     /* error_code */
    frame[17] = (u64)dtask_task_entry; /* rip */
    frame[18] = (u64)cs;               /* cs  */
    frame[19] = 0x202;                 /* rflags (IF=1) */
    frame[20] = ((u64)frame - 16) & ~0xFULL;
    frame[21] = (u64)ss;               /* ss  */
    *(u64 *)frame[20] = (u64)dtask_task_exit;
    t->saved.rsp    = (u64)frame;
    t->saved.rip    = (u64)dtask_task_entry;
    t->saved.cs     = cs;
    t->saved.rflags = 0x202;
    for (u32 i = 0; i < DTASK_SIMD_SLOTS * 2; i++) t->saved.simd[i] = 0;
}

static int dtask_add_common(const char *name, void (*func)(void),
                            u64 period, u32 uses_simd) {
    if (g_task_count >= DTASK_MAX) {
        klog("[task] FAILED: too many tasks\n");
        return DEPRESSURE_OUT_OF_MEMORY;
    }
    if (!name || !func) {
        klog("[task] FAILED: null name/func\n");
        return DEPRESSURE_NULL_DATA;
    }

    dtask_t *t = &g_tasks[g_task_count];
    t->name = name;
    t->func = func;
    t->period = period ? period : 1;
    t->next_tick = g_tick + 1;
    t->active = 1;
    t->uses_simd = uses_simd ? 1 : 0;
    t->stack_base = (u64)g_task_stacks[g_task_count];
    t->stack_size = DTASK_STACK_SIZE;
    t->stack_top  = t->stack_base + DTASK_STACK_SIZE;
    t->stack_top &= ~0xFULL;

    dtask_prepare_initial(t);

    klog("[task] add[");
    klog_dec(g_task_count);
    klog("] name=");
    klog(name);
    klog(" func=");
    klog_hex64((u64)func);
    klog(" period=");
    klog_dec(t->period);
    klog(" stack=");
    klog_hex64(t->stack_base);
    klog("-");
    klog_hex64(t->stack_top);
    klog(" rsp=");
    klog_hex64(t->saved.rsp);
    klog("\n");

    g_task_count++;
    return DEPRESSURE_STATUS_OK;
}

int dkrnl_task_add(const char *name, void (*func)(void), u64 period) {
    return dtask_add_common(name, func, period, 0);
}

int dkrnl_task_add_simd(const char *name, void (*func)(void), u64 period) {
    return dtask_add_common(name, func, period, 1);
}

int dkrnl_task_remove(const char *name) {
    if (!name) return DEPRESSURE_NULL_DATA;
    for (u32 i = 0; i < g_task_count; i++) {
        if (!g_tasks[i].active) continue;
        int match = 1;
        for (int k = 0; k < 32; k++) {
            char a = g_tasks[i].name[k];
            char b = name[k];
            if (a != b) { match = 0; break; }
            if (a == 0) break;
        }
        if (match) {
            g_tasks[i].active = 0;
            if (&g_tasks[i] == g_current_task) {
                g_need_resched = 1;
            }
            return DEPRESSURE_STATUS_OK;
        }
    }
    return DEPRESSURE_NOT_FOUND;
}

void dkrnl_task_yield(void) {
    g_need_resched = 1;
    __asm__ volatile ("sti; hlt");
}

const char *dkrnl_current_task_name(void) {
    return g_current_task ? g_current_task->name : 0;
}

u64 dkrnl_get_tick(void) {
    return g_tick;
}

void dkrnl_kill_current_task(void) {
    if (g_current_task) {
        g_current_task->active = 0;
    }
    g_need_resched = 1;
}

u16 pit_log     = 0;
u64 last_update = 0;
u64 last_poll   = 0;
u64 dkrnl_timer_tick(interrupt_frame_t *frame) {
    if (pit_log==0) {
        klog("[kmain] PIT armed, IF=");
        {
            u64 rflags;
            __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));
            klog_dec((rflags >> 9) & 1);
            klog("\n");
        }
        pit_log=1;
        timer_set_source(DTIMER_SOURCE_PIT);
    }
    /* Update Cores */
    if (g_tick - last_update >= 1) {
        dsmp_update_loads();
        last_update = g_tick;
    }
    g_tick++;

    if (g_task_count == 0) return (u64)frame;

    if (g_current_task) {
        dtask_t *cur = g_current_task;
        cur->saved.r15        = frame->r15;
        cur->saved.r14        = frame->r14;
        cur->saved.r13        = frame->r13;
        cur->saved.r12        = frame->r12;
        cur->saved.r11        = frame->r11;
        cur->saved.r10        = frame->r10;
        cur->saved.r9         = frame->r9;
        cur->saved.r8         = frame->r8;
        cur->saved.rdi        = frame->rdi;
        cur->saved.rsi        = frame->rsi;
        cur->saved.rbp        = frame->rbp;
        cur->saved.rdx        = frame->rdx;
        cur->saved.rcx        = frame->rcx;
        cur->saved.rbx        = frame->rbx;
        cur->saved.rax        = frame->rax;
        cur->saved.vector     = frame->vector;
        cur->saved.error_code = frame->error_code;
        cur->saved.rip        = frame->rip;
        cur->saved.cs         = frame->cs;
        cur->saved.rflags     = frame->rflags;
        cur->saved.rsp        = (u64)frame;

        if (cur->uses_simd) {
            dtask_save_xmm(cur->saved.simd);
        }
    }

    u32 start = g_current_index;
    u32 next  = start;
    int found = 0;

    if (g_need_resched && g_current_task) {
        for (u32 i = 1; i <= g_task_count; i++) {
            u32 idx = (start + i) % g_task_count;
            if (g_tasks[idx].active) { next = idx; found = 1; break; }
        }
    } else {
        for (u32 i = 1; i <= g_task_count; i++) {
            u32 idx = (start + i) % g_task_count;
            if (!g_tasks[idx].active) continue;
            if (g_tick >= g_tasks[idx].next_tick) {
                next = idx;
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        if (g_current_task) {
            if (g_current_task->uses_simd) {
                dtask_load_xmm(g_current_task->saved.simd);
            }
            g_need_resched = 0;
            return g_current_task->saved.rsp;
        }
        for (u32 i = 0; i < g_task_count; i++) {
            if (g_tasks[i].active) { next = i; found = 1; break; }
        }
        if (!found) return (u64)frame;
    }

    if (g_tick <= 5) {
        klog("[tick ");
        klog_dec(g_tick);
        klog("] switch -> task[");
        klog_dec(next);
        klog("] name=");
        klog(g_tasks[next].name);
        klog(" func=");
        klog_hex64((u64)g_tasks[next].func);
        klog(" rsp=");
        klog_hex64(g_tasks[next].saved.rsp);
        klog("\n");
    }

    g_current_index = next;
    g_current_task  = &g_tasks[next];

    if (g_current_task->uses_simd) {
        dtask_load_xmm(g_current_task->saved.simd);
    }

    g_need_resched = 0;

    return g_current_task->saved.rsp;
}

u64 dsmp_get_total_ram(void) { return g_total_ram; }

static dstatus_t driver_add_ok(u64 a, u64 b, u64 *out) {
    if (a + b < a) return DEPRESSURE_OVERFLOW;
    *out = a + b;
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t align_driver_base(u64 address, u64 *out) {
    if (address > U64_MAX - (DRIVER_LOAD_ALIGN - 1))
        return DEPRESSURE_OVERFLOW;
    *out = (address + DRIVER_LOAD_ALIGN - 1) & ~(DRIVER_LOAD_ALIGN - 1);
    return DEPRESSURE_STATUS_OK;
}

static dstatus_t delf_load_next(void *buf, u64 size, u64 *next_base,
                                u64 *loaded_base, u64 *out_entry) {
    dstatus_t st;
    u64 span;
    u64 candidate;
    u64 end;
    u64 next;
    u64 entry;

    *loaded_base = 0;
    *out_entry = 0;

    span = delf_image_span(buf, size);
    if (span == 0) return DEPRESSURE_CORRUPTED_DATA;

    st = align_driver_base(*next_base, &candidate);
    if (DSTATUS_IS_ERR(st)) return st;

    st = driver_add_ok(candidate, span, &end);
    if (DSTATUS_IS_ERR(st)) return st;

    if (candidate < KERNEL_RESERVED_END && end > KERNEL_RESERVED_START) {
        candidate = KERNEL_RESERVED_END;
    }

    st = driver_add_ok(candidate, span, &end);
    if (DSTATUS_IS_ERR(st)) return st;

    if (end > DRIVER_LOAD_LIMIT) return DEPRESSURE_OUT_OF_MEMORY;

    st = align_driver_base(end, &next);
    if (DSTATUS_IS_ERR(st)) return st;

    *loaded_base = candidate;
    entry = delf_load_at(buf, size, candidate);
    if (entry == 0) return DEPRESSURE_CORRUPTED_DATA;

    *next_base = next;
    *out_entry = entry;
    return DEPRESSURE_STATUS_OK;
}

ddriver_t *DFindAndGetDriver(const char *name) {
    dkernel_registry_t *registry;
    ddriver_t *driver;
    u32 visited = 0;

    if (!name || !name[0]) return 0;

    registry = dreg_get();
    if (!registry || registry->pattern != DKES_PATTERN) return 0;

    driver = (ddriver_t *)registry->first_driver;
    while (driver && visited++ < MAX_REGISTERED_DRIVERS) {
        u32 i = 0;
        while (i < DDRIVER_NAME_MAX &&
               driver->name[i] != 0 &&
               name[i] != 0 &&
               driver->name[i] == name[i]) {
            i++;
        }
        if (i < DDRIVER_NAME_MAX &&
            driver->name[i] == 0 &&
            name[i] == 0) return driver;
        driver = (ddriver_t *)driver->next;
    }
    return 0;
}

int task_handle_fault(const interrupt_frame_t *frame, task_fault_info_t *out_info) {
    if (!out_info) return 0;

    dtask_t *t = g_current_task;

    out_info->vector     = frame->vector;
    out_info->error_code = frame->error_code;
    out_info->rip        = frame->rip;
    out_info->cr2        = 0;
    out_info->task_name  = t ? t->name : "(kernel)";

    if (frame->vector == 14) {
        u64 cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        out_info->cr2 = cr2;
    }

    if (!t || !t->active) {
        return 0;
    }

    t->active = 0;
    g_need_resched = 1;

    if (t->saved.rip == frame->rip) {
        t->saved.rip = (u64)dtask_task_exit;
    }

    return 1;
}

void task_log_fault(const task_fault_info_t *info) {
    if (!info) return;

    klog("\n[FAULT] task=");
    klog(info->task_name ? info->task_name : "?");
    klog(" vector=");
    klog_dec(info->vector);
    klog(" error=");
    klog_hex64(info->error_code);
    klog(" rip=");
    klog_hex64(info->rip);
    if (info->vector == 14) {
        klog(" cr2=");
        klog_hex64(info->cr2);
    }
    klog("\n");

    klog("[FAULT] task terminated, continuing\n");
}

static dkernel_api_t g_kernel_api = {
    .version              = DKRNL_API_VERSION,
    ._pad                 = 0,
    .register_driver      = (int (*)(void *))dreg_register_driver,
    .task_add             = dkrnl_task_add,
    .task_add_simd        = dkrnl_task_add_simd,
    .task_remove          = dkrnl_task_remove,
    .write_file           = uefi_write_file,
    .read_file            = uefi_load_file,
    .smp_get_ncpu         = dsmp_get_ncpu,
    .smp_get_load         = dsmp_get_load,
    .get_total_ram        = dsmp_get_total_ram,
    .get_storage_bytes    = dstorage_get_total,
    .get_storage_count    = dstorage_get_count,
    .get_storage_device_size = dstorage_get_device_size,
    .smp_add_metrics      = dsmp_add_metrics,
    .DFindAndGetDriver    = DFindAndGetDriver,
    .hpet_info            = &g_hpet_info,
    .DCreateHeap          = DCreateHeap,
    .DWriteHeap           = DWriteHeap,
    .DReadHeap            = DReadHeap,
    .DCloseHeap           = DCloseHeap,
    .timer_set_source     = timer_set_source,
    .timer_get_source     = timer_get_source,
    .timer_set_hz         = timer_set_hz,
    .timer_get_hz         = timer_get_hz,
    .timer_source_name           = timer_source_name,
    .timer_get_ioapic_available  = (u32 (*)(void))ioapic_available,
    .timer_get_lapic_id          = lapic_get_id,
};

void kmain(void) {
    volatile u64 *mark = (volatile u64 *)MARK_ADDR;
    volatile u64 *dbg  = (volatile u64 *)DBG_ADDR;
    dstatus_t st;

    early_uart_puts("\n[kmain] === Depressure OS ===\n");
    early_uart_puts("[kmain] early log ready\n");

    if (!interrupts_init()) {
        early_uart_puts("[kmain] failed to install GDT/IDT\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    early_uart_puts("[kmain] GDT/IDT installed\n");

    klog("[kmain] cs=");
    klog_hex64(interrupts_kernel_cs());
    klog(" ds=");
    klog_hex64(interrupts_kernel_ds());
    klog(" ss=");
    klog_hex64(interrupts_kernel_ss());
    klog("\n");

    *mark = 0xCCCC0001;
    klog("[kmain] mark 1\n");

    dreg_init();

    st = timer_init_sources();
    klog("[kmain] timer sources init: ");
    klog_status(st);
    klog(" IOAPIC=");
    klog_dec(ioapic_available());
    klog(" LAPIC_ID=");
    klog_dec(lapic_get_id());
    klog("\n");

    /* Heap Memory Block init */
    st = dmemory_init();
    klog("[kmain] heap init: ");
    klog_status(st);
    klog(" size=");
    klog_dec(DHMB_SIZE / (1024 * 1024));
    klog(" MB slots=");
    klog_dec(dmemory_free_slots());
    klog("\n");

    u64 count = uefi_list_dir(driver_dir, entries, MAX_DRIVERS);
    dbg[0] = count;
    for (u64 i = DBG_DRIVER_TABLE;
         i < DBG_DRIVER_TABLE + MAX_DRIVERS * 2; i++) dbg[i] = 0;

    klog("[kmain] drivers found: ");
    klog_dec(count);
    klog("\n");

    if (count == 0) {
        klog("[kmain] no drivers found\n");
        *mark = 0xCCCC00FF;
        for (;;) __asm__ volatile ("hlt");
    }
    *mark = 0xCCCC0003;

    u64 loaded = 0;
    u64 next_base = 0x400000;

    for (u64 i = 0; i < count; i++) {
        ddir_entry_t *e = &entries[i];

        u64 nlen = e->name_len;
        if (nlen > 200) nlen = 200;

        u64 pos = 0;
        for (int k = 0; k < 17; k++) full_path[pos++] = path_prefix[k];
        for (u64 k = 0; k < nlen; k++) full_path[pos++] = e->name[k];
        full_path[pos] = 0;

        klog("[kmain] loading: ");
        for (u64 k = 0; k < nlen && k < 32; k++) {
            char cc[2] = { (char)e->name[k], 0 };
            klog(cc);
        }
        klog("\n");

        u64 bytes = uefi_load_file((const u16 *)full_path, driver_buf,
                                    sizeof(driver_buf));
        if (bytes == 0) {
            klog("[kmain] failed to load\n");
            continue;
        }
        if (!delf_check(driver_buf)) {
            klog("[kmain] not ELF: skipping\n");
            continue;
        }

        u64 load_base = 0;
        u64 entry = 0;
        st = delf_load_next(driver_buf, bytes, &next_base,
                             &load_base, &entry);
        dbg[DBG_DRIVER_TABLE + i * 2 + 0] = load_base;
        dbg[DBG_DRIVER_TABLE + i * 2 + 1] = entry;

        klog("[kmain] load[");
        klog_dec(i);
        klog("] size=");
        klog_dec(bytes);
        klog(" base=");
        klog_hex64(load_base);
        klog(" entry=");
        klog_hex64(entry);
        klog(" st=");
        klog_status(st);
        klog("\n");

        if (DSTATUS_IS_ERR(st) || entry == 0) continue;

        typedef void (*driver_entry_t)(void *api, void *fb_info);
        driver_entry_t fn = (driver_entry_t)entry;
        fn(&g_kernel_api, (void *)FB_INFO_ADDR);

        dbg[10 + i] = entry;
        loaded++;
    }

    dbg[1] = loaded;
    klog("[kmain] loaded: ");
    klog_dec(loaded);
    klog("\n");

    if (loaded == 0) {
        *mark = 0xCCCC00BB;
        for (;;) __asm__ volatile ("hlt");
    }
    *mark = 0xCCCC0004;

    ddriver_t *acpi_driver = DFindAndGetDriver("acpi");
    const acpi_api_t *acpi = acpi_driver ?
        (const acpi_api_t *)acpi_driver->api : 0;
    dstatus_t hpet_status = dhpet_init(acpi);
    klog("[kmain] HPET initialization: ");
    klog_status(hpet_status);
    klog("\n");
    klog("[kmain] HPET GAS space=");
    klog_dec(g_hpet_info.address_space_id);
    klog(" width=");
    klog_dec(g_hpet_info.register_bit_width);
    klog(" offset=");
    klog_dec(g_hpet_info.register_bit_offset);
    klog(" address=");
    klog_hex64(g_hpet_info.mmio_base);
    klog("\n");
    if (DSTATUS_IS_OK(hpet_status)) {
        klog("[kmain] HPET MMIO=");
        klog_hex64(g_hpet_info.mmio_base);
        klog(" frequency=");
        klog_dec(g_hpet_info.frequency_hz);
        klog(" Hz timers=");
        klog_dec(g_hpet_info.timer_count);
        klog("\n");
    }

    dkernel_registry_t *reg = dreg_get();
    if (reg->first_driver == 0) {
        klog("[kmain] no drivers in registry\n");
        *mark = 0xCCCC00AA;
        for (;;) __asm__ volatile ("hlt");
    }

    ddriver_t *registered;
    dws_api_t *dws = 0;

    registered = DFindAndGetDriver("dws");
    if (registered) dws = (dws_api_t *)registered->api;

    uart_api_t *uart = 0;
    registered = DFindAndGetDriver("uart");
    if (registered) uart = (uart_api_t *)registered->api;
    g_uart = uart;

    if (g_uart) {
        klog("[kmain] uart driver registered, flushing buffer...\n");
        uart_flush();
    }

    dkbd_api_t *kbd = 0;
    registered = DFindAndGetDriver("dkbd");
    if (registered) kbd = (dkbd_api_t *)registered->api;

    dmouse_api_t *mouse = 0;
    registered = DFindAndGetDriver("dmouse");
    if (registered) mouse = (dmouse_api_t *)registered->api;

    ddriver_t *umcas_drv = DFindAndGetDriver("umcas");
    if (umcas_drv && umcas_drv->api) {
        dumcas_api_t *umcas = (dumcas_api_t *)umcas_drv->api;
        dsmp_set_umcas_submit(umcas->submit);
    }

    if (!dws || !dws->fill) {
        klog("[kmain] no dws driver\n");
        *mark = 0xCCCC00CC;
        for (;;) __asm__ volatile ("hlt");
    }
    *mark = 0xCCCC0005;

    dws->fill(0x001F3F);

    EFI_BOOT_SERVICES *bs = *(EFI_BOOT_SERVICES **)0x200108;
    int ncpu = dsmp_init(bs);
    dbg[50] = (u64)ncpu;

    klog("[kmain] ncpu = ");
    klog_dec(ncpu);
    klog("\n");

    if (ncpu > 1) {
        u64 saved_rbp, saved_rbx, saved_r12, saved_r13, saved_r14, saved_r15;
        __asm__ volatile (
            "mov %%rbp, %0\n"
            "mov %%rbx, %1\n"
            "mov %%r12, %2\n"
            "mov %%r13, %3\n"
            "mov %%r14, %4\n"
            "mov %%r15, %5\n"
            : "=r"(saved_rbp), "=r"(saved_rbx), "=r"(saved_r12),
            "=r"(saved_r13), "=r"(saved_r14), "=r"(saved_r15)
            :
            : "memory"
        );

        dsmp_start_workers(bs);

        __asm__ volatile (
            "mov %0, %%rbp\n"
            "mov %1, %%rbx\n"
            "mov %2, %%r12\n"
            "mov %3, %%r13\n"
            "mov %4, %%r14\n"
            "mov %5, %%r15\n"
            :
            : "r"(saved_rbp), "r"(saved_rbx), "r"(saved_r12),
            "r"(saved_r13), "r"(saved_r14), "r"(saved_r15)
            : "memory"
        );
    }

    g_total_ram = uefi_get_total_ram(bs);
    dbg[60] = g_total_ram;

    klog("[kmain] total RAM: ");
    klog_dec(g_total_ram / (1024 * 1024));
    klog(" MB\n");

    dstorage_detect(bs);
    dbg[61] = dstorage_get_total();
    dbg[62] = dstorage_get_count();

    klog("[kmain] storage: ");
    klog_dec(dstorage_get_count());
    klog(" devices\n");

    static const u16 logo_path[] = {
        '\\','D','e','p','r','e','s','s','u','r','e','\\',
        'l','o','g','o','.','b','m','p', 0
    };

    static u8 bmp_buf[0x100000];
    u64 bmp_bytes = uefi_load_file(logo_path, bmp_buf, sizeof(bmp_buf));
    dbg[5] = bmp_bytes;

    if (bmp_bytes > 0 && dbmp_check(bmp_buf, bmp_bytes)) {
        dbmp_info_t bmp;
        if (dbmp_parse(bmp_buf, bmp_bytes, &bmp) == 0) {
            dbg[6] = bmp.width;
            dbg[7] = bmp.height;
            dbg[8] = bmp.bpp;

            static u32 rgb_pixels[160 * 90];
            u64 w = bmp.width;
            u64 h = bmp.height;
            if (w > 160) w = 160;
            if (h > 90)  h = 90;

            for (u64 yy = 0; yy < h; yy++) {
                for (u64 xx = 0; xx < w; xx++) {
                    rgb_pixels[yy * w + xx] = dbmp_get_pixel(&bmp, xx, yy);
                }
            }

            typedef struct fb_info {
                u64 base;
                u64 width;
                u64 height;
                u64 pitch;
                u64 format;
                u64 size;
            } fb_info_t;

            fb_info_t *fb = (fb_info_t *)0x200000;
            u64 scr_w = fb->width;
            u64 scr_h = fb->height;

            u64 logo_w = (scr_w * 70) / 100;
            u64 logo_h = (scr_h * 70) / 100;
            u64 logo_x = (scr_w - logo_w) / 2;
            u64 logo_y = (scr_h - logo_h) / 2;

            dws->blit_scaled(logo_x, logo_y, logo_w, logo_h,
                              rgb_pixels, w, h);

            uefi_stall(2000000);

            dws->fill(0x00000000);
            *mark = 0xCCCC000B;
        }
    }

    dboot_config_t cfg;
    st = dboot_load(&cfg);
    dbg[30] = cfg.boot_mode;
    dbg[31] = cfg.log_level;

    klog("[kmain] boot mode: ");
    klog_dec(cfg.boot_mode);
    klog(" (st=");
    klog_status(st);
    klog(")\n");

    if (cfg.boot_mode == 0) {
        static const u16 devcon_path[] = {
            '\\','D','e','p','r','e','s','s','u','r','e','\\',
            'D','e','v','C','o','n','.','e','l','f', 0
        };

        u64 bytes = uefi_load_file(devcon_path, driver_buf,
                                    sizeof(driver_buf));
        dbg[32] = bytes;

        if (bytes > 0 && delf_check(driver_buf)) {
            u64 load_base = 0;
            u64 entry = 0;
            st = delf_load_next(driver_buf, bytes, &next_base,
                                 &load_base, &entry);
            dbg[33] = entry;

            klog("[kmain] DevCon entry=");
            klog_hex64(entry);
            klog(" st=");
            klog_status(st);
            klog("\n");

            if (entry) {
                typedef void *(*prog_entry_t)(void *api, void *fb_info,
                                              void *uart, void *dws,
                                              void *kbd, void *boot_mode);
                prog_entry_t fn = (prog_entry_t)entry;
                (void)fn(&g_kernel_api,
                         (void *)FB_INFO_ADDR,
                         (void *)g_uart,
                         (void *)dws,
                         (void *)kbd,
                         (void *)(u64)cfg.boot_mode);
            }
        }
    } else if (cfg.boot_mode == 1) {
        static const u16 depmgr_path[] = {
            '\\','D','e','p','r','e','s','s','u','r','e','\\',
            'D','e','p','M','g','r','.','e','l','f', 0
        };

        u64 bytes = uefi_load_file(depmgr_path, driver_buf,
                                    sizeof(driver_buf));
        dbg[32] = bytes;

        if (bytes > 0 && delf_check(driver_buf)) {
            u64 load_base = 0;
            u64 entry = 0;
            st = delf_load_next(driver_buf, bytes, &next_base,
                                 &load_base, &entry);
            dbg[33] = entry;

            klog("[kmain] DepMgr entry=");
            klog_hex64(entry);
            klog(" st=");
            klog_status(st);
            klog("\n");

            if (entry) {
                typedef void *(*prog_entry_t)(void *api, void *fb_info,
                              void *uart, void *dws,
                              void *kbd, void *mouse,
                              void *boot_mode, void *ncpu);
                prog_entry_t fn = (prog_entry_t)entry;
                (void)fn(&g_kernel_api,
                        (void *)FB_INFO_ADDR,
                        (void *)g_uart,
                        (void *)dws,
                        (void *)kbd,
                        (void *)mouse,
                        (void *)(u64)cfg.boot_mode,
                        (void *)(u64)ncpu);
            }
        }
    }

    *mark = 0xCCCC000F;

    // LHGC Test: Check if Lightweight Heap Garbage Collector correctly clear Leak heaps.
    {
        D_HEAP_POINTER leak = DCreateHeap("kmain_leak");
        if (leak) {
            u8 payload[128];
            for (int i = 0; i < 128; i++) payload[i] = (u8)(i ^ 0x5A);

            dstatus_t ws = DWriteHeap(leak, 0, payload, sizeof(payload));
            klog("[kmain] leak heap created, write st=");
            klog_status(ws);
            klog("\n");
        } else {
            klog("[kmain] leak heap FAILED\n");
        }
    }
    dmemory_set_uart((void *)uart);
    st = dkrnl_task_add("LHGC", lhgc_task, 200);
    klog("[kmain] LHGC task: ");
    klog_status(st);
    klog("\n");

    klog("[kmain] ===== TASK DUMP =====\n");
    klog("[kmain] task_count=");
    klog_dec(g_task_count);
    klog("\n");
    for (u32 i = 0; i < g_task_count; i++) {
        klog("[kmain] task[");
        klog_dec(i);
        klog("] name=");
        klog(g_tasks[i].name);
        klog(" func=");
        klog_hex64((u64)g_tasks[i].func);
        klog(" period=");
        klog_dec(g_tasks[i].period);
        klog(" active=");
        klog_dec(g_tasks[i].active);
        klog(" rsp=");
        klog_hex64(g_tasks[i].saved.rsp);
        klog(" rip=");
        klog_hex64(g_tasks[i].saved.rip);
        klog(" cs=");
        klog_hex64(g_tasks[i].saved.cs);
        klog("\n");
    }

    dmemory_dump(klog);
    klog("[kmain] =====================\n");

    klog("[kmain] entering tasker loop\n");

    pit_init(TASK_QUANTUM_HZ);

    *mark = 0xCCCC000D;
    for (;;) __asm__ volatile ("hlt");
}