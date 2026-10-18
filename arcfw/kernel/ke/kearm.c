/*++

Copyright (c) 2026

Module Name:

    kearm.c

Abstract:

    ARMv7-A kernel architecture glue for the NT 3.5 / Raspberry Pi 2 port: the
    system startup C entry, which initializes the boot processor's PCR from the
    structures the OS Loader allocated, turns on the caches and calls
    KiInitializeKernel (the KE/MIPS X4START.S KiSystemBegin sequence), and the
    APC software interrupt.

Environment:

    Kernel mode.

--*/

#include "ki.h"

extern VOID KiArmInitializeVectors(VOID);
extern VOID KiArmStartTranslation(PLOADER_PARAMETER_BLOCK LoaderBlock);

//
// KiApcInterrupt - the APC_LEVEL software interrupt (PCR->InterruptRoutine
// [APC_LEVEL]): deliver kernel mode APCs through the genuine KiDeliverApc.
// User mode APCs are delivered as a thread returns to user mode (KiTrapExit),
// so no frames are needed (KE/MIPS XXAPCINT.S builds none for kernel mode).
//

VOID KiApcInterrupt(VOID)
{
    KiDeliverApc(KernelMode, NULL, NULL);
}

//
// Turn on the caches, which the ARC firmware leaves off: invalidate the data and
// unified caches by set and way to the level of coherency, the instruction cache,
// the branch predictor and the TB, then enable the data and instruction caches
// and branch prediction. The translation table walks are cached as well
// (tbarm.c). The interlocked operations need the caches on a Cortex-A53, which
// makes exclusive accesses to uncached memory through a global monitor the
// BCM2837 does not have.
//

#define SCTLR_C     (1 << 2)
#define SCTLR_Z     (1 << 11)
#define SCTLR_I     (1 << 12)

static VOID KiArmEnableCaches(VOID)
{
    ULONG Clidr = _MoveFromCoprocessor(15, 1, 0, 0, 1);
    ULONG Level, Ccsidr, LineShift, Ways, Sets, WayShift, Way, Set;

    for (Level = 0; Level < ((Clidr >> 24) & 7); Level += 1) {
        if (((Clidr >> (Level * 3)) & 7) < 2) {
            continue;
        }

        _MoveToCoprocessor(Level << 1, 15, 2, 0, 0, 0);
        __isb(_ARM_BARRIER_SY);
        Ccsidr = _MoveFromCoprocessor(15, 1, 0, 0, 0);
        LineShift = (Ccsidr & 7) + 4;
        Ways = (Ccsidr >> 3) & 0x3FF;
        Sets = (Ccsidr >> 13) & 0x7FFF;
        WayShift = Ways != 0 ? _CountLeadingZeros(Ways) : 0;
        for (Way = 0; Way <= Ways; Way += 1) {
            for (Set = 0; Set <= Sets; Set += 1) {
                _MoveToCoprocessor((Way << WayShift) | (Set << LineShift) | (Level << 1),
                                   15, 0, 7, 6, 2);
            }
        }
    }

    _MoveToCoprocessor(0, 15, 2, 0, 0, 0);
    __dsb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(0, 15, 0, 7, 5, 0);
    _MoveToCoprocessor(0, 15, 0, 7, 5, 6);
    _MoveToCoprocessor(0, 15, 0, 8, 7, 0);
    __dsb(_ARM_BARRIER_SY);
    __isb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(_MoveFromCoprocessor(15, 0, 1, 0, 0) | SCTLR_C | SCTLR_Z | SCTLR_I,
                       15, 0, 1, 0, 0);
    __isb(_ARM_BARRIER_SY);
}

//
// System startup, called from KiSystemStartup (armstart.asm) with the loader
// parameter block: install the exception vectors, initialize the PCR from the
// structures the OS Loader allocated, start translation, turn on the caches,
// and initialize the kernel (the KE/MIPS X4START.S KiSystemBegin sequence).
// Returns as the idle thread at HIGH_LEVEL.
//

VOID KiArmInitialize(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPCR Pcr = (PKPCR)(KSEG0_BASE | (LoaderBlock->u.Arm.PcrPage << PAGE_SHIFT));
    ULONG Midr;

    KiArmInitializeVectors();
    KiSetProcessorControlRegion(Pcr);

    //
    // Initialize the PCR from the structures and stacks the OS Loader allocated
    // (BlSetupForNt), as KE/MIPS X4START.S KiSystemBegin does.
    //

    Midr = _MoveFromCoprocessor(15, 0, 0, 0, 0);
    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;
    Pcr->Prcb = (PKPRCB)LoaderBlock->Prcb;
    Pcr->InitialStack = (PVOID)LoaderBlock->KernelStack;
    Pcr->PanicStack = (PVOID)LoaderBlock->u.Arm.PanicStack;
    Pcr->InterruptStack = LoaderBlock->u.Arm.InterruptStack;
    Pcr->CurrentThread = (PKTHREAD)LoaderBlock->Thread;
    Pcr->Teb = NULL;
    _MoveToCoprocessor(0, 15, 0, 13, 0, 3);
    Pcr->ProcessorId = Midr;
    Pcr->Number = 0;
    Pcr->CurrentIrql = HIGH_LEVEL;
    Pcr->CachePolicy = CACHED_POLICY;
    Pcr->AlignedCachePolicy = CACHED_POLICY << 3;
    Pcr->FirstLevelDcacheSize = LoaderBlock->u.Arm.FirstLevelDcacheSize;
    Pcr->FirstLevelDcacheFillSize = LoaderBlock->u.Arm.FirstLevelDcacheFillSize;
    Pcr->FirstLevelIcacheSize = LoaderBlock->u.Arm.FirstLevelIcacheSize;
    Pcr->FirstLevelIcacheFillSize = LoaderBlock->u.Arm.FirstLevelIcacheFillSize;
    Pcr->SecondLevelDcacheSize = LoaderBlock->u.Arm.SecondLevelDcacheSize;
    Pcr->SecondLevelDcacheFillSize = LoaderBlock->u.Arm.SecondLevelDcacheFillSize;
    Pcr->SecondLevelIcacheSize = LoaderBlock->u.Arm.SecondLevelIcacheSize;
    Pcr->SecondLevelIcacheFillSize = LoaderBlock->u.Arm.SecondLevelIcacheFillSize;

    //
    // The data and instruction cache fill sizes and alignments: those of the
    // second level caches if there are, else of the first (the DMA alignment
    // of the HAL and the default alignment requirement of a device).
    //

    Pcr->DcacheFillSize = Pcr->SecondLevelDcacheSize != 0 ?
        Pcr->SecondLevelDcacheFillSize : Pcr->FirstLevelDcacheFillSize;
    Pcr->DcacheAlignment = Pcr->DcacheFillSize - 1;
    Pcr->IcacheFillSize = Pcr->SecondLevelIcacheSize != 0 ?
        Pcr->SecondLevelIcacheFillSize : Pcr->FirstLevelIcacheFillSize;
    Pcr->IcacheAlignment = Pcr->IcacheFillSize - 1;

    //
    // Build and load the kernel's translation tables and the page directory.
    //

    KiArmStartTranslation(LoaderBlock);
    KiArmEnableCaches();

    KiInitializeKernel((PKPROCESS)LoaderBlock->Process,
                       (PKTHREAD)LoaderBlock->Thread,
                       (PVOID)LoaderBlock->KernelStack,
                       (PKPRCB)LoaderBlock->Prcb,
                       0,
                       LoaderBlock);
}
