; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text

global ap_worker_asm
extern d_ap_worker_c

ap_worker_asm:
    jmp d_ap_worker_c