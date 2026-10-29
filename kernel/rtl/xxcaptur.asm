;++
;
; Module Name:
;
;    xxcaptur.asm
;
; Abstract:
;
;    This module implements the capture and the restore of the context of
;    the caller (the ARM port of RTL\MIPS\XXCAPTUR.S).
;
;--

#include "ksarm.h"
#include "kxarm.h"

#if !defined(NTOS_KERNEL_RUNTIME)
        IMPORT  ZwContinue
#endif

        TEXTAREA

;++
;
; VOID
; RtlCaptureContext (
;    OUT PCONTEXT ContextRecord
;    )
;
; Routine Description:
;
;    This function captures the context of the caller in the specified
;    context record: the integer, control and floating state, the program
;    counter being the return address of the call.
;
;--

        LEAF_ENTRY RtlCaptureContext
        str     r1, [r0, #CxR1]
        add     r1, r0, #CxR2
        stm     r1, {r2-r12}                    ; save integer registers r2 - r12
        str     r0, [r0, #CxR0]
        str     sp, [r0, #CxSp]
        str     lr, [r0, #CxLr]
        bic     r1, lr, #1
        str     r1, [r0, #CxPc]                 ; the continuation is the return address
        mrs     r1, cpsr
        orr     r1, r1, #CPSR_THUMB             ; Thumb state
        str     r1, [r0, #CxCpsr]
        vmrs    r1, fpscr
        str     r1, [r0, #CxFpscr]
        add     r1, r0, #CxD
        vstm    r1!, {d0-d15}                   ; save floating registers d0 - d31
        vstm    r1, {d16-d31}
        ldr     r1, =CONTEXT_FULL
        str     r1, [r0, #CxContextFlags]
        bx      lr
        LEAF_END RtlCaptureContext

;++
;
; VOID
; RtlpRestoreContext (
;    IN PCONTEXT ContextRecord,
;    IN PEXCEPTION_RECORD ExceptionRecord OPTIONAL
;    )
;
; Routine Description:
;
;    This function restores the context of the caller to the specified
;    context. It is used by RtlUnwind to continue at the target of an unwind
;    in the current mode: in user mode by the continue system service, in
;    kernel mode directly, where the processor state other than the
;    condition flags is not restored.
;
;    If an exception record is specified and the exception status is
;    STATUS_LONGJUMP, then the nonvolatile registers are first set to their
;    state at the call to setjmp.
;
;    N.B. There is no return from this routine.
;
;--

        LEAF_ENTRY RtlpRestoreContext
        cbz     r1, %F5                         ; no exception record
        ldr     r2, [r1, #ErExceptionCode]
        ldr     r3, =STATUS_LONGJUMP
        cmp     r2, r3
        bne     %F5                             ; not a long jump
        ldr     r2, [r1, #ErExceptionInformation] ; jump buffer
        ldr     r3, [r2, #JbR4]                 ; move integer registers r4 - r11
        str     r3, [r0, #CxR4]
        ldr     r3, [r2, #JbR5]
        str     r3, [r0, #CxR5]
        ldr     r3, [r2, #JbR6]
        str     r3, [r0, #CxR6]
        ldr     r3, [r2, #JbR7]
        str     r3, [r0, #CxR7]
        ldr     r3, [r2, #JbR8]
        str     r3, [r0, #CxR8]
        ldr     r3, [r2, #JbR9]
        str     r3, [r0, #CxR9]
        ldr     r3, [r2, #JbR10]
        str     r3, [r0, #CxR10]
        ldr     r3, [r2, #JbR11]
        str     r3, [r0, #CxR11]
        ldr     r3, [r2, #JbSp]                 ; move the stack pointer and the continuation
        str     r3, [r0, #CxSp]
        ldr     r3, [r2, #JbPc]
        str     r3, [r0, #CxPc]
        str     r3, [r0, #CxLr]
        add     r3, r2, #JbD                    ; move floating registers d8 - d15
        vldm    r3, {d8-d15}
        add     r3, r0, #CxD + 8 * 8
        vstm    r3, {d8-d15}

5
#if !defined(NTOS_KERNEL_RUNTIME)

;
; User mode: continue with the continue system service.
;

        movs    r1, #0                          ; do not test alert
        bl      ZwContinue
#else

;
; Kernel mode: restore the floating state and the condition flags, put the
; continuation and r12 below the stack of the context, and restore the
; integer registers.
;

        mov     r12, r0
        add     r1, r12, #CxD
        vldm    r1!, {d0-d15}
        vldm    r1, {d16-d31}
        ldr     r1, [r12, #CxFpscr]
        vmsr    fpscr, r1
        ldr     r1, [r12, #CxCpsr]
        msr     APSR_nzcvq, r1
        ldr     r1, [r12, #CxSp]
        sub     r1, r1, #8
        ldr     r2, [r12, #CxR12]
        ldr     r3, [r12, #CxPc]
        orr     r3, r3, #1                      ; Thumb state
        strd    r2, r3, [r1]
        mov     sp, r1
        ldr     lr, [r12, #CxLr]
        add     r12, r12, #CxR0
        ldm     r12, {r0-r11}
        pop     {r12, pc}
#endif
        LEAF_END RtlpRestoreContext

        END
