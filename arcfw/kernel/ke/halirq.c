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

extern VOID HalpClockInterrupt0(PKTRAP_FRAME TrapFrame);     // ke/clock.c - the clock ISR

//
// Device interrupt service routines receive the trap frame the IRQ entry built
// (ke/trap.S), so the clock ISR can hand it to KeUpdateSystemTime/KeUpdateRunTime
// for per-tick run-time accounting. PCR->InterruptRoutine[] is the generic
// PKINTERRUPT_ROUTINE; a device vector is dispatched through this signature (the
// software-interrupt vectors [APC_LEVEL]/[DISPATCH_LEVEL] keep the VOID form).
//

typedef VOID (*PKI_DEVICE_ISR)(PKTRAP_FRAME TrapFrame);

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
// KiInterruptDistribution - the generic interrupt distributor (the MIPS
// KiInterruptException / X4TRAP.S analog), called from ke/trap.S KiIrqEntry with
// interrupts masked and the freshly-built KTRAP_FRAME in r0. Identify the highest-
// priority pending source, resolve it to its IRQL vector, and call its ISR through
// PCR->InterruptRoutine[], passing the trap frame so the ISR can drive per-tick
// run-time accounting - the genuine "dispatch by vector, one ISR per interrupt"
// model. The clock is the only wired source today (legacy bank 1, bit 3 ->
// CLOCK2_LEVEL). The IRQL raise/lower bracket lives in the ISR (HalBegin/End
// SystemInterrupt); ACK happens inside the ISR at the source device (the BCM
// controllers have no EOI). Additional sources, the per-core IRQ_SOURCE priority
// decode, and genuine nesting arrive with the multi-device increment.
//
// (NB: the name is KiInterruptDistribution, not KiInterruptDispatch - the latter
// is NT's KINTERRUPT driver-ISR dispatcher, VOID-signatured in ki.h.)
//

VOID
KiInterruptDistribution (
    IN PKTRAP_FRAME TrapFrame
    )
{
    if ((HalpBcmPending(HAL_CLOCK_BANK) & (1u << HAL_CLOCK_BIT)) != 0)
        ((PKI_DEVICE_ISR)PCR->InterruptRoutine[CLOCK2_LEVEL])(TrapFrame);
}

//
// HalBeginSystemInterrupt / HalEndSystemInterrupt - the IRQL bracket an interrupt
// service routine wraps itself in (the x86 HAL model; NT keeps these HAL-private,
// declared in inc/halirq.h). The BCM controllers have NO in-controller acknowledge
// (no EOI register), so this bracket is thin: Begin only raises IRQL and rejects a
// spurious assert, End only lowers it. The interrupt is acknowledged at the SOURCE
// device by the ISR (e.g. ke/clock.c writes the system-timer CS), never here - a
// controller EOI write would be a silent no-op and the line would re-assert into an
// interrupt storm.
//

BOOLEAN
HalBeginSystemInterrupt (
    IN KIRQL Irql,
    IN ULONG Vector,
    OUT PKIRQL OldIrql
    )
{
    //
    // Spurious-reject (the X4TRAP.S deassert race): an interrupt can assert and then
    // deassert before the dispatcher reads the controller. If the source's pending
    // bit is no longer set this is not a real interrupt - leave IRQL untouched and
    // return FALSE so the ISR returns without acknowledging a phantom. The system
    // timer stays pending until its CS match bit is cleared, so a real tick passes.
    //

    if (Vector <= HAL_MAXIMUM_BCM_VECTOR &&
        (HalpBcmPending(HAL_VECTOR_BANK(Vector)) & (1u << HAL_VECTOR_BIT(Vector))) == 0)
        return FALSE;

    //
    // Raise to the interrupt's IRQL and return the level to restore. CPSR.I is
    // already masked (the CPU masked it on IRQ entry); KeRaiseIrql at the clock's
    // CLOCK2_LEVEL (>= DISPATCH_LEVEL) keeps it masked through the ISR.
    //

    KeRaiseIrql(Irql, OldIrql);
    return TRUE;
}

VOID
HalEndSystemInterrupt (
    IN KIRQL Irql,
    IN ULONG Vector
    )
{
    UNREFERENCED_PARAMETER(Vector);

    //
    // Lower back to the interrupted IRQL. KeLowerIrql's tail delivers any software
    // interrupt that became eligible during the ISR - e.g. the DISPATCH_LEVEL
    // request KeUpdateRunTime raises on a thread quantum end drains the DPC queue
    // here, before the interrupt returns.
    //

    KeLowerIrql(Irql);
}

//
// HalDisableSystemInterrupt - the JXSYSINT.C analog. Mask one system interrupt
// at the BCM legacy controller. The genuine HAL serializes against concurrent
// enable/disable with a spin lock taken at HIGH_LEVEL; on this uniprocessor
// kernel a spin lock degenerates to the IRQL raise (there is no second CPU to
// exclude), so raising to HIGH_LEVEL is the real mutual exclusion. The BCM
// enable/disable registers are independent write-1-to-act bits, so the mask is a
// single store with no read-modify-write.
//

VOID
HalDisableSystemInterrupt (
    IN ULONG Vector,
    IN KIRQL Irql
    )
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irql);

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    if (Vector <= HAL_MAXIMUM_BCM_VECTOR)
        HalpDisableBcmIrq(HAL_VECTOR_BANK(Vector), HAL_VECTOR_BIT(Vector));
    KeLowerIrql(OldIrql);
}

//
// HalEnableSystemInterrupt - the JXSYSINT.C analog. Unmask one system interrupt
// at the BCM legacy controller (see HalDisableSystemInterrupt for the locking
// model). InterruptMode (LevelSensitive / Latched) is accepted for the standard
// HAL signature but has no per-IRQ effect here: the BCM legacy controller has no
// per-source edge/level configuration register. Irql is unused until the two-tier
// IRQL masking increment builds the per-IRQL controller-enable snapshots
// (PCR->IrqlTable). Returns TRUE when the vector is a controller source.
//

BOOLEAN
HalEnableSystemInterrupt (
    IN ULONG Vector,
    IN KIRQL Irql,
    IN KINTERRUPT_MODE InterruptMode
    )
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Irql);
    UNREFERENCED_PARAMETER(InterruptMode);

    if (Vector > HAL_MAXIMUM_BCM_VECTOR)
        return FALSE;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    HalpEnableBcmIrq(HAL_VECTOR_BANK(Vector), HAL_VECTOR_BIT(Vector));
    KeLowerIrql(OldIrql);
    return TRUE;
}

//
// HalGetInterruptVector - the JXSYSINT.C analog. Map a bus interrupt to the
// system interrupt vector + IRQL a driver hands to KeInitializeInterrupt. The
// Raspberry Pi 2 has only the on-chip "Internal" bus (no ISA/EISA), so an
// Internal request passes straight through: the system vector is the BCM IRQ
// number and the IRQL is the caller-supplied bus level. Any other bus type is
// absent on this platform and resolves to nothing.
//

ULONG
HalGetInterruptVector (
    IN INTERFACE_TYPE InterfaceType,
    IN ULONG BusNumber,
    IN ULONG BusInterruptLevel,
    IN ULONG BusInterruptVector,
    OUT PKIRQL Irql,
    OUT PKAFFINITY Affinity
    )
{
    UNREFERENCED_PARAMETER(BusNumber);

    if (InterfaceType == Internal) {
        *Affinity = 1;
        *Irql = (KIRQL)BusInterruptLevel;
        return BusInterruptVector;
    }

    *Affinity = 0;
    *Irql = 0;
    return 0;
}
