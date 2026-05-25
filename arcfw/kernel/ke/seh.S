@ KiSehSetjmp / KiSehLongjmp - the non-local jump primitive under the ARM32 SEH
@ frame chain (kiseh.h). A minimal, kernel-owned setjmp/longjmp: save and restore
@ the AAPCS callee-saved registers plus sp and lr. Mode-agnostic - the caller is
@ responsible for being in the correct processor mode (SVC) before KiSehLongjmp,
@ which ke/trap.S arranges (cps #0x13) before resuming a faulting frame.

.syntax unified
.arm
.text

@ int KiSehSetjmp(KSEH_JMPBUF *Buf)  -- Buf[0..7]=r4-r11, Buf[8]=sp, Buf[9]=lr.
@ Returns 0 on the direct (setjmp) call; KiSehLongjmp makes it appear to return
@ non-zero. Declared returns_twice in kiseh.h so the compiler treats it like setjmp.
.global KiSehSetjmp
KiSehSetjmp:
    stmia   r0!, {r4-r11}       @ callee-saved, still the caller's values
    str     sp, [r0], #4        @ caller's stack pointer (no push performed here)
    str     lr, [r0]            @ return address
    mov     r0, #0
    bx      lr

@ void KiSehLongjmp(KSEH_JMPBUF *Buf, int Value) -- resume at the matching
@ KiSehSetjmp call site, which returns Value (forced to 1 if Value==0).
.global KiSehLongjmp
KiSehLongjmp:
    ldmia   r0!, {r4-r11}
    ldr     sp, [r0], #4
    ldr     lr, [r0]
    movs    r0, r1
    moveq   r0, #1
    bx      lr

.section .note.GNU-stack,"",%progbits
