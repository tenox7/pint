/*++

Copyright (c) 2026

Module Name:

    halirq.c

Abstract:

    KE/ARM HAL interrupt path for the Raspberry Pi 2 (BCM2836) - the NTHALS/HALFXS
    (MIPS Jazz) interrupt model mirrored onto the BCM controllers. The genuine NT
    interrupt design is: the 2nd-level dispatch is KERNEL code indexed BY IRQL (not
    by hardware IRQ number); PCR->InterruptRoutine[] holds one ISR per IRQL/vector
    (KiInitializeKernel wires [0]=PassiveRelease, [APC_LEVEL]=Apc, [DISPATCH_LEVEL]=
    Dispatch; the HAL adds the device + clock vectors); PCR->IrqlMask/IrqlTable are
    the IRQL <-> hardware-enable translation tables.

    This file provides HalpInitializeInterrupts (the XXINITNT.C analog: mask every
    device source, build the IRQL tables, install the device ISRs) and the generic
    interrupt distributor KiInterruptDispatch (the KiInterruptException analog,
    called from the IRQ entry in ke/trap.S): read the controller pending state,
    resolve the source to its IRQL, and call PCR->InterruptRoutine[vector].

    The clock (the only wired source today) is the BCM2835 system-timer match-3
    (legacy bank 1, bit 3); its ISR HalpClockInterrupt0 lives in ke/clock.c and runs
    at CLOCK2_LEVEL. The register contract is in inc/halirq.h. Compiled into BOTH
    the minimal kernel (make-kernel.sh) and the executive kernel (make-execlink.sh).

Environment:

    Kernel mode.

--*/

#include "ki.h"
#include "halirq.h"

//
// The BCM2835 system-timer interval clock: legacy interrupt-controller bank 1,
// bit 3 (= IRQ 3), serviced at CLOCK2_LEVEL. ke/clock.c owns the ISR + the device.
//

#define HAL_CLOCK_BANK  1u
#define HAL_CLOCK_BIT   3u

extern VOID HalpClockInterrupt0(VOID);          // ke/clock.c - the clock ISR

//
// HalpInitializeInterrupts - the XXINITNT.C analog. Bring the BCM controllers to a
// known masked state, prepare the PCR IRQL translation tables, and install the
// device ISRs into PCR->InterruptRoutine[] (KiInitializeKernel already wired the
// software-interrupt vectors [0]/[APC_LEVEL]/[DISPATCH_LEVEL]). Run in both builds
// before the clock is armed (ke/kearm.c), with interrupts still masked.
//

VOID
HalpInitializeInterrupts (
    VOID
    )
{
    PKPCR Pcr = PCR;
    ULONG i;

    //
    // Mask every device interrupt at both controllers; sources are enabled one at
    // a time via HalEnableSystemInterrupt (the clock is armed by KiArmStartClock).
    // The enable/disable bits are independent write-1-to-act, so a single
    // all-ones store disables a whole bank with no read-modify-write.
    //

    HAL_REG(HAL_IRQ_DISABLE1) = 0xFFFFFFFFu;
    HAL_REG(HAL_IRQ_DISABLE2) = 0xFFFFFFFFu;
    HAL_REG(HAL_IRQ_DISABLE_BASIC) = 0xFFFFFFFFu;
    HAL_REG(HAL_CORE_TIMER_CTL(0)) = 0;             // ARM generic-timer ints off (we use the system timer)

    //
    // The PCR IRQL translation tables. The full per-IRQL controller-enable
    // snapshots (IrqlTable) and the vector->IRQL priority map (IrqlMask) are built
    // when a second device IRQL exists (the two-tier masking + nesting increment);
    // until then IRQL is the coarse CPSR.I model and the single clock dispatches
    // directly, so the tables start cleared.
    //

    for (i = 0; i < sizeof(Pcr->IrqlTable) / sizeof(Pcr->IrqlTable[0]); i += 1)
        Pcr->IrqlTable[i] = 0;
    for (i = 0; i < sizeof(Pcr->IrqlMask) / sizeof(Pcr->IrqlMask[0]); i += 1)
        Pcr->IrqlMask[i] = 0;

    //
    // Install the clock ISR at its IRQL vector and reserve the level (IPI_LEVEL ==
    // CLOCK2_LEVEL == 7 is already in ReservedVectors from KiInitializeKernel).
    //

    Pcr->InterruptRoutine[CLOCK2_LEVEL] = (PKINTERRUPT_ROUTINE)HalpClockInterrupt0;
    Pcr->ReservedVectors |= (1u << CLOCK2_LEVEL);
}

//
// KiInterruptDispatch - the generic interrupt distributor (the KiInterruptException
// analog), called from ke/trap.S KiIrqEntry with interrupts masked. Identify the
// highest-priority pending source, resolve it to its IRQL vector, and call its ISR
// through PCR->InterruptRoutine[] - the genuine "dispatch by vector, one ISR per
// interrupt" model. The clock is the only wired source today (legacy bank 1, bit 3
// -> CLOCK2_LEVEL). ACK happens inside the ISR at the source device (the BCM
// controllers have no EOI). Additional sources, the per-core IRQ_SOURCE priority
// decode, and genuine nesting arrive with the multi-device increment.
//

VOID
KiInterruptDispatch (
    VOID
    )
{
    if ((HalpBcmPending(HAL_CLOCK_BANK) & (1u << HAL_CLOCK_BIT)) != 0)
        PCR->InterruptRoutine[CLOCK2_LEVEL]();
}
