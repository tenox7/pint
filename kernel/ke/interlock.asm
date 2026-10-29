;++
;
; Module Name:
;
;    interlock.asm
;
; Abstract:
;
;    The interlocked operations, with the exclusive monitor (ldrex/strex, the
;    MIPS ll/sc): the ARM implementation of the executive interlocked
;    interfaces, which need no spin lock, of the Interlocked functions, and
;    of the 64-bit compare exchange, the 64-bit exchange add and the sequenced
;    singly linked lists of EX\MIPS\X4INTRLK.S (ldrexd/strexd, the MIPS
;    lld/scd). Each but the lists is a full barrier: a memory barrier before
;    and after.
;    INTERLOCKED_RESULT values: ResultZero = 0, ResultNegative = 1,
;    ResultPositive = 2.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

;
; INTERLOCKED_RESULT
; ExInterlockedIncrementLong (
;    IN PLONG Addend,
;    IN PKSPIN_LOCK Lock
;    )
;

        LEAF_ENTRY ExInterlockedIncrementLong
        ALTERNATE_ENTRY ExArmInterlockedIncrementLong
        dmb
10      ldrex   r1, [r0]
        adds    r1, r1, #1
        strex   r2, r1, [r0]
        cmp     r2, #0
        bne     %B10
        dmb
        b       InterlockedResult
        LEAF_END ExInterlockedIncrementLong

;
; INTERLOCKED_RESULT
; ExInterlockedDecrementLong (
;    IN PLONG Addend,
;    IN PKSPIN_LOCK Lock
;    )
;

        LEAF_ENTRY ExInterlockedDecrementLong
        ALTERNATE_ENTRY ExArmInterlockedDecrementLong
        dmb
10      ldrex   r1, [r0]
        subs    r1, r1, #1
        strex   r2, r1, [r0]
        cmp     r2, #0
        bne     %B10
        dmb
        b       InterlockedResult
        LEAF_END ExInterlockedDecrementLong

        LEAF_ENTRY_NO_PDATA InterlockedResult
        cmp     r1, #0
        ite     eq
        moveq   r0, #0                          ; ResultZero
        movne   r0, #2                          ; ResultPositive
        it      lt
        movlt   r0, #1                          ; ResultNegative
        bx      lr
        LEAF_END_NO_PDATA InterlockedResult

;
; ULONG
; ExInterlockedExchangeUlong (
;    IN PULONG Target,
;    IN ULONG Value,
;    IN PKSPIN_LOCK Lock
;    )
;
; LONG
; InterlockedExchange (
;    IN OUT PLONG Target,
;    IN LONG Value
;    )
;

        LEAF_ENTRY ExInterlockedExchangeUlong
        ALTERNATE_ENTRY ExArmInterlockedExchangeUlong
        ALTERNATE_ENTRY InterlockedExchange
        dmb
10      ldrex   r2, [r0]
        strex   r3, r1, [r0]
        cmp     r3, #0
        bne     %B10
        dmb
        mov     r0, r2
        bx      lr
        LEAF_END ExInterlockedExchangeUlong

;
; LONG
; InterlockedIncrement (
;    IN PLONG Addend
;    )
;
; Returns the new value.
;

        LEAF_ENTRY InterlockedIncrement
        dmb
10      ldrex   r1, [r0]
        adds    r1, r1, #1
        strex   r2, r1, [r0]
        cmp     r2, #0
        bne     %B10
        dmb
        mov     r0, r1
        bx      lr
        LEAF_END InterlockedIncrement

;
; LONG
; InterlockedDecrement (
;    IN PLONG Addend
;    )
;
; Returns the new value.
;

        LEAF_ENTRY InterlockedDecrement
        dmb
10      ldrex   r1, [r0]
        subs    r1, r1, #1
        strex   r2, r1, [r0]
        cmp     r2, #0
        bne     %B10
        dmb
        mov     r0, r1
        bx      lr
        LEAF_END InterlockedDecrement

;
; LONG
; InterlockedExchangeAdd (
;    IN OUT PLONG Addend,
;    IN LONG Increment
;    )
;
; Returns the old value.
;

        LEAF_ENTRY InterlockedExchangeAdd
        dmb
10      ldrex   r2, [r0]
        add     r3, r2, r1
        strex   r12, r3, [r0]
        cmp     r12, #0
        bne     %B10
        dmb
        mov     r0, r2
        bx      lr
        LEAF_END InterlockedExchangeAdd

;
; PVOID
; InterlockedCompareExchange (
;    IN OUT PVOID *Destination,
;    IN PVOID Exchange,
;    IN PVOID Comperand
;    )
;
; Returns the old value.
;

        LEAF_ENTRY InterlockedCompareExchange
        dmb
10      ldrex   r3, [r0]
        cmp     r3, r2
        bne     %F20
        strex   r12, r1, [r0]
        cmp     r12, #0
        bne     %B10
20      dmb
        mov     r0, r3
        bx      lr
        LEAF_END InterlockedCompareExchange

;
; ULONGLONG
; ExpInterlockedCompareExchange64 (
;    IN PULONGLONG Destination,
;    IN PULONGLONG Exchange,
;    IN PULONGLONG Comperand
;    )
;
; Store the exchange value in the destination if the destination holds the
; comperand, with ldrexd/strexd (the MIPS lld/scd). Returns the old value of
; the destination.
;

        NESTED_ENTRY ExpInterlockedCompareExchange64
        PROLOG_PUSH {r4-r6, lr}
        mov     r12, r0                         ; destination
        ldrd    r4, r5, [r2]                    ; comperand
        ldrd    r2, r3, [r1]                    ; exchange value
        dmb
10      ldrexd  r0, r1, [r12]                   ; old value
        cmp     r0, r4
        it      eq
        cmpeq   r1, r5
        bne     %F20                            ; not the comperand
        strexd  r6, r2, r3, [r12]
        cmp     r6, #0
        bne     %B10
20      dmb
        EPILOG_POP {r4-r6, pc}
        NESTED_END ExpInterlockedCompareExchange64

;
; LARGE_INTEGER
; ExpInterlockedExchangeAddLargeInteger (
;    IN PLARGE_INTEGER Addend,
;    IN LARGE_INTEGER Increment
;    )
;
; Add the increment to the addend with ldrexd/strexd (the MIPS lld/scd), as
; unsigned arithmetic, which raises no overflow. A large integer is returned
; in memory: r0 holds the address of the result, which receives the initial
; value of the addend, r1 the address of the addend, r2 and r3 the increment.
;

        NESTED_ENTRY ExpInterlockedExchangeAddLargeInteger
        PROLOG_PUSH {r4-r7}
        dmb
10      ldrexd  r4, r5, [r1]                    ; initial value
        adds    r6, r4, r2
        adc     r7, r5, r3
        strexd  r12, r6, r7, [r1]
        cmp     r12, #0
        bne     %B10
        dmb
        strd    r4, r5, [r0]                    ; return the initial value
        EPILOG_POP {r4-r7}
        EPILOG_RETURN
        NESTED_END ExpInterlockedExchangeAddLargeInteger

;
; PSINGLE_LIST_ENTRY
; ExpInterlockedPopEntrySList (
;    IN PSLIST_HEADER ListHead
;    )
;
; PSINGLE_LIST_ENTRY
; ExpInterlockedPushEntrySList (
;    IN PSLIST_HEADER ListHead,
;    IN PSINGLE_LIST_ENTRY ListEntry
;    )
;
; The sequenced singly linked lists. The list head is a quadword: the first
; entry, and above it the depth of the list and a sequence that each change
; increments. As on MIPS, the head is read, the change made ready, and the
; head reloaded exclusively and stored only if it is the same: no other
; access comes between ldrexd and strexd, and the sequence makes a head that
; was changed and changed back differ. Pop returns the entry removed, NULL
; if the list is empty; push returns the previous first entry. The memory
; barriers order an entry before it is in the list and after it is removed.
;

        NESTED_ENTRY ExpInterlockedPopEntrySList
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
        NESTED_END ExpInterlockedPopEntrySList

        NESTED_ENTRY ExpInterlockedPushEntrySList
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
        NESTED_END ExpInterlockedPushEntrySList

        END
