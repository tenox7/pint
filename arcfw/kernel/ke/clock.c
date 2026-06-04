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

    The timer-table expiration scan and the KeUpdateRunTime tail (thread-quantum
    and run-time billing) of the genuine KeUpdateSystemTime need a populated
    KTRAP_FRAME and the dispatcher; they arrive with the scheduler (Phase 1).

Environment:

    Kernel mode.

--*/

#include "ki.h"
#include "halirq.h"

#ifndef KI_RUN_EXECUTIVE
#define KI_RUN_EXECUTIVE 0
#endif

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
// Arm the periodic system-timer interrupt: enable IRQ 3 in the controller and
// set the first compare. Called once interrupts are about to be enabled.
//

VOID KiArmStartClock(VOID)
{
    HalpEnableBcmIrq(1, 3);                          // enable the system-timer match-3 IRQ (bank 1, bit 3)
    REG(ST_C3) = REG(ST_CLO) + KI_CLOCK_PERIOD;
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
// The KiTimerTableListHead expiration scan (queue KiTimerExpireDpc, request a
// DISPATCH software interrupt) and the KeUpdateRunTime tail (charge the tick to
// thread/process/DPC/interrupt time and decrement the thread quantum) require a
// populated KTRAP_FRAME and the dispatcher database - deferred to the scheduler
// (Phase 1).
//

VOID
KeUpdateSystemTime (
    IN struct _KTRAP_FRAME *TrapFrame,
    IN ULONG TimeIncrement
    )
{
    ULONGLONG Time;

    UNREFERENCED_PARAMETER(TrapFrame);

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

VOID HalpClockInterrupt0(VOID)
{
    REG(ST_CS) = ST3_IRQ;                               // acknowledge the match at the device
    REG(ST_C3) = REG(ST_CLO) + KI_CLOCK_PERIOD;         // re-arm the next tick
#if KI_RUN_EXECUTIVE
    KeUpdateSystemTime((struct _KTRAP_FRAME *)0, HalpCurrentTimeIncrement);
#else
    KiClockTick();
#endif
}
