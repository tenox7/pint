;++
;
; Module Name:
;
;    spinlock.asm
;
; Abstract:
;
;    Spin locks: the ARM port of KE\MIPS\XXSPINLK.S. An executive spin lock is
;    acquired at DISPATCH_LEVEL and holds the address of the owning thread, as
;    on MIPS; a kernel spin lock is acquired at the current IRQL, DISPATCH_LEVEL
;    or above. The lock sequences are those of kxspin.h; in the NT_UP build
;    raising IRQL excludes every other user of a lock and the lock itself is
;    not touched.
;
;--

#include "ksarm.h"
#include "kxarm.h"
#include "kxspin.h"

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

        NESTED_ENTRY KeAcquireSpinLock
        PROLOG_PUSH {r4, lr}
        mov     r4, r0
        movs    r0, #DISPATCH_LEVEL
        bl      KeRaiseIrql                     ; raise IRQL, previous IRQL to *OldIrql
        CP_READ r1, 15, 0, 13, 0, 3             ; current thread, the owner
        SPIN_LOCK_ACQUIRE r4, r1, r2
        EPILOG_POP {r4, pc}
        NESTED_END KeAcquireSpinLock

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
        movs    r2, #0
        SPIN_LOCK_RELEASE r0, r2
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
; Raise IRQL to DISPATCH_LEVEL and try to acquire the spin lock. If the lock
; is owned, IRQL is restored and FALSE returned.
;

        NESTED_ENTRY KeTryToAcquireSpinLock
        PROLOG_PUSH {r4, r5, r6, lr}
        mov     r4, r0
        mov     r5, r1
        movs    r0, #DISPATCH_LEVEL
        bl      KeRaiseIrql                     ; raise IRQL, previous IRQL to *OldIrql
        CP_READ r1, 15, 0, 13, 0, 3             ; current thread, the owner
        SPIN_LOCK_TRY r4, r1, r2
        bne     %F10
        movs    r0, #1                          ; TRUE
        EPILOG_POP {r4, r5, r6, pc}
10      ldrb    r0, [r5]                        ; owned: restore the previous IRQL
        bl      KeLowerIrql
        movs    r0, #0                          ; FALSE
        EPILOG_POP {r4, r5, r6, pc}
        NESTED_END KeTryToAcquireSpinLock

;
; VOID
; KiAcquireSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;
; Acquire a kernel spin lock at the current IRQL, DISPATCH_LEVEL or above.
;

        LEAF_ENTRY KiAcquireSpinLock
        ALTERNATE_ENTRY KeAcquireSpinLockAtDpcLevel
        CP_READ r1, 15, 0, 13, 0, 3             ; current thread, the owner
        SPIN_LOCK_ACQUIRE r0, r1, r2
        bx      lr
        LEAF_END KiAcquireSpinLock

;
; VOID
; KiReleaseSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;
; Release a kernel spin lock.
;

        LEAF_ENTRY KiReleaseSpinLock
        ALTERNATE_ENTRY KeReleaseSpinLockFromDpcLevel
        movs    r1, #0
        SPIN_LOCK_RELEASE r0, r1
        bx      lr
        LEAF_END KiReleaseSpinLock

;
; BOOLEAN
; KiTryToAcquireSpinLock (
;    IN PKSPIN_LOCK SpinLock
;    )
;
; Try to acquire a kernel spin lock at the current IRQL.
;

        LEAF_ENTRY KiTryToAcquireSpinLock
        CP_READ r1, 15, 0, 13, 0, 3             ; current thread, the owner
        SPIN_LOCK_TRY r0, r1, r2
        ite     eq
        moveq   r0, #1                          ; TRUE
        movne   r0, #0                          ; FALSE
        bx      lr
        LEAF_END KiTryToAcquireSpinLock

        END
