;++
;
; Module Name:
;
;    callout.asm
;
; Abstract:
;
;    The call out from kernel mode to user mode, its return, and the switch
;    to a large kernel stack: the ARM port of KE\MIPS\CALLOUT.S.
;
;    A call out runs a user mode function on the thread's kernel stack below
;    the frame of the call out, the callout frame: it is the thread's initial
;    stack while the callback runs, so that the services and traps of the
;    callback build their frames below it, and its callback stack, where
;    NtCallbackReturn finds the state to return with. As the kernel enters
;    user mode only from a trap frame at the initial stack (the svc stack
;    pointer then stands at the initial stack, ke\trap.asm), the call out
;    exits to user mode as a system service does, from a trap frame below the
;    callout frame made from that of the current service, continuing at the
;    user callback dispatcher.
;
;--

#include "ksarm.h"
#include "kxarm.h"

#define CPSR_IRQ_DISABLE 0x80
#define CPSR_IT_HIGH 0x06000000
#define CPSR_IT_LOW 0xfc00

#define UserMode 1

        IMPORT  KeUserCallbackDispatcher
        IMPORT  KiTrapExit
        IMPORT  MmGrowKernelStack
        IMPORT  RtlMoveMemory

        TEXTAREA

;++
;
; NTSTATUS
; KiCallUserMode (
;    IN PVOID *OutputBuffer,
;    IN PULONG OutputLength
;    )
;
; Routine Description:
;
;    This function calls a user mode function: KeUserModeCallback has put the
;    user callout frame on the user stack, its address the user stack pointer
;    of the trap frame of the current system service.
;
;    The callout frame holds the nonvolatile state, the output arguments, the
;    trap frame and callback stack addresses of the thread, the floating
;    status and the initial stack. The trap frame of the exit to user mode is
;    that of the service, continuing at KeUserCallbackDispatcher, and below it
;    the exception frame holds the user nonvolatile state from the exception
;    frame of the service, from where KiTrapExit loads it.
;
;    N.B. NtCallbackReturn returns to the caller of this function, with the
;         stack and the callout frame as this function leaves them.
;
;    N.B. This function returns to its caller itself only if the kernel stack
;         must be grown and cannot be.
;
; Arguments:
;
;    OutputBuffer (r0) - Supplies the address of the variable that receives
;        the address of the output buffer.
;
;    OutputLength (r1) - Supplies the address of the variable that receives
;        the length of the output buffer.
;
; Return Value:
;
;    The status of the callback, or that of the failed growth of the stack.
;
;--

        NESTED_ENTRY KiCallUserMode
        PROLOG_STACK_ALLOC 12                   ; R0, R1, Pad
        PROLOG_PUSH {r4-r11, lr}
        PROLOG_STACK_ALLOC 16                   ; CbStk, TrFr, Fpscr, InStk
        PROLOG_VPUSH {d8-d15}
        str     r0, [sp, #CuR0]                 ; output buffer address
        str     r1, [sp, #CuR1]                 ; output length address

;
; Grow the kernel stack if less than KERNEL_LARGE_STACK_COMMIT of it is left
; below the callout frame.
;

        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r5, [r0, #PcCurrentThread]
        ldr     r0, [r5, #ThStackLimit]
        sub     r1, sp, #KERNEL_LARGE_STACK_COMMIT
        cmp     r1, r0
        bhs     %F10                            ; the stack is large enough
        mov     r0, sp
        bl      MmGrowKernelStack
        cmp     r0, #0
        bne     %F20                            ; the stack cannot be grown

;
; Save the state of the thread in the callout frame and build the frames of
; the exit to user mode, with interrupts disabled: the callout frame becomes
; the initial stack when they are complete.
;
;   r4 - PCR
;   r5 - current thread
;   r6 - trap frame of the service
;   r7 - callout frame
;   r8 - trap frame of the exit
;

10      cpsid   i
        CP_READ r4, 15, 0, 13, 0, 4             ; PCR of the processor run on now
        ldr     r0, [r5, #ThStackLimit]
        str     r0, [r4, #PcStackLimit]         ; the stack limit, grown or not
        ldr     r6, [r5, #ThTrapFrame]
        ldr     r0, [r5, #ThCallbackStack]
        str     r0, [sp, #CuCbStk]              ; save the callback stack address
        str     r6, [sp, #CuTrFr]               ; save the trap frame address
        vmrs    r0, fpscr
        str     r0, [sp, #CuFpscr]              ; save the floating status
        ldr     r0, [r4, #PcInitialStack]
        str     r0, [sp, #CuInStk]              ; save the initial stack
        mov     r7, sp
        sub     r8, sp, #TrapFrameLength
        ldm     r6, {r0-r3}                     ; r0 - r3 as at the service call
        stm     r8, {r0-r3}
        ldr     r0, [r6, #TrR12]
        str     r0, [r8, #TrR12]
        ldr     r0, [r6, #TrSp]                 ; the user callout frame
        str     r0, [r8, #TrSp]
        ldr     r0, [r6, #TrLr]
        str     r0, [r8, #TrLr]
        ldr     r0, =KeUserCallbackDispatcher
        ldr     r0, [r0]
        bic     r0, r0, #1
        str     r0, [r8, #TrPc]                 ; continue at the user callback dispatcher
        ldr     r0, [r6, #TrCpsr]
        bic     r0, r0, #CPSR_IT_HIGH
        bic     r0, r0, #CPSR_IT_LOW
        orr     r0, r0, #CPSR_THUMB
        str     r0, [r8, #TrCpsr]
        ldr     r0, [r6, #TrFpscr]              ; the user floating status
        str     r0, [r8, #TrFpscr]
        movs    r0, #0
        str     r0, [r8, #TrExceptionActive]
        str     r0, [r8, #TrOldIrql]
        str     r0, [r8, #TrFaultAddress]
        str     r0, [r8, #TrFaultStatus]
        str     r0, [r8, #TrTrapFrame]
        movs    r0, #UserMode
        str     r0, [r8, #TrPreviousMode]
        sub     r0, r6, #ExceptionFrameLength   ; the user nonvolatile state
        sub     r1, r8, #ExceptionFrameLength
        vldm    r0!, {d8-d15}
        vstm    r1!, {d8-d15}
        ldm     r0!, {r2, r3, r9, r10}          ; r4 - r7
        stm     r1!, {r2, r3, r9, r10}
        ldm     r0, {r2, r3, r9, r10}           ; r8 - r11
        stm     r1, {r2, r3, r9, r10}
        str     r7, [r5, #ThCallbackStack]      ; the callout frame is the callback stack
        str     r7, [r5, #ThInitialStack]       ; and the initial stack
        str     r7, [r4, #PcInitialStack]
        mov     sp, r8
        b       KiTrapExit                      ; exit to user mode

;
; The kernel stack cannot be grown.
;

20      EPILOG_VPOP {d8-d15}
        EPILOG_STACK_FREE 16
        EPILOG_POP {r4-r11, lr}
        EPILOG_STACK_FREE 12
        EPILOG_RETURN
        NESTED_END KiCallUserMode

;++
;
; PVOID
; KeSwitchKernelStack (
;    IN PVOID StackBase,
;    IN PVOID StackLimit
;    )
;
; Routine Description:
;
;    This function switches the current thread to the specified (large)
;    kernel stack: the old stack, from the stack pointer to its base, is
;    copied to the top of the new one, the trap frame address of the thread
;    and the frame pointer (r11) of the caller are moved with it, and the
;    stack pointer.
;
;    N.B. Nothing else on the stack may hold the address of something on the
;         stack.
;
; Arguments:
;
;    StackBase (r0) - Supplies the base of the new kernel stack.
;
;    StackLimit (r1) - Supplies the limit of the new kernel stack.
;
; Return Value:
;
;    The base of the old kernel stack.
;
;--

        NESTED_ENTRY KeSwitchKernelStack
        PROLOG_PUSH {r4-r8, lr}
        mov     r4, r0                          ; new stack base
        mov     r5, r1                          ; new stack limit
        CP_READ r6, 15, 0, 13, 0, 4             ; PCR
        ldr     r6, [r6, #PcCurrentThread]
        ldr     r7, [r6, #ThStackBase]          ; old stack base
        sub     r8, r4, r7                      ; the displacement of the stack
        ldr     r0, [r6, #ThTrapFrame]
        add     r0, r0, r8
        str     r0, [r6, #ThTrapFrame]          ; move the trap frame address
        mov     r1, sp                          ; copy the stack to the new one
        add     r0, r1, r8
        sub     r2, r7, r1
        bl      RtlMoveMemory
        mov     r2, sp
        sub     r0, r11, r2                     ; move the frame pointer of the
        sub     r1, r7, r2                      ; caller if it is on the stack
        cmp     r0, r1
        it      lo
        addlo   r11, r11, r8
        mrs     r12, cpsr
        cpsid   i
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR of the processor run on now
        str     r4, [r6, #ThInitialStack]
        str     r4, [r6, #ThStackBase]
        str     r5, [r6, #ThStackLimit]
        movs    r1, #1
        strb    r1, [r6, #ThLargeStack]
        str     r4, [r0, #PcInitialStack]
        str     r5, [r0, #PcStackLimit]
        add     sp, sp, r8                      ; switch to the new stack
        msr     cpsr_c, r12
        mov     r0, r7
        EPILOG_POP {r4-r8, pc}
        NESTED_END KeSwitchKernelStack

;++
;
; NTSTATUS
; NtCallbackReturn (
;    IN PVOID OutputBuffer OPTIONAL,
;    IN ULONG OutputLength,
;    IN NTSTATUS Status
;    )
;
; Routine Description:
;
;    This function returns from a user mode callback to the caller of
;    KiCallUserMode: the output buffer address and length are stored for it,
;    the state of the thread is restored from the callout frame, and the
;    stack trimmed to the callout frame, which is released as KiCallUserMode
;    releases it. The frames of this service are left behind.
;
; Arguments:
;
;    OutputBuffer (r0) - Supplies the address of an output buffer.
;
;    OutputLength (r1) - Supplies the length of the output buffer.
;
;    Status (r2) - Supplies the status returned to the caller of the
;        callback.
;
; Return Value:
;
;    STATUS_NO_CALLBACK_ACTIVE if no callback is active; otherwise there is no
;    return and Status is returned to the caller of the callback.
;
;--

        LEAF_ENTRY NtCallbackReturn
        CP_READ r3, 15, 0, 13, 0, 4             ; PCR
        ldr     r3, [r3, #PcCurrentThread]
        ldr     r12, [r3, #ThCallbackStack]
        cmp     r12, #0
        beq     %F10                            ; no callback is active
        ldr     r4, [r12, #CuR0]
        str     r0, [r4]                        ; store the output buffer address
        ldr     r4, [r12, #CuR1]
        str     r1, [r4]                        ; store the output buffer length
        ldr     r4, [r12, #CuTrFr]
        str     r4, [r3, #ThTrapFrame]          ; restore the trap frame address
        ldr     r4, [r12, #CuCbStk]
        str     r4, [r3, #ThCallbackStack]      ; restore the callback stack address
        ldr     r4, [r12, #CuFpscr]
        vmsr    fpscr, r4                       ; restore the floating status
        mov     r0, r2                          ; the status of the callback
        ldr     r4, [r12, #CuInStk]
        mrs     r1, cpsr
        cpsid   i
        CP_READ r5, 15, 0, 13, 0, 4             ; PCR of the processor run on now
        str     r4, [r3, #ThInitialStack]       ; restore the initial stack
        str     r4, [r5, #PcInitialStack]
        mov     sp, r12                         ; trim the stack to the callout frame
        msr     cpsr_c, r1
        vpop    {d8-d15}                        ; return as KiCallUserMode
        add     sp, sp, #16
        pop     {r4-r11, lr}
        add     sp, sp, #12
        bx      lr

10      ldr     r0, =STATUS_NO_CALLBACK_ACTIVE
        bx      lr
        LEAF_END NtCallbackReturn

        END
