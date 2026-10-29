/*++

Copyright (c) 2026

Module Name:

    timindex.c

Abstract:

    KE/ARM timer table index computation - the C port of KE/MIPS/TIMINDEX.S.
    Computes the timer table index for a timer object and stores its due time:

        Due Time = Current Time - Interval        (Interval is relative/negative)
        Index    = (Due Time / Maximum Time) & (TIMER_TABLE_SIZE - 1)

    The division is performed by reciprocal multiplication (KiTimeIncrementReciprocal
    / KiTimeIncrementShiftCount, computed by KiComputeReciprocal), exactly as the
    MIPS assembler does: take the high 64 bits of (DueTime * Reciprocal), shift
    right by the shift count, and mask. The 64x64->128 high part and the final
    shift are done with native 32-bit multiplies/shifts (no 64-bit divide), so
    no compiler runtime support is required.

Environment:

    Kernel mode.

--*/

#include "ki.h"

ULONG
KiComputeTimerTableIndex (
    IN LARGE_INTEGER Interval,
    IN LARGE_INTEGER CurrentTime,
    IN PKTIMER Timer
    )
{
    ULONGLONG Due;
    ULONGLONG Recip;
    ULONG dlo, dhi, rlo, rhi;
    ULONGLONG p0, p1, p2, p3, mid, hi;
    ULONG hilo, hihi, sc, result;

    //
    // Due time = current time - interval; store it in the timer object.
    //

    Due = (ULONGLONG)CurrentTime.QuadPart - (ULONGLONG)Interval.QuadPart;
    Timer->DueTime.QuadPart = (LONGLONG)Due;

    //
    // High 64 bits of (Due * Reciprocal), accumulated from 32x32->64 partials.
    //

    Recip = (ULONGLONG)KiTimeIncrementReciprocal.QuadPart;
    dlo = (ULONG)Due;  dhi = (ULONG)(Due >> 32);
    rlo = (ULONG)Recip; rhi = (ULONG)(Recip >> 32);

    p0 = (ULONGLONG)dlo * rlo;
    p1 = (ULONGLONG)dlo * rhi;
    p2 = (ULONGLONG)dhi * rlo;
    p3 = (ULONGLONG)dhi * rhi;

    mid = (p0 >> 32) + (ULONG)p1 + (ULONG)p2;
    hi  = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);

    //
    // Shift the high part right by the shift count (1..31) and mask to the
    // table size.
    //

    sc = (ULONG)(UCHAR)KiTimeIncrementShiftCount;
    hilo = (ULONG)hi;  hihi = (ULONG)(hi >> 32);
    result = (hilo >> sc) | (hihi << (32 - sc));

    return result & (TIMER_TABLE_SIZE - 1);
}
