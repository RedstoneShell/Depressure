; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text.entry
global _de1000_start
extern de1000_main

_de1000_start:
    jmp de1000_main