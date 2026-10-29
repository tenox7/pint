;++
;
; Module Name:
;
;    ldrthunk.asm
;
; Abstract:
;
;    The loader thunk: the ARM port of DLL\MIPS\LDRTHUNK.S.
;
;--

#include "kxarm.h"

        IMPORT  LdrpInitialize

        TEXTAREA

;++
;
; VOID
; LdrInitializeThunk (
;    IN PVOID NormalContext,
;    IN PVOID SystemArgument1,
;    IN PVOID SystemArgument2
;    )
;
; Routine Description:
;
;    This function is the user mode APC routine of a new thread: it passes the
;    address of the context record on the stack to LdrpInitialize.
;
;--

        LEAF_ENTRY LdrInitializeThunk
        mov     r0, sp                          ; context record
        b       LdrpInitialize
        LEAF_END LdrInitializeThunk

        END
