BITS 64
DEFAULT REL

section .text.entry
global _dws_start
extern dws_main

_dws_start:
    ; RDI = dkernel_api_t *
    ; RSI = fb_info_t *
    call dws_main
    ret