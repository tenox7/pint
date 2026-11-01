;++
;
; Module Name:
;
;    slist.asm
;
; Abstract:
;
;    The sequenced list that timtp times (SlistTest): the ARM port of
;    SDKTOOLS\TIMTP\MIPS\SLIST.S, as ExpInterlockedPopEntrySList and
;    ExpInterlockedPushEntrySList of the kernel (ke\interlock.asm).
;
;--

#include "kxarm.h"

        TEXTAREA

;++
;
; PSINGLE_LIST_ENTRY
; InterlockedPopEntrySList (
;    IN PSLIST_HEADER ListHead
;    )
;
; Routine Description:
;
;    This function removes the first entry of a sequenced list. The head (its
;    first entry, depth and sequence) changes with one exclusive store; the
;    barrier orders the entry after it is removed.
;
; Return Value:
;
;    The entry removed, or NULL if the list is empty.
;
;--

        NESTED_ENTRY InterlockedPopEntrySList
        PROLOG_PUSH {r4-r6, lr}
10      ldrd    r2, r3, [r0]                    ; first entry, depth and sequence
        cbz     r2, %F20                        ; the list is empty
        ldr     r1, [r2]                        ; its successor
        ldrexd  r4, r5, [r0]                    ; reload the head exclusively
        cmp     r4, r2
        it      eq
        cmpeq   r5, r3
        bne     %B10                            ; the head has changed
        sub     r5, r3, #1                      ; depth - 1
        add     r5, r5, #0x10000                ; sequence + 1
        strexd  r6, r1, r5, [r0]                ; the successor is first
        cmp     r6, #0
        bne     %B10
        dmb
20      mov     r0, r2
        EPILOG_POP {r4-r6, pc}
        NESTED_END InterlockedPopEntrySList

;++
;
; PSINGLE_LIST_ENTRY
; InterlockedPushEntrySList (
;    IN PSLIST_HEADER ListHead,
;    IN PSINGLE_LIST_ENTRY ListEntry
;    )
;
; Routine Description:
;
;    This function inserts an entry at the head of a sequenced list. The
;    barrier orders the entry's link before the entry is in the list.
;
; Return Value:
;
;    The previous first entry, or NULL if the list was empty.
;
;--

        NESTED_ENTRY InterlockedPushEntrySList
        PROLOG_PUSH {r4-r6, lr}
10      ldrd    r2, r3, [r0]                    ; first entry, depth and sequence
        str     r2, [r1]                        ; it follows the new entry
        dmb
        ldrexd  r4, r5, [r0]                    ; reload the head exclusively
        cmp     r4, r2
        it      eq
        cmpeq   r5, r3
        bne     %B10                            ; the head has changed
        add     r5, r3, #1                      ; depth + 1
        add     r5, r5, #0x10000                ; sequence + 1
        strexd  r6, r1, r5, [r0]                ; the new entry is first
        cmp     r6, #0
        bne     %B10
        mov     r0, r2
        EPILOG_POP {r4-r6, pc}
        NESTED_END InterlockedPushEntrySList

        END
