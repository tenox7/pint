;++
;
; Module Name:
;
;    udiv64.asm
;
; Abstract:
;
;    __rt_udiv64, the unsigned 64-bit division Microsoft C for ARM calls: r0:r1
;    divisor, r2:r3 dividend -> r0:r1 quotient, r2:r3 remainder, worked by
;    RtlpDivideUnsigned64 (dividend, divisor, &remainder), which returns the quotient.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  RtlpDivideUnsigned64

        TEXTAREA

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

        END
