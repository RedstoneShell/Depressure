BITS 64
DEFAULT REL

section .text.entry
global _de1000_start
extern de1000_main

_de1000_start:
    jmp de1000_main