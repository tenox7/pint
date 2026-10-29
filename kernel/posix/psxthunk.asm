;++
;
; Module Name:
;
;    psxthunk.asm
;
; Abstract:
;
;    The thunks of the POSIX client DLL: the ARM port of
;    POSIX\CLIENT\MIPS\PSXTHUNK.S. The POSIX server sends a client thread to
;    them with RtlRemoteCall: the context record of the interrupted code is at
;    sp and in r4, the arguments follow in r5 - r7. Their unwind data
;    describes that context record, so an unwind out of a signal handler
;    continues in the interrupted code.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  PdxNullApiCaller
        IMPORT  PdxSignalDeliverer

        TEXTAREA

;++
;
; VOID
; _PdxNullApiCaller (
;    IN PCONTEXT Context
;    )
;
; Routine Description:
;
;    The server makes a thread that must not run on (a process that may not
;    fork) call the null POSIX API; the thread then continues with the
;    context record.
;
;--

        NESTED_ENTRY _PdxNullApiCaller
        PROLOG_PUSH_CONTEXT
        mov     r0, r4                          ; context record
        bl      PdxNullApiCaller                ; continues the context
        NESTED_END _PdxNullApiCaller

;++
;
; VOID
; _PdxSignalDeliverer (
;    IN PCONTEXT Context,
;    IN sigset_t PreviousBlockMask,
;    IN int Signal,
;    IN _handler Handler
;    )
;
; Routine Description:
;
;    The server delivers a signal: the handler is called, the signal mask
;    is set back and the thread continues with the context record.
;
;--

        NESTED_ENTRY _PdxSignalDeliverer
        PROLOG_PUSH_CONTEXT
        mov     r0, r4                          ; context record
        mov     r1, r5                          ; previous block mask
        mov     r2, r6                          ; signal number
        mov     r3, r7                          ; signal handler
        bl      PdxSignalDeliverer              ; continues the context
        NESTED_END _PdxSignalDeliverer

        END
