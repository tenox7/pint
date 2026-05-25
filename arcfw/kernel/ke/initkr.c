/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    initkr.c

Abstract:

    This module contains the code to initialize the kernel data structures
    and to initialize the idle thread, its process, and the processor control
    block.

    ARM32 / Raspberry Pi 2 port: the ARM analog of PRIVATE/NTOS/KE/MIPS/INITKR.C,
    ported as faithfully as the architecture allows. ARM deltas are marked
    "ARM port:". The processor control region is reached through TPIDRPRW
    (cp15 c13), not a fixed virtual address, so there is no PcrPage to record.
    The executive is not yet ported (the MSVC-SEH wall), so KiInitializeKernel
    completes all real kernel-arch initialization and then stops at the
    ExpInitializeExecutive boundary - it does not fake the executive.

Author:

    David N. Cutler (davec) 11-Apr-1990

Environment:

    Kernel mode only.

--*/

#include "ki.h"

VOID
KiArmReportInitialized (
    IN CCHAR Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    );

VOID
KiInitializeKernel (
    IN PKPROCESS Process,
    IN PKTHREAD Thread,
    IN PVOID IdleStack,
    IN PKPRCB Prcb,
    IN CCHAR Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

/*++

Routine Description:

    This function gains control after the system has been bootstrapped and
    before the system has been initialized. Its function is to initialize
    the kernel data structures, initialize the idle thread and process objects,
    initialize the processor control block, call the executive initialization
    routine, and then return to the system startup routine.

Arguments:

    Process - Supplies a pointer to a control object of type process for
        the specified processor.

    Thread - Supplies a pointer to a dispatcher object of type thread for
        the specified processor.

    IdleStack - Supplies a pointer the base of the real kernel stack for
        idle thread on the specified processor.

    Prcb - Supplies a pointer to a processor control block for the specified
        processor.

    Number - Supplies the number of the processor that is being
        initialized.

    LoaderBlock - Supplies a pointer to the loader parameter block.

Return Value:

    None.

--*/

{

    LONG Index;
    ULONG DirectoryTableBase[2];

    //
    // Perform platform dependent processor initialization.
    //

    HalInitializeProcessor(Number);

    //
    // Save the address of the loader parameter block.
    //

    KeLoaderBlock = LoaderBlock;

    //
    // Initialize the processor block.
    //

    Prcb->MinorVersion = PRCB_MINOR_VERSION;
    Prcb->MajorVersion = PRCB_MAJOR_VERSION;
    Prcb->BuildType = 0;

#if DBG

    Prcb->BuildType |= PRCB_BUILD_DEBUG;

#endif

#if defined(NT_UP)

    Prcb->BuildType |= PRCB_BUILD_UNIPROCESSOR;

#endif

    Prcb->CurrentThread = Thread;
    Prcb->NextThread = (PKTHREAD)NULL;
    Prcb->IdleThread = Thread;
    Prcb->Number = Number;
    Prcb->SetMember = 1 << Number;

    //
    // ARM port: the PCR is addressed through TPIDRPRW (set in the startup stub),
    // not mapped at a fixed virtual page, so there is no PcrPage to record while
    // the kernel runs MMU-off.
    //

    Prcb->PcrPage = 0;
    KeInitializeDpc(&Prcb->QuantumEndDpc,
                    (PKDEFERRED_ROUTINE)KiQuantumEnd,
                    NIL);

    //
    // Initialize DPC listhead and lock.
    //

    InitializeListHead(&Prcb->DpcListHead);
    KeInitializeSpinLock(&Prcb->DpcLock);

    //
    // Set address of processor block.
    //

    KiProcessorBlock[Number] = Prcb;

    //
    // Initialize the address of the bus error routines.
    //

    PCR->DataBusError = KeBusError;
    PCR->InstructionBusError = KeBusError;

    //
    // Initialize the idle thread initial kernel stack value.
    //

    PCR->InitialStack = IdleStack;

    //
    // Initialize all interrupt vectors to transfer control to the unexpected
    // interrupt routine.
    //
    // N.B. This interrupt object is never actually "connected" to an interrupt
    //      vector via KeConnectInterrupt. It is initialized and then connected
    //      by simply storing the address of the dispatch code in the interrupt
    //      vector.
    //

    if (Number == 0) {

        //
        // Initialize the address of the interrupt dispatch routine.
        //

        KxUnexpectedInterrupt.DispatchAddress = KiUnexpectedInterrupt;

        //
        // Copy the interrupt dispatch code template into the interrupt object
        // and flush the dcache so the code is actually in memory.
        //

        for (Index = 0; Index < DISPATCH_LENGTH; Index += 1) {
            KxUnexpectedInterrupt.DispatchCode[Index] = KiInterruptTemplate[Index];
        }

        HalSweepDcache();
    }

    for (Index = 0; Index < MAXIMUM_VECTOR; Index += 1) {
        PCR->InterruptRoutine[Index] =
                    (PKINTERRUPT_ROUTINE)(&KxUnexpectedInterrupt.DispatchCode);
    }

    //
    // Initialize the profile count and interval.
    //

    PCR->ProfileCount = 0;
    PCR->ProfileInterval = 0x200000;

    //
    // Initialize the passive release, APC, and DPC interrupt vectors.
    //

    PCR->InterruptRoutine[0] = KiPassiveRelease;
    PCR->InterruptRoutine[APC_LEVEL] = KiApcInterrupt;
    PCR->InterruptRoutine[DISPATCH_LEVEL] = KiDispatchInterrupt;
    PCR->ReservedVectors = (1 << PASSIVE_LEVEL) | (1 << APC_LEVEL) |
                                        (1 << DISPATCH_LEVEL) | (1 << IPI_LEVEL);

    //
    // Initialize the set member for the current processor, set IRQL to
    // APC_LEVEL, and set the processor number.
    //

    PCR->CurrentIrql = APC_LEVEL;
    PCR->SetMember = 1 << Number;
    PCR->Number = Number;

    //
    // Set the initial stall execution scale factor. This value will be
    // recomputed later by the HAL.
    //

    PCR->StallScaleFactor = 50;

    //
    // Set address of process object in thread object.
    //

    Thread->ApcState.Process = Process;

    //
    // Set the appropriate member in the active processors set.
    //

    SetMember(Number, KeActiveProcessors);

    //
    // Set the number of processors based on the maximum of the current
    // number of processors and the current processor number.
    //

    if ((Number + 1) > KeNumberProcessors) {
        KeNumberProcessors = Number + 1;
    }

    //
    // If the initial processor is being initialized, then initialize the
    // per system data structures.
    //

    if (Number == 0) {

        //
        // Initialize the address of the restart block for the boot master.
        //

        Prcb->RestartBlock = SYSTEM_BLOCK->RestartBlock;

        //
        // ARM port: the kernel debugger (KdInitSystem) is not yet wired for
        // ARM; the boot proceeds with no debugger, as on a production machine
        // with none attached. Restored when KD/ARM transport lands.
        //

        //
        // Initialize processor block array.
        //

        for (Index = 1; Index < MAXIMUM_PROCESSORS; Index += 1) {
            KiProcessorBlock[Index] = (PKPRCB)NULL;
        }

        //
        // Perform architecture independent initialization.
        //

        KiInitSystem();

        //
        // Initialize idle thread process object and then set:
        //
        //      1. all the quantum values to the maximum possible.
        //      2. the process in the balance set.
        //      3. the active processor mask to the specified processor.
        //

        //
        // ARM port: KeInitializeProcess dereferences DirectoryTableBase[0..1].
        // The MIPS self-mapped PDE address (PDE_BASE | ...) is a valid pointer
        // only with the MMU on; the kernel runs MMU-off, so pass a real array.
        // The directory table base is not used for translation yet (it gets the
        // real TTBR0 when the MMU and per-process address spaces are brought up).
        //

        DirectoryTableBase[0] = 0;
        DirectoryTableBase[1] = 0;
        KeInitializeProcess(Process,
                            (KPRIORITY)0,
                            (KAFFINITY)(0x7f),
                            DirectoryTableBase,
                            FALSE);

        Process->ThreadQuantum = MAXCHAR;

    }

    //
    // Initialize idle thread object and then set:
    //
    //      1. the initial kernel stack to the specified idle stack.
    //      2. the next processor number to the specified processor.
    //      3. the thread priority to the highest possible value.
    //      4. the state of the thread to running.
    //      5. the thread affinity to the specified processor.
    //      6. the specified processor member in the process active processors
    //          set.
    //

    KeInitializeThread(Thread, (PVOID)((ULONG)IdleStack - PAGE_SIZE),
                       (PKSYSTEM_ROUTINE)NULL, (PKSTART_ROUTINE)NULL,
                       (PVOID)NULL, (PCONTEXT)NULL, (PVOID)NULL, Process);

    Thread->InitialStack = IdleStack;
    Thread->NextProcessor = Number;
    Thread->Priority = HIGH_PRIORITY;
    Thread->State = Running;
    Thread->Affinity = (KAFFINITY)(1 << Number);
    Thread->WaitIrql = DISPATCH_LEVEL;

    //
    // If the current processor is 0, then set the appropriate bit in the active
    // summary of the idle process.
    //

    if (Number == 0) {
        SetMember(Number, Process->ActiveProcessors);
    }

    //
    // Insert thread in active matrix.
    //

    InsertActiveMatrix(Number, HIGH_PRIORITY);

    //
    // Execute the executive initialization.
    //
    // ARM port: the portable executive (Ex/Mm/Ob/Ps/Io/Se/Cm/...) is not yet
    // ported - it requires the MSVC structured-exception mechanism gcc does not
    // implement and ~278K lines of source. KiInitializeKernel has completed all
    // real kernel-architecture initialization (PCR, PRCB, idle process/thread,
    // interrupt vectors, IRQL). Report the live initialized state and halt at
    // the ExpInitializeExecutive boundary. The executive is NOT stubbed - it is
    // simply not reached yet; this is the next frontier of the port.
    //

    KiArmReportInitialized(Number, LoaderBlock);

    return;
}
