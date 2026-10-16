;++
;
; Module Name:
;
;    frnd.asm
;
; Abstract:
;
;    double _frnd(double x) - round to integer by the current rounding mode;
;    NaNs and infinities are not handled (FP32\TRAN\MIPS\FRND.S for ARM).
;
;--

#include "kxarm.h"

        TEXTAREA

        LEAF_ENTRY _frnd
        vcmp.f64 d0, #0
        vmrs    APSR_nzcv, fpscr
        it      eq
        bxeq    lr                              ; zero: the argument (its sign)
        vldr    d1, Two52
        vabs.f64 d2, d0
        vcmp.f64 d2, d1
        vmrs    APSR_nzcv, fpscr
        it      ge
        bxge    lr                              ; |x| >= 2^52: x is integral
        vcmp.f64 d0, #0
        vmrs    APSR_nzcv, fpscr
        blt     %F10
        vadd.f64 d0, d0, d1
        vsub.f64 d0, d0, d1
        bx      lr
10      vsub.f64 d0, d0, d1
        vadd.f64 d0, d0, d1
        bx      lr
        ALIGN   8
Two52   DCQ     0x4330000000000000              ; 2^52
        LEAF_END _frnd

        END
