/*++

Copyright (c) 2026

Module Name:

    clock.c

Abstract:

    The clock and profile interrupt routines the HAL calls: the port of
    KE/MIPS/XXCLOCK.S. KeUpdateSystemTime advances the interrupt time on every
    clock interrupt and, on a full tick, the system time and the tick count, then
    bills run time and decrements the quantum (KeUpdateRunTime); an expired timer
    queues the timer expiration DPC. KeProfileInterrupt counts the interrupted PC
    into the active profile objects.

Environment:

    Kernel mode.

--*/

#include "ki.h"

//
// KeUpdateSystemTime - the clock tick, ported from KE/MIPS/XXCLOCK.S. Called
// from the clock ISR each interrupt with the time increment in 100-ns units.
//
// Every interrupt advances InterruptTime. KiTickOffset counts the increment down
// to a full tick; on a full tick SystemTime advances by KeTimeAdjustment and
// KeTickCount by one, its low part mirrored into SharedUserData->TickCountLow.
// The 64-bit times are stored High2 -> Low -> High1 so the lock-free readers
// (arm.h), which retry while High1Time != High2Time, never see a torn value.
//
// If the timer at the current hand (or, on a full tick, the next hand) has
// expired, the timer expiration DPC is queued with the hand value. A full tick
// then charges run time and decrements the quantum (KeUpdateRunTime).
//

static BOOLEAN
KiTimerExpired (
    IN ULONG Hand,
    IN ULONGLONG InterruptTime
    )
{
    PLIST_ENTRY ListHead = &KiTimerTableListHead[Hand & (TIMER_TABLE_SIZE - 1)];
    PKTIMER Timer;

    if (ListHead->Flink == ListHead) {
        return FALSE;
    }

    Timer = CONTAINING_RECORD(ListHead->Flink, KTIMER, TimerListEntry);
    return (ULONGLONG)Timer->DueTime.QuadPart <= InterruptTime;
}

VOID
KeUpdateSystemTime (
    IN struct _KTRAP_FRAME *TrapFrame,
    IN ULONG TimeIncrement
    )
{
    ULONGLONG InterruptTime;
    ULONGLONG Time;
    BOOLEAN FullTick;
    BOOLEAN Expired;
    BOOLEAN Enable;
    PKPRCB Prcb;
    ULONG Hand;

    InterruptTime = ((ULONGLONG)(ULONG)SharedUserData->InterruptTime.High1Time << 32) |
                    (ULONGLONG)SharedUserData->InterruptTime.LowPart;
    InterruptTime += TimeIncrement;
    SharedUserData->InterruptTime.High2Time = (LONG)(InterruptTime >> 32);
    SharedUserData->InterruptTime.LowPart   = (ULONG)InterruptTime;
    SharedUserData->InterruptTime.High1Time = (LONG)(InterruptTime >> 32);

    Hand = KeTickCount.LowPart;
    KiTickOffset -= TimeIncrement;
    FullTick = (LONG)KiTickOffset <= 0;
    Expired = FALSE;
    if (FullTick) {
        Time = ((ULONGLONG)(ULONG)SharedUserData->SystemTime.High1Time << 32) |
               (ULONGLONG)SharedUserData->SystemTime.LowPart;
        Time += KeTimeAdjustment;
        SharedUserData->SystemTime.High2Time = (LONG)(Time >> 32);
        SharedUserData->SystemTime.LowPart   = (ULONG)Time;
        SharedUserData->SystemTime.High1Time = (LONG)(Time >> 32);

        Time = ((ULONGLONG)(ULONG)KeTickCount.High1Time << 32) |
               (ULONGLONG)KeTickCount.LowPart;
        Time += 1;
        SharedUserData->TickCountLow = (ULONG)Time;
        KeTickCount.High2Time = (LONG)(Time >> 32);
        KeTickCount.LowPart   = (ULONG)Time;
        KeTickCount.High1Time = (LONG)(Time >> 32);

        KiTickOffset += KeMaximumIncrement;
        Expired = KiTimerExpired(Hand, InterruptTime);
        if (Expired == FALSE) {
            Hand += 1;
        }
    }

    if (Expired == FALSE) {
        Expired = KiTimerExpired(Hand, InterruptTime);
    }

    //
    // Put the timer expiration DPC in the DPC list and request a dispatch
    // interrupt.
    //

    if (Expired != FALSE) {
        Prcb = KeGetCurrentPrcb();
        Enable = KiDisableInterrupts();
        if (KiTimerExpireDpc.Lock == NULL) {
            KiTimerExpireDpc.Lock = &Prcb->DpcLock;
            KiTimerExpireDpc.SystemArgument1 = (PVOID)Hand;
            InsertTailList(&Prcb->DpcListHead, &KiTimerExpireDpc.DpcListEntry);
            KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }

        KiRestoreInterrupts(Enable);
    }

    if (FullTick) {
        KeUpdateRunTime(TrapFrame);
    }
}

//
// KeUpdateRunTime - the KE/MIPS/XXCLOCK.S tail (lines 286-396). Run on every full
// tick to bill the elapsed quantum to the current thread, its process, and the
// processor, and to decrement the thread's quantum, requesting a dispatch interrupt
// on a quantum end. The accounting bucket is selected by the interrupted context
// captured in the trap frame (ke/trap.S): the saved CPSR's mode field gives kernel
// vs user, and the recorded OldIrql distinguishes thread-kernel / DPC / interrupt
// time. A uniprocessor kernel, so the process-time updates need no interlock.
//

VOID
KeUpdateRunTime (
    IN PKTRAP_FRAME TrapFrame
    )
{
    extern UCHAR KiClockQuantumDecrement;
    PKPRCB Prcb = PCR->Prcb;
    PKTHREAD Thread = Prcb->CurrentThread;
    PKPROCESS Process = Thread->ApcState.Process;
    SCHAR Quantum;

    //
    // ARM CPSR mode field (bits [4:0]) == 0b10000 is User mode; any other value is
    // a privileged (kernel) mode (post-boot interrupts are taken in SVC).
    //

    if ((TrapFrame->Cpsr & 0x1Fu) == 0x10u) {

        //
        // Previous mode user: charge thread, process, and processor user time.
        //

        Thread->UserTime += 1;
        Process->UserTime += 1;
        Prcb->UserTime += 1;

    } else {

        KIRQL OldIrql = (KIRQL)TrapFrame->OldIrql;

        //
        // Previous mode kernel. Above DISPATCH_LEVEL is interrupt-service time; at
        // DISPATCH_LEVEL with a DPC running is DPC time; otherwise the current
        // thread's kernel time. Interrupt/DPC time is charged to the processor only
        // (no thread/process owns it), thread-kernel time to all three.
        //

        if (OldIrql > DISPATCH_LEVEL) {
            Prcb->InterruptTime += 1;
            Prcb->KernelTime += 1;
        } else if (OldIrql == DISPATCH_LEVEL && PCR->DpcRoutineActive) {
            Prcb->DpcTime += 1;
            Prcb->KernelTime += 1;
        } else {
            Thread->KernelTime += 1;
            Process->KernelTime += 1;
            Prcb->KernelTime += 1;
        }
    }

    //
    // Decrement the current thread's quantum (KiDecrementQuantum). On a quantum end,
    // flag the PRCB and request a DISPATCH_LEVEL software interrupt: it drains the
    // DPC queue now (at the HalEndSystemInterrupt -> KeLowerIrql tail) and will run
    // the scheduler's quantum-end reschedule once the dispatcher exists (Phase 1).
    //

    Quantum = (SCHAR)(Thread->Quantum - (SCHAR)KiClockQuantumDecrement);
    Thread->Quantum = Quantum;
    if (Quantum <= 0) {
        Prcb->QuantumEnd = (ULONG)TrapFrame;
        KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
}


//
// KeProfileInterrupt - the KE/MIPS/XXCLOCK.S KeProfileInterrupt. Called by the
// HAL's profile interrupt with the trap frame; counts the interrupted PC into
// the buckets of the current process's profile objects and the system's.
//

static VOID
KiProcessProfileList (
    IN PKTRAP_FRAME TrapFrame,
    IN PLIST_ENTRY ListHead
    )
{
    PLIST_ENTRY Next;
    PKPROFILE Profile;
    ULONG Pc = TrapFrame->Pc;

    for (Next = ListHead->Flink; Next != ListHead; Next = Next->Flink) {
        Profile = CONTAINING_RECORD(Next, KPROFILE, ProfileListEntry);
        if (Pc >= (ULONG)Profile->RangeBase && Pc < (ULONG)Profile->RangeLimit) {
            *(PULONG)((PUCHAR)Profile->Buffer +
                      (((Pc - (ULONG)Profile->RangeBase) >> Profile->BucketShift) & ~3)) += 1;
        }
    }
}

VOID
KeProfileInterrupt (
    IN PKTRAP_FRAME TrapFrame
    )
{
    KiProcessProfileList(TrapFrame,
                         &KeGetCurrentThread()->ApcState.Process->ProfileListHead);

    KiProcessProfileList(TrapFrame, &KiProfileListHead);
}
