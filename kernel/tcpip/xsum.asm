;++
;
; Module Name:
;
;    xsum.asm
;
; Abstract:
;
;    The checksum of TCP/IP: the ARM port of TCPIP\TCP\MIPS\XSUM.S. The
;    buffer is summed in 16-byte blocks of words with the carries added
;    back, then the words, halfword and byte left, and the sum folded to 16
;    bits. A buffer that starts at an odd address is summed from the next
;    byte with its first byte in the high lane, and the folded sum swapped.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

;++
;
; ULONG
; tcpxsum (
;    IN ULONG Checksum,
;    IN PUCHAR Source,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function computes the checksum of the specified buffer.
;
; Arguments:
;
;    Checksum (r0) - Supplies the initial checksum value.
;
;    Source (r1) - Supplies a pointer to the checksum buffer.
;
;    Length (r2) - Supplies the length of the buffer in bytes.
;
; Return Value:
;
;    The computed checksum is returned as the function value.
;
;--

        NESTED_ENTRY tcpxsum
        PROLOG_PUSH {r4-r7, lr}

        mov     r3, #0                  ; clear computed checksum
        mov     r12, #0                 ; clear odd address flag
        cmp     r2, #0                  ; check for no bytes
        beq     %F90                    ; if eq, no bytes to checksum
        tst     r1, #1                  ; check if buffer halfword aligned
        beq     %F10                    ; if eq, aligned
        mov     r12, #1                 ; set odd address flag
        ldrb    r4, [r1], #1            ; get first byte of buffer
        lsl     r3, r4, #8              ; put it in the high lane
        subs    r2, r2, #1              ; reduce count of bytes
        beq     %F90                    ; if eq, no more bytes

;
; Align the buffer to a word.
;

10      cmp     r2, #2                  ; check for a halfword
        blo     %F80                    ; if lo, a byte at most
        tst     r1, #2                  ; check if buffer word aligned
        beq     %F20                    ; if eq, aligned
        ldrh    r4, [r1], #2            ; add first halfword
        add     r3, r3, r4              ;
        sub     r2, r2, #2              ; reduce count of bytes

;
; Checksum 16-byte blocks.
;

20      subs    r2, r2, #16             ; check for a 16-byte block
        blo     %F40                    ; if lo, none
30      ldm     r1!, {r4-r7}            ; load 16-byte block
        adds    r3, r3, r4              ; add with carries
        adcs    r3, r3, r5              ;
        adcs    r3, r3, r6              ;
        adcs    r3, r3, r7              ;
        adc     r3, r3, #0              ; add last carry
        subs    r2, r2, #16             ; reduce count of bytes
        bhs     %B30                    ; if hs, more blocks
40      add     r2, r2, #16             ; residual bytes

;
; Checksum the words, the halfword and the byte left.
;

50      cmp     r2, #4                  ; check for a word
        blo     %F70                    ; if lo, none
        ldr     r4, [r1], #4            ; add word with carry
        adds    r3, r3, r4              ;
        adc     r3, r3, #0              ;
        sub     r2, r2, #4              ; reduce count of bytes
        b       %B50                    ;

70      cmp     r2, #2                  ; check for a halfword
        blo     %F80                    ; if lo, none
        ldrh    r4, [r1], #2            ; add halfword with carry
        adds    r3, r3, r4              ;
        adc     r3, r3, #0              ;
        sub     r2, r2, #2              ; reduce count of bytes
80      cmp     r2, #0                  ; check for a byte
        beq     %F90                    ; if eq, none
        ldrb    r4, [r1]                ; add last byte in the low lane
        adds    r3, r3, r4              ;
        adc     r3, r3, #0              ;

;
; Fold the computed checksum, swap its bytes if the buffer started at an
; odd address, and combine it with the input checksum.
;

90      lsr     r4, r3, #16             ; fold to 16 bits
        uxth    r3, r3                  ;
        add     r3, r3, r4              ;
        lsr     r4, r3, #16             ;
        uxth    r3, r3                  ;
        add     r3, r3, r4              ;
        cmp     r12, #0                 ; check odd address flag
        beq     %F100                   ; if eq, aligned
        rev16   r3, r3                  ; swap checksum bytes
100     adds    r0, r0, r3              ; combine checksums
        adc     r0, r0, #0              ; add carry
        lsr     r1, r0, #16             ; fold to 16 bits
        uxth    r0, r0                  ;
        add     r0, r0, r1              ;
        lsr     r1, r0, #16             ;
        uxth    r0, r0                  ;
        add     r0, r0, r1              ;
        EPILOG_POP {r4-r7, pc}

        NESTED_END tcpxsum

        END
