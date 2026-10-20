;++
;
; Module Name:
;
;    muldiv.asm
;
; Abstract:
;
;    Large integer multiply and divide: the ARM port of
;    BASE\CLIENT\MIPS\MULDIV.S.
;
;--

#include "kxarm.h"

        TEXTAREA

;++
;
; LARGE_INTEGER
; RtlEnlargedIntegerMultiply (
;    IN LONG Multiplicand,
;    IN LONG Multiplier
;    )
;
; Routine Description:
;
;    This function multiplies a signed integer by a signed integer and
;    returns a signed large integer result, stored at the address in r0.
;
;--

        LEAF_ENTRY RtlEnlargedIntegerMultiply
        smull   r2, r3, r1, r2
        stm     r0, {r2, r3}
        bx      lr
        LEAF_END RtlEnlargedIntegerMultiply

;++
;
; ULONG
; RtlEnlargedUnsignedDivide (
;    IN ULARGE_INTEGER Dividend,
;    IN ULONG Divisor,
;    IN PULONG Remainder
;    )
;
; Routine Description:
;
;    This function divides an unsigned large integer (r0, r1) by an unsigned
;    long (r2) and returns the quotient, and the remainder if r3 is not NULL.
;    It is assumed that no overflow will occur.
;
;--

        LEAF_ENTRY RtlEnlargedUnsignedDivide
        mov     r12, #32
10      lsls    r1, r1, #1                      ; shift the next dividend bit into
        orr     r1, r1, r0, lsr #31             ; the partial remainder
        lsls    r0, r0, #1                      ; shift the quotient
        cmp     r1, r2
        itt     hs
        subhs   r1, r1, r2                      ; subtract out the divisor
        orrhs   r0, r0, #1                      ; merge a quotient bit
        subs    r12, r12, #1
        bne     %B10
        cbz     r3, %F20
        str     r1, [r3]                        ; store the remainder
20      bx      lr
        LEAF_END RtlEnlargedUnsignedDivide

        END
