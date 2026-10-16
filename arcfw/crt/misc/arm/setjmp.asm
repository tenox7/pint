;++
;
; Module Name:
;
;    setjmp.asm
;
; Abstract:
;
;    This module implements the ARM specific routine to perform a setjmp (the
;    ARM port of CRT32\MISC\MIPS\SETJMP.S).
;
;    N.B. This module has a version that is safe with respect to structured
;       exception handling (setjmpex.asm): when it is linked, setjmp records
;       the establisher frame of its caller, and longjmp unwinds to it.
;
;--

#include "ksarm.h"
#include "kxarm.h"

;
; The address of the safe setjmp routine, if it is linked.
;

        COMMON  _setjmpexused, 4

        TEXTAREA

;++
;
; int
; setjmp (
;    IN jmp_buf JumpBuffer
;    )
;
; Routine Description:
;
;    This function saves the current nonvolatile register state in the
;    specified jump buffer and returns a function value of zero.
;
;--

        LEAF_ENTRY setjmp
        ldr     r12, =_setjmpexused
        ldr     r12, [r12]
        cmp     r12, #0
        bne     %F10                            ; provide a safe setjmp

;
; Provide unsafe handling of setjmp.
;

        movs    r1, #0
        str     r1, [r0, #JbFrame]              ; no frame to unwind
        add     r1, r0, #JbR4
        stm     r1, {r4-r11}                    ; save integer registers r4 - r11
        str     sp, [r0, #JbSp]                 ; save stack pointer
        str     lr, [r0, #JbPc]                 ; save setjmp return address
        vmrs    r1, fpscr
        str     r1, [r0, #JbFpscr]              ; save floating status
        add     r1, r0, #JbD
        vstm    r1, {d8-d15}                    ; save floating registers d8 - d15
        movs    r0, #0
        bx      lr

;
; Provide safe handling of setjmp.
;

10      bx      r12
        LEAF_END setjmp

        END
