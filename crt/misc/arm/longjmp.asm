;++
;
; Module Name:
;
;    longjmp.asm
;
; Abstract:
;
;    This module implements the ARM specific routine to perform a long jump
;    (the ARM port of CRT32\MISC\MIPS\LONGJMP.S).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  RtlUnwind

        TEXTAREA

;++
;
; int
; longjmp (
;    IN jmp_buf JumpBuffer,
;    IN int ReturnValue
;    )
;
; Routine Description:
;
;    This function performs a long jump to the context specified by the jump
;    buffer: directly, or, if the jump buffer has an establisher frame (a safe
;    setjmp), by an unwind to it.
;
;--

        NESTED_ENTRY longjmp
        PROLOG_PUSH {r4, lr}
        PROLOG_STACK_ALLOC ExceptionRecordLength
        cmp     r1, #0
        it      eq
        moveq   r1, #1                          ; a nonzero return value
        ldr     r2, [r0, #JbFrame]
        cbnz    r2, %F20                        ; provide a safe longjmp

;
; Provide unsafe handling of longjmp.
;

        add     r2, r0, #JbR4
        ldm     r2, {r4-r11}                    ; restore integer registers r4 - r11
        ldr     r2, [r0, #JbFpscr]
        vmsr    fpscr, r2                       ; restore floating status
        add     r2, r0, #JbD
        vldm    r2, {d8-d15}                    ; restore floating registers d8 - d15
        ldr     r2, [r0, #JbPc]                 ; setjmp return address
        ldr     sp, [r0, #JbSp]                 ; restore stack pointer
        mov     r0, r1
        bx      r2                              ; jump back to the setjmp site

;
; Provide safe handling of longjmp: an exception record with the long jump
; status code and the address of the jump buffer as its parameter is made,
; and the frame of the setjmp is unwound to.
;

20      mov     r3, r1                          ; return value
        ldr     r12, =STATUS_LONGJUMP
        str     r12, [sp, #ErExceptionCode]
        movs    r12, #0
        str     r12, [sp, #ErExceptionFlags]
        str     r12, [sp, #ErExceptionRecord]
        str     r12, [sp, #ErExceptionAddress]
        movs    r12, #1
        str     r12, [sp, #ErNumberParameters]
        str     r0, [sp, #ErExceptionInformation]
        ldr     r1, [r0, #JbPc]                 ; target instruction address
        ldr     r0, [r0, #JbFrame]              ; target frame
        mov     r2, sp                          ; exception record
        bl      RtlUnwind
        b       %B20
        NESTED_END longjmp

        END
