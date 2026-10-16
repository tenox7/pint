/*++

Module Name:

    gettick.c

Abstract:

    NtGetTickCount for ARM (the ARM EX/MIPS/GETTICK.S): the tick count scaled
    to milliseconds by the 8.24 fixed-point multiplier computed at Phase 0
    (ExComputeTickCountMultiplier).

Environment:

    Kernel mode only.

--*/

#include "exp.h"

#undef NtGetTickCount

ULONG
NtGetTickCount (
    VOID
    )
{
    return (ULONG)(((ULONGLONG)KeTickCount.LowPart * ExpTickCountMultiplier) >> 24);
}
