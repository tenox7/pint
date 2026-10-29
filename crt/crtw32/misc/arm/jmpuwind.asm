;++
;
; Module Name:
;
;    jmpuwind.asm
;
; Abstract:
;
;    This module implements the ARM specific routines to execute an exception
;    filter or a termination handler, to jump to the runtime library unwind
;    routine, and to tell the debugger of a nonlocal goto (the ARM port of
;    CRTW32\MISC\MIPS\JMPUWIND.S).
;
;--

#include "kxarm.h"

        IMPORT  RtlUnwind

;
; The nonlocal goto the debugger reads at _NLG_Dispatch: a signature, the
; target address, the code of the goto and the frame of its target.
;

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  _NLG_Destination
_NLG_Destination
        DCD     0x19930520                      ; signature
        DCD     0                               ; handler address
        DCD     0                               ; code
        DCD     0                               ; frame pointer

        TEXTAREA

;++
;
; LONG
; __C_ExecuteExceptionFilter (
;    IN PEXCEPTION_POINTERS ExceptionPointers,
;    IN ULONG EstablisherFrame,
;    IN EXCEPTION_FILTER ExceptionFilter,
;    IN PNONVOLATILE_REGISTERS NonVolatileRegisters
;    )
;
; Routine Description:
;
;    This function calls the specified exception filter routine with the
;    nonvolatile registers of its establisher function, in which the filter
;    addresses the locals of the function (the static link of the MIPS).
;
; Return Value:
;
;    The value returned by the exception filter routine.
;
;--

        NESTED_ENTRY __C_ExecuteExceptionFilter
        PROLOG_PUSH {r3-r11, lr}
        PROLOG_VPUSH {d8-d15}
        ldm     r3!, {r4-r11}                   ; nonvolatile registers of the establisher
        vldm    r3, {d8-d15}
        blx     r2                              ; call the exception filter
        EPILOG_VPOP {d8-d15}
        EPILOG_POP {r3-r11, pc}
        NESTED_END __C_ExecuteExceptionFilter

;++
;
; VOID
; __C_ExecuteTerminationHandler (
;    IN BOOLEAN AbnormalTermination,
;    IN ULONG EstablisherFrame,
;    IN TERMINATION_HANDLER TerminationHandler,
;    IN PNONVOLATILE_REGISTERS NonVolatileRegisters
;    )
;
; Routine Description:
;
;    This function calls the specified termination handler routine with the
;    nonvolatile registers of its establisher function.
;
;--

        NESTED_ENTRY __C_ExecuteTerminationHandler
        PROLOG_PUSH {r3-r11, lr}
        PROLOG_VPUSH {d8-d15}
        ldm     r3!, {r4-r11}                   ; nonvolatile registers of the establisher
        vldm    r3, {d8-d15}
        blx     r2                              ; call the termination handler
        EPILOG_VPOP {d8-d15}
        EPILOG_POP {r3-r11, pc}
        NESTED_END __C_ExecuteTerminationHandler

;++
;
; VOID
; __jump_unwind (
;    IN PVOID EstablishFrame,
;    IN PVOID TargetPc
;    )
;
; Routine Description:
;
;    This function transfers control to unwind. It is used by the compiler
;    when a goto out of the body of a try statement occurs.
;
; Arguments:
;
;    EstablishFrame (r0) - Supplies the establisher frame of the target of
;       the unwind.
;
;    TargetPc (r1) - Supplies the target instruction address where control
;       is to be transfered to after the unwind operation is complete.
;
;--

        LEAF_ENTRY __jump_unwind
        movs    r2, #0                          ; set NULL exception record address
        movs    r3, #0                          ; set destination return value
        b       RtlUnwind                       ; unwind to specified target
        LEAF_END __jump_unwind

;++
;
; VOID
; _NLG_Notify (
;    IN PVOID pFunclet,
;    IN PVOID EstablisherFrame,
;    IN ULONG Code
;    )
;
; Routine Description:
;
;    This function gives the debugger the target of a nonlocal goto (an
;    exception handler, a termination handler, a long jump): the debugger
;    reads _NLG_Destination at _NLG_Dispatch.
;
;    N.B. This function changes no register but r12.
;
; Arguments:
;
;    pFunclet (r0) - Supplies the target address of the nonlocal goto.
;
;    EstablisherFrame (r1) - Supplies the frame of the establisher function.
;
;    Code (r2) - Supplies the code of the nonlocal goto.
;
;--

        LEAF_ENTRY _NLG_Notify
        ldr     r12, =_NLG_Destination
        str     r0, [r12, #4]                   ; target address, for the debugger
        str     r2, [r12, #8]                   ; code
        str     r1, [r12, #12]                  ; frame
        EXPORT  _NLG_Dispatch
_NLG_Dispatch
        bx      lr
        LEAF_END _NLG_Notify

;++
;
; VOID
; _NLG_ExecuteTerminationHandler (
;    IN ULONG AbnormalTermination,
;    IN ULONG EstablisherFrame,
;    IN TERMINATION_HANDLER TerminationHandler,
;    IN PNONVOLATILE_REGISTERS NonVolatileRegisters
;    )
;
; Routine Description:
;
;    This function calls the specified termination handler routine as
;    __C_ExecuteTerminationHandler does, with the return address of the call
;    at _NLG_Return2, where the debugger finds the end of the handler.
;
;--

        NESTED_ENTRY _NLG_ExecuteTerminationHandler
        PROLOG_PUSH {r3-r11, lr}
        PROLOG_VPUSH {d8-d15}
        ldm     r3!, {r4-r11}                   ; nonvolatile registers of the establisher
        vldm    r3, {d8-d15}
        blx     r2                              ; call the termination handler
        EXPORT  _NLG_Return2
_NLG_Return2
        EPILOG_VPOP {d8-d15}
        EPILOG_POP {r3-r11, pc}
        NESTED_END _NLG_ExecuteTerminationHandler

        END
