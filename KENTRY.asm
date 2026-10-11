BITS 64
DEFAULT REL

section .text
global _start
extern kmain

_start:
    cli
    mov  rsp, stack_top
    and  rsp, -16
    xor  rbp, rbp

    mov  r11, 0x300000
    mov  rax, 0xBBBB0001
    mov  [r11], rax

    jmp  kmain

.hang:
    hlt
    jmp .hang

section .stack nobits alloc noexec write align=16
global stack_bottom
global stack_top
stack_bottom:
    resb 0x100000
stack_top: