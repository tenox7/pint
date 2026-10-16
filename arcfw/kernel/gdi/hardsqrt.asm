;++
;
; Module Name:
;
;    hardsqrt.asm
;
; Abstract:
;
;    Single precision square root: the ARM port of
;    GDI\MATH\MIPS\HARDSQRT.S.
;
;--

#include "kxarm.h"

        TEXTAREA

;
; FLOAT xSqrt(IN FLOAT Value)
;

        LEAF_ENTRY xSqrt
        vsqrt.f32 s0, s0                        ; compute square root
        bx      lr
        LEAF_END xSqrt

        END
