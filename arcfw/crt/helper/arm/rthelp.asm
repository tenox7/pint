;++
;
; Module Name:
;
;    rthelp.asm
;
; Abstract:
;
;    The run-time helpers Microsoft C for ARM calls: the integer divisions (the
;    processor divides 32-bit integers, the 64-bit ones are worked in C), the
;    64-bit right shifts and the stack probe of a large frame - the ARM analog of
;    the CRT32NT MIPS helpers. A division by zero is trapped by the caller, which
;    tests the divisor and executes the divide by zero instruction.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  RtlpDivideUnsigned64
        IMPORT  RtlpDivideSigned64

        TEXTAREA

;
; __rt_sdiv, __rt_udiv: r0 divisor, r1 dividend -> r0 quotient, r1 remainder.
;

        LEAF_ENTRY __rt_sdiv
        sdiv    r2, r1, r0
        mls     r1, r2, r0, r1
        mov     r0, r2
        bx      lr
        LEAF_END __rt_sdiv

        LEAF_ENTRY __rt_udiv
        udiv    r2, r1, r0
        mls     r1, r2, r0, r1
        mov     r0, r2
        bx      lr
        LEAF_END __rt_udiv

;
; __rt_sdiv64, __rt_udiv64: r0:r1 divisor, r2:r3 dividend -> r0:r1 quotient, r2:r3
; remainder. The worker takes (dividend, divisor, &remainder) and returns the
; quotient.
;

        NESTED_ENTRY __rt_udiv64
        PROLOG_PUSH {r4, lr}
        PROLOG_STACK_ALLOC 16
        mov     r12, r0
        mov     r0, r2
        mov     r2, r12
        mov     r12, r1
        mov     r1, r3
        mov     r3, r12
        add     r12, sp, #8
        str     r12, [sp]
        bl      RtlpDivideUnsigned64
        ldrd    r2, r3, [sp, #8]
        EPILOG_STACK_FREE 16
        EPILOG_POP {r4, pc}
        NESTED_END __rt_udiv64

        NESTED_ENTRY __rt_sdiv64
        PROLOG_PUSH {r4, lr}
        PROLOG_STACK_ALLOC 16
        mov     r12, r0
        mov     r0, r2
        mov     r2, r12
        mov     r12, r1
        mov     r1, r3
        mov     r3, r12
        add     r12, sp, #8
        str     r12, [sp]
        bl      RtlpDivideSigned64
        ldrd    r2, r3, [sp, #8]
        EPILOG_STACK_FREE 16
        EPILOG_POP {r4, pc}
        NESTED_END __rt_sdiv64

;
; __rt_srsh, __rt_ursh: r0:r1 shifted right by r2 (0 to 63), arithmetic and
; logical.
;

        LEAF_ENTRY __rt_srsh
        subs    r3, r2, #32
        bpl     %F1
        rsb     r3, r2, #32
        lsr     r0, r0, r2
        lsl     r3, r1, r3
        orr     r0, r0, r3
        asr     r1, r1, r2
        bx      lr
1       asr     r0, r1, r3
        asr     r1, r1, #31
        bx      lr
        LEAF_END __rt_srsh

        LEAF_ENTRY __rt_ursh
        subs    r3, r2, #32
        bpl     %F1
        rsb     r3, r2, #32
        lsr     r0, r0, r2
        lsl     r3, r1, r3
        orr     r0, r0, r3
        lsr     r1, r1, r2
        bx      lr
1       lsr     r0, r1, r3
        mov     r1, #0
        bx      lr
        LEAF_END __rt_ursh

;
; __helper_divide_by_0: trap an integer division by zero.
;

        LEAF_ENTRY __helper_divide_by_0
        DCW     THUMB_DIVIDE_BY_0
        bx      lr
        LEAF_END __helper_divide_by_0

;
; __chkstk: r4 the size of a frame in words -> r4 its size in bytes. Each page of
; the frame is touched in order down from the stack pointer, as the guard page of
; an NT thread stack grows it (the MIPS _RtlCheckStack). Only r4 changes.
;

        NESTED_ENTRY __chkstk
        PROLOG_PUSH {r5, r6}
        lsl     r4, r4, #2
        add     r5, sp, #8
        sub     r6, r5, r4
1       sub     r5, r5, #0x1000
        cmp     r5, r6
        blo     %F2
        ldr     r12, [r5]
        b       %B1
2       ldr     r12, [r6]
        EPILOG_POP {r5, r6}
        EPILOG_RETURN
        NESTED_END __chkstk

        END
