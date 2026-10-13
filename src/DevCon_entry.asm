; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text.entry
global _devcon_start
extern devcon_main

_devcon_start:
    ; RDI = dkernel_api_t *
    ; RSI = fb_info_t *
    ; RDX = uart_api_t *
    ; RCX = dws_api_t *
    ; R8  = dkbd_api_t *
    ; R9  = boot_mode (int)
    jmp devcon_main