BITS 64
DEFAULT REL

section .text.entry
global _dkbd_start
extern dkbd_main

_dkbd_start:
    ; RDI = dkernel_api_t *
    ; RSI = fb_info_t *
    jmp dkbd_main