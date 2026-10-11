BITS 64
DEFAULT REL

section .text.entry
global _dsimd_start
extern dsimd_main

_dsimd_start:
    jmp dsimd_main