BITS 64
DEFAULT REL

section .text

global ap_worker_asm
extern d_ap_worker_c

ap_worker_asm:
    jmp d_ap_worker_c