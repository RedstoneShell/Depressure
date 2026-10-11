BITS 64
DEFAULT REL

section .text

extern interrupt_dispatch

%macro ISR_STUB 1
isr_%1:
%if (%1 = 8) || (%1 = 10) || (%1 = 11) || (%1 = 12) || (%1 = 13) || \
    (%1 = 14) || (%1 = 17) || (%1 = 21) || (%1 = 29) || (%1 = 30)
    push qword 0
    mov dword [rsp], %1
%else
    push qword 0
    push qword 0
    mov dword [rsp], %1
%endif
    jmp interrupt_common
%endmacro

%assign vector 0
%rep 256
    ISR_STUB vector
%assign vector vector + 1
%endrep

interrupt_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    mov rbx, rsp
    and rsp, -16
    call interrupt_dispatch
    mov rsp, rax
    mov [0x300110], rsp

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

gdt_reload:
    lgdt [rdi]
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov ax, cx
    ltr ax

    push rsi
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ret

section .rodata align=8
global interrupt_stub_table
interrupt_stub_table:
%assign vector 0
%rep 256
    dq isr_%+vector
%assign vector vector + 1
%endrep

section .text
global gdt_reload