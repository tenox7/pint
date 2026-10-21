;++
;
; Module Name:
;
;    trap.asm
;
; Abstract:
;
;    The exception vectors of the processor and the dispatch of traps, system
;    services and interrupts: the ARM port of KE\MIPS\X4TRAP.S. Exceptions are
;    taken in Thumb state (SCTLR.TE).
;
;    Aborts are first offered to the TB miss handler (KiFillTb), which runs on
;    the abort mode stack. Every other abort, and an undefined instruction,
;    enters KiTrapEntry, which builds a trap frame on the kernel stack; the
;    trapped r0 and r1, resume address, status and fault address and status
;    are parked in the PCR meanwhile (the MIPS SavedT7/SavedEpc), as building
;    the frame may itself take a TB miss. KiTrapDispatch then saves the
;    nonvolatile state in an exception frame below the trap frame and calls a
;    C handler that resolves the trap or describes an exception, which is
;    dispatched by KiDispatchException.
;
;    A system service enters KiSystemService by an svc from user mode, with
;    the service number in r12, or by a branch to KiServiceInternal from a
;    kernel mode stub. Interrupts enter KiInterruptException, which calls the
;    HAL interrupt dispatcher through PCR->InterruptRoutine[IRQ_VECTOR] on the
;    interrupt stack. Each builds the same trap and exception frames, whose
;    unwind data describes them, and returns through KiTrapExit, which
;    dispatches the interrupt requests made meanwhile.
;
;    On every entry from user mode the trap frame is the base trap frame at
;    the thread's initial stack, and the user nonvolatile state is in the
;    exception frame immediately below it, where the context of the thread is
;    read and set (ps\psctxarm.c) and from where KiTrapExit reloads it.
;
;--

#include "ksarm.h"
#include "kxarm.h"

#define MODE_USR 0x10
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_UND 0x1b
#define MODE_SYS 0x1f
#define MODE_MASK 0x1f
#define CPSR_IRQ_DISABLE 0x80
#define CPSR_ABORT_DISABLE 0x100

#define SCTLR_V 0x2000
#define SCTLR_TE 0x40000000

;
; The processors, and the sizes of the abort and undefined mode stacks of each.
;

#define KI_ARM_PROCESSORS 4
#define ABORT_STACK_SHIFT 12
#define ABORT_STACK_SIZE 4096
#define UNDEFINED_STACK_SHIFT 6
#define UNDEFINED_STACK_SIZE 64

#define UserMode 1
#define KernelMode 0

;
; The stack a kernel mode trap needs below the trapped stack pointer.
;

#define TRAP_STACK_NEEDED (16 + TrapFrameLength + ExceptionFrameLength + ExceptionRecordLength + 8)

;
; The offsets of the trap frame from the stack pointer in the bodies of the
; trap dispatch, system service and trap exit.
;

#define TdTrapFrame (8 + ExceptionRecordLength + ExceptionFrameLength)
#define SsTrapFrame (TrapFrameArguments + ExceptionFrameLength)
#define TxTrapFrame (ExceptionFrameLength + 8)

        IMPORT  KiFillTb
        IMPORT  KiDataAbortException
        IMPORT  KiPrefetchAbortException
        IMPORT  KiUndefinedInstruction
        IMPORT  KiDispatchException
        IMPORT  KiDispatchInterruptRequest
        IMPORT  KiDeliverApc
        IMPORT  KiPanicStackSwitch
        IMPORT  KiSystemServiceHandler
        IMPORT  KiServiceTable
        IMPORT  KiServiceLimit
        IMPORT  KiArgumentTable
        IMPORT  KiSetServerWaitClientEvent
        IMPORT  KeRaiseIrql
        IMPORT  KeLowerIrql
        IMPORT  KeBugCheckEx

;
; The exception vectors. VBAR requires 32-byte alignment.
;

        AREA    |.text$vec|, CODE, READONLY, ALIGN=5
        EXPORT  KiArmVectorTable
KiArmVectorTable
        b.w     KiArmVectorTable                ; 0x00 reset
        b.w     KiUndefinedInstructionEntry     ; 0x04 undefined instruction
        b.w     KiSystemServiceEntry            ; 0x08 supervisor call
        b.w     KiPrefetchAbortEntry            ; 0x0c prefetch abort
        b.w     KiDataAbortEntry                ; 0x10 data abort
        b.w     KiArmVectorTable                ; 0x14 reserved
        b.w     KiInterruptEntry                ; 0x18 interrupt
        b.w     KiFastInterruptEntry            ; 0x1c fast interrupt

        TEXTAREA

;
; Data abort: abort mode, lr = the aborted instruction + 8. The TB miss
; handler first: a translation or write fault on an address memory management
; has validly mapped is satisfied by copying its PTE into the hardware tables,
; and the aborted instruction is restarted. KiFillTb touches only the direct
; map, so it cannot itself abort.
;

        LEAF_ENTRY_NO_PDATA KiDataAbortEntry
        sub     lr, lr, #8                      ; the aborted instruction
        push    {r0-r3, r12, lr}
        CP_READ r0, 15, 0, 6, 0, 0              ; DFAR
        CP_READ r1, 15, 0, 5, 0, 0              ; DFSR
        mrs     r2, spsr                        ; the aborted mode
        bl      KiFillTb
        cmp     r0, #0
        pop     {r0-r3, r12, lr}
        it      ne
        subsne  pc, lr, #0                      ; filled: restart the instruction
        push    {r0, r1}
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [sp]
        str     r1, [r0, #PcSavedR0]
        ldr     r1, [sp, #4]
        str     r1, [r0, #PcSavedR1]
        add     sp, sp, #8
        CP_READ r1, 15, 0, 6, 0, 0              ; DFAR
        str     r1, [r0, #PcFaultAddress]
        CP_READ r1, 15, 0, 5, 0, 0              ; DFSR
        str     r1, [r0, #PcFaultStatus]
        ldr     r1, =KiDataAbortException
        b       KiTrapEntry
        LEAF_END_NO_PDATA KiDataAbortEntry

;
; Prefetch abort: abort mode, lr = the aborted instruction + 4. A breakpoint
; instruction is a prefetch abort with a debug event status.
;

        LEAF_ENTRY_NO_PDATA KiPrefetchAbortEntry
        sub     lr, lr, #4                      ; the aborted instruction
        push    {r0-r3, r12, lr}
        CP_READ r0, 15, 0, 6, 0, 2              ; IFAR
        CP_READ r1, 15, 0, 5, 0, 1              ; IFSR
        mrs     r2, spsr
        bl      KiFillTb
        cmp     r0, #0
        pop     {r0-r3, r12, lr}
        it      ne
        subsne  pc, lr, #0                      ; filled: restart the fetch
        push    {r0, r1}
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [sp]
        str     r1, [r0, #PcSavedR0]
        ldr     r1, [sp, #4]
        str     r1, [r0, #PcSavedR1]
        add     sp, sp, #8
        CP_READ r1, 15, 0, 6, 0, 2              ; IFAR
        str     r1, [r0, #PcFaultAddress]
        CP_READ r1, 15, 0, 5, 0, 1              ; IFSR
        str     r1, [r0, #PcFaultStatus]
        ldr     r1, =KiPrefetchAbortException
        b       KiTrapEntry
        LEAF_END_NO_PDATA KiPrefetchAbortEntry

;
; Undefined instruction: undefined mode, lr = the instruction + 2 in Thumb
; state, + 4 in ARM state.
;

        LEAF_ENTRY_NO_PDATA KiUndefinedInstructionEntry
        push    {r0, r1}
        mrs     r0, spsr
        tst     r0, #CPSR_THUMB
        ite     ne
        subne   lr, lr, #2
        subeq   lr, lr, #4                      ; the undefined instruction
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [sp]
        str     r1, [r0, #PcSavedR0]
        ldr     r1, [sp, #4]
        str     r1, [r0, #PcSavedR1]
        add     sp, sp, #8
        movs    r1, #0
        str     r1, [r0, #PcFaultAddress]
        str     r1, [r0, #PcFaultStatus]
        ldr     r1, =KiUndefinedInstruction
        b       KiTrapEntry
        LEAF_END_NO_PDATA KiUndefinedInstructionEntry

;
; KiTrapEntry - the common trap entry, in abort or undefined mode with r0 =
; the PCR, r1 = the C handler, lr = the resume address and the trapped r0, r1
; and fault address and status in the PCR.
;
; The trap frame is built on the kernel stack: for a user mode trap it is the
; base trap frame; for a kernel mode trap it is below the trapped stack
; pointer S, 8-byte aligned, leaving room above it for the final rfe slots at
; S - 8. The handler,
;
;     BOOLEAN Handler(PKTRAP_FRAME TrapFrame, PEXCEPTION_RECORD ExceptionRecord)
;
; returns FALSE if it resolved the trap and TRUE to dispatch the exception it
; described.
;

        LEAF_ENTRY_NO_PDATA KiTrapEntry
        str     lr, [r0, #PcSavedPc]
        mrs     lr, spsr
        str     lr, [r0, #PcSavedCpsr]
        cps     #MODE_SVC                       ; interrupts stay disabled
        ldr     r0, [r0, #PcSavedCpsr]
        and     r0, r0, #MODE_MASK
        cmp     r0, #MODE_USR
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r0, [r0, #PcInitialStack]
        bne     %F10
        sub     r0, r0, #TrapFrameLength        ; user mode: the base trap frame
        b       %F20
10      sub     r0, r0, #KERNEL_STACK_SIZE      ; kernel mode: check for stack overflow
        add     r0, r0, #TRAP_STACK_NEEDED
        cmp     sp, r0
        blo     KiKernelStackOverflow
        sub     r0, sp, #16 + TrapFrameLength
        bic     r0, r0, #7
20      str     r2, [r0, #TrR2]
        str     r3, [r0, #TrR3]
        str     r12, [r0, #TrR12]
        str     sp, [r0, #TrSp]                 ; the trapped sp and lr (kernel mode)
        str     lr, [r0, #TrLr]
        mov     sp, r0
        mov     r12, r1                         ; handler
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [r0, #PcSavedR0]
        str     r1, [sp, #TrR0]
        ldr     r1, [r0, #PcSavedR1]
        str     r1, [sp, #TrR1]
        ldr     r1, [r0, #PcSavedPc]
        str     r1, [sp, #TrPc]
        ldr     r1, [r0, #PcFaultAddress]
        str     r1, [sp, #TrFaultAddress]
        ldr     r1, [r0, #PcFaultStatus]
        str     r1, [sp, #TrFaultStatus]
        ldr     r1, [r0, #PcSavedCpsr]
        str     r1, [sp, #TrCpsr]
        vmrs    r2, fpscr
        str     r2, [sp, #TrFpscr]
        movs    r2, #0
        str     r2, [sp, #TrExceptionActive]
        str     r2, [sp, #TrOldIrql]
        str     r2, [sp, #TrOnInterruptStack]
        and     r3, r1, #MODE_MASK
        cmp     r3, #MODE_USR
        bne     %F30
        movs    r2, #UserMode
        cps     #MODE_SYS                       ; the user sp and lr
        mov     r0, sp
        mov     r3, lr
        cps     #MODE_SVC
        str     r0, [sp, #TrSp]
        str     r3, [sp, #TrLr]
30      str     r2, [sp, #TrPreviousMode]
        b       KiTrapDispatch
        LEAF_END_NO_PDATA KiTrapEntry

;
; KiTrapDispatch - entered with sp = the trap frame and r12 = the handler.
; The nonvolatile state is saved in the exception frame below the trap frame,
; and below it are the exception record and the fifth argument of
; KiDispatchException. The handler is called with interrupts as they were at
; the trap.
;
; KiExceptionDispatch saves the volatile floating state in the trap frame and
; calls KiDispatchException(ExceptionRecord, ExceptionFrame, TrapFrame,
; PreviousMode, TRUE), then resumes through KiExceptionExit.
;

        NESTED_ENTRY KiTrapDispatch
        PROLOG_PUSH_TRAP_FRAME
        PROLOG_STACK_ALLOC 16                   ; Fpscr, Psr, SwapReturn, Lr
        PROLOG_PUSH {r4-r11}
        PROLOG_VPUSH {d8-d15}
        PROLOG_STACK_ALLOC ExceptionRecordLength + 8
        ldr     r0, [sp, #TdTrapFrame + TrCpsr]
        mrs     r1, cpsr
        bic     r1, r1, #CPSR_IRQ_DISABLE | CPSR_ABORT_DISABLE
        and     r0, r0, #CPSR_IRQ_DISABLE | CPSR_ABORT_DISABLE
        orr     r1, r1, r0
        msr     cpsr_xc, r1                     ; interrupts as at the trap
        add     r0, sp, #TdTrapFrame
        add     r1, sp, #8                      ; exception record
        blx     r12                             ; resolve the trap or describe the exception
        cmp     r0, #0
        bne     KiExceptionDispatch
        add     sp, sp, #ExceptionRecordLength + 8
        vpop    {d8-d15}
        pop     {r4-r11}
        add     sp, sp, #16
        b       KiTrapExit

        EXPORT  KiExceptionDispatch
KiExceptionDispatch
        add     r2, sp, #TdTrapFrame
        add     r0, r2, #TrVolatileD            ; save the volatile floating state
        vstm    r0!, {d0-d7}
        vstm    r0, {d16-d31}
        add     r0, sp, #8                      ; exception record
        add     r1, sp, #8 + ExceptionRecordLength ; exception frame
        ldr     r3, [r2, #TrPreviousMode]
        movs    r12, #1                         ; first chance
        str     r12, [sp]
        bl      KiDispatchException
        add     sp, sp, #ExceptionRecordLength + 8
        b       KiExceptionExit
        NESTED_END KiTrapDispatch

;
; KiExceptionExit - restore the volatile floating state from the trap frame
; and the nonvolatile state from the exception frame at sp, and exit through
; KiTrapExit. Also the exit of a user thread's startup.
;

        LEAF_ENTRY_NO_PDATA KiExceptionExit
        add     r1, sp, #ExceptionFrameLength   ; trap frame
        add     r0, r1, #TrVolatileD
        vldm    r0!, {d0-d7}
        vldm    r0, {d16-d31}
        vpop    {d8-d15}
        pop     {r4-r11}
        add     sp, sp, #16
        b       KiTrapExit
        LEAF_END_NO_PDATA KiExceptionExit

;
; KiTrapExit - return from the trap frame at sp, with the nonvolatile state
; of the trapped code in its registers, or, for user mode, in the exception
; frame below the trap frame. If the trapped code resumes with interrupts
; enabled, the interrupt requests above the current IRQL are dispatched first
; (on MIPS the pending cause bits interrupt as the PSR is restored), with
; interrupts disabled until IRQL is raised to the level of a request: an
; interrupt that is still asserted then nests at that level, and does not
; dispatch the request again on a deeper stack each time it is taken. A kernel
; mode trap resumes through the rfe slots below the trapped stack pointer. A
; user mode trap first clears kernel mode alerted and delivers any pending
; user mode APC (the MIPS exits request an APC interrupt; here the exit loop
; delivers it at APC_LEVEL), then reloads the nonvolatile state from the
; exception frame and resumes through SPSR_svc, leaving the kernel stack
; empty.
;

        NESTED_ENTRY KiTrapExit
        PROLOG_PUSH_TRAP_FRAME
        PROLOG_STACK_ALLOC ExceptionFrameLength + 8 ; the exception frame, and the old IRQL below it
10      cpsid   i
        ldr     r2, [sp, #TxTrapFrame + TrCpsr]
        tst     r2, #CPSR_IRQ_DISABLE
        bne     %F20                            ; resumed with interrupts disabled
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [r0, #PcInterruptRequest]
        ldrb    r0, [r0, #PcCurrentIrql]
        lsrs    r1, r1, r0
        lsrs    r1, r1, #1
        beq     %F20                            ; no request above the current IRQL
        mov     r1, r2                          ; resumed with interrupts enabled
        bl      KiDispatchInterruptRequest
        b       %B10

20      and     r3, r2, #MODE_MASK
        cmp     r3, #MODE_USR
        beq     %F40
        add     sp, sp, #TxTrapFrame            ; kernel mode
        ldr     r0, [sp, #TrFpscr]
        vmsr    fpscr, r0
        ldr     r0, [sp, #TrSp]
        ldr     r1, [sp, #TrPc]
        bic     r1, r1, #1
        strd    r1, r2, [r0, #-8]               ; rfe slots <- resume pc, cpsr
        ldr     lr, [sp, #TrLr]
        ldr     r12, [sp, #TrR12]
        ldm     sp, {r0-r3}
        ldr     sp, [sp, #TrSp]
        sub     sp, sp, #8
        rfeia   sp!                             ; resume the trapped kernel code

40      CP_READ r1, 15, 0, 13, 0, 4             ; user mode
        ldr     r1, [r1, #PcCurrentThread]
        movs    r3, #0
        strb    r3, [r1, #ThAlerted]            ; clear kernel mode alerted
        ldrb    r3, [r1, #ThApcState + AsUserApcPending]
        cmp     r3, #0
        bne     %F50
        add     sp, sp, #TxTrapFrame
        ldr     r0, [sp, #TrFpscr]
        vmsr    fpscr, r0
        sub     r0, sp, #ExceptionFrameLength
        vldm    r0!, {d8-d15}                   ; reload the user nonvolatile state
        ldm     r0, {r4-r11}
        msr     spsr_cxsf, r2
        ldr     r0, [sp, #TrSp]
        ldr     r1, [sp, #TrLr]
        cps     #MODE_SYS                       ; the user sp and lr
        mov     sp, r0
        mov     lr, r1
        cps     #MODE_SVC
        ldr     lr, [sp, #TrPc]
        bic     lr, lr, #1
        ldr     r12, [sp, #TrR12]
        ldm     sp, {r0-r3}
        add     sp, sp, #TrapFrameLength
        subs    pc, lr, #0                      ; resume user mode

50      movs    r0, #APC_LEVEL                  ; deliver the user mode APC
        mov     r1, sp                          ; old IRQL
        bl      KeRaiseIrql
        cpsie   i
        movs    r0, #UserMode
        add     r1, sp, #8                      ; exception frame
        add     r2, sp, #TxTrapFrame            ; trap frame
        bl      KiDeliverApc
        ldrb    r0, [sp]
        bl      KeLowerIrql
        b       %B10
        NESTED_END KiTrapExit

;
; The kernel stack has overflowed: switch to the panic stack and bug check
; (the MIPS XCODE_PANIC path).
;

        LEAF_ENTRY_NO_PDATA KiKernelStackOverflow
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     sp, [r0, #PcPanicStack]
        str     sp, [r0, #PcInitialStack]
        b       KiPanicStackSwitch
        LEAF_END_NO_PDATA KiKernelStackOverflow

;
; KiSystemServiceEntry - an svc from user mode: supervisor mode with
; interrupts disabled, lr = the address after the svc, sp = the initial
; kernel stack of the thread, r12 = the service number and r0 - r3 the first
; four arguments. The base trap frame is built and the service dispatched by
; KiSystemService; it returns after the svc.
;

        LEAF_ENTRY_NO_PDATA KiSystemServiceEntry
        sub     sp, sp, #TrapFrameLength        ; the base trap frame
        stm     sp, {r0-r3}
        str     r12, [sp, #TrR12]
        str     lr, [sp, #TrPc]
        mrs     r0, spsr
        str     r0, [sp, #TrCpsr]
        and     r0, r0, #MODE_MASK
        cmp     r0, #MODE_USR
        bne     KiKernelModeSvc
        cps     #MODE_SYS                       ; the user sp and lr
        mov     r0, sp
        mov     r1, lr
        cps     #MODE_SVC
        str     r0, [sp, #TrSp]
        str     r1, [sp, #TrLr]
        movs    r1, #UserMode
        b       KiSystemService

;
; An svc in kernel mode has replaced the link register of the code that
; executed it: kernel mode services are called through KiServiceInternal.
;

KiKernelModeSvc
        ldr     r0, =TRAP_CAUSE_UNKNOWN
        ldr     r1, [sp, #TrPc]
        ldr     r2, [sp, #TrCpsr]
        movs    r3, #0
        str     r3, [sp]
        bl      KeBugCheckEx
        LEAF_END_NO_PDATA KiSystemServiceEntry

;
; KiServiceInternal - a system service called in kernel mode, by the branch
; of a stub with r12 = the service number and lr = the return address. A trap
; frame is built below the stack pointer, as the trap of an svc in kernel
; mode would; the service returns to lr.
;

        LEAF_ENTRY_NO_PDATA KiServiceInternal
        sub     sp, sp, #16 + TrapFrameLength
        stm     sp, {r0-r3}
        str     r12, [sp, #TrR12]
        add     r0, sp, #16 + TrapFrameLength
        str     r0, [sp, #TrSp]
        str     lr, [sp, #TrLr]
        bic     r0, lr, #1
        str     r0, [sp, #TrPc]
        mrs     r0, cpsr
        orr     r0, r0, #CPSR_THUMB
        str     r0, [sp, #TrCpsr]
        cpsid   i
        movs    r1, #KernelMode
        b       KiSystemService
        LEAF_END_NO_PDATA KiServiceInternal

;
; KiSystemService - the system service dispatcher (the MIPS
; KiSystemServiceDispatch), entered with interrupts disabled, sp = the trap
; frame, r1 = the new previous mode and r12 = the service number. The
; nonvolatile state is saved in the exception frame below the trap frame and
; the previous mode of the thread in the trap frame. The arguments passed in
; memory are captured from the caller's stack below the exception frame, and
; the service is called with interrupts as at the call.
;
;   r4 - trap frame
;   r5 - PCR
;   r6 - current thread
;   r7 - previous mode
;
; The exception handler of the dispatcher, KiSystemServiceHandler, completes
; the service with the status of an exception that the capture of the
; arguments raises.
;

        NESTED_ENTRY KiSystemService,,KiSystemServiceHandler
        PROLOG_PUSH_TRAP_FRAME
        PROLOG_STACK_ALLOC 16                   ; Fpscr, Psr, SwapReturn, Lr
        PROLOG_PUSH {r4-r11}
        PROLOG_VPUSH {d8-d15}
        PROLOG_STACK_ALLOC TrapFrameArguments
        add     r4, sp, #SsTrapFrame
        vmrs    r0, fpscr
        str     r0, [r4, #TrFpscr]
        movs    r0, #0
        str     r0, [r4, #TrExceptionActive]
        str     r0, [r4, #TrOldIrql]
        str     r0, [r4, #TrFaultAddress]
        str     r0, [r4, #TrFaultStatus]
        str     r0, [r4, #TrOnInterruptStack]
        CP_READ r5, 15, 0, 13, 0, 4             ; PCR
        ldr     r6, [r5, #PcCurrentThread]
        ldrb    r0, [r6, #ThPreviousMode]
        str     r0, [r4, #TrPreviousMode]       ; save the old previous mode
        strb    r1, [r6, #ThPreviousMode]       ; set the new previous mode
        mov     r7, r1
        ldr     r0, [r4, #TrCpsr]
        cmp     r12, #0
        blt     %F70                            ; negative: a fast event pair service
        ldr     r3, =KiServiceLimit
        ldr     r3, [r3]
        cmp     r12, r3
        bhs     %F60                            ; invalid service number
        ldr     r3, =KiServiceTable
        ldr     r8, [r3, r12, lsl #2]           ; service routine
        ldr     r3, =KiArgumentTable
        ldrb    r9, [r3, r12]                   ; bytes of arguments in memory
        tst     r0, #CPSR_IRQ_DISABLE
        bne     %F10
        cpsie   i                               ; interrupts as at the call
10      cmp     r9, #0
        beq     %F40

;
; Capture the arguments passed in memory on the caller's stack, so the caller
; cannot change them after they are probed. A user stack outside the user
; region is replaced by the probe address, which faults.
;

        ldr     r3, [r4, #TrSp]
        cmp     r7, #UserMode
        bne     KiSystemServiceStartAddress
        ldr     r0, =MM_USER_PROBE_ADDRESS
        cmp     r3, r0
        it      hs
        movhs   r3, r0

        EXPORT  KiSystemServiceStartAddress
KiSystemServiceStartAddress
        mov     r0, sp
20      ldr     r1, [r3], #4
        str     r1, [r0], #4
        subs    r9, r9, #4
        bne     %B20

        EXPORT  KiSystemServiceEndAddress
KiSystemServiceEndAddress
40      ldm     r4, {r0-r3}                     ; the arguments in registers
        blx     r8                              ; call the system service

;
; KiSystemServiceExit - store the completion status, count the call, restore
; the previous mode of the thread, and return through the trap frame.
;

        EXPORT  KiSystemServiceExit
KiSystemServiceExit
        str     r0, [r4, #TrR0]
        cpsid   i
        CP_READ r5, 15, 0, 13, 0, 4             ; PCR of the processor run on now
        ldr     r1, [r5, #PcPrcb]
        ldr     r2, [r1, #PbSystemCalls]
        adds    r2, r2, #1
        str     r2, [r1, #PbSystemCalls]
        ldr     r3, [r4, #TrPreviousMode]
        strb    r3, [r6, #ThPreviousMode]       ; restore the old previous mode
        add     sp, sp, #TrapFrameArguments
        vpop    {d8-d15}
        pop     {r4-r11}
        add     sp, sp, #16
        b       KiTrapExit

;
; An invalid service number completes with STATUS_INVALID_SYSTEM_SERVICE.
;

60      ldr     r0, =STATUS_INVALID_SYSTEM_SERVICE
        b       KiSystemServiceExit

;
; A negative service number is the fast path event pair service of a client
; or server thread: the bit of the number that selects which event of the
; thread's pair is set and which is waited on is the displacement from one
; event to the other.
;

70      ldr     r8, [r6, #EtEventPair]
        ldr     r9, =STATUS_NO_EVENT_PAIR
        cmp     r8, #0
        beq     %F80                            ; no event pair is associated with the thread
        tst     r0, #CPSR_IRQ_DISABLE
        bne     %F75
        cpsie   i                               ; interrupts as at the call
75      and     r3, r12, #SET_EVENT_PAIR_MASK
        add     r0, r8, #EpEventLow
        add     r0, r0, r3                      ; the event to set
        add     r1, r8, #EpEventHigh
        sub     r1, r1, r3                      ; the event to wait on
        mov     r2, r7                          ; the wait mode
        bl      KiSetServerWaitClientEvent
        b       KiSystemServiceExit
80      mov     r0, r9
        b       KiSystemServiceExit
        NESTED_END KiSystemService

;
; KiFastInterruptEntry - the fast interrupt input is not used and stays masked.
;

        LEAF_ENTRY_NO_PDATA KiFastInterruptEntry
        subs    pc, lr, #4
        LEAF_END_NO_PDATA KiFastInterruptEntry

;
; KiInterruptEntry - IRQ mode, lr = the interrupted instruction + 4, with
; interrupts disabled. The resume state is pushed on the supervisor stack and
; the trap frame built as for a trap: the base trap frame for user mode, below
; the interrupted stack pointer for kernel mode.
;

        LEAF_ENTRY_NO_PDATA KiInterruptEntry
        sub     lr, lr, #4
        srsdb   sp!, #MODE_SVC                  ; resume pc and cpsr to the supervisor stack
        cps     #MODE_SVC                       ; interrupts stay disabled
        push    {r0, r1}
        ldr     r0, [sp, #12]                   ; interrupted cpsr
        and     r0, r0, #MODE_MASK
        cmp     r0, #MODE_USR
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [r0, #PcInitialStack]
        bne     %F10
        sub     r1, r1, #TrapFrameLength        ; user mode: the base trap frame
        b       %F20
10      sub     r1, r1, #KERNEL_STACK_SIZE      ; kernel mode: check for stack overflow
        add     r1, r1, #TRAP_STACK_NEEDED
        cmp     sp, r1
        bhs     %F15
        ldr     r1, [sp, #8]                    ; the interrupted pc, as a trap saves it
        str     r1, [r0, #PcSavedPc]
        movs    r1, #0
        str     r1, [r0, #PcFaultAddress]
        b       KiKernelStackOverflow
15      sub     r1, sp, #TrapFrameLength        ; below the rfe slots and the scratch
        bic     r1, r1, #7
20      str     r2, [r1, #TrR2]
        str     r3, [r1, #TrR3]
        str     r12, [r1, #TrR12]
        pop     {r2, r3}
        str     r2, [r1, #TrR0]
        str     r3, [r1, #TrR1]
        pop     {r2, r3}                        ; resume pc, cpsr
        str     r2, [r1, #TrPc]
        str     r3, [r1, #TrCpsr]
        str     sp, [r1, #TrSp]                 ; the interrupted sp and lr (kernel mode)
        str     lr, [r1, #TrLr]
        mov     sp, r1
        vmrs    r2, fpscr
        str     r2, [sp, #TrFpscr]
        movs    r2, #0
        str     r2, [sp, #TrExceptionActive]
        str     r2, [sp, #TrPreviousMode]
        str     r2, [sp, #TrFaultAddress]
        str     r2, [sp, #TrFaultStatus]
        and     r3, r3, #MODE_MASK
        cmp     r3, #MODE_USR
        bne     %F30
        cps     #MODE_SYS                       ; the user sp and lr
        mov     r0, sp
        mov     r2, lr
        cps     #MODE_SVC
        str     r0, [sp, #TrSp]
        str     r2, [sp, #TrLr]
30      b       KiInterruptException
        LEAF_END_NO_PDATA KiInterruptEntry

;
; KiInterruptException - entered with interrupts disabled and sp = the trap
; frame. Every interrupt is above DISPATCH_LEVEL, so the HAL interrupt
; dispatcher runs on the interrupt stack (the MIPS KiSwitchStacks): it is
; called through PCR->InterruptRoutine[IRQ_VECTOR] with r0 = its address and
; r1 = the trap frame, raises IRQL to the level of the interrupt it takes and
; returns with interrupts disabled. The previous IRQL is then restored and the
; interrupt returns through KiTrapExit, which dispatches the requests it made,
; so it may end in a context switch.
;
;   r4 - exception frame
;   r5 - trap frame
;   r6 - PCR
;

        NESTED_ENTRY KiInterruptException
        PROLOG_PUSH_TRAP_FRAME
        PROLOG_STACK_ALLOC 16                   ; Fpscr, Psr, SwapReturn, Lr
        PROLOG_PUSH {r4-r11}
        PROLOG_VPUSH {d8-d15}
        PROLOG_STACK_SAVE r4
        add     r5, sp, #ExceptionFrameLength
        CP_READ r6, 15, 0, 13, 0, 4             ; PCR
        ldrb    r0, [r6, #PcCurrentIrql]
        str     r0, [r5, #TrOldIrql]
        ldr     r0, [r6, #PcOnInterruptStack]
        str     r0, [r5, #TrOnInterruptStack]
        str     sp, [r6, #PcOnInterruptStack]   ; set the stack indicator
        cmp     r0, #0
        bne     %F10                            ; already on the interrupt stack
        ldr     r0, [r6, #PcInitialStack]
        str     r0, [r6, #PcSavedInitialStack]
        ldr     sp, [r6, #PcInterruptStack]
        str     sp, [r6, #PcInitialStack]
10      ldr     r0, [r6, #PcInterruptRoutine + IRQ_VECTOR * 4]
        mov     r1, r5
        blx     r0                              ; the HAL interrupt dispatcher
        ldr     r0, [r5, #TrOnInterruptStack]
        str     r0, [r6, #PcOnInterruptStack]   ; restore the stack indicator
        cmp     r0, #0
        bne     %F20
        ldr     r0, [r6, #PcSavedInitialStack]
        str     r0, [r6, #PcInitialStack]       ; back on the thread stack
20      ldr     r0, [r6, #PcPrcb]
        ldr     r1, [r0, #PbInterruptCount]
        adds    r1, r1, #1
        str     r1, [r0, #PbInterruptCount]
        ldr     r0, [r5, #TrOldIrql]
        strb    r0, [r6, #PcCurrentIrql]        ; restore the previous IRQL
        mov     sp, r4
        vpop    {d8-d15}
        pop     {r4-r11}
        add     sp, sp, #16
        b       KiTrapExit
        NESTED_END KiInterruptException

;
; VOID KiPassiveRelease(VOID) - an interrupt that has been passively released.
;

        LEAF_ENTRY KiPassiveRelease
        bx      lr
        LEAF_END KiPassiveRelease

;
; VOID KiArmInitializeVectors(ULONG Number) - take exceptions in Thumb state
; through the vector table at VBAR, and set the stacks of the abort and
; undefined modes, those of the processor (the registers of each processor
; are its own).
;

        LEAF_ENTRY KiArmInitializeVectors
        CP_READ r1, 15, 0, 1, 0, 0              ; SCTLR
        bic     r1, r1, #SCTLR_V                ; the exception base is VBAR
        orr     r1, r1, #SCTLR_TE               ; exceptions are taken in Thumb state
        CP_WRITE r1, 15, 0, 1, 0, 0
        isb
        ldr     r1, =KiArmVectorTable
        bic     r1, r1, #1
        CP_WRITE r1, 15, 0, 12, 0, 0            ; VBAR
        isb
        adds    r0, r0, #1
        mrs     r2, cpsr
        cps     #MODE_ABT
        ldr     r1, =KiArmAbortStacks
        add     r1, r1, r0, lsl #ABORT_STACK_SHIFT
        mov     sp, r1
        cps     #MODE_UND
        ldr     r1, =KiArmUndefinedStacks
        add     r1, r1, r0, lsl #UNDEFINED_STACK_SHIFT
        mov     sp, r1
        msr     cpsr_c, r2
        bx      lr
        LEAF_END KiArmInitializeVectors

;
; The stacks of the abort mode, on which the TB miss handler runs, and of the
; undefined mode, one of each per processor: the four cores of a Raspberry Pi
; (the firmware has a restart block for each).
;

        AREA    |.bss|, NOINIT, READWRITE, ALIGN=3
KiArmAbortStacks
        SPACE   ABORT_STACK_SIZE * KI_ARM_PROCESSORS
KiArmUndefinedStacks
        SPACE   UNDEFINED_STACK_SIZE * KI_ARM_PROCESSORS

        END
