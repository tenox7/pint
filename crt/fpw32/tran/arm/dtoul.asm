;++
;
; Module Name:
;
;    dtoul.asm
;
; Abstract:
;
;    unsigned long _dtoul(double x) - double to unsigned long conversion by the
;    current rounding mode (FPW32\TRAN\MIPS\DTOUL.S for ARM): x up to 2^31 - 1
;    converts directly, x up to 2 * (2^31 - 1) as x - (2^31 - 1) plus 2^31 - 1,
;    and a negative or larger x gives ~0.
;
;--

#include "kxarm.h"

        TEXTAREA

        LEAF_ENTRY _dtoul
        vmov    r0, r1, d0
        tst     r1, #0x80000000
        bne     %F10                            ; negative
        mvn     r2, #0x80000000                 ; 2^31 - 1
        vmov    s2, r2
        vcvt.f64.s32 d1, s2
        vcmp.f64 d0, d1
        vmrs    APSR_nzcv, fpscr
        bgt     %F20                            ; above 2^31 - 1
        vcvtr.s32.f64 s4, d0
        vmov    r0, s4
        bx      lr
20      vsub.f64 d0, d0, d1
        vcmp.f64 d0, d1
        vmrs    APSR_nzcv, fpscr
        bgt     %F10                            ; above 2 * (2^31 - 1)
        vcvtr.s32.f64 s4, d0
        vmov    r0, s4
        add     r0, r0, r2
        bx      lr
10      mvn     r0, #0
        bx      lr
        LEAF_END _dtoul

        END
