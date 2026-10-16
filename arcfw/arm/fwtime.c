/*++

Module Name:

    fwtime.c

Abstract:

    This module implements the ARC firmware time operations for the Raspberry
    Pi 2 (the ARM FW\MIPS\FWTIME.C). The Pi has no realtime clock, so the time
    is never valid and is returned as all zeros, as the Jazz firmware does
    when its clock is invalid. The relative time counts the seconds of the 1 MHz
    system timer since power on.

--*/

#include "fwp.h"


static TIME_FIELDS FwTime;
static ULONG FwSeconds;
static ULONG FwMicroseconds;
static ULONG FwLastCount;

PTIME_FIELDS
FwGetTime (
    VOID
    )
{
    RtlZeroMemory(&FwTime, sizeof(FwTime));
    return &FwTime;
}

ULONG
FwGetRelativeTime (
    VOID
    )

/*++

Routine Description:

    This routine returns the time in seconds since the system was powered on,
    counting the wraps of the 32-bit microsecond counter.

--*/

{
    ULONG Now = FwGetMicroseconds();

    FwMicroseconds += Now - FwLastCount;
    FwLastCount = Now;
    FwSeconds += FwMicroseconds / 1000000;
    FwMicroseconds %= 1000000;
    return FwSeconds;
}

VOID
FwTimeInitialize (
    VOID
    )
{
    SYSTEM_BLOCK->FirmwareVector[GetTimeRoutine] = (PVOID)FwGetTime;
    SYSTEM_BLOCK->FirmwareVector[GetRelativeTimeRoutine] = (PVOID)FwGetRelativeTime;
    FwLastCount = FwGetMicroseconds();
    FwSeconds = FwLastCount / 1000000;
    FwMicroseconds = FwLastCount % 1000000;
}
