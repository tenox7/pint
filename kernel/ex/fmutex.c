/*++

Module Name:

    fmutex.c

Abstract:

    Fast mutex support for ARM (the ARM EX/MIPS/FMUTEX.S). A fast mutex is an
    executive object owned at APC_LEVEL; the ownership count is 1 when free and
    is decremented atomically (an interlocked decrement) to acquire. A contended acquirer
    waits on the mutex event; the releaser sets the event and boosts the waiter.

Environment:

    Kernel mode only.

--*/

#include "exp.h"

BOOLEAN
KiDisableInterrupts (
    VOID
    );

VOID
KiRestoreInterrupts (
    IN BOOLEAN Enable
    );

#define ExpDisableInterrupts() KiDisableInterrupts()
#define ExpRestoreInterrupts(Enable) KiRestoreInterrupts((BOOLEAN)(Enable))

PVOID
InterlockedCompareExchange (
    IN OUT PVOID *Destination,
    IN PVOID Exchange,
    IN PVOID Comperand
    );

//
// Decrement the count of a fast mutex if it is free, interlocked with the
// other processors (the MIPS ll/sc of FMUTEX.S).
//

static __inline BOOLEAN
ExpTryDecrementCount (
    IN PFAST_MUTEX FastMutex
    )
{
    LONG Count;

    do {
        Count = FastMutex->Count;
        if (Count <= 0) {
            return FALSE;
        }

    } while ((LONG)InterlockedCompareExchange((PVOID *)&FastMutex->Count,
                                              (PVOID)(Count - 1),
                                              (PVOID)Count) != Count);

    return TRUE;
}

VOID
ExAcquireFastMutex (
    IN PFAST_MUTEX FastMutex
    )

/*++

Routine Description:

    This function acquires ownership of a fast mutex and raises IRQL to
    APC level.

--*/

{
    KIRQL OldIrql;

    KeRaiseIrql(APC_LEVEL, &OldIrql);
    if (InterlockedDecrement(&FastMutex->Count) < 0) {
        FastMutex->Contention += 1;
        KeWaitForSingleObject(&FastMutex->Event, Executive, KernelMode, FALSE, NULL);
    }

    FastMutex->OldIrql = OldIrql;
    FastMutex->Owner = KeGetCurrentThread();
}

VOID
ExReleaseFastMutex (
    IN PFAST_MUTEX FastMutex
    )

/*++

Routine Description:

    This function releases ownership of a fast mutex and lowers IRQL to its
    previous level. A waiter is released and boosted to the releaser's
    priority.

--*/

{
    KIRQL OldIrql = (KIRQL)FastMutex->OldIrql;

    FastMutex->Owner = NULL;
    if (InterlockedIncrement(&FastMutex->Count) != 1) {
        KeSetEventBoostPriority(&FastMutex->Event, &FastMutex->Owner);
    }

    KeLowerIrql(OldIrql);
}

BOOLEAN
ExTryToAcquireFastMutex (
    IN PFAST_MUTEX FastMutex
    )

/*++

Routine Description:

    This function attempts to acquire ownership of a fast mutex and, if
    successful, raises IRQL to APC level. Acquisition and the IRQL raise are
    atomic with respect to interrupts, as on MIPS.

--*/

{
    ULONG Cpsr;
    KIRQL OldIrql;

    Cpsr = ExpDisableInterrupts();
    if (!ExpTryDecrementCount(FastMutex)) {
        ExpRestoreInterrupts(Cpsr);
        return FALSE;
    }

    KeRaiseIrql(APC_LEVEL, &OldIrql);
    ExpRestoreInterrupts(Cpsr);
    FastMutex->OldIrql = OldIrql;
    FastMutex->Owner = KeGetCurrentThread();
    return TRUE;
}

VOID
ExAcquireFastMutexUnsafe (
    IN PFAST_MUTEX FastMutex
    )

/*++

Routine Description:

    This function acquires ownership of a fast mutex without raising IRQL.

--*/

{
    if (InterlockedDecrement(&FastMutex->Count) < 0) {
        FastMutex->Contention += 1;
        KeWaitForSingleObject(&FastMutex->Event, Executive, KernelMode, FALSE, NULL);
    }

    FastMutex->Owner = KeGetCurrentThread();
}

VOID
ExReleaseFastMutexUnsafe (
    IN PFAST_MUTEX FastMutex
    )

/*++

Routine Description:

    This function releases ownership of a fast mutex without changing IRQL.

--*/

{
    FastMutex->Owner = NULL;
    if (InterlockedIncrement(&FastMutex->Count) != 1) {
        KeSetEventBoostPriority(&FastMutex->Event, &FastMutex->Owner);
    }
}

BOOLEAN
ExTryToAcquireFastMutexUnsafe (
    IN PFAST_MUTEX FastMutex
    )

/*++

Routine Description:

    This function attempts to acquire ownership of a fast mutex without
    raising IRQL.

--*/

{
    if (!ExpTryDecrementCount(FastMutex)) {
        return FALSE;
    }

    FastMutex->Owner = KeGetCurrentThread();
    return TRUE;
}
