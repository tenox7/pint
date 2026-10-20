;++
;
; Module Name:
;
;    fwtrap.asm
;
; Abstract:
;
;    The ARC firmware's program transfers (the ARM part of FW\MIPS\FWTRAP.S).
;
;--

#include "kxarm.h"

        IMPORT  FwPrivateExecute
        IMPORT  FwTemporaryStack
        IMPORT  FwBootSystem
        IMPORT  FwInitialStackTop

        TEXTAREA

;
; VOID FwpTransfer(ULONG ExecAddr, ULONG StackAddr, ULONG Argc, PCHAR Argv[],
;                  PCHAR Envp[])
;
; Call a loaded program at ExecAddr on the stack StackAddr with its arguments,
; and return on the firmware's stack when it returns.
;

        NESTED_ENTRY FwpTransfer
        PROLOG_PUSH {r4-r6, lr}
        PROLOG_STACK_SAVE r4
        mov     r5, r0
        ldr     r12, [sp, #16]                  ; Envp
        mov     sp, r1
        mov     r0, r2
        mov     r1, r3
        mov     r2, r12
        blx     r5
        EPILOG_STACK_RESTORE r4
        EPILOG_POP {r4-r6, pc}
        NESTED_END FwpTransfer

;
; ARC_STATUS FwExecute(PCHAR Path, ULONG Argc, PCHAR Argv[], PCHAR Envp[])
;
; Load and invoke a program from the firmware's temporary stack, as the running
; program and its stack may be overwritten by the one loaded (FwPrivateExecute),
; and return to the caller's stack when it returns.
;

        NESTED_ENTRY FwExecute
        PROLOG_PUSH {r4, lr}
        PROLOG_STACK_SAVE r4
        ldr     r12, =FwTemporaryStack
        ldr     r12, [r12]
        mov     sp, r12
        bl      FwPrivateExecute
        EPILOG_STACK_RESTORE r4
        EPILOG_POP {r4, pc}
        NESTED_END FwExecute

;
; VOID FwpEnterInteractiveMode(VOID)
;
; Return to the boot manager on the firmware's initial stack, abandoning the
; program that called.
;

        LEAF_ENTRY FwpEnterInteractiveMode
        ldr     r12, =FwInitialStackTop
        mov     sp, r12
        b       FwBootSystem
        LEAF_END FwpEnterInteractiveMode

;
; VOID FwEnableAsynchronousAborts(VOID)
;
; Take the external aborts the core reports after the access, as those of
; writes, through the vectors, from when the firmware can display them.
;

        LEAF_ENTRY FwEnableAsynchronousAborts
        cpsie   a
        isb
        bx      lr
        LEAF_END FwEnableAsynchronousAborts

;
; BOOLEAN FwProbeRead(ULONG Address, PULONG Value)
;
; Read a word of a device that may not be there: an abort of the read, also
; one the core reports after it, returns FALSE (FwDataAbort), as from the
; stack this saved. An abort of an earlier access is taken before.
;

        NESTED_ENTRY FwProbeRead
        PROLOG_PUSH {r4, lr}
        dsb
        isb
        ldr     r2, =FwProbeStack
        mov     r3, sp
        str     r3, [r2]
        ldr     r3, [r0]
        dsb
        isb
        str     r3, [r1]
        mov     r3, #0
        str     r3, [r2]
        mov     r0, #1
        EPILOG_POP {r4, pc}
        NESTED_END FwProbeRead

;
; The exception vectors of the firmware (VBAR): a data abort of a read of
; FwProbeRead returns FALSE from it, in the supervisor mode it ran in, with
; asynchronous aborts as they were; any other exception displays its state on
; its own stack and stops the processor (FwExceptionReport).
;

        IMPORT  FwExceptionReport

        AREA    |.text$vectors|, CODE, READONLY, ALIGN=5
        THUMB

        EXPORT  FwExceptionVectors
FwExceptionVectors
        b.w     FwReset
        b.w     FwUndefined
        b.w     FwSupervisorCall
        b.w     FwPrefetchAbort
        b.w     FwDataAbort
        b.w     FwReserved
        b.w     FwInterrupt
        b.w     FwFastInterrupt

FwReset
        mov     r0, #0
        b       FwException
FwUndefined
        mov     r0, #1
        b       FwException
FwSupervisorCall
        mov     r0, #2
        b       FwException
FwPrefetchAbort
        mov     r0, #3
        b       FwException
FwReserved
        mov     r0, #5
        b       FwException
FwInterrupt
        mov     r0, #6
        b       FwException
FwFastInterrupt
        mov     r0, #7
        b       FwException

FwDataAbort
        ldr     r12, =FwProbeStack
        ldr     r12, [r12]
        cmp     r12, #0
        beq     %F2
        mrs     r0, spsr
        cps     #0x13
        mov     sp, r12
        tst     r0, #0x100
        bne     %F1
        cpsie   a
1       ldr     r12, =FwProbeStack
        mov     r0, #0
        str     r0, [r12]
        pop     {r4, pc}
2       mov     r0, #4

FwException
        mov     r1, lr
        mrs     r2, spsr
        cps     #0x13
        ldr     r3, =FwExceptionStackTop
        mov     sp, r3
        bl      FwExceptionReport

        LTORG

        AREA    |.data|, DATA

FwProbeStack
        DCD     0

        AREA    |.bss|, NOINIT, READWRITE, ALIGN=3
FwExceptionStack
        SPACE   0x1000
FwExceptionStackTop

        END
