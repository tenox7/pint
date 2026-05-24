/* Link script for the stand-in NT kernel (kernel.c).
 *
 * Linked at ImageBase + SectionRVA = 0x01000000 + 0x1000 = 0x01001000. peldr.c
 * maps the single PE section at section RVA 0x1000 above the allocated image base
 * (which equals the PE ImageBase 0x01000000, so no relocation - see peldr.c:409),
 * i.e. at 0x01001000. Linking here makes the kernel's absolute references resolve
 * to where they are actually loaded. mkpe.py derives AddressOfEntryPoint from the
 * ELF entry (= 0x01001000 - ImageBase = 0x1000).
 *
 * 16 MiB clears the loader image + embedded ramdisk blob (QEMU build, image ~7 MiB
 * and growing) and the firmware-staged ramdisk (real-HW build, ends ~14 MiB), so
 * BlAllocateDescriptor places the image at exactly this base in both builds.
 *
 * .text.entry (start.S _KiSystemStartup) is first so the entry is at the section
 * start (RVA 0x1000). The image must be .bss-free: peldr loads no zero-fill region
 * for us (mkpe sets VirtualSize == SizeOfRawData), and make-kernel.sh asserts .bss
 * is empty. The ported HAL display code (jxdisp.c) has ~14 zero-initialized globals,
 * so rather than force each into .data with a dummy initializer, .bss and COMMON are
 * folded INTO the .data output section here: placing NOBITS input sections inside a
 * PROGBITS output section makes ld emit them as zero bytes in the file image, so they
 * load correctly, `size` reports a zero .bss, and the assert + mkpe stay happy.
 */
ENTRY(_KiSystemStartup)

SECTIONS
{
    . = 0x01001000;

    .text   : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : {
        *(.data*)
        . = ALIGN(8);
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
