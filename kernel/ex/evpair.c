/*++

Module Name:

    evpair.c

Abstract:

    The thread event pair services for ARM (the ARM EX/MIPS/EVPAIR.S): set one
    event of the client/server event pair referenced by the current thread
    and wait on the other, finishing in the dispatcher's optimized
    KiSetServerWaitClientEvent. Both assume they are called from user mode.

Environment:

    Kernel mode only.

--*/

#include "exp.h"

NTSTATUS
NtSetLowWaitHighThread (
    VOID
    )
{
    PEEVENT_PAIR EventPair = PsGetCurrentThread()->EventPair;

    if (EventPair == NULL) {
        return STATUS_NO_EVENT_PAIR;
    }

    return KiSetServerWaitClientEvent(&EventPair->KernelEventPair.EventLow,
                                      &EventPair->KernelEventPair.EventHigh,
                                      UserMode);
}

NTSTATUS
NtSetHighWaitLowThread (
    VOID
    )
{
    PEEVENT_PAIR EventPair = PsGetCurrentThread()->EventPair;

    if (EventPair == NULL) {
        return STATUS_NO_EVENT_PAIR;
    }

    return KiSetServerWaitClientEvent(&EventPair->KernelEventPair.EventHigh,
                                      &EventPair->KernelEventPair.EventLow,
                                      UserMode);
}
