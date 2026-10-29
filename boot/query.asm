;++
;
; Module Name:
;
;    query.asm
;
; Abstract:
;
;    BlQueryImplementationAndRevision, the ARM BOOT\LIB\MIPS\QUERY.S: return the
;    processor's main ID register and the floating point system ID register.
;
;--

#include "kxarm.h"

        TEXTAREA

;
; VOID
; BlQueryImplementationAndRevision (
;    OUT PULONG ProcessorId,
;    OUT PULONG FloatingId
;    )
;

        LEAF_ENTRY BlQueryImplementationAndRevision
        mrc     p15, 0, r2, c0, c0, 0           ; main ID register
        str     r2, [r0]
        vmrs    r2, fpsid                       ; floating point system ID
        str     r2, [r1]
        bx      lr
        LEAF_END BlQueryImplementationAndRevision

        END
