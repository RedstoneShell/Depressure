BITS 64
DEFAULT REL

section .text.entry
global _uart_start
extern uart_main

_uart_start:
    ; RDI = dkernel_api_t *
    ; RSI = fb_info_t *
    jmp uart_main