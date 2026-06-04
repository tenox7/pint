/*++

Copyright (c) 2026

Module Name:

    halirq.h

Abstract:

    BCM2835 / BCM2836 interrupt-controller register contract + inline accessors -
    the single source of the Pi 2 interrupt-controller register map for the HAL
    interrupt path (ke/halirq.c, ke/clock.c).

    The Raspberry Pi 2 has TWO cascaded controllers: the legacy BCM2835 "armctrl"
    at 0x3F00B200 (the GPU / peripheral / system-timer aggregator) whose single
    combined output is routed to core 0 as the BCM2836 per-core controller's local
    IRQ 8 ("GPU IRQ"), and the BCM2836 per-core controller at 0x40000000 (ARM
    generic timer, mailbox/IPI, per-core IRQ source).

    The only CPU-level interrupt mask is CPSR.I - ARM has no MIPS-style hardware
    IRQL priority ladder, so the NT IRQL model is software-emulated on top of these
    controllers (see the HAL interrupt path). Neither controller has an EOI/ack
    register: an interrupt is acknowledged at the SOURCE device (e.g. writing the
    match bit of the system-timer CS at 0x3F003000), never here.

    Both controllers are Device / strongly-ordered memory, so every access is a
    32-bit aligned volatile load/store (HAL_REG); the enable/disable bits are
    independent (write-1-to-act) and must never be read-modify-written.

--*/

#ifndef _HALIRQ_H_
#define _HALIRQ_H_

//
// Legacy BCM2835 ARM interrupt controller (armctrl).
//

#define HAL_ARMCTRL_BASE      0x3F00B200u
#define HAL_IRQ_BASIC_PEND    (HAL_ARMCTRL_BASE + 0x00u)   // basic pending
#define HAL_IRQ_PEND1         (HAL_ARMCTRL_BASE + 0x04u)   // pending, IRQs 0-31
#define HAL_IRQ_PEND2         (HAL_ARMCTRL_BASE + 0x08u)   // pending, IRQs 32-63
#define HAL_FIQ_CONTROL       (HAL_ARMCTRL_BASE + 0x0Cu)
#define HAL_IRQ_ENABLE1       (HAL_ARMCTRL_BASE + 0x10u)   // enable, IRQs 0-31
#define HAL_IRQ_ENABLE2       (HAL_ARMCTRL_BASE + 0x14u)   // enable, IRQs 32-63
#define HAL_IRQ_ENABLE_BASIC  (HAL_ARMCTRL_BASE + 0x18u)
#define HAL_IRQ_DISABLE1      (HAL_ARMCTRL_BASE + 0x1Cu)   // disable, IRQs 0-31
#define HAL_IRQ_DISABLE2      (HAL_ARMCTRL_BASE + 0x20u)   // disable, IRQs 32-63
#define HAL_IRQ_DISABLE_BASIC (HAL_ARMCTRL_BASE + 0x24u)

//
// BCM2836 per-core local controller. The boot core is 0; the legacy controller
// and the GPU/system-timer IRQs route to core 0 (IRQ_SOURCE bit 8).
//

#define HAL_LOCAL_BASE        0x40000000u
#define HAL_CORE_TIMER_CTL(c) (HAL_LOCAL_BASE + 0x40u + 4u * (c))  // ARM generic-timer int control
#define HAL_CORE_MBOX_CTL(c)  (HAL_LOCAL_BASE + 0x50u + 4u * (c))  // mailbox int control (IPI)
#define HAL_CORE_IRQ_SRC(c)   (HAL_LOCAL_BASE + 0x60u + 4u * (c))  // per-core IRQ source (read)
#define HAL_LOCAL_GPU_PENDING (1u << 8)                            // IRQ_SOURCE: cascaded legacy controller

#define HAL_REG(a)            (*(volatile ULONG *)(ULONG)(a))

//
// Enable / disable a legacy-controller IRQ. The enable and disable registers hold
// independent write-1-to-act bits, so a single store enables (or disables) exactly
// one IRQ with no read-modify-write. Bank 1 = IRQs 0-31, bank 2 = IRQs 32-63.
//

static __inline VOID
HalpEnableBcmIrq (ULONG Bank, ULONG Bit)
{
    HAL_REG(Bank == 2 ? HAL_IRQ_ENABLE2 : HAL_IRQ_ENABLE1) = (1u << Bit);
}

static __inline VOID
HalpDisableBcmIrq (ULONG Bank, ULONG Bit)
{
    HAL_REG(Bank == 2 ? HAL_IRQ_DISABLE2 : HAL_IRQ_DISABLE1) = (1u << Bit);
}

static __inline ULONG
HalpBcmPending (ULONG Bank)
{
    return HAL_REG(Bank == 2 ? HAL_IRQ_PEND2 : HAL_IRQ_PEND1);
}

#endif // _HALIRQ_H_
