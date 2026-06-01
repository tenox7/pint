@ KiSystemStartup - the kernel entry the OS Loader transfers control to, and the
@ high-half bring-up head.
@
@ The loader PE-loads NTOSKRNL.EXE and enters here with r0 = LOADER_PARAMETER_BLOCK
@ (the KE/MIPS X4START.S contract). The kernel is LINKED in KSEG0 (ImageBase
@ 0x81000000 = 0x80000000 | physical-load), but peldr loads it at physical
@ 0x01000000 (it masks ImageBase & 0x1fffffff) and enters here at the physical
@ address with the MMU off. So this head runs POSITION-INDEPENDENT - PC-relative
@ literal loads and bl only - until it builds the boot page table, enables the MMU
@ (which maps KSEG0 0x80000000 -> physical 0), and branches to the KSEG0 virtual
@ address. From KiArmHighHalf on, the kernel executes at its linked high-half VA.

.section .text.entry, "ax"
.arm
.global KiSystemStartup
KiSystemStartup:
    ldr     sp, [r0, #24]            @ sp = LoaderBlock->KernelStack (low, identity-mapped)
    mov     r11, r0                  @ preserve LoaderBlock across the C calls (r11 callee-saved)

    ldr     r4, =MiArmL1             @ KSEG0 (high) link address of the boot L1 table
    ldr     r5, =0x80000000          @ KSEG0 base
    sub     r0, r4, r5               @ r0 = physical address of the L1 table
    mov     r10, r0                  @ preserve physical L1
    bl      MiArmBuildBootPageTable  @ fill identity + KSEG0 + device map (PIC, MMU off)

    mov     r0, r10                  @ r0 = physical L1
    bl      MiArmTurnOnMmu           @ TTBR0 = L1, DACR, SCTLR.M = 1  -> MMU on

    ldr     pc, =KiArmHighHalf       @ branch to the KSEG0 virtual address (now mapped)
KiArmHighHalf:
    mov     r0, r11                  @ r0 = LoaderBlock
    bl      KiArmInitialize          @ executing in KSEG0 now; does not return
1:  wfi
    b       1b
    .ltorg

.section .note.GNU-stack,"",%progbits
