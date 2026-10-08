BITS 64
DEFAULT REL

section .text.entry
global _acpi_start
extern acpi_main

_acpi_start:
    jmp acpi_main
