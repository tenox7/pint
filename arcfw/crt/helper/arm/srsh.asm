;++
;
; Module Name:
;
;    srsh.asm
;
; Abstract:
;
;    __rt_srsh, the arithmetic 64-bit right shift Microsoft C for ARM calls: r0:r1
;    shifted right by r2 (0 to 63).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

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

        END
