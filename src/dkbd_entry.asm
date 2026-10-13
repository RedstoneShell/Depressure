; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text.entry
global _dkbd_start
extern dkbd_main

_dkbd_start:
    ; RDI = dkernel_api_t *
    ; RSI = fb_info_t *
    jmp dkbd_main