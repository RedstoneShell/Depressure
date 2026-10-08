#include "interrupts.h"
#include "dtypes.h"
#include "dkrnl.h"

#define IDT_ENTRIES 256
#define GDT_ENTRIES 256

#define IRQ_BASE 32
#define IRQ_COUNT 16
#define IRQ_TIMER 0

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_FREQ  1193182u

typedef struct __attribute__((packed)) table_register {
    u16 limit;
    u64 base;
} table_register_t;

typedef struct __attribute__((packed)) idt_gate {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 attributes;
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
    u8 base_middle;
    u8 access;
    u8 limit_flags;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
} tss_descriptor_t;

static u64 g_gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static idt_gate_t g_idt[IDT_ENTRIES] __attribute__((aligned(16)));
static task_state_segment_t g_tss __attribute__((aligned(16)));
static u8 g_double_fault_stack[0x4000] __attribute__((aligned(16)));
static table_register_t g_gdtr;
static table_register_t g_idtr;
static u16 g_kernel_code_selector;
static u16 g_kernel_data_selector;
static u32 g_double_fault_ist;

extern u8 stack_top[];
extern void gdt_reload(const table_register_t *gdtr, u16 code_selector,
                       u16 data_selector, u16 tss_selector);
extern void (*interrupt_stub_table[IDT_ENTRIES])(void);

static void copy_bytes(void *destination, const void *source, u64 size) {
    u8 *dst = (u8 *)destination;
    const u8 *src = (const u8 *)source;
    for (u64 i = 0; i < size; i++) dst[i] = src[i];
}

static void panic_putc(char value) {
    u8 status;
    do {
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((u16)0x3FD));
    } while ((status & 0x20) == 0);
    __asm__ volatile ("outb %0, %1" : : "a"((u8)value), "Nd"((u16)0x3F8));
}

static void panic_puts(const char *text) {
    while (*text) {
        if (*text == '\n') panic_putc('\r');
        panic_putc(*text++);
    }
}

static void panic_put_hex(u64 value) {
    static const char hex[] = "0123456789ABCDEF";
    panic_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        panic_putc(hex[(value >> shift) & 0xF]);
    }
}

static void panic_put_dec(u64 value) {
    char buf[24];
    int i = 0;
    if (value == 0) { panic_putc('0'); return; }
    while (value > 0) { buf[i++] = '0' + (value % 10); value /= 10; }
    while (i > 0) panic_putc(buf[--i]);
}

static void set_idt_gate(u32 vector, void (*handler)(void), u8 ist) {
    u64 address = (u64)handler;
    idt_gate_t *gate = &g_idt[vector];

    gate->offset_low = (u16)address;
    gate->selector = g_kernel_code_selector;
    gate->ist = ist & 0x7;
    gate->attributes = 0x8E;
    gate->offset_middle = (u16)(address >> 16);
    gate->offset_high = (u32)(address >> 32);
    gate->reserved = 0;
}

static int copy_current_tss(const table_register_t *old_gdtr,
                            u64 old_gdt_bytes) {
    u16 selector;
    u64 index;
    u64 descriptor;
    u64 descriptor_high;
    u64 base;
    u32 limit;
    u8 access;
    const task_state_segment_t *old_tss;

    __asm__ volatile ("str %0" : "=r"(selector));
    if ((selector & 0x4) != 0) return 0;

    index = selector >> 3;
    if (index * sizeof(u64) + 2 * sizeof(u64) > old_gdt_bytes) return 0;

    descriptor = *(const u64 *)(old_gdtr->base + index * sizeof(u64));
    descriptor_high =
        *(const u64 *)(old_gdtr->base + (index + 1) * sizeof(u64));
    access = (u8)(descriptor >> 40);
    limit = (u32)(descriptor & 0xFFFF) |
            (u32)((descriptor >> 48) & 0x0F) << 16;
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
    u64 old_entries = (old_bytes + sizeof(u64) - 1) / sizeof(u64);
    u64 code_index;
    u64 data_index;
    u64 tss_index;
    u64 tss_base = (u64)&g_tss;
    tss_descriptor_t *tss_descriptor;

    if (old_bytes > sizeof(g_gdt) - 4 * sizeof(u64)) return 0;
    copy_bytes(g_gdt, (const void *)old_gdtr->base, old_bytes);

    code_index = old_entries;
    data_index = code_index + 1;
    tss_index = data_index + 1;
    if (tss_index + 2 > GDT_ENTRIES) return 0;

    g_gdt[code_index] = 0x00AF9A000000FFFFULL;
    g_gdt[data_index] = 0x00CF92000000FFFFULL;
    tss_descriptor = (tss_descriptor_t *)&g_gdt[tss_index];
    tss_descriptor->limit_low = sizeof(g_tss) - 1;
    tss_descriptor->base_low = (u16)tss_base;
    tss_descriptor->base_middle = (u8)(tss_base >> 16);
    tss_descriptor->access = 0x89;
    tss_descriptor->limit_flags = 0;
    tss_descriptor->base_high = (u8)(tss_base >> 24);
    tss_descriptor->base_upper = (u32)(tss_base >> 32);
    tss_descriptor->reserved = 0;

    g_gdtr.base = (u64)g_gdt;
    g_gdtr.limit = (u16)((tss_index + 2) * sizeof(u64) - 1);
    g_kernel_code_selector = (u16)(code_index * sizeof(u64));
    g_kernel_data_selector = (u16)(data_index * sizeof(u64));

    gdt_reload(&g_gdtr, g_kernel_code_selector,
               g_kernel_data_selector,
               (u16)(tss_index * sizeof(u64)));
    return 1;
}

int interrupts_init(void) {
    table_register_t old_gdtr;
    table_register_t old_idtr;
    u32 used_ist = 0;
    u32 ist_index = 0;
    u64 idt_bytes;

    __asm__ volatile ("sgdt %0" : "=m"(old_gdtr));
    __asm__ volatile ("sidt %0" : "=m"(old_idtr));

    if (old_gdtr.base == 0 || old_gdtr.limit == 0 ||
        old_idtr.base == 0 || old_idtr.limit < 31 * sizeof(idt_gate_t) +
                                                   sizeof(idt_gate_t)) return 0;
    idt_bytes = (u64)old_idtr.limit + 1;
    if (idt_bytes > sizeof(g_idt)) return 0;

    copy_bytes(g_idt, (const void *)old_idtr.base, idt_bytes);

    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        if ((u64)(i + 1) * sizeof(idt_gate_t) > idt_bytes) break;
        used_ist |= 1u << (g_idt[i].ist & 0x7);
    }
    used_ist &= ~1u;

    for (u32 i = 1; i <= 7; i++) {
        if ((used_ist & (1u << i)) == 0) {
            ist_index = i;
            break;
        }
    }

    if (used_ist != 0) {
        if (!copy_current_tss(&old_gdtr, (u64)old_gdtr.limit + 1)) return 0;
    } else {
        g_tss.rsp0 = (u64)stack_top;
        g_tss.iomap_base = sizeof(g_tss);
    }

    if (ist_index != 0) {
        g_tss.ist[ist_index - 1] =
            (u64)(g_double_fault_stack + sizeof(g_double_fault_stack));
        g_double_fault_ist = ist_index;
    }

    if (!install_gdt(&old_gdtr)) return 0;

    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        u8 ist = 0;
        if (i == 8 && g_double_fault_ist != 0) {
            ist = (u8)g_double_fault_ist;
        } else if (i == 2) {
        }
        set_idt_gate(i, interrupt_stub_table[i], ist);
    }

    g_idtr.base = (u64)g_idt;
    g_idtr.limit = (u16)(sizeof(g_idt) - 1);
    __asm__ volatile ("lidt %0" : : "m"(g_idtr) : "memory");

    return 1;
}

u16 interrupts_kernel_cs(void) { return g_kernel_code_selector; }
u16 interrupts_kernel_ds(void) { return g_kernel_data_selector; }
u16 interrupts_kernel_ss(void) { return g_kernel_data_selector; }

void pit_init(u32 hz) {
    /* Remap PIC: master -> 0x20..0x27, slave -> 0x28..0x2F */
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x11), "Nd"((u16)0x20));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x11), "Nd"((u16)0xA0));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x20), "Nd"((u16)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x28), "Nd"((u16)0xA1));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x04), "Nd"((u16)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x02), "Nd"((u16)0xA1));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x01), "Nd"((u16)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x01), "Nd"((u16)0xA1));

    /* Unmask only IRQ0 */
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0xFE), "Nd"((u16)0x21));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0xFF), "Nd"((u16)0xA1));

    if (hz == 0) hz = 100;
    u32 divisor = PIT_FREQ / hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x36), "Nd"((u16)PIT_CMD));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)(divisor & 0xFF)),
                                       "Nd"((u16)PIT_CH0));
    __asm__ volatile ("outb %0, %1" : : "a"((u8)((divisor >> 8) & 0xFF)),
                                       "Nd"((u16)PIT_CH0));
}

void irq_dispatch(u32 irq, interrupt_frame_t *frame) {
    (void)frame;

    if (irq == IRQ_TIMER) return;

    /* Slave PIC EOI */
    if (irq >= 8) {
        __asm__ volatile ("outb %0, %1" : : "a"((u8)0x20), "Nd"((u16)0xA0));
    }
    /* Master PIC EOI */
    __asm__ volatile ("outb %0, %1" : : "a"((u8)0x20), "Nd"((u16)0x20));
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

static void dump_exception(interrupt_frame_t *frame) {
    u64 vector = frame->vector;

    *(volatile u64 *)0x300000 = 0xE000000000000000ULL | (vector & 0xFF);

    panic_puts("\n[kernel] exception ");
    if (vector < 32) panic_puts(exception_names[vector]);
    else panic_puts("Unknown");
    panic_puts(" vector=");
    panic_put_hex(vector);
    panic_puts(" error=");
    panic_put_hex(frame->error_code);
    panic_puts("\n  rip=");
    panic_put_hex(frame->rip);
    panic_puts(" cs=");
    panic_put_hex(frame->cs);
    panic_puts(" rflags=");
    panic_put_hex(frame->rflags);
    panic_puts("\n  rax=");
    panic_put_hex(frame->rax);
    panic_puts(" rbx=");
    panic_put_hex(frame->rbx);
    panic_puts(" rcx=");
    panic_put_hex(frame->rcx);
    panic_puts(" rdx=");
    panic_put_hex(frame->rdx);
    panic_puts("\n  rsi=");
    panic_put_hex(frame->rsi);
    panic_puts(" rdi=");
    panic_put_hex(frame->rdi);
    panic_puts(" rbp=");
    panic_put_hex(frame->rbp);
    panic_puts(" rsp=");
    panic_put_hex((u64)frame);
    panic_puts(" rsp_before_iretq=");
    panic_put_hex(*(volatile u64 *)0x300110);
    if (vector == 14) {
        u64 cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        panic_puts("\n  cr2=");
        panic_put_hex(cr2);
    }
    panic_puts("\n");
}

u64 interrupt_dispatch(interrupt_frame_t *frame) {
    u64 vector = frame->vector;

    if (vector >= IRQ_BASE && vector < IRQ_BASE + IRQ_COUNT) {
        u32 irq = (u32)(vector - IRQ_BASE);
        if (irq == IRQ_TIMER) {
            extern u64 dkrnl_timer_tick(interrupt_frame_t *frame);
            u64 new_rsp = dkrnl_timer_tick(frame);
            __asm__ volatile ("outb %0, %1" : : "a"((u8)0x20), "Nd"((u16)0x20));
            return new_rsp;
        }
        irq_dispatch(irq, frame);
        return (u64)frame;
    }

    if (vector < 32) {
        dump_exception(frame);
        for (;;) __asm__ volatile ("cli; hlt");
    }

    if (vector >= IRQ_BASE + IRQ_COUNT && vector < IRQ_BASE + IRQ_COUNT + 16) {
        if (vector == IRQ_BASE + 7) {
        } else if (vector == IRQ_BASE + 15) {
            __asm__ volatile ("outb %0, %1" : : "a"((u8)0x20), "Nd"((u16)0x20));
        }
    }

    return (u64)frame;
}