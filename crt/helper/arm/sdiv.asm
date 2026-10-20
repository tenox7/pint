;++
;
; Module Name:
;
;    sdiv.asm
;
; Abstract:
;
;    __rt_sdiv, the signed 32-bit division Microsoft C for ARM calls: r0 divisor,
;    r1 dividend -> r0 quotient, r1 remainder. The caller traps a zero divisor.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

        LEAF_ENTRY __rt_sdiv
        sdiv    r2, r1, r0
        mls     r1, r2, r0, r1
        mov     r0, r2
        bx      lr
        LEAF_END __rt_sdiv

        END
