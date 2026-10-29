;++
;
; Module Name:
;
;    handlers.asm
;
; Abstract:
;
;    This module implements the ARM specific routines of the C++ exception
;    handling: the call of a funclet (a catch block or an unwind action), the
;    unwind of the frames below the frame of a catch and the jump to the
;    continuation of a catch (the ARM port of CRTW32\EH\MIPS\HANDLERS.S and
;    CRTW32\EH\PPC\UNWIND.S).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  _NLG_Notify
        IMPORT  _SaveUnwindContext
        IMPORT  RtlCaptureContext
        IMPORT  RtlUnwind
        IMPORT  memcpy

        TEXTAREA

;++
;
; PVOID
; _CallSettingFrame (
;    IN PVOID pFunclet,
;    IN PVOID pFrame,
;    IN PNONVOLATILE_REGISTERS NonVolatileRegisters,
;    IN ULONG NLGCode
;    )
;
; Routine Description:
;
;    This function calls a funclet with the nonvolatile registers of its
;    function, in which the funclet addresses the locals of the function. The
;    frame is left at the stack pointer the funclet starts with, its
;    establisher frame: for a catch block its frame info, which the frame
;    handler of the funclet reads there. The debugger is told of the call and
;    of the end of a catch block (_NLG_Notify).
;
; Return Value:
;
;    The value returned by the funclet: the continuation address of a catch
;    block.
;
;--

        NESTED_ENTRY _CallSettingFrame
        PROLOG_PUSH {r3-r11, lr}
        PROLOG_VPUSH {d8-d15}
        PROLOG_STACK_ALLOC 8
        str     r1, [sp]                        ; frame, at the establisher frame of the funclet
        str     r3, [sp, #4]                    ; code of the nonlocal goto
        mov     r4, r2                          ; nonvolatile registers
        mov     r2, r3
        bl      _NLG_Notify                     ; tell the debugger (changes r12 only)
        mov     r3, r4
        ldm     r3!, {r4-r11}                   ; nonvolatile registers of the function
        vldm    r3, {d8-d15}
        blx     r0                              ; call the funclet
        EXPORT  _NLG_Return
_NLG_Return
        ldr     r2, [sp, #4]                    ; code of the nonlocal goto
        cmp     r2, #0x100                      ; a catch block entered?
        bne     %F10                            ; if ne, no
        movs    r2, #2                          ; a catch block left
        ldr     r1, [sp]                        ; frame
        bl      _NLG_Notify                     ; tell the debugger (r0 continuation)
10
        EPILOG_STACK_FREE 8
        EPILOG_VPOP {d8-d15}
        EPILOG_POP {r3-r11, pc}
        NESTED_END _CallSettingFrame

;++
;
; VOID
; _UnwindNestedFrames (
;    IN EHRegistrationNode *pFrame,
;    IN EHExceptionRecord *pExcept,
;    IN PCONTEXT pContext
;    )
;
; Routine Description:
;
;    This function unwinds the frames below the target frame (calls their
;    handlers to unwind them) and returns: the frame handler of the target
;    frame keeps the context of the target frame for the jump to the
;    continuation of the catch and gives the unwind the context this function
;    captured, with its return point, to continue at. The context of the
;    exception is kept, and the exception is no longer unwinding, in case it
;    is rethrown.
;
;--

UnOriginalContext EQU 0
UnLocalContext EQU ContextFrameLength
UnPrevious EQU 2 * ContextFrameLength
UnFrameLength EQU 2 * ContextFrameLength + 8

        NESTED_ENTRY _UnwindNestedFrames
        PROLOG_PUSH {r3-r11, lr}
        PROLOG_VPUSH {d8-d15}
        PROLOG_STACK_ALLOC UnFrameLength
        mov     r4, r0                          ; target frame
        mov     r5, r1                          ; exception record
        mov     r6, r2                          ; context of the exception
        adr     r7, %F10                        ; return point
        add     r0, sp, #UnOriginalContext
        mov     r1, r6
        mov     r2, #ContextFrameLength
        bl      memcpy                          ; keep the context of the exception
        add     r0, sp, #UnLocalContext
        bl      RtlCaptureContext               ; the context to return with
        str     r7, [sp, #UnLocalContext + CxPc]
        add     r0, sp, #UnLocalContext
        bl      _SaveUnwindContext              ; for the frame handler of the target frame
        str     r0, [sp, #UnPrevious]           ; the unwind context it replaces
        mov     r0, r4                          ; target frame
        mov     r1, r7                          ; target address
        mov     r2, r5                          ; exception record
        movs    r3, #0                          ; return value
        bl      RtlUnwind                       ; unwind to the target frame

;
; The unwind continues here, in the context captured above.
;

10      ldr     r0, [sp, #UnPrevious]
        bl      _SaveUnwindContext              ; that of an enclosing unwind again
        mov     r0, r6                          ; context of the exception
        add     r1, sp, #UnOriginalContext
        mov     r2, #ContextFrameLength
        bl      memcpy                          ; restore it
        ldr     r0, [r5, #ErExceptionFlags]
        bic     r0, r0, #2                      ; clear EXCEPTION_UNWINDING
        str     r0, [r5, #ErExceptionFlags]
        EPILOG_STACK_FREE UnFrameLength
        EPILOG_VPOP {d8-d15}
        EPILOG_POP {r3-r11, pc}
        NESTED_END _UnwindNestedFrames

;++
;
; VOID
; _JumpToContinuation (
;    IN ULONG TargetAddress,
;    IN PCONTEXT ContextRecord
;    )
;
; Routine Description:
;
;    This function continues at the continuation address of a catch with the
;    nonvolatile registers and the stack pointer of the frame of the catch,
;    from the specified context.
;
;    N.B. There is no return from this routine.
;
;--

        LEAF_ENTRY _JumpToContinuation
        add     r2, r1, #CxD + 8 * 8
        vldm    r2, {d8-d15}                    ; restore floating registers d8 - d15
        ldr     r2, [r1, #CxFpscr]
        vmsr    fpscr, r2
        add     r2, r1, #CxR4
        ldm     r2, {r4-r11}                    ; restore integer registers r4 - r11
        ldr     sp, [r1, #CxSp]
        orr     r0, r0, #1                      ; Thumb state
        bx      r0                              ; continue
        LEAF_END _JumpToContinuation

        END
