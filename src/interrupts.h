#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "dtypes.h"
#include "dstat.h"

typedef struct interrupt_frame {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rdi;
    u64 rsi;
    u64 rbp;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} interrupt_frame_t;

typedef struct task_fault_info {
    const char *task_name;
    u64         vector;
    u64         error_code;
    u64         rip;
    u64         cr2;
} task_fault_info_t;
#define DTIMER_SOURCE_PIT    0u
#define DTIMER_SOURCE_HPET   1u
#define DTIMER_SOURCE_LAPIC  2u
#define DTIMER_SOURCE_COUNT  3u
#define IRQ_BASE            32
#define IRQ_TIMER_PIT       32      /* IRQ0 -> 0x20 */
#define VECTOR_HPET         48      /* 0x30 */
#define VECTOR_LAPIC        64      /* 0x40 */
int task_handle_fault(const interrupt_frame_t *frame,
                      task_fault_info_t *out_info);
void task_log_fault(const task_fault_info_t *info);
int interrupts_init(void);
u64 interrupt_dispatch(interrupt_frame_t *frame);
void pit_init(u32 hz);
void irq_dispatch(u32 irq, interrupt_frame_t *frame);
u16 interrupts_kernel_cs(void);
u16 interrupts_kernel_ds(void);
u16 interrupts_kernel_ss(void);
dstatus_t timer_init_sources(void);
dstatus_t timer_set_source(u32 source);
u32       timer_get_source(void);
dstatus_t timer_set_hz(u32 hz);
u32       timer_get_hz(void);
const char *timer_source_name(u32 source);
dstatus_t ioapic_init(void);
void      ioapic_route_irq(u32 gsi, u8 vector, u8 dest_apic_id, int masked);
int       ioapic_available(void);
void lapic_enable(void);
u32  lapic_get_id(void);

#endif