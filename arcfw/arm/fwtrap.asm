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

        END
