;++
;
; Module Name:
;
;    irql.asm
;
; Abstract:
;
;    IRQL management: the ARM port of KE\MIPS\XXIRQL.S.
;
;    The IRQ input of the processor is masked by CPSR.I, which serves as the
;    MIPS PSR interrupt enable: changing IRQL never changes it. The interrupt
;    controller has no priority logic, so IRQL is held in the PCR and the HAL
;    enforces it as it dispatches interrupts (the lazy IRQL of the i386 HAL):
;    a source that interrupts at or below the current IRQL is masked and
;    requested at its level.
;
;    PCR->InterruptRequest holds the requested levels, bit n for IRQL n, and
;    a request is dispatched when IRQL drops below it. A request at APC_LEVEL
;    or DISPATCH_LEVEL is a software interrupt, delivered at its level through
;    PCR->InterruptRoutine[level] (KiApcInterrupt, KiDispatchInterrupt) when
;    interrupts are enabled. One above is a hardware interrupt the HAL masked;
;    PCR->InterruptRoutine[level] unmasks its sources so they interrupt again.
;
;    The PCR is the one of the processor whose privileged thread id register
;    holds its address. A thread below DISPATCH_LEVEL with interrupts enabled
;    may be switched to another processor between reading that register and
;    using the PCR, so these routines read and change the PCR with interrupts
;    disabled, and read the register again after any call that may switch.
;
;--

#include "ksarm.h"
#include "kxarm.h"

#define CPSR_IRQ_DISABLE 0x80

        TEXTAREA

;++
;
; KIRQL
; KeGetCurrentIrql (
;    VOID
;    )
;
; Routine Description:
;
;    This function returns the current IRQL of the processor it runs on.
;
;--

        LEAF_ENTRY KeGetCurrentIrql
        mrs     r1, cpsr
        cpsid   i
        CP_READ r3, 15, 0, 13, 0, 4             ; PCR
        ldrb    r0, [r3, #PcCurrentIrql]
        msr     cpsr_c, r1
        bx      lr
        LEAF_END KeGetCurrentIrql

;++
;
; VOID
; KeLowerIrql (
;    IN KIRQL NewIrql
;    )
;
; Routine Description:
;
;    This function lowers IRQL to the specified value and dispatches the
;    requests above it.
;
;--

        LEAF_ENTRY KeLowerIrql
        mrs     r12, cpsr
        cpsid   i
        CP_READ r3, 15, 0, 13, 0, 4             ; PCR
        strb    r0, [r3, #PcCurrentIrql]
        ldr     r1, [r3, #PcInterruptRequest]
        msr     cpsr_c, r12
        lsrs    r1, r1, r0
        lsrs    r1, r1, #1
        it      eq
        bxeq    lr                              ; no request above the new IRQL
        mov     r1, r12
        b       KiDispatchInterruptRequest
        LEAF_END KeLowerIrql

;++
;
; VOID
; KeRaiseIrql (
;    IN KIRQL NewIrql,
;    OUT PKIRQL OldIrql
;    )
;
; Routine Description:
;
;    This function raises IRQL to the specified value and returns the
;    previous IRQL.
;
;--

        LEAF_ENTRY KeRaiseIrql
        mrs     r12, cpsr
        cpsid   i
        CP_READ r3, 15, 0, 13, 0, 4             ; PCR
        ldrb    r2, [r3, #PcCurrentIrql]
        strb    r0, [r3, #PcCurrentIrql]
        msr     cpsr_c, r12
        strb    r2, [r1]
        bx      lr
        LEAF_END KeRaiseIrql

;++
;
; VOID
; KiRequestSoftwareInterrupt (
;    IN KIRQL RequestIrql
;    )
;
; Routine Description:
;
;    This function requests a software interrupt. One above the current IRQL
;    is dispatched at once if interrupts are enabled (the MIPS cause bit
;    interrupts at once); otherwise when IRQL drops below it.
;
;--

        LEAF_ENTRY KiRequestSoftwareInterrupt
        mrs     r2, cpsr
        cpsid   i
        CP_READ r3, 15, 0, 13, 0, 4             ; PCR
        movs    r1, #1
        lsls    r1, r1, r0
        ldr     r12, [r3, #PcInterruptRequest]
        orr     r12, r12, r1
        str     r12, [r3, #PcInterruptRequest]
        ldrb    r1, [r3, #PcCurrentIrql]
        msr     cpsr_c, r2
        cmp     r0, r1
        it      ls
        bxls    lr                              ; at or below the current IRQL
        tst     r2, #CPSR_IRQ_DISABLE
        it      ne
        bxne    lr                              ; interrupts disabled
        mov     r0, r1
        mov     r1, r2
        b       KiDispatchInterruptRequest
        LEAF_END KiRequestSoftwareInterrupt

;++
;
; VOID
; KiDispatchInterruptRequest (
;    IN KIRQL Irql,
;    IN ULONG Psr
;    )
;
; Routine Description:
;
;    This function dispatches the requests above Irql, highest level first. A
;    hardware request is cleared and PCR->InterruptRoutine[level] called with
;    interrupts disabled. A software request is dispatched only if interrupts
;    are enabled in Psr: it is cleared, IRQL raised to its level, then
;    interrupts are enabled and PCR->InterruptRoutine[level] called, which may
;    switch context, here or on return on another processor. Interrupts stay
;    disabled until IRQL is raised, so an interrupt taken during the dispatch
;    nests above the level of the request. Returns at Irql with interrupts as
;    at the call.
;
;--

        NESTED_ENTRY KiDispatchInterruptRequest
        PROLOG_PUSH {r4-r8, lr}
        mrs     r7, cpsr
        cpsid   i
        mov     r4, r0
        movs    r6, #2
        lsls    r6, r6, r4
        subs    r6, r6, #1                      ; the levels at or below Irql
        tst     r1, #CPSR_IRQ_DISABLE
        it      ne
        orrne   r6, r6, #(1 << APC_LEVEL) | (1 << DISPATCH_LEVEL) ; disabled: no software interrupts
10      cpsid   i
        CP_READ r5, 15, 0, 13, 0, 4             ; PCR of the processor run on now
        ldr     r1, [r5, #PcInterruptRequest]
        bics    r2, r1, r6
        beq     %F20
        clz     r2, r2
        rsb     r2, r2, #31                     ; highest requested level
        movs    r3, #1
        lsls    r3, r3, r2
        bic     r1, r1, r3
        str     r1, [r5, #PcInterruptRequest]
        add     r0, r5, #PcInterruptRoutine
        ldr     r0, [r0, r2, lsl #2]
        movs    r1, #0
        cmp     r2, #DISPATCH_LEVEL
        bhi     %F15
        strb    r2, [r5, #PcCurrentIrql]
        cpsie   i
        blx     r0                              ; software interrupt
        cpsid   i
        CP_READ r5, 15, 0, 13, 0, 4             ; PCR of the processor run on now
        strb    r4, [r5, #PcCurrentIrql]
        b       %B10
15      blx     r0                              ; unmask the hardware interrupt
        b       %B10
20      msr     cpsr_c, r7
        EPILOG_POP {r4-r8, pc}
        NESTED_END KiDispatchInterruptRequest

;++
;
; BOOLEAN
; KiDisableInterrupts (
;    VOID
;    )
;
; Routine Description:
;
;    This function disables interrupts and returns TRUE if they were enabled.
;
;--

        LEAF_ENTRY KiDisableInterrupts
        mrs     r1, cpsr
        cpsid   i
        tst     r1, #CPSR_IRQ_DISABLE
        ite     eq
        moveq   r0, #1
        movne   r0, #0
        bx      lr
        LEAF_END KiDisableInterrupts

;++
;
; VOID
; KiRestoreInterrupts (
;    IN BOOLEAN Enable
;    )
;
; Routine Description:
;
;    This function enables interrupts if Enable is TRUE.
;
;--

        LEAF_ENTRY KiRestoreInterrupts
        tst     r0, #0xff
        beq     %F10
        cpsie   i
10      bx      lr
        LEAF_END KiRestoreInterrupts

        END
