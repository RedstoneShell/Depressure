BITS 64
DEFAULT REL

section .text.entry
global _pci_start
extern pci_main

_pci_start:
    jmp pci_main