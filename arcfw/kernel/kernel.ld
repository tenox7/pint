/* Link script for the NT kernel (kernel.c).
 *
 * Linked in the high half: ImageBase + SectionRVA = 0x81000000 + 0x1000 =
 * 0x81001000, where 0x81000000 = KSEG0 (0x80000000) | physical-load (0x01000000).
 * peldr.c masks the ImageBase (& 0x1fffffff) to a physical page and loads the PE
 * section at physical 0x01001000; once ke/armstart.S enables the MMU, KSEG0 maps
 * 0x80000000 -> physical 0, so the kernel's absolute references (all 0x81xxxxxx)
 * resolve to the loaded image. The armstart head runs position-independent until
 * that switch. mkpe.py derives AddressOfEntryPoint from the ELF entry
 * (= 0x81001000 - ImageBase 0x81000000 = 0x1000).
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
    . = 0x81001000;

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
