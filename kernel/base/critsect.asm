;++
;
; Module Name:
;
;    critsect.asm
;
; Abstract:
;
;    Interlocked increment and decrement support: the ARM port of
;    BASE\CLIENT\MIPS\CRITSECT.S, the Win32 functions that support user mode
;    critical sections. The counts are updated with ldrex/strex (the MIPS
;    ll/sc); the unique id of the current thread comes from its TEB, which
;    the user read only thread id register holds.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  RtlpWaitForCriticalSection
        IMPORT  RtlpUnWaitCriticalSection

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
; NTSTATUS
; EnterCriticalSection (
;    IN PRTL_CRITICAL_SECTION CriticalSection
;    )
;
; Routine Description:
;
;    This function enters a critical section: it takes it if it is free,
;    counts a recursive entry by its owner, or waits for it.
;
;    N.B. This function is duplicated in the runtime library.
;
;--

        NESTED_ENTRY EnterCriticalSection
        PROLOG_PUSH {r4-r6, lr}
        add     r3, r0, #CsLockCount
10      ldrex   r1, [r3]                        ; increment the lock count
        adds    r1, r1, #1
        strex   r2, r1, [r3]
        cmp     r2, #0
        bne     %B10
        dmb
        CP_READ r2, 15, 0, 13, 0, 3             ; current TEB
        ldr     r2, [r2, #TeClientId + 4]       ; current thread unique id
        cbnz    r1, %F20
        str     r2, [r0, #CsOwningThread]       ; not owned: set the owner
        movs    r0, #STATUS_SUCCESS
        EPILOG_POP {r4-r6, pc}

20      ldr     r1, [r0, #CsOwningThread]
        cmp     r1, r2
        bne     %F30
        ldr     r1, [r0, #CsRecursionCount]     ; owned by this thread: recursion
        adds    r1, r1, #1
        str     r1, [r0, #CsRecursionCount]
        movs    r0, #STATUS_SUCCESS
        EPILOG_POP {r4-r6, pc}

30      mov     r4, r0                          ; owned by another thread: wait
        mov     r5, r2
        bl      RtlpWaitForCriticalSection
        str     r5, [r4, #CsOwningThread]
        movs    r0, #STATUS_SUCCESS
        EPILOG_POP {r4-r6, pc}
        NESTED_END EnterCriticalSection

;++
;
; NTSTATUS
; LeaveCriticalSection (
;    IN PRTL_CRITICAL_SECTION CriticalSection
;    )
;
; Routine Description:
;
;    This function leaves a critical section: it undoes a recursive entry, or
;    releases the section and wakes a waiter if there is one.
;
;    N.B. This function is duplicated in the runtime library.
;
;--

        NESTED_ENTRY LeaveCriticalSection
        PROLOG_PUSH {r4, lr}
        add     r3, r0, #CsLockCount
        ldr     r1, [r0, #CsRecursionCount]
        subs    r1, r1, #1
        bpl     %F30
        movs    r2, #0
        str     r2, [r0, #CsOwningThread]       ; no longer owned
        dmb
20      ldrex   r1, [r3]                        ; decrement the lock count
        subs    r1, r1, #1
        strex   r2, r1, [r3]
        cmp     r2, #0
        bne     %B20
        cmp     r1, #0
        blt     %F50                            ; no waiter
        bl      RtlpUnWaitCriticalSection
        movs    r0, #STATUS_SUCCESS
        EPILOG_POP {r4, pc}

30      str     r1, [r0, #CsRecursionCount]     ; still owned
40      ldrex   r1, [r3]
        subs    r1, r1, #1
        strex   r2, r1, [r3]
        cmp     r2, #0
        bne     %B40
50      movs    r0, #STATUS_SUCCESS
        EPILOG_POP {r4, pc}
        NESTED_END LeaveCriticalSection

        END
