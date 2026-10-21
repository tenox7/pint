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
;    interfaces, which need no spin lock, and of the Interlocked functions.
;    Each is a full barrier: a memory barrier before and after.
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

        END
