/*++

Copyright (c) 2026

Module Name:

    ctxsw.c

Abstract:

    KE/ARM context switch and dispatcher-exit logic - the C half of the KE/MIPS
    X4CTXSW.S analog (the register save/restore and thread/idle entries are in
    ctxsw.S). KiSwapContext performs a context switch to a target thread, the
    raw stack/register swap being KiSwapStacks (ctxsw.S). KiInitializeContextThread
    seeds a new thread's kernel stack so its first KiSwapStacks restore enters
    KiThreadStartup.

    These run once the scheduler is active. During phase-0 KiInitializeKernel
    (which halts at the ExpInitializeExecutive boundary) they are not entered;
    they are present and correct so the kernel links and so the scheduler works
    when it is brought up.

Environment:

    Kernel mode.

--*/

#include "ki.h"

extern VOID KiSwapStacks(PVOID *OldKernelStack, PVOID NewKernelStack);

//
// Kernel switch frame saved by KiSwapStacks (ctxsw.S). Layout, low address to
// high: VFP d8-d15, then r4-r11, then lr - it MUST match the push/pop order in
// KiSwapStacks.
//

typedef struct _KSWITCH_FRAME {
    ULONGLONG D[8];
    ULONG R[8];
    ULONG Lr;
} KSWITCH_FRAME, *PKSWITCH_FRAME;

//
// Build the initial kernel context for a thread (the KE/MIPS THREDINI.C
// KiInitializeContextThread analog). The first switch to this thread restores
// the frame below and enters KiThreadStartup with r4=SystemRoutine,
// r5=StartRoutine, r6=StartContext.
//

VOID
KiInitializeContextThread (
    IN PKTHREAD Thread,
    IN PKSYSTEM_ROUTINE SystemRoutine,
    IN PKSTART_ROUTINE StartRoutine,
    IN PVOID StartContext,
    IN PCONTEXT ContextFrame
    )
{
    PKSWITCH_FRAME Frame;

    UNREFERENCED_PARAMETER(ContextFrame);

    Frame = (PKSWITCH_FRAME)
        (((ULONG)Thread->InitialStack - sizeof(KSWITCH_FRAME)) & ~7u);
    RtlZeroMemory(Frame, sizeof(KSWITCH_FRAME));

    Frame->R[0] = (ULONG)SystemRoutine;
    Frame->R[1] = (ULONG)StartRoutine;
    Frame->R[2] = (ULONG)StartContext;
    Frame->Lr = (ULONG)KiThreadStartup;

    Thread->PreviousMode = KernelMode;
    Thread->KernelStack = (PVOID)Frame;
}

//
// Swap context to the specified thread, optionally readying the previous
// thread. Entered at DISPATCH_LEVEL with the dispatcher database locked; on the
// eventual return to the caller (when this thread is resumed) IRQL is lowered
// to the thread's wait IRQL and the wait completion status is returned. Mirrors
// KE/MIPS/X4CTXSW.S KiSwapContext + SwapContext.
//

NTSTATUS
FASTCALL
KiSwapContext (
    IN PKTHREAD Thread,
    IN BOOLEAN Ready
    )
{
    PKPRCB Prcb = PCR->Prcb;
    PKTHREAD OldThread = Prcb->CurrentThread;

    if (Ready != FALSE) {
        KiReadyThread(OldThread);
    }

    Prcb->CurrentThread = Thread;
    PCR->CurrentThread = Thread;
    Thread->State = Running;
    PCR->InitialStack = Thread->InitialStack;

    if (Thread->ApcState.Process != OldThread->ApcState.Process) {
        KiSwapProcess(Thread->ApcState.Process, OldThread->ApcState.Process);
    }

    KiSwapStacks(&OldThread->KernelStack, Thread->KernelStack);

    //
    // Reached when OldThread (this thread) is later switched back in.
    //

    KeLowerIrql(OldThread->WaitIrql);
    return (NTSTATUS)(ULONG)OldThread->WaitStatus;
}

//
// Switch the address space to a new process. The kernel currently runs MMU-off
// with a single identity map, so there is no page directory or ASID to switch
// yet - the TTBR0 / CONTEXTIDR switch lands with the MMU and per-process
// address spaces. Returns FALSE (no process-id wrap).
//

BOOLEAN
KiSwapProcess (
    IN PKPROCESS NewProcess,
    IN PKPROCESS OldProcess
    )
{
    UNREFERENCED_PARAMETER(NewProcess);
    UNREFERENCED_PARAMETER(OldProcess);
    return FALSE;
}

//
// Unlock the dispatcher database: if a thread has been selected to run on this
// processor and a switch is permitted, switch to it; otherwise restore the
// previous IRQL. Mirrors KE/MIPS/X4CTXSW.S KiUnlockDispatcherDatabase (the
// uniprocessor path).
//

VOID
FASTCALL
KiUnlockDispatcherDatabase (
    IN KIRQL OldIrql
    )
{
    PKPRCB Prcb = PCR->Prcb;
    PKTHREAD NextThread = Prcb->NextThread;

    if ((NextThread != NULL) && (OldIrql < DISPATCH_LEVEL)) {
        Prcb->NextThread = NULL;
        PCR->CurrentThread->WaitIrql = OldIrql;
        KiSwapContext(NextThread, TRUE);
    }

    KeLowerIrql(OldIrql);
}
