; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (c) 2026 RedstoneShell
;
; This file is part of the Depressure Operating System.

BITS 64
DEFAULT REL

section .text.entry
global _pci_start
extern pci_main

_pci_start:
    jmp pci_main