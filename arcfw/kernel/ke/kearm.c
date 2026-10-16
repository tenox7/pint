/*++

Copyright (c) 2026

Module Name:

    kearm.c

Abstract:

    ARMv7-A kernel architecture glue for the NT 3.5 / Raspberry Pi 2 port: the
    system startup C entry, which initializes the boot processor's PCR from the
    structures the OS Loader allocated and calls KiInitializeKernel (the KE/MIPS
    X4START.S KiSystemBegin sequence), and the APC software interrupt.

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
// System startup, called from KiSystemStartup (armstart.asm) with the loader
// parameter block: install the exception vectors, initialize the PCR from the
// structures the OS Loader allocated, start translation, and initialize the
// kernel (the KE/MIPS X4START.S KiSystemBegin sequence). Returns as the idle
// thread at HIGH_LEVEL.
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
    // Build and load the kernel's translation tables and the page directory.
    //

    KiArmStartTranslation(LoaderBlock);

    KiInitializeKernel((PKPROCESS)LoaderBlock->Process,
                       (PKTHREAD)LoaderBlock->Thread,
                       (PVOID)LoaderBlock->KernelStack,
                       (PKPRCB)LoaderBlock->Prcb,
                       0,
                       LoaderBlock);
}
