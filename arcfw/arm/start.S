//
// ARM reset stub (replaces BOOT/STARTUP/I386/SU.ASM). Linked first so the entry is
// at the image base (0x8000 = the Raspberry Pi firmware's native 32-bit kernel load
// address; QEMU loads the ELF at the same LMA).
//
// Three things must happen here, in this order, before any C runs:
//   1. Park secondary cores. All four Cortex-A7 cores enter here. The loader is
//      single-core, so cores 1-3 are sent to a WFI loop; only core 0 continues.
//      (Booting the raw image via QEMU's raw -kernel path parks them in a QEMU stub,
//      but the ELF/real-hardware paths do not - without this they run the loader
//      concurrently and fault.)
//   2. Drop HYP -> SVC if needed. Real Pi 2 firmware enters a 32-bit kernel in HYP
//      mode, QEMU in SVC. This must precede setting SP, which is mode-banked.
//   3. Set SP, invalidate I-cache/branch predictor (defensive; MMU/caches stay off),
//      zero BSS, call cmain.
//

.section .text.start
.global _start
_start:
    // --- 1. Park secondary cores (only core 0, MPIDR Aff0 == 0, continues) ---
    mrc     p15, 0, r0, c0, c0, 5       // read MPIDR
    and     r0, r0, #3
    cmp     r0, #0
    beq     2f
1:  wfi                                 // secondary cores: halt forever
    b       1b

    // --- 2. Drop HYP -> SVC if needed (no-op when firmware already entered in SVC) ---
2:  mrs     r0, cpsr
    and     r1, r0, #0x1f
    cmp     r1, #0x1a                   // 0x1a = HYP mode
    bne     3f
    bic     r0, r0, #0x1f
    orr     r0, r0, #0x13               // SVC mode
    orr     r0, r0, #0xc0               // mask IRQ + FIQ
    msr     spsr_hyp, r0
    adr     r1, 3f
    msr     elr_hyp, r1
    eret                                // exception-return into SVC at label 3
3:  cps     #0x13                       // ensure SVC (no-op if we just switched)

    // --- Allow unaligned accesses (clear SCTLR.A) ---
    // The ported NT filesystem code reads packed on-disk structures (FAT BPB,
    // dirents) through `UNALIGNED` casts that GCC compiles to plain LDR/LDRH at
    // unaligned addresses. NT's UNALIGNED (__unaligned on MSVC) is empty for GCC, so
    // those accesses must be legal at the hardware level. ARMv7 handles unaligned
    // LDR/LDRH/STR/STRH natively when SCTLR.A is clear (as Linux/U-Boot run); the
    // firmware may leave it set, so clear it here before any C runs. (LDRD/STM still
    // require alignment regardless - those stay the compiler's concern.)
    mrc     p15, 0, r0, c1, c0, 0       // read SCTLR
    bic     r0, r0, #(1 << 1)           // clear A (alignment-check enable)
    mcr     p15, 0, r0, c1, c0, 0       // write SCTLR
    isb

    // --- 3. Stack (now SVC-banked), cache/BP invalidate, BSS, cmain ---
    ldr     sp, =_stack_top

    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0       // ICIALLU - invalidate entire I-cache
    mcr     p15, 0, r0, c7, c5, 6       // BPIALL  - invalidate branch predictor
    dsb
    isb

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
4:  cmp     r0, r1
    bhs     5f
    str     r2, [r0], #4
    b       4b

5:  bl      cmain
6:  wfi
    b       6b

.section .note.GNU-stack,"",%progbits
