ENTRY(_dumcas_start)

SECTIONS
{
    . = 0x400000 + SIZEOF_HEADERS;

    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}