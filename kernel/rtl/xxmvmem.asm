;++
;
; Module Name:
;
;    xxmvmem.asm
;
; Abstract:
;
;    This module implements the memory compare, move, zero and fill routines
;    (the ARM port of RTL\MIPS\XXMVMEM.S). Memory is moved and filled in
;    32-byte blocks with load and store multiple, then in words, then in
;    bytes. As on MIPS, the C runtime routines memcpy, memmove and memset are
;    entry points of these routines; they return the destination address.
;
;    N.B. These routines never make an unaligned access. The frame buffer and
;         the common buffers of devices are device memory, and the firmware
;         and the loader run with the MMU off, where all memory is strongly
;         ordered: an unaligned access to either faults. When the source and
;         the destination are not aligned alike, the destination is aligned
;         and the source is read in aligned words that are shifted together
;         (what the MIPS code does with lwl and lwr). Such a read never leaves
;         the words that hold source bytes, so it cannot touch another page.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

;++
;
; ULONG
; RtlCompareMemory (
;    IN PVOID Source1,
;    IN PVOID Source2,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function compares two blocks of memory and returns the number of
;    bytes that compared equal.
;
; Arguments:
;
;    Source1 (r0) - Supplies a pointer to the first block of memory to
;       compare.
;
;    Source2 (r1) - Supplies a pointer to the second block of memory to
;       compare.
;
;    Length (r2) - Supplies the length, in bytes, of the memory to be
;       compared.
;
; Return Value:
;
;    The number of bytes that compared equal is returned as the function
;    value. If all bytes compared equal, then the length of the original
;    block of memory is returned.
;
;--

        NESTED_ENTRY RtlCompareMemory
        PROLOG_PUSH {r4, lr}
        mov     r4, r0                          ; save the start of the first block
        orr     r3, r0, r1                      ; check if both blocks word aligned
        tst     r3, #3
        bne     %F30                            ; if ne, compare bytes
        b       %F20

10      ldr     r3, [r0]                        ; compare a word
        ldr     r12, [r1]
        cmp     r3, r12
        bne     %F30                            ; if ne, find the byte that differs
        adds    r0, r0, #4
        adds    r1, r1, #4
        subs    r2, r2, #4
20      cmp     r2, #4
        bhs     %B10                            ; if hs, a word to compare

30      cbz     r2, %F50                        ; compare bytes
40      ldrb    r3, [r0]
        ldrb    r12, [r1]
        cmp     r3, r12
        bne     %F50                            ; if ne, the bytes differ
        adds    r0, r0, #1
        adds    r1, r1, #1
        subs    r2, r2, #1
        bne     %B40

50      subs    r0, r0, r4                      ; the number of bytes that compared equal
        EPILOG_POP {r4, pc}
        NESTED_END RtlCompareMemory

;++
;
; ULONG
; RtlEqualMemory (
;    IN PVOID Source1,
;    IN PVOID Source2,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function returns TRUE if two blocks of memory hold the same bytes
;    and FALSE if not, comparing as RtlCompareMemory does.
;
; Arguments:
;
;    Source1 (r0) - Supplies the address of the first block.
;
;    Source2 (r1) - Supplies the address of the second block.
;
;    Length (r2) - Supplies the length of the blocks in bytes.
;
;--

        LEAF_ENTRY RtlEqualMemory
        orr     r3, r0, r1                      ; check if both blocks word aligned
        tst     r3, #3
        bne     %F30                            ; if ne, compare bytes
        b       %F20

10      ldr     r3, [r0], #4                    ; compare a word
        ldr     r12, [r1], #4
        cmp     r3, r12
        bne     %F60                            ; if ne, not equal
        subs    r2, r2, #4
20      cmp     r2, #4
        bhs     %B10                            ; if hs, a word to compare

30      cbz     r2, %F50                        ; compare bytes
40      ldrb    r3, [r0], #1
        ldrb    r12, [r1], #1
        cmp     r3, r12
        bne     %F60                            ; if ne, not equal
        subs    r2, r2, #1
        bne     %B40

50      movs    r0, #1                          ; TRUE
        bx      lr

60      movs    r0, #0                          ; FALSE
        bx      lr
        LEAF_END RtlEqualMemory

;++
;
; VOID
; RtlMoveMemory (
;    IN PVOID Destination,
;    IN PVOID Source,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function moves memory either forward or backward, aligned or
;    unaligned, in 32-byte blocks, followed by 4-byte blocks, followed by
;    any remaining bytes.
;
; Arguments:
;
;    Destination (r0) - Supplies a pointer to the destination address of
;       the move operation.
;
;    Source (r1) - Supplies a pointer to the source address of the move
;       operation.
;
;    Length (r2) - Supplies the length, in bytes, of the memory to be moved.
;
; Return Value:
;
;    None (memcpy and memmove: the destination address).
;
;    N.B. The C runtime entry points memmove and memcpy are equivalent to
;         RtlMoveMemory thus alternate entry points are provided for these
;         routines.
;
;--

        LEAF_ENTRY RtlMoveMemory
        ALTERNATE_ENTRY memcpy
        ALTERNATE_ENTRY memmove
        subs    r3, r0, r1                      ; compute distance of destination above source
        beq     %F20                            ; if eq, destination is the source
        cmp     r3, r2                          ; check if destination within the source
        blo     RtlpMoveBackward                ; if lo, the move must be performed backward
        cmp     r2, #16                         ; check if fewer than 16 bytes
        bhs     RtlpMoveForward                 ; if hs, move blocks forward
        cbz     r2, %F20                        ; if zero, nothing to move
        mov     r12, r0                         ; keep the destination address
10      ldrb    r3, [r1], #1                    ; move bytes
        subs    r2, r2, #1
        strb    r3, [r12], #1
        bne     %B10
20      bx      lr
        LEAF_END RtlMoveMemory

;++
;
; VOID
; RtlpMoveForward (
;    IN PVOID Destination,
;    IN PVOID Source,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function moves 16 or more bytes forward. The destination is first
;    word aligned. If the source is then word aligned too, 32-byte blocks
;    and words are moved; otherwise each destination word is made from two
;    aligned source words. The remaining bytes are moved last.
;
; Arguments:
;
;    As RtlMoveMemory.
;
; Return Value:
;
;    None.
;
;--

        NESTED_ENTRY RtlpMoveForward
        PROLOG_PUSH {r0, r1, r4-r10, lr}
        eor     r12, r0, r1                     ; compare alignment bits
        ands    r3, r0, #3                      ; check if destination word aligned
        beq     %F20                            ; if eq, aligned
        rsb     r3, r3, #4                      ; compute bytes until aligned
        sub     r2, r2, r3                      ; reduce number of bytes to move
10      ldrb    r4, [r1], #1                    ; move bytes until aligned
        subs    r3, r3, #1
        strb    r4, [r0], #1
        bne     %B10

20      tst     r12, #3                         ; check if source aligned alike
        bne     %F60                            ; if ne, shift source words together
        subs    r2, r2, #32                     ; check if a 32-byte block to move
        blo     %F40                            ; if lo, no 32-byte block
30      ldm     r1!, {r3-r10}                   ; move a 32-byte block
        subs    r2, r2, #32
        stm     r0!, {r3-r10}
        bhs     %B30
40      adds    r2, r2, #32 - 4                 ; check if a word to move
        blo     %F50                            ; if lo, no word
45      ldr     r3, [r1], #4                    ; move a word
        subs    r2, r2, #4
        str     r3, [r0], #4
        bhs     %B45
50      adds    r2, r2, #4                      ; compute remaining bytes
        b       %F80

60      and     r12, r1, #3                     ; compute offset of source in its word
        bic     r1, r1, #3                      ; align the source address
        lsls    r12, r12, #3                    ; right shift of the current word
        rsb     r3, r12, #32                    ; left shift of the next word
        ldr     r4, [r1], #4                    ; get the current source word
        subs    r2, r2, #4                      ; check if a word to move
        blo     %F75                            ; if lo, no word
70      ldr     r5, [r1], #4                    ; get the next source word
        lsr     r4, r4, r12                     ; merge the bytes of the current word
        lsl     r6, r5, r3                      ; with those of the next word
        orr     r4, r4, r6
        str     r4, [r0], #4                    ; store a destination word
        mov     r4, r5                          ; the next word becomes the current one
        subs    r2, r2, #4
        bhs     %B70
75      adds    r2, r2, #4                      ; compute remaining bytes
        sub     r1, r1, #4                      ; compute the address of the next
        add     r1, r1, r12, lsr #3             ; source byte

80      cbz     r2, %F90                        ; if zero, move complete
85      ldrb    r3, [r1], #1                    ; move the remaining bytes
        subs    r2, r2, #1
        strb    r3, [r0], #1
        bne     %B85
90      EPILOG_POP {r0, r1, r4-r10, pc}
        NESTED_END RtlpMoveForward

;++
;
; VOID
; RtlpMoveBackward (
;    IN PVOID Destination,
;    IN PVOID Source,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function moves memory backward, from the end, when the destination
;    lies within the source: as RtlpMoveForward with the ends of the blocks
;    word aligned.
;
; Arguments:
;
;    As RtlMoveMemory.
;
; Return Value:
;
;    None.
;
;--

        NESTED_ENTRY RtlpMoveBackward
        PROLOG_PUSH {r0, r1, r4-r10, lr}
        add     r0, r0, r2                      ; compute ending destination address
        add     r1, r1, r2                      ; compute ending source address
        cmp     r2, #16                         ; check if fewer than 16 bytes
        blo     %F85                            ; if lo, move bytes
        eor     r12, r0, r1                     ; compare alignment bits
        ands    r3, r0, #3                      ; check if destination end word aligned
        beq     %F20                            ; if eq, aligned
        sub     r2, r2, r3                      ; reduce number of bytes to move
10      ldrb    r4, [r1, #-1]!                  ; move bytes until aligned
        subs    r3, r3, #1
        strb    r4, [r0, #-1]!
        bne     %B10

20      tst     r12, #3                         ; check if source aligned alike
        bne     %F60                            ; if ne, shift source words together
        subs    r2, r2, #32                     ; check if a 32-byte block to move
        blo     %F40                            ; if lo, no 32-byte block
30      ldmdb   r1!, {r3-r10}                   ; move a 32-byte block
        subs    r2, r2, #32
        stmdb   r0!, {r3-r10}
        bhs     %B30
40      adds    r2, r2, #32 - 4                 ; check if a word to move
        blo     %F50                            ; if lo, no word
45      ldr     r3, [r1, #-4]!                  ; move a word
        subs    r2, r2, #4
        str     r3, [r0, #-4]!
        bhs     %B45
50      adds    r2, r2, #4                      ; compute remaining bytes
        b       %F80

60      and     r12, r1, #3                     ; compute offset of source end in its word
        bic     r1, r1, #3                      ; align the source address
        lsls    r12, r12, #3                    ; right shift of the lower word
        rsb     r3, r12, #32                    ; left shift of the upper word
        ldr     r4, [r1]                        ; get the upper source word
        subs    r2, r2, #4                      ; check if a word to move
        blo     %F75                            ; if lo, no word
70      ldr     r5, [r1, #-4]!                  ; get the lower source word
        lsl     r4, r4, r3                      ; merge the bytes of the upper word
        lsr     r6, r5, r12                     ; with those of the lower word
        orr     r4, r4, r6
        str     r4, [r0, #-4]!                  ; store a destination word
        mov     r4, r5                          ; the lower word becomes the upper one
        subs    r2, r2, #4
        bhs     %B70
75      adds    r2, r2, #4                      ; compute remaining bytes
        add     r1, r1, r12, lsr #3             ; compute the end of the source bytes left

80      cbz     r2, %F90                        ; if zero, move complete
85      ldrb    r3, [r1, #-1]!                  ; move the remaining bytes
        subs    r2, r2, #1
        strb    r3, [r0, #-1]!
        bne     %B85
90      EPILOG_POP {r0, r1, r4-r10, pc}
        NESTED_END RtlpMoveBackward

;++
;
; VOID
; RtlZeroMemory (
;    IN PVOID Destination,
;    IN ULONG Length
;    )
;
; Routine Description:
;
;    This function zeros memory.
;
; Arguments:
;
;    Destination (r0) - Supplies a pointer to the memory to zero.
;
;    Length (r1) - Supplies the length, in bytes, of the memory to be zeroed.
;
; Return Value:
;
;    None.
;
;--

        LEAF_ENTRY RtlZeroMemory
        movs    r2, #0                          ; the fill pattern
        b       RtlpFillMemory
        LEAF_END RtlZeroMemory

;++
;
; VOID
; RtlFillMemory (
;    IN PVOID Destination,
;    IN ULONG Length,
;    IN UCHAR Fill
;    )
;
; Routine Description:
;
;    This function fills memory with a byte value.
;
; Arguments:
;
;    Destination (r0) - Supplies a pointer to the memory to fill.
;
;    Length (r1) - Supplies the length, in bytes, of the memory to be filled.
;
;    Fill (r2) - Supplies the fill byte.
;
; Return Value:
;
;    None.
;
;--

        LEAF_ENTRY RtlFillMemory
        and     r2, r2, #0xff                   ; copy the fill byte to all four bytes
        orr     r2, r2, r2, lsl #8
        orr     r2, r2, r2, lsl #16
        b       RtlpFillMemory
        LEAF_END RtlFillMemory

;++
;
; PVOID
; memset (
;    IN PVOID Destination,
;    IN int Fill,
;    IN size_t Length
;    )
;
; Routine Description:
;
;    This function is the C runtime entry point of RtlFillMemory, whose
;    arguments it takes in the other order.
;
; Return Value:
;
;    The destination address.
;
;--

        LEAF_ENTRY memset
        and     r3, r1, #0xff                   ; copy the fill byte to all four bytes
        mov     r1, r2                          ; the length
        orr     r2, r3, r3, lsl #8
        orr     r2, r2, r2, lsl #16
        b       RtlpFillMemory
        LEAF_END memset

;++
;
; VOID
; RtlFillMemoryUlong (
;    IN PVOID Destination,
;    IN ULONG Length,
;    IN ULONG Pattern
;    )
;
; Routine Description:
;
;    This function fills memory with a longword pattern.
;
; Arguments:
;
;    Destination (r0) - Supplies a pointer to the memory to fill; it must be
;       longword aligned.
;
;    Length (r1) - Supplies the length, in bytes, of the memory to be filled;
;       a remainder of a longword is not filled.
;
;    Pattern (r2) - Supplies the fill pattern.
;
; Return Value:
;
;    None.
;
;--

        LEAF_ENTRY RtlFillMemoryUlong
        bic     r1, r1, #3                      ; whole longwords only
        b       RtlpFillMemory
        LEAF_END RtlFillMemoryUlong

;++
;
; VOID
; RtlpFillMemory (
;    IN PVOID Destination,
;    IN ULONG Length,
;    IN ULONG Pattern
;    )
;
; Routine Description:
;
;    This function fills memory with a pattern whose bytes are stored as
;    they lie in the pattern word: bytes up to a word, then words, then the
;    remaining bytes. RtlpFillBlocks fills 32 bytes or more.
;
; Arguments:
;
;    Destination (r0) - Supplies a pointer to the memory to fill.
;
;    Length (r1) - Supplies the length, in bytes, of the memory to be filled.
;
;    Pattern (r2) - Supplies the fill pattern: a byte in all four bytes, or a
;       longword when the destination and the length are longword multiples.
;
; Return Value:
;
;    None.
;
;--

        LEAF_ENTRY RtlpFillMemory
        cmp     r1, #32                         ; check if 32 bytes or more
        bhs     RtlpFillBlocks                  ; if hs, fill blocks
        cbz     r1, %F40                        ; if zero, nothing to fill
        mov     r12, r0                         ; keep the destination address
10      tst     r12, #3                         ; check if destination word aligned
        beq     %F20                            ; if eq, aligned
        strb    r2, [r12], #1                   ; fill a byte
        subs    r1, r1, #1
        bne     %B10
        bx      lr

20      subs    r1, r1, #4                      ; check if a word to fill
        blo     %F30                            ; if lo, no word
25      str     r2, [r12], #4                   ; fill a word
        subs    r1, r1, #4
        bhs     %B25
30      adds    r1, r1, #4                      ; compute remaining bytes
        beq     %F40                            ; if eq, fill complete
35      strb    r2, [r12], #1                   ; fill the remaining bytes
        subs    r1, r1, #1
        bne     %B35
40      bx      lr
        LEAF_END RtlpFillMemory

        NESTED_ENTRY RtlpFillBlocks
        PROLOG_PUSH {r0, r1, r4-r10, lr}
        ands    r3, r0, #3                      ; check if destination word aligned
        beq     %F20                            ; if eq, aligned
        rsb     r3, r3, #4                      ; compute bytes until aligned
        sub     r1, r1, r3                      ; reduce number of bytes to fill
10      strb    r2, [r0], #1                    ; fill bytes until aligned
        subs    r3, r3, #1
        bne     %B10

20      mov     r3, r2                          ; the pattern in eight registers
        mov     r4, r2
        mov     r5, r2
        mov     r6, r2
        mov     r7, r2
        mov     r8, r2
        mov     r9, r2
        mov     r10, r2
        subs    r1, r1, #32                     ; check if a 32-byte block to fill
        blo     %F40                            ; if lo, no 32-byte block
30      stm     r0!, {r3-r10}                   ; fill a 32-byte block
        subs    r1, r1, #32
        bhs     %B30
40      adds    r1, r1, #32 - 4                 ; check if a word to fill
        blo     %F50                            ; if lo, no word
45      str     r2, [r0], #4                    ; fill a word
        subs    r1, r1, #4
        bhs     %B45
50      adds    r1, r1, #4                      ; compute remaining bytes
        beq     %F90                            ; if eq, fill complete
55      strb    r2, [r0], #1                    ; fill the remaining bytes
        subs    r1, r1, #1
        bne     %B55
90      EPILOG_POP {r0, r1, r4-r10, pc}
        NESTED_END RtlpFillBlocks

        END
