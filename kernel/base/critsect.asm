;++
;
; Module Name:
;
;    critsect.asm
;
; Abstract:
;
;    Interlocked increment, decrement, exchange, compare exchange and
;    exchange add: the ARM port of BASE\CLIENT\MIPS\CRITSECT.S. The values
;    are updated with ldrex/strex (the MIPS ll/sc), with memory barriers
;    before and after, so that each is a full barrier on a multiprocessor.
;    The critical section functions of kernel32 are those of ntdll
;    (kernel32.src).
;
;--

#include "kxarm.h"

        TEXTAREA

;++
;
; LONG
; InterlockedIncrement (
;    IN PLONG Addend
;    )
;
; Routine Description:
;
;    This function increments the addend and returns the updated value.
;
;--

        LEAF_ENTRY InterlockedIncrement
        mov     r2, r0
        dmb
10      ldrex   r0, [r2]
        adds    r0, r0, #1
        strex   r1, r0, [r2]
        cmp     r1, #0
        bne     %B10
        dmb
        bx      lr
        LEAF_END InterlockedIncrement

;++
;
; LONG
; InterlockedDecrement (
;    IN PLONG Addend
;    )
;
; Routine Description:
;
;    This function decrements the addend and returns the updated value.
;
;--

        LEAF_ENTRY InterlockedDecrement
        mov     r2, r0
        dmb
10      ldrex   r0, [r2]
        subs    r0, r0, #1
        strex   r1, r0, [r2]
        cmp     r1, #0
        bne     %B10
        dmb
        bx      lr
        LEAF_END InterlockedDecrement

;++
;
; LONG
; InterlockedExchange (
;    IN OUT LPLONG Target,
;    IN LONG Value
;    )
;
; Routine Description:
;
;    This function exchanges a longword with the target and returns the
;    value of the target.
;
;--

        LEAF_ENTRY InterlockedExchange
        mov     r2, r0
        dmb
10      ldrex   r0, [r2]
        strex   r3, r1, [r2]
        cmp     r3, #0
        bne     %B10
        dmb
        bx      lr
        LEAF_END InterlockedExchange

;++
;
; PVOID
; InterlockedCompareExchange (
;    IN OUT PVOID *Destination,
;    IN PVOID Exchange,
;    IN PVOID Comperand
;    )
;
; Routine Description:
;
;    This function stores the exchange value in the destination if the
;    destination holds the comperand, and returns the initial value of the
;    destination.
;
;--

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

;++
;
; LONG
; InterlockedExchangeAdd (
;    IN PLONG Addend,
;    IN LONG Increment
;    )
;
; Routine Description:
;
;    This function adds the increment to the addend and returns the initial
;    value of the addend.
;
;--

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

        END
