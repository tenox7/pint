/*++

Copyright (c) 2026

Module Name:

    clock.c

Abstract:

    The clock and profile interrupt routines the HAL calls: the port of
    KE/MIPS/XXCLOCK.S. KeUpdateSystemTime advances the interrupt time on every
    clock interrupt and, on a full tick, the system time and the tick count, then
    bills run time, tunes the DPC queue depth and decrements the quantum
    (KeUpdateRunTime); an expired timer queues the timer expiration DPC.
    KeProfileInterruptWithSource counts the interrupted PC into the active
    profile objects of the profile source.

    The tick count, the clock increments and the DPC tuning values live with
    the high frequency spin locks (ex/hifreqlk.asm).

Environment:

    Kernel mode.

--*/

#include "ki.h"

//
// Order the stores of a lock-free time value for the other processors.
//

#if defined(NT_UP)
#define KiOrderStores()
#else
#define KiOrderStores() __dmb(_ARM_BARRIER_SY)
#endif

//
// Add a tick to the run time of a process.
//

#if defined(NT_UP)
#define KiAddProcessTime(Time) (*(Time) += 1)
#else
#define KiAddProcessTime(Time) InterlockedIncrement((PLONG)(Time))
#endif

extern ULONG KiIdealDpcRate;

//
// KeUpdateSystemTime - the clock tick, ported from KE/MIPS/XXCLOCK.S. Called
// from the clock ISR each interrupt with the time increment in 100-ns units.
//
// Every interrupt advances InterruptTime. KiTickOffset counts the increment down
// to a full tick; on a full tick SystemTime advances by KeTimeAdjustment and
// the 64-bit KeTickCount by one, its low part mirrored into
// SharedUserData->TickCountLow. The 64-bit times are stored High2 -> Low ->
// High1, with a barrier between the stores for the other processors, so the
// lock-free readers (arm.h), which retry while High1Time != High2Time, never
// see a torn value; old drivers read the tick count the same way. Only the
// processor that keeps the system time calls it.
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
    KiOrderStores();
    SharedUserData->InterruptTime.LowPart   = (ULONG)InterruptTime;
    KiOrderStores();
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
        KiOrderStores();
        SharedUserData->SystemTime.LowPart   = (ULONG)Time;
        KiOrderStores();
        SharedUserData->SystemTime.High1Time = (LONG)(Time >> 32);

        Time = ((ULONGLONG)(ULONG)KeTickCount.High1Time << 32) |
               (ULONGLONG)KeTickCount.LowPart;
        Time += 1;
        SharedUserData->TickCountLow = (ULONG)Time;
        KeTickCount.High2Time = (LONG)(Time >> 32);
        KiOrderStores();
        KeTickCount.LowPart   = (ULONG)Time;
        KiOrderStores();
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
    // Put the timer expiration DPC in the DPC list, under the DPC lock, count
    // it in the DPC queue depth, and request a dispatch interrupt.
    //

    if (Expired != FALSE) {
        Prcb = KeGetCurrentPrcb();
        Enable = KiDisableInterrupts();
        KiAcquireSpinLock(&Prcb->DpcLock);
        if (KiTimerExpireDpc.Lock == NULL) {
            KiTimerExpireDpc.Lock = &Prcb->DpcLock;
            KiTimerExpireDpc.SystemArgument1 = (PVOID)Hand;
            InsertTailList(&Prcb->DpcListHead, &KiTimerExpireDpc.DpcListEntry);
            Prcb->DpcQueueDepth += 1;
            KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }

        KiReleaseSpinLock(&Prcb->DpcLock);
        KiRestoreInterrupts(Enable);
    }

    if (FullTick) {
        KeUpdateRunTime(TrapFrame);
    }
}

//
// KeUpdateRunTime - the KE/MIPS/XXCLOCK.S routine. Run on every full tick, on
// each processor from its own clock interrupt, to bill the tick to the current
// thread, its process, and the processor, to tune the DPC queue depth of the
// processor, and to decrement the thread's quantum, requesting a dispatch
// interrupt on a quantum end. The accounting bucket is selected by the
// interrupted context captured in the trap frame (ke/trap.asm): the saved
// CPSR's mode field gives kernel vs user, and the recorded OldIrql and the
// DPC routine active flag of the PRCB distinguish thread kernel, DPC and
// interrupt time. The times of a process, whose threads may run on several
// processors at once, are added interlocked (the MIPS ll/sc of XXCLOCK.S).
//

VOID
KeUpdateRunTime (
    IN PKTRAP_FRAME TrapFrame
    )
{
    PKPRCB Prcb = PCR->Prcb;
    PKTHREAD Thread = Prcb->CurrentThread;
    PKPROCESS Process = Thread->ApcState.Process;
    ULONG DpcCount;
    ULONG DpcRate;
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
        KiAddProcessTime(&Process->UserTime);
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
        } else if (OldIrql == DISPATCH_LEVEL && Prcb->DpcRoutineActive != 0) {
            Prcb->DpcTime += 1;
            Prcb->KernelTime += 1;
        } else {
            Thread->KernelTime += 1;
            KiAddProcessTime(&Process->KernelTime);
            Prcb->KernelTime += 1;
        }
    }

    //
    // The DPC request rate is the mean of the last rate and the requests of
    // this tick.
    //

    DpcCount = Prcb->DpcCount;
    DpcRate = ((DpcCount - Prcb->DpcLastCount) + Prcb->DpcRequestRate) >> 1;
    Prcb->DpcLastCount = DpcCount;
    Prcb->DpcRequestRate = DpcRate;

    //
    // DPCs that wait in the queue with no DPC routine running and no dispatch
    // interrupt requested were held back by the queue depth: request the
    // dispatch interrupt now, restart the adjustment countdown, and, while the
    // request rate is below the ideal rate, lower the maximum queue depth (not
    // below one) so that they are not held back as long. Otherwise count the
    // adjustment threshold down, and when it runs out, raise the maximum depth
    // again by one, up to its initial value.
    //

    if ((Prcb->DpcQueueDepth != 0) &&
        ((Prcb->DpcInterruptRequested | Prcb->DpcRoutineActive) == 0)) {

        KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
        Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;
        if ((DpcRate < KiIdealDpcRate) && (Prcb->MaximumDpcQueueDepth > 1)) {
            Prcb->MaximumDpcQueueDepth -= 1;
        }

    } else {
        Prcb->AdjustDpcThreshold -= 1;
        if (Prcb->AdjustDpcThreshold == 0) {
            Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;
            if (Prcb->MaximumDpcQueueDepth != KiMaximumDpcQueueDepth) {
                Prcb->MaximumDpcQueueDepth += 1;
            }
        }
    }

    //
    // Decrement the current thread's quantum. On a quantum end of a thread other
    // than the idle thread, flag the PRCB and request a dispatch interrupt: the
    // dispatch interrupt (ke/ctxsw.asm) then runs KiQuantumEnd.
    //

    Quantum = (SCHAR)(Thread->Quantum - CLOCK_QUANTUM_DECREMENT);
    Thread->Quantum = Quantum;
    if ((Quantum <= 0) && (Thread != Prcb->IdleThread)) {
        Prcb->QuantumEnd = (ULONG)TrapFrame;
        KiRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
}


//
// KeProfileInterruptWithSource - the KE/MIPS/XXCLOCK.S routine. Called by the
// HAL's profile interrupt with the trap frame and the profile source; counts
// the interrupted PC into the buckets of the profile objects of that source
// that profile this processor, those of the current process and the system's.
// KeProfileInterrupt is the older entry for the time source.
//

static VOID
KiProcessProfileList (
    IN PKTRAP_FRAME TrapFrame,
    IN KPROFILE_SOURCE Source,
    IN PLIST_ENTRY ListHead
    )
{
    PLIST_ENTRY Next;
    PKPROFILE Profile;
    ULONG Pc = TrapFrame->Pc;
    KAFFINITY SetMember = PCR->SetMember;

    for (Next = ListHead->Flink; Next != ListHead; Next = Next->Flink) {
        Profile = CONTAINING_RECORD(Next, KPROFILE, ProfileListEntry);
        if ((Profile->Source == (CSHORT)Source) &&
            (Pc >= (ULONG)Profile->RangeBase) && (Pc < (ULONG)Profile->RangeLimit) &&
            ((Profile->Affinity & SetMember) != 0)) {
            *(PULONG)((PUCHAR)Profile->Buffer +
                      (((Pc - (ULONG)Profile->RangeBase) >> Profile->BucketShift) & ~3)) += 1;
        }
    }
}

VOID
KeProfileInterruptWithSource (
    IN PKTRAP_FRAME TrapFrame,
    IN KPROFILE_SOURCE ProfileSource
    )
{
    KiAcquireSpinLock(&KiProfileLock);
    KiProcessProfileList(TrapFrame, ProfileSource,
                         &KeGetCurrentThread()->ApcState.Process->ProfileListHead);

    KiProcessProfileList(TrapFrame, ProfileSource, &KiProfileListHead);
    KiReleaseSpinLock(&KiProfileLock);
}

VOID
KeProfileInterrupt (
    IN PKTRAP_FRAME TrapFrame
    )
{
    KeProfileInterruptWithSource(TrapFrame, ProfileTime);
}
