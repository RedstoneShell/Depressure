// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 RedstoneShell
//
// This file is part of the Depressure Operating System.

#include "interrupts.h"
#include "dtypes.h"
#include "dkrnl.h"
#include "dstat.h"
#include "acpi.h"
#include "hpet.h"

#define IDT_ENTRIES 256
#define GDT_ENTRIES 256

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_FREQ  1193182u

#define LAPIC_BASE      0xFEE00000ULL
#define IOAPIC_REG_SEL  0x00
#define IOAPIC_REG_WIN  0x10
#define IOAPIC_REG_REDT 0x10

typedef struct __attribute__((packed)) table_register {
    u16 limit;
    u64 base;
} table_register_t;

typedef struct __attribute__((packed)) idt_gate {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  attributes;
    u16 offset_middle;
    u32 offset_high;
    u32 reserved;
} idt_gate_t;

typedef struct __attribute__((packed)) task_state_segment {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} task_state_segment_t;

typedef struct __attribute__((packed)) tss_descriptor {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  limit_flags;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} tss_descriptor_t;

static u64 g_gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static idt_gate_t g_idt[IDT_ENTRIES] __attribute__((aligned(16)));
static task_state_segment_t g_tss __attribute__((aligned(16)));
static u8  g_double_fault_stack[0x4000] __attribute__((aligned(16)));
static table_register_t g_gdtr;
static table_register_t g_idtr;
static u16 g_kernel_code_selector;
static u16 g_kernel_data_selector;
static u32 g_double_fault_ist;

static volatile u32 g_timer_source = DTIMER_SOURCE_PIT;
static volatile u32 g_timer_hz     = 1000;

static u64 g_ioapic_base     = 0;
static u32 g_ioapic_gsi_base = 0;
static u32 g_apic_id         = 0;

extern u8 stack_top[];
extern void gdt_reload(const table_register_t *gdtr, u16 code_selector,
                       u16 data_selector, u16 tss_selector);
extern void (*interrupt_stub_table[IDT_ENTRIES])(void);
extern u64 dkrnl_timer_tick(interrupt_frame_t *frame);

static inline void outb(u16 p, u8 v) { __asm__ volatile ("outb %0, %1" ::"a"(v), "Nd"(p)); }
static inline u8  inb(u16 p) { u8 r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }

static inline u32 mmio_r32(u64 base, u32 off) { return *(volatile u32 *)(base + off); }
static inline void mmio_w32(u64 base, u32 off, u32 v) { *(volatile u32 *)(base + off) = v; }

static void copy_bytes(void *dst, const void *src, u64 n) {
    u8 *d = (u8 *)dst; const u8 *s = (const u8 *)src;
    for (u64 i = 0; i < n; i++) d[i] = s[i];
}

static void panic_putc(char v) {
    u8 s;
    do { __asm__ volatile ("inb %1, %0" : "=a"(s) : "Nd"((u16)0x3FD)); } while ((s & 0x20) == 0);
    __asm__ volatile ("outb %0, %1" ::"a"((u8)v), "Nd"((u16)0x3F8));
}
static void panic_puts(const char *t) { while (*t) { if (*t=='\n') panic_putc('\r'); panic_putc(*t++); } }
static void panic_put_hex(u64 v) {
    static const char h[] = "0123456789ABCDEF";
    panic_puts("0x");
    for (int i = 60; i >= 0; i -= 4) panic_putc(h[(v >> i) & 0xF]);
}
static void panic_put_dec(u64 v) {
    char b[24]; int i = 0;
    if (v == 0) { panic_putc('0'); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i) panic_putc(b[--i]);
}

static void set_idt_gate(u32 vector, void (*handler)(void), u8 ist) {
    u64 a = (u64)handler;
    idt_gate_t *g = &g_idt[vector];
    g->offset_low    = (u16)a;
    g->selector      = g_kernel_code_selector;
    g->ist           = ist & 0x7;
    g->attributes    = 0x8E;
    g->offset_middle = (u16)(a >> 16);
    g->offset_high   = (u32)(a >> 32);
    g->reserved      = 0;
}

static int copy_current_tss(const table_register_t *old_gdtr, u64 old_gdt_bytes) {
    u16 selector; u64 index, descriptor, descriptor_high, base;
    u32 limit; u8 access;
    const task_state_segment_t *old_tss;

    __asm__ volatile ("str %0" : "=r"(selector));
    if ((selector & 0x4) != 0) return 0;
    index = selector >> 3;
    if (index * sizeof(u64) + 2 * sizeof(u64) > old_gdt_bytes) return 0;
    descriptor = *(const u64 *)(old_gdtr->base + index * sizeof(u64));
    descriptor_high = *(const u64 *)(old_gdtr->base + (index + 1) * sizeof(u64));
    access = (u8)(descriptor >> 40);
    limit = (u32)(descriptor & 0xFFFF) | (u32)((descriptor >> 48) & 0x0F) << 16;
    if ((access & 0x80) == 0 ||
        ((access & 0x0F) != 0x09 && (access & 0x0F) != 0x0B) ||
        limit < sizeof(task_state_segment_t) - 1) return 0;
    base = ((descriptor >> 16) & 0xFFFF) |
           ((descriptor >> 32) & 0xFF) << 16 |
           ((descriptor >> 56) & 0xFF) << 24 |
           (descriptor_high & 0xFFFFFFFF) << 32;
    if (base == 0) return 0;
    old_tss = (const task_state_segment_t *)base;
    g_tss.rsp0 = (u64)stack_top;
    g_tss.rsp1 = old_tss->rsp1;
    g_tss.rsp2 = old_tss->rsp2;
    for (u32 i = 0; i < 7; i++) g_tss.ist[i] = old_tss->ist[i];
    g_tss.iomap_base = sizeof(g_tss);
    return 1;
}

static int install_gdt(const table_register_t *old_gdtr) {
    u64 old_bytes = (u64)old_gdtr->limit + 1;
    u64 old_entries = (old_bytes + 7) / 8;
    u64 code_index, data_index, tss_index;
    u64 tss_base = (u64)&g_tss;
    tss_descriptor_t *td;

    if (old_bytes > sizeof(g_gdt) - 4 * sizeof(u64)) return 0;
    copy_bytes(g_gdt, (const void *)old_gdtr->base, old_bytes);

    code_index = old_entries;
    data_index = code_index + 1;
    tss_index  = data_index + 1;
    if (tss_index + 2 > GDT_ENTRIES) return 0;

    g_gdt[code_index] = 0x00AF9A000000FFFFULL;
    g_gdt[data_index] = 0x00CF92000000FFFFULL;
    td = (tss_descriptor_t *)&g_gdt[tss_index];
    td->limit_low    = sizeof(g_tss) - 1;
    td->base_low     = (u16)tss_base;
    td->base_middle  = (u8)(tss_base >> 16);
    td->access       = 0x89;
    td->limit_flags  = 0;
    td->base_high    = (u8)(tss_base >> 24);
    td->base_upper   = (u32)(tss_base >> 32);
    td->reserved     = 0;

    g_gdtr.base  = (u64)g_gdt;
    g_gdtr.limit = (u16)((tss_index + 2) * sizeof(u64) - 1);
    g_kernel_code_selector = (u16)(code_index * sizeof(u64));
    g_kernel_data_selector = (u16)(data_index * sizeof(u64));

    gdt_reload(&g_gdtr, g_kernel_code_selector, g_kernel_data_selector,
               (u16)(tss_index * sizeof(u64)));
    return 1;
}

int interrupts_init(void) {
    table_register_t old_gdtr, old_idtr;
    u32 used_ist = 0, ist_index = 0;
    u64 idt_bytes;

    __asm__ volatile ("sgdt %0" : "=m"(old_gdtr));
    __asm__ volatile ("sidt %0" : "=m"(old_idtr));

    if (old_gdtr.base == 0 || old_gdtr.limit == 0 ||
        old_idtr.base == 0 ||
        old_idtr.limit < 32 * sizeof(idt_gate_t)) return 0;
    idt_bytes = (u64)old_idtr.limit + 1;
    if (idt_bytes > sizeof(g_idt)) return 0;

    copy_bytes(g_idt, (const void *)old_idtr.base, idt_bytes);

    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        if ((u64)(i + 1) * sizeof(idt_gate_t) > idt_bytes) break;
        used_ist |= 1u << (g_idt[i].ist & 0x7);
    }
    used_ist &= ~1u;

    for (u32 i = 1; i <= 7; i++) {
        if ((used_ist & (1u << i)) == 0) { ist_index = i; break; }
    }

    if (used_ist != 0) {
        if (!copy_current_tss(&old_gdtr, (u64)old_gdtr.limit + 1)) return 0;
    } else {
        g_tss.rsp0 = (u64)stack_top;
        g_tss.iomap_base = sizeof(g_tss);
    }
    if (ist_index != 0) {
        g_tss.ist[ist_index - 1] = (u64)(g_double_fault_stack + sizeof(g_double_fault_stack));
        g_double_fault_ist = ist_index;
    }
    if (!install_gdt(&old_gdtr)) return 0;

        for (u32 i = 0; i < IDT_ENTRIES; i++) {
        int ours = 0;

        if (i < 32)                    ours = 1;   /* CPU exceptions 0x00..0x1F */
        else if (i == IRQ_TIMER_PIT)   ours = 1;   /* 0x20 PIT */
        else if (i == VECTOR_HPET)     ours = 1;   /* 0x30 HPET */
        else if (i == VECTOR_LAPIC)    ours = 1;   /* 0x40 LAPIC */

        if (!ours) continue;

        u8 ist = 0;
        if (i == 8 && g_double_fault_ist != 0) ist = (u8)g_double_fault_ist;
        set_idt_gate(i, interrupt_stub_table[i], ist);
    }

    g_idtr.base  = (u64)g_idt;
    g_idtr.limit = (u16)(sizeof(g_idt) - 1);
    __asm__ volatile ("lidt %0" ::"m"(g_idtr) : "memory");
    return 1;
}

u16 interrupts_kernel_cs(void) { return g_kernel_code_selector; }
u16 interrupts_kernel_ds(void) { return g_kernel_data_selector; }
u16 interrupts_kernel_ss(void) { return g_kernel_data_selector; }

static void pic_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
}
static void pic_mask_all(void) {
    outb(0x21, 0xFF); outb(0xA1, 0xFF);
}
static void pic_unmask_irq0(void) {
    outb(0x21, 0xFE); outb(0xA1, 0xFF);
}

static inline u32 lapic_read(u32 reg) { return mmio_r32(LAPIC_BASE, reg); }
static inline void lapic_write(u32 reg, u32 v) { mmio_w32(LAPIC_BASE, reg, v); }

void lapic_enable(void) {
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= (1u << 11);
    lo &= ~(0xFFF00000u);
    lo |= 0xFEE00000u;
    __asm__ volatile ("wrmsr" ::"a"(lo), "d"(hi), "c"(0x1B));
    lapic_write(0xF0, 0x1FF);
    lapic_write(0x80, 0);
}

u32 lapic_get_id(void) { return lapic_read(0x20) >> 24; }

static void lapic_timer_start(u32 hz) {
    u32 ticks_per_sec = 6250000u;
    u32 init = ticks_per_sec / hz;
    if (init == 0) init = 1;
    lapic_write(0x3E0, 0x3);
    lapic_write(0x380, init);
    lapic_write(0x320, VECTOR_LAPIC | (1u << 17));
}

static void lapic_timer_stop(void) {
    lapic_write(0x320, (1u << 16));
    lapic_write(0x380, 0);
}

static u32 ioapic_read(u32 reg) {
    mmio_w32(g_ioapic_base, IOAPIC_REG_SEL, reg);
    return mmio_r32(g_ioapic_base, IOAPIC_REG_WIN);
}
static void ioapic_write(u32 reg, u32 v) {
    mmio_w32(g_ioapic_base, IOAPIC_REG_SEL, reg);
    mmio_w32(g_ioapic_base, IOAPIC_REG_WIN, v);
}

int ioapic_available(void) { return g_ioapic_base != 0; }

dstatus_t ioapic_init(void) {
    ddriver_t *drv = DFindAndGetDriver("acpi");
    if (!drv || !drv->api) return DEPRESSURE_NOT_FOUND;
    acpi_api_t *acpi = (acpi_api_t *)drv->api;

    u64 addr = 0; u32 gsi_base = 0;
    if (!acpi->get_ioapic) return DEPRESSURE_NOT_SUPPORTED;
    dstatus_t s = acpi->get_ioapic(0, &addr, &gsi_base);
    if (DSTATUS_IS_ERR(s) || addr == 0) return DEPRESSURE_NOT_FOUND;

    g_ioapic_base     = addr;
    g_ioapic_gsi_base = gsi_base;
    g_apic_id         = lapic_read(0x20) >> 24;
    return DEPRESSURE_STATUS_OK;
}

void ioapic_route_irq(u32 gsi, u8 vector, u8 dest_apic_id, int masked) {
    if (!g_ioapic_base) return;
    u32 reg = IOAPIC_REG_REDT + (gsi - g_ioapic_gsi_base) * 2;
    u32 lo = (u32)vector | (masked ? (1u << 16) : 0);
    u32 hi = ((u32)dest_apic_id) << 24;
    ioapic_write(reg, lo);
    ioapic_write(reg + 1, hi);
}

void pit_init(u32 hz) {
    pic_remap();
    pic_mask_all();
    pic_unmask_irq0();

    if (hz == 0) hz = 100;
    u32 divisor = PIT_FREQ / hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (u8)(divisor & 0xFF));
    outb(PIT_CH0, (u8)((divisor >> 8) & 0xFF));
}

static void pit_stop(void) {
    outb(0x21, 0xFF);
    outb(PIT_CMD, 0x30);
    outb(PIT_CH0, 0x00);
    outb(PIT_CH0, 0x00);
}

#define HPET_REG_CAP        0x000
#define HPET_REG_CONFIG     0x010
#define HPET_REG_COMP0_CFG  0x100
#define HPET_REG_COMP0_VAL  0x108

static dstatus_t hpet_start_periodic(u32 hz) {
    if (!g_hpet_info.enabled || !g_hpet_info.mmio_base)
        return DEPRESSURE_HPET_NOT_INITIALIZED;

    u64 period_fs = g_hpet_info.period_femtoseconds;
    if (!period_fs) return DEPRESSURE_HPET_INVALID_PERIOD;

    u64 freq_hz = 1000000000000000ULL / period_fs;
    u64 ticks = freq_hz / hz;
    if (ticks == 0) ticks = 1;

    volatile u64 *regs = (volatile u64 *)g_hpet_info.mmio_base;

    u64 cfg = (1ULL << 1) | (1ULL << 2) | (1ULL << 3);
    regs[HPET_REG_COMP0_CFG / 8] = cfg;
    regs[HPET_REG_COMP0_VAL / 8] = ticks;
    return DEPRESSURE_STATUS_OK;
}

static void hpet_stop_periodic(void) {
    if (!g_hpet_info.enabled || !g_hpet_info.mmio_base) return;
    volatile u64 *regs = (volatile u64 *)g_hpet_info.mmio_base;
    u64 cfg = regs[HPET_REG_COMP0_CFG / 8];
    cfg &= ~(1ULL << 2);
    cfg &= ~(1ULL << 3);
    regs[HPET_REG_COMP0_CFG / 8] = cfg;
}

dstatus_t timer_init_sources(void) {
    g_timer_source = DTIMER_SOURCE_PIT;
    g_timer_hz     = 1000;

    lapic_enable();
    ioapic_init();
    return DEPRESSURE_STATUS_OK;
}

dstatus_t timer_set_hz(u32 hz) {
    if (hz < 10 || hz > 100000) return DEPRESSURE_OUT_OF_RANGE;
    g_timer_hz = hz;
    u32 cur = g_timer_source;
    g_timer_source = 0xFF;
    return timer_set_source(cur);
}

u32 timer_get_hz(void) { return g_timer_hz; }

const char *timer_source_name(u32 source) {
    switch (source) {
        case DTIMER_SOURCE_PIT:   return "PIT";
        case DTIMER_SOURCE_HPET:  return "HPET";
        case DTIMER_SOURCE_LAPIC: return "LAPIC";
        default:                  return "?";
    }
}

dstatus_t timer_set_source(u32 source) {
    if (source >= DTIMER_SOURCE_COUNT) return DEPRESSURE_OUT_OF_RANGE;
    if (source == g_timer_source) return DEPRESSURE_STATUS_OK;

    switch (g_timer_source) {
        case DTIMER_SOURCE_PIT:   pit_stop();           break;
        case DTIMER_SOURCE_HPET:  hpet_stop_periodic(); break;
        case DTIMER_SOURCE_LAPIC: lapic_timer_stop();   break;
    }

    dstatus_t s = DEPRESSURE_STATUS_OK;
    switch (source) {
        case DTIMER_SOURCE_PIT:
            pit_init(g_timer_hz);
            break;

        case DTIMER_SOURCE_HPET:
            if (!ioapic_available()) { s = DEPRESSURE_NOT_SUPPORTED; break; }
            if (!g_hpet_info.enabled) { s = DEPRESSURE_HPET_NOT_INITIALIZED; break; }
            ioapic_route_irq(2, VECTOR_HPET, (u8)g_apic_id, 0);
            s = hpet_start_periodic(g_timer_hz);
            break;

        case DTIMER_SOURCE_LAPIC:
            lapic_timer_start(g_timer_hz);
            break;
    }

    if (DSTATUS_IS_ERR(s)) {
        pit_init(g_timer_hz);
        g_timer_source = DTIMER_SOURCE_PIT;
        return s;
    }
    g_timer_source = source;
    return DEPRESSURE_STATUS_OK;
}

u32 timer_get_source(void) { return g_timer_source; }

void irq_dispatch(u32 irq, interrupt_frame_t *frame) {
    (void)frame;
    if (irq == 0) return;
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static const char *exception_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection",
    "Page Fault", "Reserved", "x87 Floating-Point",
    "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security", "Reserved"
};

static void build_fault_info(interrupt_frame_t *frame, task_fault_info_t *out) {
    out->vector     = frame->vector;
    out->error_code = frame->error_code;
    out->rip        = frame->rip;
    out->cs         = frame->cs;
    out->rflags     = frame->rflags;
    out->rsp        = frame->rsp;
    out->cr2        = 0;
    out->rax = frame->rax;
    out->rbx = frame->rbx;
    out->rcx = frame->rcx;
    out->rdx = frame->rdx;
    out->rsi = frame->rsi;
    out->rdi = frame->rdi;
    out->rbp = frame->rbp;
    out->r8  = frame->r8;
    out->r9  = frame->r9;
    out->r10 = frame->r10;
    out->r11 = frame->r11;
    out->r12 = frame->r12;
    out->r13 = frame->r13;
    out->r14 = frame->r14;
    out->r15 = frame->r15;

    if (frame->vector == 14) {
        u64 cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        out->cr2 = cr2;
    }

    out->task_name  = dkrnl_current_task_name();
    out->task_index = 0;
    out->tick       = dkrnl_get_tick();
}

static void log_exception(interrupt_frame_t *frame) {
    u64 vector = frame->vector;

    panic_puts("\n[kernel] === EXCEPTION ===\n");
    panic_puts("[kernel] name:   ");
    if (vector < 32) panic_puts(exception_names[vector]);
    else panic_puts("Unknown");
    panic_puts("\n");

    panic_puts("[kernel] vector: ");
    panic_put_hex(vector);
    panic_puts("\n");

    panic_puts("[kernel] error:  ");
    panic_put_hex(frame->error_code);
    panic_puts("\n");

    panic_puts("[kernel] task:   ");
    const char *tn = dkrnl_current_task_name();
    panic_puts(tn ? tn : "<none>");
    panic_puts("\n");

    panic_puts("[kernel] tick:   ");
    panic_put_dec(dkrnl_get_tick());
    panic_puts("\n");

    panic_puts("[kernel] rip:    ");
    panic_put_hex(frame->rip);
    panic_puts("\n");

    panic_puts("[kernel] cs:     ");
    panic_put_hex(frame->cs);
    panic_puts("\n");

    panic_puts("[kernel] rflags: ");
    panic_put_hex(frame->rflags);
    panic_puts("\n");

    panic_puts("[kernel] rsp:    ");
    panic_put_hex(frame->rsp);
    panic_puts("\n");

    panic_puts("[kernel] rax:    ");
    panic_put_hex(frame->rax);
    panic_puts("\n");

    panic_puts("[kernel] rbx:    ");
    panic_put_hex(frame->rbx);
    panic_puts("\n");

    panic_puts("[kernel] rcx:    ");
    panic_put_hex(frame->rcx);
    panic_puts("\n");

    panic_puts("[kernel] rdx:    ");
    panic_put_hex(frame->rdx);
    panic_puts("\n");

    panic_puts("[kernel] rsi:    ");
    panic_put_hex(frame->rsi);
    panic_puts("\n");

    panic_puts("[kernel] rdi:    ");
    panic_put_hex(frame->rdi);
    panic_puts("\n");

    panic_puts("[kernel] rbp:    ");
    panic_put_hex(frame->rbp);
    panic_puts("\n");

    if (vector == 14) {
        u64 cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        panic_puts("[kernel] cr2:    ");
        panic_put_hex(cr2);
        panic_puts("\n");
    }
    panic_puts("[kernel] ================\n");
}

static void show_fault_gui_and_kill(interrupt_frame_t *frame) {
    task_fault_info_t info;
    build_fault_info(frame, &info);

    panic_puts("\n[kernel] === EXCEPTION ===\n");
    panic_puts("[kernel] name:   ");
    panic_puts(info.task_name ? info.task_name : "?");
    panic_puts("\n");
    panic_puts("[kernel] vector: ");
    panic_put_hex(info.vector);
    panic_puts("\n");
    panic_puts("[kernel] error:  ");
    panic_put_hex(info.error_code);
    panic_puts("\n");
    panic_puts("[kernel] rip:    ");
    panic_put_hex(info.rip);
    panic_puts("\n");
    panic_puts("[kernel] rsp:    ");
    panic_put_hex(info.rsp);
    panic_puts("\n");
    if (info.vector == 14) {
        panic_puts("[kernel] cr2:    ");
        panic_put_hex(info.cr2);
        panic_puts("\n");
    }
    panic_puts("[kernel] ================\n");

    extern dkernel_api_t *dkrnl_get_api(void);
    dkernel_api_t *api = dkrnl_get_api();
    if (api && api->fault_show) {
        api->fault_show(&info);
    }

    for (volatile u64 w = 0; w < 50000000; w++) { }

    panic_puts("[kernel] killing task\n");
    dkrnl_kill_current_task();
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}

u64 interrupt_dispatch(interrupt_frame_t *frame) {
    u64 vector = frame->vector;

    if (vector == IRQ_TIMER_PIT) {
        u64 new_rsp = dkrnl_timer_tick(frame);
        outb(0x20, 0x20);
        return new_rsp;
    }

    if (vector == VECTOR_HPET) {
        u64 new_rsp = dkrnl_timer_tick(frame);
        lapic_write(0xB0, 0);
        return new_rsp;
    }

    if (vector == VECTOR_LAPIC) {
        u64 new_rsp = dkrnl_timer_tick(frame);
        lapic_write(0xB0, 0);
        return new_rsp;
    }

    if (vector >= IRQ_BASE && vector < IRQ_BASE + 16) {
        irq_dispatch((u32)(vector - IRQ_BASE), frame);
        return (u64)frame;
    }

    if (vector < 32) {
        show_fault_gui_and_kill(frame);
    }

    return (u64)frame;
}