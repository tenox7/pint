/*++

Module Name:

    intrlock.c

Abstract:

    Executive interlocked operations for ARM (the ARM EX/MIPS/X4INTRLK.S).
    These may be called at any IRQL and from interrupt service routines, so
    each sequence runs with interrupts disabled; on a multiprocessor build
    the supplied spin lock would additionally be held. The single-word
    increment/decrement/exchange forms are ke/interlock.S (LDREX/STREX).

Environment:

    Kernel mode only, uniprocessor.

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

ULONG
ExInterlockedAddUlong (
    IN PULONG Addend,
    IN ULONG Increment,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    ULONG Value = *Addend;

    *Addend = Value + Increment;
    ExpRestoreInterrupts(Cpsr);
    return Value;
}

LARGE_INTEGER
ExInterlockedAddLargeInteger (
    IN PLARGE_INTEGER Addend,
    IN LARGE_INTEGER Increment,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    LARGE_INTEGER Value = *Addend;

    Addend->QuadPart = Value.QuadPart + Increment.QuadPart;
    ExpRestoreInterrupts(Cpsr);
    return Value;
}

VOID
ExInterlockedAddLargeStatistic (
    IN PLARGE_INTEGER Addend,
    IN ULONG Increment
    )
{
    ULONG Cpsr = ExpDisableInterrupts();

    Addend->QuadPart += Increment;
    ExpRestoreInterrupts(Cpsr);
}

PLIST_ENTRY
ExInterlockedInsertHeadList (
    IN PLIST_ENTRY ListHead,
    IN PLIST_ENTRY ListEntry,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    PLIST_ENTRY First = ListHead->Flink;

    ListEntry->Flink = First;
    ListEntry->Blink = ListHead;
    First->Blink = ListEntry;
    ListHead->Flink = ListEntry;
    ExpRestoreInterrupts(Cpsr);
    return (First == ListHead) ? NULL : First;
}

PLIST_ENTRY
ExInterlockedInsertTailList (
    IN PLIST_ENTRY ListHead,
    IN PLIST_ENTRY ListEntry,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    PLIST_ENTRY Last = ListHead->Blink;

    ListEntry->Flink = ListHead;
    ListEntry->Blink = Last;
    Last->Flink = ListEntry;
    ListHead->Blink = ListEntry;
    ExpRestoreInterrupts(Cpsr);
    return (Last == ListHead) ? NULL : Last;
}

PLIST_ENTRY
ExInterlockedRemoveHeadList (
    IN PLIST_ENTRY ListHead,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    PLIST_ENTRY Entry = ListHead->Flink;

    if (Entry == ListHead) {
        Entry = NULL;

    } else {
        ListHead->Flink = Entry->Flink;
        Entry->Flink->Blink = ListHead;
    }

    ExpRestoreInterrupts(Cpsr);
    return Entry;
}

PSINGLE_LIST_ENTRY
ExInterlockedPopEntryList (
    IN PSINGLE_LIST_ENTRY ListHead,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    PSINGLE_LIST_ENTRY First = ListHead->Next;

    if (First != NULL) {
        ListHead->Next = First->Next;
    }

    ExpRestoreInterrupts(Cpsr);
    return First;
}

PSINGLE_LIST_ENTRY
ExInterlockedPushEntryList (
    IN PSINGLE_LIST_ENTRY ListHead,
    IN PSINGLE_LIST_ENTRY ListEntry,
    IN PKSPIN_LOCK Lock
    )
{
    ULONG Cpsr = ExpDisableInterrupts();
    PSINGLE_LIST_ENTRY First = ListHead->Next;

    ListEntry->Next = First;
    ListHead->Next = ListEntry;
    ExpRestoreInterrupts(Cpsr);
    return First;
}
