ENTRY(_start)

SECTIONS
{
    . = 0x00008000;

    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }

    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;

    . = . + 0x10000;
    . = ALIGN(8);
    _stack_top = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
