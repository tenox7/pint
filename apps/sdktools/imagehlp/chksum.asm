;++
;
; Module Name:
;
;    chksum.asm
;
; Abstract:
;
;    The checksum of a buffer: the ARM port of
;    SDKTOOLS\IMAGEHLP\MIPS\CHKSUM.S.
;
;--

#include "kxarm.h"

        TEXTAREA

;++
;
; USHORT
; ChkSum (
;    IN ULONG Checksum,
;    IN PUSHORT Source,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function computes the checksum of the specified buffer: the sum of
;    its 16-bit words with the carries folded back into the low 16 bits,
;    32 bytes at a time, then the remaining words.
;
; Arguments:
;
;    Checksum (r0) - Supplies the initial checksum value.
;
;    Source (r1) - Supplies a pointer to the buffer that is checksumed.
;
;    Length (r2) - Supplies the length of the buffer in words.
;
; Return Value:
;
;    The computed checksum is returned as the function value.
;
;--

        LEAF_ENTRY ChkSum
        lsrs    r12, r2, #4                     ; the number of 32-byte blocks
        beq     %F20
10      ldrh    r3, [r1], #2                    ; add a 32-byte block
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        ldrh    r3, [r1], #2
        add     r0, r0, r3
        uxth    r3, r0                          ; add the sum bits to the carry bits
        add     r0, r3, r0, lsr #16
        subs    r12, r12, #1
        bne     %B10
20      ands    r2, r2, #15                     ; the remaining words
        beq     %F40
30      ldrh    r3, [r1], #2
        add     r0, r0, r3
        subs    r2, r2, #1
        bne     %B30
40      uxth    r3, r0                          ; fold the checksum into 16 bits
        add     r0, r3, r0, lsr #16
        add     r0, r0, r0, lsr #16
        uxth    r0, r0
        bx      lr
        LEAF_END ChkSum

        END
