/*++

Copyright (c) 2026

Module Name:

    linkdata.c

Abstract:

    Executive DATA globals in the link closure (exec-link-probe.sh) whose genuine
    defining file (IO/IODATA.C, CACHE/*) is not yet compiled. They MUST be real
    data (the IO manager / executive WRITE to the locks during init), so they
    cannot be left to the auto-stub fixed point, which would emit them as
    functions. Types are taken from the headers that declare them (IOP.H: extern
    KSPIN_LOCK), so a mismatch fails loudly. Each is replaced by its genuine
    definition once its owning file compiles.

Environment:

    Kernel mode (executive bring-up).

--*/

#include "ki.h"

//
// I/O manager spin locks (IOP.H: extern KSPIN_LOCK ...; defined in IODATA.C).
// Initialized by KeInitializeSpinLock during IoInitSystem; zero is the unowned
// state, so a zeroed definition is also a safe pre-init value.
//

KSPIN_LOCK IoStatisticsLock;
KSPIN_LOCK IopCancelSpinLock;
KSPIN_LOCK IopCompletionLock;
KSPIN_LOCK IopDatabaseLock;
KSPIN_LOCK IopErrorLogAllocationLock;
KSPIN_LOCK IopErrorLogLock;
KSPIN_LOCK IopFastLockSpinLock;
KSPIN_LOCK IopLargeIrpLock;
KSPIN_LOCK IopMdlLock;
KSPIN_LOCK IopSmallIrpLock;
KSPIN_LOCK IopTimerLock;
KSPIN_LOCK IopVpbSpinLock;

//
// Cache manager statistics (CACHE/*). Touched only after CcInitializeCacheManager
// (a Phase 1 step), so unused during the Phase 0 bring-up; CcMissCounter points
// at a real word so an incidental dereference does not fault.
//

ULONG CcDataFlushes;
ULONG CcDataPages;
static ULONG CcMissCounterStorage;
PULONG CcMissCounter = &CcMissCounterStorage;
