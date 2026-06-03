/*++

Copyright (c) 2026

Module Name:

    initarm.c

Abstract:

    The ARMv7 analog of MM/MIPS/INITMIPS.C MiInitMachineDependent - the machine-
    dependent half of MmInitSystem Phase 0. MmInitSystem (MM/MMINIT.C:382) calls
    MiInitMachineDependent(LoaderBlock) and then immediately allocates from the
    nonpaged pool (ExAllocatePoolWithTag at MMINIT.C:388), so this routine must
    build the nonpaged pool before returning.

    This is the FIRST increment: stand up the initial nonpaged pool (x86-style:
    the whole pool in the system VA region, individually paged - physical pages
    need not be contiguous), then call the genuine portable pool builders
    (MiInitializeNonPagedPool + InitializePool). It reuses the proven page-table
    primitives from ke/mmuarm.c (MiArmMapSystemPage / MiArmEnsureL2), which run on
    the self-map MiArmInitMachineDependent already built (KI_MMU_BUILD_TEST) before
    ExpInitializeExecutive. PFN-database wiring (MmPfnDatabase) and the system-PTE
    pool follow in later increments.

Environment:

    Kernel mode, Phase 0 system initialization. Compiled with the MM include path
    so mi.h / miarm.h supply MMPTE and the Mm* pool globals.

--*/

#include "mi.h"

extern void  KiEmit(const char *);
extern void  KiEmitHex(unsigned long);
extern ULONG MiArmMapSystemPage(ULONG va);      // ke/mmuarm.c: map one paged page
extern VOID  MiArmEnsureL2(ULONG va);           // ke/mmuarm.c: back a megabyte's L2

VOID
MiInitMachineDependent (
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    ULONG initSize = 4 * 1024 * 1024;           // 4 MB initial nonpaged pool
    ULONG maxSize  = initSize + 0x100000;       // + 1 MB expansion (system PTEs)
    ULONG startVa, endVa, va, mapped;

    UNREFERENCED_PARAMETER (LoaderBlock);

    KiEmit("\nMiInitMachineDependent (initarm.c, ARMv7):\n");

    //
    // Place the nonpaged pool just below MM_NONPAGED_POOL_END, megabyte-aligned.
    //
    endVa   = (ULONG)MM_NONPAGED_POOL_END;      // 0xFFBE0000
    startVa = (endVa - maxSize) & ~0xFFFFFu;

    MmNonPagedPoolStart   = (PVOID)startVa;
    MmNonPagedPoolEnd     = (PVOID)(endVa - 1);
    MmNonPagedSystemStart = (PVOID)startVa;
    MmSizeOfNonPagedPoolInBytes  = initSize;
    MmMaximumNonPagedPoolInBytes = maxSize;
    MmPageAlignedPoolBase[NonPagedPool] = MmNonPagedPoolStart;

    //
    // Map the INITIAL pool region (startVa .. startVa+initSize) page by page from
    // the free list. These get real ARMv7 descriptors (L1[M]->L2 wired) so the
    // pool memory is accessible.
    //
    mapped = 0;
    for (va = startVa; va < startVa + initSize; va += PAGE_SIZE) {
        if (MiArmMapSystemPage(va) == 0) {
            KiEmit("  *** OUT OF MEMORY mapping pool at "); KiEmitHex(va);
            KiEmit(" ***\n");
            return;
        }
        mapped += 1;
    }

    //
    // Back the EXPANSION region's megabytes with empty (zeroed) L2s so
    // MiInitializeNonPagedPool's MiGetPteAddress(expansion) reads Valid==0 and
    // MiInitializeSystemPtes can write the expansion system PTEs there (L1 NOT
    // wired - those are software PTEs the MMU must not walk yet).
    //
    for (va = startVa + initSize; va < endVa; va += 0x100000)
        MiArmEnsureL2(va);
    MiArmEnsureL2(endVa - PAGE_SIZE);

    KiEmit("  nonpaged pool VA "); KiEmitHex(startVa);
    KiEmit(" .. "); KiEmitHex(endVa);
    KiEmit(" ("); KiEmitHex(mapped); KiEmit(" pages mapped)\n");

    //
    // Build the genuine portable pool structures.
    //
    KiEmit("  -> MiInitializeNonPagedPool\n");
    MiInitializeNonPagedPool(MmNonPagedPoolStart);
    KiEmit("  -> InitializePool(NonPagedPool)\n");
    InitializePool(NonPagedPool, 0);
    KiEmit("  MiInitMachineDependent: nonpaged pool ready\n");
}
