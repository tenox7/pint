;++
;
; Module Name:
;
;    setjmpex.asm
;
; Abstract:
;
;    This module implements the ARM specific routine to provide a setjmp that
;    is safe with respect to structured exception handling (the ARM port of
;    CRTW32\MISC\MIPS\SETJMPEX.S).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  RtlLookupFunctionEntry
        IMPORT  RtlVirtualUnwind

;
; Define the variable that makes setjmp and longjmp safe with respect to
; structured exception handling.
;

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  _setjmpexused
_setjmpexused
        DCD     _setjmpex

;
; The frame of _setjmpex: the in memory arguments of RtlVirtualUnwind, its
; outputs, and a context record of the caller of setjmp.
;

#define SjImageBase 12
#define SjHandlerData 16
#define SjEstablisherFrame 20
#define SjContext 24
#define SjFrameLength (24 + ContextFrameLength)

        TEXTAREA

;++
;
; int
; _setjmpex (
;    IN jmp_buf JumpBuffer
;    )
;
; Routine Description:
;
;    This function saves the current nonvolatile register state in the
;    specified jump buffer, with the establisher frame of its caller, which
;    longjmp unwinds to, and returns a function value of zero.
;
;--

        NESTED_ENTRY _setjmpex
        PROLOG_PUSH {r4-r6, lr}
        PROLOG_STACK_ALLOC SjFrameLength

;
; Save the nonvolatile state in both the jump buffer and a context record of
; the caller, which is used to compute its establisher frame.
;

        mov     r6, r0                          ; jump buffer
        add     r0, sp, #SjFrameLength
        ldm     r0, {r0-r2}                     ; r4 - r6 of the caller
        str     r0, [r6, #JbR4]
        str     r1, [r6, #JbR5]
        str     r2, [r6, #JbR6]
        str     r7, [r6, #JbR7]
        str     r8, [r6, #JbR8]
        str     r9, [r6, #JbR9]
        str     r10, [r6, #JbR10]
        str     r11, [r6, #JbR11]
        add     r3, sp, #SjFrameLength + 16     ; stack pointer of the caller
        str     r3, [r6, #JbSp]
        str     lr, [r6, #JbPc]                 ; setjmp return address
        vmrs    r3, fpscr
        str     r3, [r6, #JbFpscr]
        add     r3, r6, #JbD
        vstm    r3, {d8-d15}
        add     r5, sp, #SjContext
        add     r3, r5, #CxR4
        stm     r3, {r0-r2}
        add     r3, r5, #CxR7
        stm     r3, {r7-r11}
        add     r3, sp, #SjFrameLength + 16
        str     r3, [r5, #CxSp]
        str     lr, [r5, #CxLr]
        bic     r3, lr, #1
        str     r3, [r5, #CxPc]
        add     r3, r5, #CxD + 8 * 8
        vstm    r3, {d8-d15}
        ldr     r3, =CONTEXT_FULL | CONTEXT_UNWOUND_TO_CALL
        str     r3, [r5, #CxContextFlags]

;
; Virtually unwind the caller to its establisher frame.
;

        bic     r4, lr, #1
        subs    r4, r4, #2                      ; control PC, within the call
        mov     r0, r4
        add     r1, sp, #SjImageBase
        bl      RtlLookupFunctionEntry
        mov     r2, r0                          ; function entry
        ldr     r0, [sp, #SjImageBase]
        mov     r1, r4
        mov     r3, r5                          ; context record
        add     r12, sp, #SjHandlerData
        str     r12, [sp]
        add     r12, sp, #SjEstablisherFrame
        str     r12, [sp, #4]
        movs    r12, #0
        str     r12, [sp, #8]
        bl      RtlVirtualUnwind
        ldr     r0, [sp, #SjEstablisherFrame]
        str     r0, [r6, #JbFrame]
        movs    r0, #0
        EPILOG_STACK_FREE SjFrameLength
        EPILOG_POP {r4-r6, pc}
        NESTED_END _setjmpex

        END
