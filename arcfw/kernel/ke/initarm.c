/*++

Copyright (c) 2026

Module Name:

    initarm.c

Abstract:

    The ARMv7 analog of MM/MIPS/INITMIPS.C MiInitMachineDependent - the machine-
    dependent half of MmInitSystem Phase 0. MmInitSystem (MM/MMINIT.C:382) calls
    MiInitMachineDependent(LoaderBlock) and then immediately allocates from the
    nonpaged pool (ExAllocatePoolWithTag at MMINIT.C:388), so this routine must
    build the nonpaged pool - and wire the PFN database - before returning.

    THE KSEG0-DIRECT POOL (the MIPS model, INITMIPS.C:367-370). The initial
    nonpaged pool is placed in the KSEG0 direct map (a CONTIGUOUS physical chunk
    reserved by ke/mmuarm.c). Because MmNonPagedPoolStart is then a KSEG0 address,
    MiAllocatePoolPages takes the MI_IS_PHYSICAL_ADDRESS branch and derives each
    page's PFN straight from the address (MI_CONVERT_PHYSICAL_TO_PFN) - it never
    reads a PTE, so it sidesteps the HARDWARE_PTE <-> ARMv7-descriptor format wall
    (an NT software PTE packs the PFN at bits 6-29; an ARMv7 small-page descriptor
    at bits 12-31, so reading one as the other yields a wrong PFN). The pool
    *expansion* still lives at a system VA below MM_NONPAGED_POOL_END
    (NonPagedPoolStartVirtual) and uses system PTEs - the MIPS bifurcation
    (Start = KSEG0 alias, End = system VA). Expansion is not exercised in Phase 0.

    PFN DATABASE. MmPfnDatabase is pointed at the database ke/mmuarm.c carved in
    the KSEG0 direct map (ARM_MMPFN is 24 bytes, byte-compatible with the real
    MMPFN: u1.Flink/PteAddress/u2.Blink/ReferenceCount/ValidPteCount/OriginalPte/
    u3 at the same offsets). The pool pages were reserved active in that pass with
    StartOfAllocation/EndOfAllocation (u3.e1 bits 1/2) clear, which is what
    MiAllocatePoolPages sets on allocation.

Environment:

    Kernel mode, Phase 0 system initialization. Compiled with the MM include path
    so mi.h / miarm.h supply MMPTE, MmPfnDatabase and the Mm* pool globals.

--*/

#include "mi.h"

extern void  KiEmit(const char *);
extern void  KiEmitHex(unsigned long);

//
// ke/mmuarm.c (ran in MiArmInitMachineDependent just before ExpInitializeExecutive):
// the carved PFN database, the physical-page accounting, and the reserved
// contiguous KSEG0 pool chunk. MiArmEnsureL2 backs a megabyte's L2 in the PTE
// window so MiGetPteAddress(expansion VA) resolves to a zeroed (invalid) PTE.
//

extern VOID  MiArmEnsureL2(ULONG va);
extern PVOID MiArmGetPfnDatabase(VOID);
extern ULONG MiArmGetPhysPages(VOID);
extern ULONG MiArmGetLowestPage(VOID);
extern ULONG MiArmGetHighestPage(VOID);
extern ULONG MiArmGetFreeCount(VOID);
extern ULONG MiArmGetPoolBasePage(VOID);
extern ULONG MiArmGetPoolPages(VOID);

VOID
MiInitMachineDependent (
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    ULONG poolBasePage = MiArmGetPoolBasePage();
    ULONG poolPages    = MiArmGetPoolPages();
    ULONG initSize     = poolPages << PAGE_SHIFT;
    ULONG poolKseg0    = KSEG0_BASE + (poolBasePage << PAGE_SHIFT);
    ULONG endVa, startVirtual, expandVa, va;

    UNREFERENCED_PARAMETER (LoaderBlock);

    KiEmit("\nMiInitMachineDependent (initarm.c, ARMv7 KSEG0 pool):\n");

    //
    // Wire the PFN database + physical-page accounting (INITMIPS.C does this
    // inline; MmInitializeMemoryLimits leaves the page-count globals 0). These
    // are the values ke/mmuarm.c computed from the loader memory-descriptor list.
    // MmPfnDatabase is the one that MUST be set for the regular pool path:
    // MiAllocatePoolPages does MI_PFN_ELEMENT(MI_CONVERT_PHYSICAL_TO_PFN(BaseVa))
    // and writes the page's StartOfAllocation/EndOfAllocation bits.
    //

    MmPfnDatabase            = (PMMPFN)MiArmGetPfnDatabase();
    MmNumberOfPhysicalPages  = MiArmGetPhysPages();
    MmLowestPhysicalPage     = MiArmGetLowestPage();
    MmHighestPhysicalPage    = MiArmGetHighestPage();

    //
    // MmAvailablePages is DELIBERATELY left 0 for this milestone. MmInitSystem
    // (MMINIT.C:445-457) computes MmResidentAvailablePages = MmAvailablePages -
    // FLUID - MmSystemCacheWsMinimum and returns FALSE when it goes negative -
    // which, with MmAvailablePages==0, it does, so MmInitSystem bails BEFORE the
    // system-cache / paged-pool build (MMINIT.C:486+) and init.c carries on to the
    // NLS snapshot + Ob/Se/Ps. Wiring MmAvailablePages (= MiArmGetFreeCount())
    // removes that early-return and opens the system-cache build, which needs the
    // real MM free lists (so MiRemoveAnyPage works) AND the ARMv7 PDE/PTE self-map
    // reconciliation (the real L1 PDE writes must agree with the PTE-window L2s) -
    // the next milestone. (void) keeps the accessor referenced.
    //

    (void)MiArmGetFreeCount();

    if (poolPages == 0) {
        KiEmit("  *** no KSEG0 pool reservation (mmuarm KI_RUN_EXECUTIVE off?) ***\n");
        return;
    }

    //
    // Lay out the pool. The initial pool is the KSEG0 contiguous chunk; the
    // expansion region is a system VA window ending at MM_NONPAGED_POOL_END.
    // MmNonPagedPoolStart (used by MiAllocatePoolPages + MiInitializeNonPagedPool
    // to build the pool free list) is the KSEG0 alias; NonPagedPoolStartVirtual
    // (passed to MiInitializeNonPagedPool) is the system VA used only for the
    // expansion system PTEs.
    //

    endVa        = (ULONG)MM_NONPAGED_POOL_END;             // 0xFFBE0000
    startVirtual = (endVa - (initSize + 0x100000)) & ~0xFFFFFu;
    expandVa     = startVirtual + initSize;

    MmSizeOfNonPagedPoolInBytes  = initSize;
    MmMaximumNonPagedPoolInBytes = endVa - startVirtual;
    MmNonPagedPoolStart   = (PVOID)poolKseg0;               // KSEG0 alias (physical)
    MmNonPagedPoolEnd     = (PVOID)(endVa - 1);             // system VA (expansion top)
    MmNonPagedSystemStart = (PVOID)startVirtual;
    MmPageAlignedPoolBase[NonPagedPool] = MmNonPagedPoolStart;

    //
    // Back the expansion region's megabytes with empty (zeroed) L2s in the PTE
    // window so MiInitializeNonPagedPool's MiGetPteAddress(expansion) reads
    // Valid==0 and MiInitializeSystemPtes can write the expansion system PTEs
    // there. L1[M] is deliberately NOT wired - the MMU must not walk this region
    // (it holds NT software-format PTEs, not ARMv7 descriptors).
    //

    for (va = expandVa; va < endVa; va += 0x100000)
        MiArmEnsureL2(va);
    MiArmEnsureL2(endVa - PAGE_SIZE);

    KiEmit("  PFN database         : "); KiEmitHex((ULONG)MmPfnDatabase);
    KiEmit("\n  physical pages       : "); KiEmitHex(MmNumberOfPhysicalPages);
    KiEmit("\n  free pages           : "); KiEmitHex(MiArmGetFreeCount());
    KiEmit("\n  nonpaged pool (KSEG0): "); KiEmitHex((ULONG)MmNonPagedPoolStart);
    KiEmit(" .. "); KiEmitHex((ULONG)MmNonPagedPoolStart + initSize);
    KiEmit("\n  pool expansion VA    : "); KiEmitHex(expandVa);
    KiEmit(" .. "); KiEmitHex(endVa);
    KiEmit("\n");

    //
    // Build the genuine portable pool structures: the initial pool's free list is
    // threaded through the KSEG0 pages (MmNonPagedPoolStart); the expansion system
    // PTEs are set up at the virtual start (the parameter).
    //

    KiEmit("  -> MiInitializeNonPagedPool\n");
    MiInitializeNonPagedPool((PVOID)startVirtual);
    KiEmit("  -> InitializePool(NonPagedPool)\n");
    InitializePool(NonPagedPool, 0);
    KiEmit("  MiInitMachineDependent: KSEG0 nonpaged pool ready\n");
}
