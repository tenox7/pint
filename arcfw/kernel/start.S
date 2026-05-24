@ Entry stub for the stand-in NT kernel (the "tiny bit of a kernel" - kernel.c).
@
@ The loader PE-loads this image (BlLoadImage / peldr.c) and transfers control
@ with r0 = LOADER_PARAMETER_BLOCK, exactly as osloader.c:792 does for ntoskrnl
@ on MIPS/Alpha. This mirrors KE/MIPS/X4START.S KiSystemStartup: load the idle
@ thread's stack from LoaderBlock->KernelStack (offset 24: after three 8-byte
@ LIST_ENTRYs), keep r0, then call C.
@
@ Linked at ImageBase(0x01000000)+SectionRVA(0x1000) = 0x01001000 so the bytes
@ peldr maps at section RVA 0x1000 match the link address. .text.entry is placed
@ first by kernel.ld, so AddressOfEntryPoint = 0x1000.

.section .text.entry, "ax"
.arm
.global _KiSystemStartup
_KiSystemStartup:
    @ Note on alignment: the HAL display code (jxdisp.c) reads the OEM font header's
    @ USHORT fields with LDRH. We do NOT clear SCTLR.A here on purpose - it would be a
    @ no-op. With the MMU off (as the kernel runs), memory is Strongly-ordered and
    @ unaligned LDRH/LDR fault REGARDLESS of SCTLR.A; the A bit only relaxes Normal
    @ memory. The fix is to keep those reads aligned: the loader embeds the font blob
    @ 16-byte aligned (build/Makefile --set-section-alignment) so every field the HAL
    @ reads lands >=2-byte aligned. See CLAUDE.md "MMU-off unaligned" gotcha.
    ldr     sp, [r0, #24]            @ sp = LoaderBlock->KernelStack (top; grows down)
    bl      KiSystemStartupC         @ r0 still = LoaderBlock (ldr left it intact)
1:  wfi                              @ KiSystemStartupC does not return
    b       1b

.section .note.GNU-stack,"",%progbits
