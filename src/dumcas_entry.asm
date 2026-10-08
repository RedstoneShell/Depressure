BITS 64
DEFAULT REL

section .text.entry
global _dumcas_start
extern dumcas_main

_dumcas_start:
    jmp dumcas_main