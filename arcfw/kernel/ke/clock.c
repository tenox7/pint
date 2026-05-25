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
    advances KeTickCount.

    Minimal-but-real: a single clock source, KeTickCount advanced with the
    High1/High2 writer protocol. The full KeUpdateSystemTime work (interrupt
    time, timer-table expiration, thread quantum) and per-level interrupt masking
    arrive with the scheduler and a full HAL.

Environment:

    Kernel mode.

--*/

#include "ki.h"

//
// BCM2835 system timer (1 MHz free-running) and interrupt controller.
//

#define SYSTIMER_BASE   0x3F003000u
#define ST_CS           (SYSTIMER_BASE + 0x00)     // control/status (match flags)
#define ST_CLO          (SYSTIMER_BASE + 0x04)     // counter low
#define ST_C3           (SYSTIMER_BASE + 0x18)     // compare register 3

#define INTCTL_BASE     0x3F00B200u
#define IRQ_PENDING1    (INTCTL_BASE + 0x04)        // pending IRQs 0-31
#define IRQ_ENABLE1     (INTCTL_BASE + 0x10)        // enable IRQs 0-31

#define ST3_IRQ         (1u << 3)                    // system timer match 3 = IRQ 3

#define REG(a)          (*(volatile ULONG *)(ULONG)(a))

//
// Tick period in microseconds (the 1 MHz timer's units). A visible demonstration
// rate; the real KeMaximumIncrement is set by the HAL.
//

#define KI_CLOCK_PERIOD 250000u

//
// Arm the periodic system-timer interrupt: enable IRQ 3 in the controller and
// set the first compare. Called once interrupts are about to be enabled.
//

VOID KiArmStartClock(VOID)
{
    REG(IRQ_ENABLE1) = ST3_IRQ;
    REG(ST_C3) = REG(ST_CLO) + KI_CLOCK_PERIOD;
}

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

//
// Interrupt dispatcher, called from the IRQ entry (trap.S) with interrupts
// masked. Identify the source, service it, acknowledge, and return. The clock
// is currently the only wired source.
//

VOID KiInterruptDispatch(VOID)
{
    if ((REG(IRQ_PENDING1) & ST3_IRQ) != 0) {
        REG(ST_CS) = ST3_IRQ;                               // acknowledge the match
        REG(ST_C3) = REG(ST_CLO) + KI_CLOCK_PERIOD;         // re-arm
        KiClockTick();
    }
}
