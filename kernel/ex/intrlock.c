/*++

Module Name:

    intrlock.c

Abstract:

    Executive interlocked operations for ARM (the ARM EX/MIPS/X4INTRLK.S).
    These may be called at any IRQL and from interrupt service routines, so
    each sequence runs with interrupts disabled and, on a multiprocessor,
    holds the supplied spin lock. The single-word increment/decrement/exchange
    forms are ke/interlock.asm (LDREX/STREX), and a large statistic is added
    with them, as it needs no lock.

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

//
// Disable interrupts and acquire the spin lock; release it and restore
// interrupts.
//

#define ExpLock(Lock, Enable) \
    (Enable) = KiDisableInterrupts(); \
    KiAcquireSpinLock(Lock)

#define ExpUnlock(Lock, Enable) \
    KiReleaseSpinLock(Lock); \
    KiRestoreInterrupts(Enable)

ULONG
ExInterlockedAddUlong (
    IN PULONG Addend,
    IN ULONG Increment,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    ULONG Value;

    ExpLock(Lock, Enable);
    Value = *Addend;
    *Addend = Value + Increment;
    ExpUnlock(Lock, Enable);
    return Value;
}

LARGE_INTEGER
ExInterlockedAddLargeInteger (
    IN PLARGE_INTEGER Addend,
    IN LARGE_INTEGER Increment,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    LARGE_INTEGER Value;

    ExpLock(Lock, Enable);
    Value = *Addend;
    Addend->QuadPart = Value.QuadPart + Increment.QuadPart;
    ExpUnlock(Lock, Enable);
    return Value;
}

VOID
ExInterlockedAddLargeStatistic (
    IN PLARGE_INTEGER Addend,
    IN ULONG Increment
    )

/*++

Routine Description:

    This function adds a value to a large statistic: interlocked to the low
    part, and a carry out of it interlocked to the high part.

--*/

{
    ULONG Low = (ULONG)InterlockedExchangeAdd((PLONG)&Addend->LowPart, (LONG)Increment);

    if (Low + Increment < Low) {
        InterlockedIncrement(&Addend->HighPart);
    }
}

PLIST_ENTRY
ExInterlockedInsertHeadList (
    IN PLIST_ENTRY ListHead,
    IN PLIST_ENTRY ListEntry,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    PLIST_ENTRY First;

    ExpLock(Lock, Enable);
    First = ListHead->Flink;
    ListEntry->Flink = First;
    ListEntry->Blink = ListHead;
    First->Blink = ListEntry;
    ListHead->Flink = ListEntry;
    ExpUnlock(Lock, Enable);
    return (First == ListHead) ? NULL : First;
}

PLIST_ENTRY
ExInterlockedInsertTailList (
    IN PLIST_ENTRY ListHead,
    IN PLIST_ENTRY ListEntry,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    PLIST_ENTRY Last;

    ExpLock(Lock, Enable);
    Last = ListHead->Blink;
    ListEntry->Flink = ListHead;
    ListEntry->Blink = Last;
    Last->Flink = ListEntry;
    ListHead->Blink = ListEntry;
    ExpUnlock(Lock, Enable);
    return (Last == ListHead) ? NULL : Last;
}

PLIST_ENTRY
ExInterlockedRemoveHeadList (
    IN PLIST_ENTRY ListHead,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    PLIST_ENTRY Entry;

    ExpLock(Lock, Enable);
    Entry = ListHead->Flink;
    if (Entry == ListHead) {
        Entry = NULL;

    } else {
        ListHead->Flink = Entry->Flink;
        Entry->Flink->Blink = ListHead;
    }

    ExpUnlock(Lock, Enable);
    return Entry;
}

PSINGLE_LIST_ENTRY
ExInterlockedPopEntryList (
    IN PSINGLE_LIST_ENTRY ListHead,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    PSINGLE_LIST_ENTRY First;

    ExpLock(Lock, Enable);
    First = ListHead->Next;
    if (First != NULL) {
        ListHead->Next = First->Next;
    }

    ExpUnlock(Lock, Enable);
    return First;
}

PSINGLE_LIST_ENTRY
ExInterlockedPushEntryList (
    IN PSINGLE_LIST_ENTRY ListHead,
    IN PSINGLE_LIST_ENTRY ListEntry,
    IN PKSPIN_LOCK Lock
    )
{
    BOOLEAN Enable;
    PSINGLE_LIST_ENTRY First;

    ExpLock(Lock, Enable);
    First = ListHead->Next;
    ListEntry->Next = First;
    ListHead->Next = ListEntry;
    ExpUnlock(Lock, Enable);
    return First;
}
