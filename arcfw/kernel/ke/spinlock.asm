;++
;
; Module Name:
;
;    spinlock.asm
;
; Abstract:
;
;    Spin locks: the ARM port of KE\MIPS\XXSPINLK.S. The kernel is built for
;    a uniprocessor (NT_UP): raising IRQL to DISPATCH_LEVEL excludes every
;    other user of a spin lock, so acquiring an executive spin lock is
;    raising IRQL and the lock itself is not touched, as in the NT_UP build
;    of the MIPS routines.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  KeRaiseIrql
        IMPORT  KeLowerIrql

        TEXTAREA

;
; VOID
; KeInitializeSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;

        LEAF_ENTRY KeInitializeSpinLock
        movs    r1, #0
        str     r1, [r0]                        ; clear spin lock value
        bx      lr
        LEAF_END KeInitializeSpinLock

;
; VOID
; KeAcquireSpinLock (
;    IN PKSPIN_LOCK SpinLock,
;    OUT PKIRQL OldIrql
;    )
;
; Raise IRQL to DISPATCH_LEVEL and acquire the spin lock.
;

        LEAF_ENTRY KeAcquireSpinLock
        movs    r0, #DISPATCH_LEVEL
        b       KeRaiseIrql
        LEAF_END KeAcquireSpinLock

;
; VOID
; KeReleaseSpinLock (
;    IN PKSPIN_LOCK SpinLock,
;    IN KIRQL OldIrql
;    )
;
; Release the spin lock and lower IRQL to its previous value.
;

        LEAF_ENTRY KeReleaseSpinLock
        mov     r0, r1
        b       KeLowerIrql
        LEAF_END KeReleaseSpinLock

;
; BOOLEAN
; KeTryToAcquireSpinLock (
;    IN PKSPIN_LOCK SpinLock,
;    OUT PKIRQL OldIrql
;    )
;
; Raise IRQL to DISPATCH_LEVEL and try to acquire the spin lock, which on a
; uniprocessor always succeeds.
;

        NESTED_ENTRY KeTryToAcquireSpinLock
        PROLOG_PUSH {r4, lr}
        movs    r0, #DISPATCH_LEVEL
        bl      KeRaiseIrql
        movs    r0, #1                          ; TRUE
        EPILOG_POP {r4, pc}
        NESTED_END KeTryToAcquireSpinLock

;
; VOID
; KiAcquireSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;
; VOID
; KiReleaseSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;
; Acquire or release a kernel spin lock at the current IRQL, DISPATCH_LEVEL
; or above.
;

        LEAF_ENTRY KiAcquireSpinLock
        ALTERNATE_ENTRY KeAcquireSpinLockAtDpcLevel
        ALTERNATE_ENTRY KiReleaseSpinLock
        ALTERNATE_ENTRY KeReleaseSpinLockFromDpcLevel
        bx      lr
        LEAF_END KiAcquireSpinLock

;
; BOOLEAN
; KiTryToAcquireSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;
; Try to acquire a kernel spin lock at the current IRQL.
;

        LEAF_ENTRY KiTryToAcquireSpinLock
        movs    r0, #1                          ; TRUE
        bx      lr
        LEAF_END KiTryToAcquireSpinLock

        END
