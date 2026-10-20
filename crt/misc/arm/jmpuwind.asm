;++
;
; Module Name:
;
;    jmpuwind.asm
;
; Abstract:
;
;    This module implements the ARM specific routines to execute an exception
;    filter or a termination handler, and to jump to the runtime library
;    unwind routine (the ARM port of CRT32NT\MISC\MIPS\JMPUWIND.S).
;
;--

#include "kxarm.h"

        IMPORT  RtlUnwind

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

        END
