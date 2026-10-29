;++
;
; Module Name:
;
;    thunk.asm
;
; Abstract:
;
;    Win32 thunks of the console server and the USER server of csrss: the ARM
;    port of NTCON\SERVER\MIPS\THUNK.S and NTUSER\SERVER\MIPS\THUNK.S, the
;    functions that must be written in assembler.
;
;--

#include "kxarm.h"

        IMPORT  FreeStackAndTerminate

        TEXTAREA

;++
;
; VOID
; SwitchStackThenTerminate (
;     IN PVOID StackLimit,
;     IN PVOID NewStack,
;     IN DWORD ExitCode
;     )
;
; Routine Description:
;
;    This function is called during the termination of a worker thread to
;    delete its stack: it switches to the stack in the TEB of the thread and
;    terminates in FreeStackAndTerminate.
;
;--

        LEAF_ENTRY SwitchStackThenTerminate
        mov     sp, r1
        mov     r1, r2
        b       FreeStackAndTerminate
        LEAF_END SwitchStackThenTerminate

        END
