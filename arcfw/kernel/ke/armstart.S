@ KiSystemStartup - the kernel entry the OS Loader transfers control to.
@
@ The loader PE-loads NTOSKRNL.EXE and enters here with r0 = LOADER_PARAMETER_BLOCK
@ (the KE/MIPS X4START.S contract). Set the idle stack pointer from the loader
@ block (KernelStack, offset 24 = after three 8-byte LIST_ENTRYs) and call the C
@ startup, which establishes the PCR (TPIDRPRW) and enters KiInitializeKernel.

.section .text.entry, "ax"
.arm
.global KiSystemStartup
KiSystemStartup:
    ldr     sp, [r0, #24]            @ sp = LoaderBlock->KernelStack (top; grows down)
    bl      KiArmInitialize          @ r0 still = LoaderBlock; does not return
1:  wfi
    b       1b

.section .note.GNU-stack,"",%progbits
