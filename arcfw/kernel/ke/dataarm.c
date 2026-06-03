/*++

Copyright (c) 2026

Module Name:

    dataarm.c

Abstract:

    The ARM analog of MM/MIPS/DATAMIPS.C / MM/ALPHA/DATALPHA.C - the per-arch MM
    data globals. The portable MM declares these "extern" in MM/MI.H (and the
    color tables in miarm.h) and each architecture's DATA<arch>.C defines them;
    there is no ARM version, so they are the data half of the executive link
    closure (exec-link-probe.sh).

    These MUST be real DATA (not function stubs): MmInitSystem and the pool
    builders WRITE to the pool pointers / locks and READ the PTE templates. The
    initializers are benign zero/NULL for the STUB-AND-LINK milestone - MmInitSystem
    re-derives the pool pointers and the PFN/color tables at runtime; the PTE
    templates start {0} (refine to the real ValidKernelPte mask composition from
    DATAMIPS.C once Phase 0 boot shows MM actually consuming them). The types are
    taken verbatim from MI.H / miarm.h so a mismatch fails loudly at compile.

Environment:

    Kernel mode (executive bring-up). Compiled with the MM include path so mi.h /
    miarm.h supply MMPTE, KSPIN_LOCK and the color-table types.

--*/

#include "mi.h"

//
// PTE templates (MI.H: extern MMPTE ...; defined per-arch in DATA<arch>.C).
//

MMPTE ZeroPte = { 0 };
MMPTE ZeroKernelPte = { 0 };
MMPTE ValidKernelPte = { 0 };
MMPTE ValidKernelPde = { 0 };
MMPTE ValidUserPte = { 0 };
MMPTE ValidPdePde = { 0 };
MMPTE DemandZeroPde = { 0 };
MMPTE TransitionPde = { 0 };
MMPTE PrototypePte = { 0 };
MMPTE NoAccessPte = { 0 };

PMMPTE MmCrashDumpPte = NULL;

//
// Pool extents (set by MiInitializeNonPagedPool / MiBuildPagedPool at runtime).
//

PVOID MmNonPagedPoolStart = NULL;       // set by MiInitMachineDependent (ke/initarm.c)
PVOID MmNonPagedPoolEnd = NULL;         // set by MiInitMachineDependent (ke/initarm.c)
//
// MmPagedPoolStart must carry MIGLOBAL.C's init value MM_PAGED_POOL_START: it is NOT
// reassigned at runtime (MiBuildPagedPool reads it to lay out the paged pool). A NULL
// here (this weak def can win over MIGLOBAL when ld does not pull MIGLOBAL.o) builds
// the paged pool at VA 0 -> allocations return ~0x1000 and fault.
//
PVOID MmPagedPoolStart = (PVOID)MM_PAGED_POOL_START;
PVOID MmPagedPoolEnd = NULL;            // set by MiBuildPagedPool

//
// MM spin locks (initialized by KeInitializeSpinLock during MmInitSystem).
//

KSPIN_LOCK MmPfnLock;
KSPIN_LOCK MmAllowWSExpansionLock;
KSPIN_LOCK MmChargeCommitmentLock;

ULONG MmTotalPagesForPagingFile = 0;

//
// Page-color tables (miarm.h declares these extern, R4000-lineage; the PFN
// allocator threads free/modified pages onto them by cache color at runtime).
//

MMCOLOR_TABLES MmFreePagesByColor[2][MM_SECONDARY_COLORS];
MMPRIMARY_COLOR_TABLES MmFreePagesByPrimaryColor[2][MM_MAXIMUM_NUMBER_OF_COLORS];
MMPFNLIST MmModifiedPageListByColor[MM_MAXIMUM_NUMBER_OF_COLORS];
