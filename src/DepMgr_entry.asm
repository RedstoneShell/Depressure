; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text.entry
global _depmgr_start
extern depmgr_main

_depmgr_start:
    ; RDI = api
    ; RSI = fb_info
    ; RDX = uart
    ; RCX = dws
    ; R8  = kbd
    ; R9  = mouse
    ; [RSP+8]  = boot_mode
    ; [RSP+16] = ncpu
    jmp depmgr_main