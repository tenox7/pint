/*++

Copyright (c) 2026

Module Name:

    clock.c

Abstract:

    KE/ARM clock interrupt and interrupt dispatch (the XXCLOCK.S analog plus a
    minimal HAL interrupt path for the Raspberry Pi 2 / BCM2835). A periodic
    interrupt from the BCM2835 system timer (compare channel 3), routed through
    the BCM2835 interrupt controller, drives the system tick: KiInterruptDispatch
    (called from the IRQ entry in trap.S) acknowledges and re-arms the timer and
    advances time.

    Two builds share this file:

      - The minimal known-good kernel (make-kernel.sh, KI_RUN_EXECUTIVE 0) has no
        executive and no KUSER_SHARED_DATA page mapped. It keeps the original
        hand-rolled tick: a single source advancing only KeTickCount with the
        High1/High2 writer protocol, at a 4 Hz demo rate. This path is unchanged
        (the minimal kernel stays byte-identical).

      - The full-executive kernel (make-execlink.sh, KI_RUN_EXECUTIVE 1) runs the
        genuine KeUpdateSystemTime (the port of KE/MIPS/XXCLOCK.S): every clock
        interrupt it advances InterruptTime, and on a full tick SystemTime,
        KeTickCount and the shared-page TickCountLow - through the live
        KUSER_SHARED_DATA page - so KeQuerySystemTime returns real, advancing time.
        The clock runs at the genuine ~10 ms NT rate, with the per-tick increment
        established by KeSetTimeIncrement (HalInitSystem, ke/linkstubs.c).

    The genuine KeUpdateRunTime tail (per-tick thread/process/processor run-time
    billing and thread-quantum decrement) runs on a full tick, consuming the
    KTRAP_FRAME the IRQ entry (ke/trap.S) builds. The timer-table expiration scan
    needs the dispatcher's timer database and stays deferred to the scheduler
    (Phase 1).

Environment:

    Kernel mode.

--*/

#include "ki.h"
#include "halirq.h"

#ifndef KI_RUN_EXECUTIVE
#define KI_RUN_EXECUTIVE 0
#endif

//
// The IRQ entry (ke/trap.S) builds the KTRAP_FRAME by hand using .equ offset
// constants; assert here that they still match inc/arm.h _KTRAP_FRAME so the two
// representations can never silently drift.
//

_Static_assert(__builtin_offsetof(KTRAP_FRAME, R0)               ==  0, "TrR0");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, R1)               ==  4, "R1");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, R2)               ==  8, "R2");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, R3)               == 12, "R3");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, R12)              == 16, "TrR12");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, Sp)               == 20, "TrSp");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, Lr)               == 24, "TrLr");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, Pc)               == 28, "TrPc");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, Cpsr)             == 32, "TrCpsr");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, Fpscr)            == 36, "TrFpscr");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, ExceptionActive)  == 40, "TrExceptionActive");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, PreviousMode)     == 44, "TrPreviousMode");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, OldIrql)          == 48, "TrOldIrql");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, FaultAddress)     == 52, "TrFaultAddress");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, FaultStatus)      == 56, "TrFaultStatus");
_Static_assert(__builtin_offsetof(KTRAP_FRAME, OnInterruptStack) == 60, "TrOnInterruptStack");
_Static_assert(((sizeof(KTRAP_FRAME) + 7) & ~7u)                 == 64, "KTRAP_FRAME_LENGTH");

//
// BCM2835 system timer (1 MHz free-running). The interrupt-controller registers
// (enable/disable/pending) live in inc/halirq.h; clock.c owns only the timer
// device (compare channel 3) and acknowledges it at the device, per the HAL
// interrupt contract (the BCM controllers have no EOI).
//

#define SYSTIMER_BASE   0x3F003000u
#define ST_CS           (SYSTIMER_BASE + 0x00)     // control/status (match flags)
#define ST_CLO          (SYSTIMER_BASE + 0x04)     // counter low
#define ST_C3           (SYSTIMER_BASE + 0x18)     // compare register 3

#define ST3_IRQ         (1u << 3)                    // system timer match 3 = IRQ 3 (bank 1 bit 3)

#define REG(a)          (*(volatile ULONG *)(ULONG)(a))

#if KI_RUN_EXECUTIVE

//
// Genuine NT clock rate (the Jazz values, JAZZDEF.H), in 100-ns units:
// MAXIMUM_INCREMENT ~= 10 ms per tick, MINIMUM_INCREMENT ~= 1 ms granularity.
// The BCM 1 MHz system-timer reload is the increment converted to microseconds
// (100 ns -> us is /10), so each compare-3 interrupt corresponds to one tick.
//

#define MAXIMUM_INCREMENT 100000u                    // 10 ms in 100-ns units
#define MINIMUM_INCREMENT 10000u                     // 1 ms in 100-ns units
#define KI_CLOCK_PERIOD   (MAXIMUM_INCREMENT / 10u)  // 10000 us = 10 ms (1 MHz timer)

//
// The per-tick time increment the clock ISR hands KeUpdateSystemTime, in 100-ns
// units (the HALFXS HalpCurrentTimeIncrement). HalInitSystem (ke/linkstubs.c)
// primes it and calls KeSetTimeIncrement; a later real HAL pipelines rate changes
// through HalSetTimeIncrement.
//

ULONG HalpCurrentTimeIncrement = MAXIMUM_INCREMENT;

#else

//
// Minimal kernel: a visible 4 Hz demonstration rate (the real increment is set
// by the HAL, which the minimal kernel does not run).
//

#define KI_CLOCK_PERIOD   250000u

#endif

//
// Arm the periodic system-timer interrupt and set the first compare. Called once
// interrupts are about to be enabled. The interrupt is enabled the genuine HAL
// way - the path a device driver's IoConnectInterrupt would take: resolve the
// source with HalGetInterruptVector (the BCM system timer is an Internal-bus
// interrupt whose bus level is the clock IRQL and whose bus vector is the BCM IRQ
// number) and unmask it at the controller with HalEnableSystemInterrupt.
//

VOID KiArmStartClock(VOID)
{
    KIRQL Irql;
    KAFFINITY Affinity;
    ULONG Vector;

    Vector = HalGetInterruptVector(Internal, 0, CLOCK2_LEVEL, HAL_TIMER_IRQ,
                                   &Irql, &Affinity);
    HalEnableSystemInterrupt(Vector, Irql, Latched);
    REG(ST_C3) = REG(ST_CLO) + KI_CLOCK_PERIOD;
}

//
// Busy-wait the given number of microseconds on the free-running 1 MHz BCM
// system-timer counter (CLO). The counter runs independently of its compare-3
// interrupt, so this is a valid time base even while that interrupt is masked -
// the HAL interrupt-gating self-test uses it to measure tick freeze/resume. The
// subtraction is unsigned so it is correct across the 32-bit counter wrap.
//

VOID KiArmSpinMicroseconds(ULONG Microseconds)
{
    ULONG Start = REG(ST_CLO);

    while ((ULONG)(REG(ST_CLO) - Start) < Microseconds)
        ;
}

#if KI_RUN_EXECUTIVE

//
// KeUpdateSystemTime - the genuine NT clock-tick body, ported from
// KE/MIPS/XXCLOCK.S (lines 72-160). Called from the clock ISR (the
// HalpClockInterrupt0 analog, X4CLOCK.S) each interrupt with the per-tick time
// increment in 100-ns units.
//
// Every interrupt advances InterruptTime. KiTickOffset counts the increment down
// to a full tick; on a full tick SystemTime advances by the (adjustable)
// KeTimeAdjustment, KeTickCount advances by one, and its low part is mirrored into
// SharedUserData->TickCountLow. All three 64-bit times are written through the
// KUSER_SHARED_DATA page (and the KeTickCount global) in the strict
// High2 -> Low -> High1 store order that the lock-free KiQuery* readers (arm.h)
// require: storing High2Time first and High1Time last guarantees a reader (which
// retries while High1Time != High2Time) sees either the wholly-old or wholly-new
// value, never a torn one. This is a uniprocessor kernel, so program-order stores
// to the volatile fields suffice (the same CPU observes its own writes in order).
//
// On a full tick the KeUpdateRunTime tail (charge the tick to thread/process/DPC/
// interrupt time per the trap frame, and decrement the thread quantum) now runs -
// it consumes the KTRAP_FRAME the IRQ entry builds (ke/trap.S). The
// KiTimerTableListHead expiration scan (queue KiTimerExpireDpc) still needs the
// timer database the dispatcher manages, so it stays deferred to Phase 1.
//

VOID
KeUpdateSystemTime (
    IN struct _KTRAP_FRAME *TrapFrame,
    IN ULONG TimeIncrement
    )
{
    ULONGLONG Time;

    //
    // Interrupt time advances on every interrupt by the time increment.
    //

    Time = ((ULONGLONG)(ULONG)SharedUserData->InterruptTime.High1Time << 32) |
           (ULONGLONG)SharedUserData->InterruptTime.LowPart;
    Time += TimeIncrement;
    SharedUserData->InterruptTime.High2Time = (LONG)(Time >> 32);
    SharedUserData->InterruptTime.LowPart   = (ULONG)Time;
    SharedUserData->InterruptTime.High1Time = (LONG)(Time >> 32);

    //
    // Count down to a full tick; a partial tick advances only interrupt time.
    //

    KiTickOffset -= TimeIncrement;
    if ((LONG)KiTickOffset > 0)
        return;
    KiTickOffset += KeMaximumIncrement;

    //
    // System time advances by the (time-adjustment) increment on a full tick.
    //

    Time = ((ULONGLONG)(ULONG)SharedUserData->SystemTime.High1Time << 32) |
           (ULONGLONG)SharedUserData->SystemTime.LowPart;
    Time += KeTimeAdjustment;
    SharedUserData->SystemTime.High2Time = (LONG)(Time >> 32);
    SharedUserData->SystemTime.LowPart   = (ULONG)Time;
    SharedUserData->SystemTime.High1Time = (LONG)(Time >> 32);

    //
    // Tick count advances by one; the low part is mirrored into the shared page
    // (NtGetTickCount reads SharedUserData->TickCountLow; KeQueryTickCount reads
    // the KeTickCount global).
    //

    Time = ((ULONGLONG)(ULONG)KeTickCount.High1Time << 32) |
           (ULONGLONG)KeTickCount.LowPart;
    Time += 1;
    SharedUserData->TickCountLow = (ULONG)Time;
    KeTickCount.High2Time = (LONG)(Time >> 32);
    KeTickCount.LowPart   = (ULONG)Time;
    KeTickCount.High1Time = (LONG)(Time >> 32);

    //
    // A full tick completed: charge thread/process/processor run time and decrement
    // the current thread's quantum (the KeUpdateRunTime tail of XXCLOCK.S). The
    // KiTimerTableListHead expiration scan that also runs here stays deferred to the
    // dispatcher (Phase 1).
    //

    KeUpdateRunTime(TrapFrame);
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

#else

//
// Advance the 64-bit tick count using the High1/High2 ordering so a reader
// (KiQueryTickCount) always sees a consistent value.
//

static VOID KiClockTick(VOID)
{
    ULONGLONG Count;

    Count = ((ULONGLONG)(ULONG)KeTickCount.High1Time << 32) |
            (ULONGLONG)KeTickCount.LowPart;
    Count += 1;

    KeTickCount.High2Time = (LONG)(Count >> 32);
    KeTickCount.LowPart = (ULONG)Count;
    KeTickCount.High1Time = (LONG)(Count >> 32);
}

#endif

//
// HalpClockInterrupt0 - the clock ISR (the X4CLOCK.S HalpClockInterrupt0 analog).
// Installed at PCR->InterruptRoutine[CLOCK2_LEVEL] by HalpInitializeInterrupts
// (ke/halirq.c) and called by the generic distributor KiInterruptDispatch once it
// has confirmed the system-timer IRQ is pending. Acknowledge the match at the
// device, re-arm the next compare, and run the genuine KeUpdateSystemTime (the
// executive kernel) or advance the tick counter (the minimal kernel).
//

VOID HalpClockInterrupt0(PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    //
    // Raise to CLOCK2_LEVEL through the HAL bracket (rejecting a spurious assert)
    // and record the interrupted IRQL in the trap frame so KeUpdateRunTime can bill
    // the tick to the right bucket (thread/DPC/interrupt time).
    //

    if (!HalBeginSystemInterrupt(CLOCK2_LEVEL, HAL_TIMER_IRQ, &OldIrql))
        return;
    TrapFrame->OldIrql = OldIrql;

    REG(ST_CS) = ST3_IRQ;                               // acknowledge the match at the device
    REG(ST_C3) = REG(ST_CLO) + KI_CLOCK_PERIOD;         // re-arm the next tick

#if KI_RUN_EXECUTIVE
    KeUpdateSystemTime(TrapFrame, HalpCurrentTimeIncrement);
#else
    KiClockTick();
#endif

    HalEndSystemInterrupt(OldIrql, HAL_TIMER_IRQ);      // lower IRQL (+ drain software ints)
}
