/*++

Module Name:

    fwtime.c

Abstract:

    This module implements the ARC firmware time operations for the Raspberry
    Pi 2 (the ARM FW\MIPS\FWTIME.C). The Pi has no realtime clock, so the time
    is that of a PC whose CMOS battery is flat: the date this firmware was
    built on, midnight, plus the time since power on. The relative time counts
    the seconds of the 1 MHz system timer since power on.

--*/

#include "fwp.h"
#include <string.h>

#define LEAP(y) ((y) % 4 == 0 && ((y) % 100 != 0 || (y) % 400 == 0))

static UCHAR MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static TIME_FIELDS FwTime;
static ULONG FwBuildDays;
static ULONG FwSeconds;
static ULONG FwMicroseconds;
static ULONG FwLastCount;

ULONG
FwGetRelativeTime (
    VOID
    );

PTIME_FIELDS
FwGetTime (
    VOID
    )

/*++

Routine Description:

    This routine returns the build date of the firmware plus the time since
    power on, January 1 1970 being a Thursday.

--*/

{
    ULONG Seconds = FwGetRelativeTime();
    ULONG Days = FwBuildDays + Seconds / 86400;
    ULONG Year = 1970, Month = 1, Length;

    FwTime.Weekday = (CSHORT)((Days + 4) % 7);
    while (Days >= (Length = 365 + LEAP(Year))) {
        Days -= Length;
        Year += 1;
    }
    while (Days >= (Length = MonthDays[Month - 1] + (Month == 2 ? LEAP(Year) : 0))) {
        Days -= Length;
        Month += 1;
    }

    FwTime.Year = (CSHORT)Year;
    FwTime.Month = (CSHORT)Month;
    FwTime.Day = (CSHORT)(Days + 1);
    FwTime.Hour = (CSHORT)(Seconds / 3600 % 24);
    FwTime.Minute = (CSHORT)(Seconds / 60 % 60);
    FwTime.Second = (CSHORT)(Seconds % 60);
    FwTime.Milliseconds = 0;
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

static ULONG
FwDaysOfBuildDate (
    VOID
    )

/*++

Routine Description:

    This routine returns the days from January 1 1970 to the __DATE__ this
    firmware was built on ("Mmm dd yyyy").

--*/

{
    static CHAR Date[] = __DATE__, Months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    ULONG Year = (Date[7] - '0') * 1000 + (Date[8] - '0') * 100 + (Date[9] - '0') * 10 + Date[10] - '0';
    ULONG Day = (Date[4] == ' ' ? 0 : Date[4] - '0') * 10 + Date[5] - '0';
    ULONG Month = 0, Days = Day - 1, Index;

    while (Month < 11 && strncmp(&Months[Month * 3], Date, 3) != 0) {
        Month += 1;
    }
    for (Index = 0; Index < Month; Index += 1) {
        Days += MonthDays[Index] + (Index == 1 ? LEAP(Year) : 0);
    }
    for (Index = 1970; Index < Year; Index += 1) {
        Days += 365 + LEAP(Index);
    }
    return Days;
}

VOID
FwTimeInitialize (
    VOID
    )
{
    SYSTEM_BLOCK->FirmwareVector[GetTimeRoutine] = (PVOID)FwGetTime;
    SYSTEM_BLOCK->FirmwareVector[GetRelativeTimeRoutine] = (PVOID)FwGetRelativeTime;
    FwBuildDays = FwDaysOfBuildDate();
    FwLastCount = FwGetMicroseconds();
    FwSeconds = FwLastCount / 1000000;
    FwMicroseconds = FwLastCount % 1000000;
}
