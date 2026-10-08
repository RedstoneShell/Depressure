BITS 64
DEFAULT REL

section .text.entry
global _dmouse_start
extern dmouse_main

_dmouse_start:
    ; RDI = dkernel_api_t *
    ; RSI = fb_info_t *
    jmp dmouse_main