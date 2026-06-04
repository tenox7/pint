@ KE/ARM exception vectors and fault entry - the ARMv7-A analog of KE/MIPS/X4TRAP.S.
@ A VBAR-installed vector table routes each CPU exception to a handler that captures
@ the fault cause and reports it via KeBugCheckEx. Until the full trap-frame /
@ interrupt-dispatch / exception-dispatch layer is built, an unhandled kernel fault
@ terminates in a bug check (NT's real outcome for an unhandled kernel exception) -
@ but now it is REPORTED with the faulting address and status, not a silent reset.
@
@ The handlers do not return; each loads the shared trap stack and tail-calls the C
@ reporter KiArmTrapReport(type, status, address, pc).

.syntax unified
.arm
.text

.align 5                                @ VBAR requires 32-byte alignment
.global KiArmVectorTable
KiArmVectorTable:
    b       .                           @ 0x00 reset (not used post-boot)
    b       KiUndefEntry                @ 0x04 undefined instruction
    b       KiSvcEntry                  @ 0x08 supervisor call (SVC/SWI)
    b       KiPrefetchEntry             @ 0x0C prefetch abort
    b       KiDataAbortEntry            @ 0x10 data abort
    b       .                           @ 0x14 reserved
    b       KiIrqEntry                  @ 0x18 IRQ
    b       KiFiqEntry                  @ 0x1C FIQ

@ Data abort: Abort mode, lr = faulting PC + 8. DFSR (c5,c0,0), DFAR (c6,c0,0).
@ Before bug-checking, offer the fault to the SEH dispatcher: KiArmDispatchFault
@ returns the jmpbuf of the nearest registered _SEH_ frame (or 0). On a hit we
@ drop to SVC mode (the banked sp = the faulting kernel stack) and resume at the
@ _SEH_TRY via KiSehLongjmp - genuine fault recovery. Status is held in callee-
@ saved r4-r6 so it survives the call and is still available for the report path.
@ The retry path uses SPSR_abt (movs pc,lr restores CPSR from it). That is safe
@ without saving SPSR_abt because no exception can nest inside MiArmTryFillFault:
@ the CPU masks async aborts (CPSR.A) and IRQ (CPSR.I) on abort entry, and the
@ fill routine touches only KSEG0-mapped memory (the PFN database / L2 tables /
@ the demand-zero page), so it cannot take a synchronous abort. If it is ever
@ extended to touch faultable memory, save/restore SPSR_abt around the bl.
KiDataAbortEntry:
    sub     lr, lr, #8                  @ lr = faulting instruction (retry point)
    ldr     sp, =KiArmTrapStackTop
    push    {r0-r12, lr}                @ save faulting context + retry pc (56 bytes, 8-aligned)
    mrc     p15, 0, r0, c6, c0, 0       @ DFAR (fault address) -> arg0
    bl      MiArmTryFillFault           @ MM demand-fill: r0 = 1 if mapped (retry), 0 otherwise
    cmp     r0, #0
    beq     .LDataNotFilled
    pop     {r0-r12, lr}                @ restore faulting context
    movs    pc, lr                      @ retry the faulting instruction (restores CPSR from SPSR)
.LDataNotFilled:
    pop     {r0-r12, lr}                @ lr = faulting pc; sp back at trap top
    mrc     p15, 0, r4, c5, c0, 0       @ DFSR
    mrc     p15, 0, r5, c6, c0, 0       @ DFAR (fault address)
    mov     r6, lr                      @ faulting pc
    mov     r0, r5
    bl      KiArmDispatchFault          @ r0 = jmpbuf* (handled) or 0 (no frame)
    cmp     r0, #0
    beq     .LDataReport
    cpsie   a, #0x13                    @ SVC mode, re-enable async aborts
    mov     r1, #1                      @ longjmp value (non-zero = exception arm)
    b       KiSehLongjmp                @ resume at the _SEH_TRY; never returns
.LDataReport:
    mov     r0, #2                      @ type: data abort
    mov     r1, r4
    mov     r2, r5
    mov     r3, r6
    b       KiArmTrapReport

@ Prefetch abort: Abort mode, lr = faulting PC + 4. IFSR (c5,c0,1), IFAR (c6,c0,2).
KiPrefetchEntry:
    sub     lr, lr, #4
    ldr     sp, =KiArmTrapStackTop
    mrc     p15, 0, r4, c5, c0, 1       @ IFSR
    mrc     p15, 0, r5, c6, c0, 2       @ IFAR
    mov     r6, lr                      @ faulting pc
    mov     r0, r5
    bl      KiArmDispatchFault
    cmp     r0, #0
    beq     .LPrefReport
    cpsie   a, #0x13
    mov     r1, #1
    b       KiSehLongjmp
.LPrefReport:
    mov     r0, #1                      @ type: prefetch abort
    mov     r1, r4
    mov     r2, r5
    mov     r3, r6
    b       KiArmTrapReport

@ Undefined instruction: Undefined mode, lr = faulting PC + 4 (ARM state).
KiUndefEntry:
    sub     lr, lr, #4
    ldr     sp, =KiArmTrapStackTop
    mov     r0, #0                      @ type: undefined instruction
    mov     r1, #0
    mov     r2, #0
    mov     r3, lr
    b       KiArmTrapReport

@ SVC / FIQ: no system-call or FIQ path yet; report as unexpected if reached.
KiSvcEntry:
    ldr     sp, =KiArmTrapStackTop
    mov     r0, #3
    b       2f
KiFiqEntry:
    sub     lr, lr, #4
    ldr     sp, =KiArmTrapStackTop
    mov     r0, #5
2:  mov     r1, #0
    mov     r2, #0
    mov     r3, lr
    b       KiArmTrapReport

@ KTRAP_FRAME field offsets (must match inc/arm.h _KTRAP_FRAME; a _Static_assert
@ in ke/clock.c guards against drift). The frame holds the volatile state captured
@ on an interrupt/exception: r0-r3, r12, the banked sp/lr, the resume Pc and saved
@ Cpsr, then software-managed fields (OldIrql is filled by HalBeginSystemInterrupt).
.equ TrR0,               0
.equ TrR12,              16
.equ TrSp,               20
.equ TrLr,               24
.equ TrPc,               28
.equ TrCpsr,             32
.equ TrFpscr,            36
.equ TrExceptionActive,  40
.equ TrPreviousMode,     44
.equ TrOldIrql,          48
.equ TrFaultAddress,     52
.equ TrFaultStatus,      56
.equ TrOnInterruptStack, 60
.equ KTRAP_FRAME_LENGTH, 64

@ IRQ: the real interrupt entry/exit. Build a KTRAP_FRAME (inc/arm.h) on the SVC
@ (kernel) stack capturing the interrupted volatile state, and pass its address in
@ r0 to the C interrupt distributor; the C distributor and ISRs preserve r4-r11 per
@ AAPCS, so the full context is preserved. Interrupts stay masked through dispatch.
@ The return uses the IRQ-stack srs/rfe frame, so the dispatcher's reads of
@ TrapFrame->Cpsr/OldIrql (KeUpdateRunTime billing) are honored while edits to
@ Pc/Cpsr are not yet propagated to the return path - the scheduler's context-
@ switch consumption of those fields arrives with the dispatcher (increment 6).
@ The banked sp/lr captured are the SVC sp/lr: post-boot IRQs are only taken in
@ the SVC-mode idle loop (user-mode capture lands when user mode does).
KiIrqEntry:
    sub     lr, lr, #4                  @ lr_irq = interrupted instruction (resume PC)
    srsdb   sp!, #0x12                  @ IRQ stack <- {resume_pc, spsr_irq} (kept for rfe)
    cps     #0x13                       @ to SVC mode (kernel stack); IRQs stay masked

    sub     sp, sp, #KTRAP_FRAME_LENGTH @ reserve the trap frame on the SVC stack
    stm     sp, {r0-r3}                 @ TrR0..TrR3
    str     r12, [sp, #TrR12]           @ TrR12
    add     r0, sp, #KTRAP_FRAME_LENGTH @ r0 = interrupted SVC sp (before the reserve)
    str     r0, [sp, #TrSp]             @ TrSp (banked SVC sp)
    str     lr, [sp, #TrLr]             @ TrLr (banked SVC lr)

    @ Recover {resume_pc, spsr} the srsdb left on the IRQ stack (peek, do not pop -
    @ the final rfe consumes them). r0-r3 are already saved, so r0/r1 are free.
    cps     #0x12                       @ IRQ mode: sp = sp_irq -> [resume_pc, spsr]
    ldr     r0, [sp]                    @ resume_pc
    ldr     r1, [sp, #4]                @ spsr_irq
    cps     #0x13                       @ back to SVC mode
    str     r0, [sp, #TrPc]             @ TrPc
    str     r1, [sp, #TrCpsr]           @ TrCpsr

    @ Clear the software-managed tail so the distributor sees a clean frame.
    mov     r1, #0
    str     r1, [sp, #TrFpscr]
    str     r1, [sp, #TrExceptionActive]
    str     r1, [sp, #TrPreviousMode]
    str     r1, [sp, #TrOldIrql]
    str     r1, [sp, #TrFaultAddress]
    str     r1, [sp, #TrFaultStatus]
    str     r1, [sp, #TrOnInterruptStack]

    mov     r0, sp                      @ r0 = PKTRAP_FRAME
    bl      KiInterruptDistribution

    ldm     sp, {r0-r3}                 @ restore TrR0..TrR3 (dispatcher preserved r4-r11)
    ldr     r12, [sp, #TrR12]
    ldr     lr, [sp, #TrLr]             @ restore banked SVC lr
    add     sp, sp, #KTRAP_FRAME_LENGTH @ pop the trap frame

    cps     #0x12                       @ back to IRQ mode
    rfeia   sp!                         @ pop {resume_pc, spsr} -> return to interrupted code

@ VOID KiArmInitializeVectors(VOID) - select VBAR vectors (SCTLR.V=0) and install
@ the table. Called from KiArmInitialize before KiInitializeKernel.
.global KiArmInitializeVectors
KiArmInitializeVectors:
    mrc     p15, 0, r1, c1, c0, 0       @ SCTLR
    bic     r1, r1, #(1 << 13)          @ V = 0: exception base is VBAR, not 0xFFFF0000
    mcr     p15, 0, r1, c1, c0, 0
    isb
    ldr     r0, =KiArmVectorTable
    mcr     p15, 0, r0, c12, c0, 0      @ VBAR
    isb
    @ initialize the IRQ-mode stack (used by the IRQ entry's srsdb/rfe).
    mrs     r2, cpsr
    cps     #0x12                       @ IRQ mode (preserves I/F masks)
    ldr     sp, =KiArmIrqStackTop
    msr     cpsr_c, r2                  @ restore SVC mode
    isb
    bx      lr

    .ltorg

.data
.align 3
KiArmTrapStack:
    .space  4096
KiArmTrapStackTop:
KiArmIrqStack:
    .space  4096
KiArmIrqStackTop:

.section .note.GNU-stack,"",%progbits
