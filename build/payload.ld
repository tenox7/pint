/* Link script for the handoff-test payload (the serial shell).
 *
 * Linked at a FIXED base because the C shell is not position-independent (it
 * references its own strings/data absolutely). The loader copies the blob to
 * exactly this address and jumps to its first byte, so:
 *
 *   0x00400000 MUST equal PAYLOAD_LOAD_BASE in arcfw/arm/init.c.
 *
 * 4 MiB is clear of the loader (0x10000, <1 MiB) and the loader heap (~0x37FE8000).
 * .text.entry (start.S _pstart) is placed first so the entry is at offset 0 of
 * the raw .bin. BSS + a 16 KiB stack follow; start.S zeroes BSS and sets sp to
 * _pstack_top. BSS/stack are NOBITS - not in the .bin - so the loader only copies
 * text+rodata+data.
 */
ENTRY(_pstart)

SECTIONS
{
    . = 0x00400000;

    .text   : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    . = ALIGN(8);
    _pbss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(8);
    _pbss_end = .;

    . = . + 0x4000;
    . = ALIGN(8);
    _pstack_top = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
