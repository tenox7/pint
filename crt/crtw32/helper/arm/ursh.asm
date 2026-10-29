;++
;
; Module Name:
;
;    ursh.asm
;
; Abstract:
;
;    __rt_ursh, the logical 64-bit right shift Microsoft C for ARM calls: r0:r1
;    shifted right by r2 (0 to 63).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

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

        END
