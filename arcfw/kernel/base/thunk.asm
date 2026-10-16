;++
;
; Module Name:
;
;    thunk.asm
;
; Abstract:
;
;    Win32 thunks: the ARM port of BASE\CLIENT\MIPS\THUNK.S, the Win32
;    functions that must be written in assembler.
;
;--

#include "kxarm.h"

        IMPORT  BaseFreeStackAndTerminate
        IMPORT  BaseAttachComplete

        TEXTAREA

;++
;
; ULONG
; BaseGetTickCount (
;    IN LARGE_INTEGER CurrentTime,
;    IN LARGE_INTEGER BootTime
;    )
;
; Routine Description:
;
;    This function computes the number of milliseconds since the system was
;    booted from the current time (r0, r1) and the boot time (r2, r3), in
;    100ns units, by a magic divide by 10000.
;
;--

        LEAF_ENTRY BaseGetTickCount
        subs    r2, r0, r2                      ; subtract the boot time
        sbcs    r3, r1, r3
        itt     mi
        movmi   r2, r0                          ; negative: use the current time
        movmi   r3, r1
        ldr     r12, =0xd1b71759                ; magic divide multiplier (10000)
        umull   r0, r1, r2, r12                 ; multiply the low part by the divisor
        umull   r2, r3, r3, r12                 ; multiply the high part by the divisor
        adds    r0, r1, r2                      ; low 32 bits of the product
        adc     r1, r3, #0                      ; high 32 bits of the product
        lsrs    r0, r0, #13                     ; shift into position
        orr     r0, r0, r1, lsl #19
        bx      lr
        LEAF_END BaseGetTickCount

;++
;
; VOID
; BaseSwitchStackThenTerminate (
;     IN PVOID StackLimit,
;     IN PVOID NewStack,
;     IN DWORD ExitCode
;     )
;
; Routine Description:
;
;    This function is called during thread termination to delete the stack
;    of the thread: it switches to the stack in the TEB of the thread and
;    terminates in BaseFreeStackAndTerminate.
;
;--

        LEAF_ENTRY BaseSwitchStackThenTerminate
        mov     sp, r1
        mov     r1, r2
        b       BaseFreeStackAndTerminate
        LEAF_END BaseSwitchStackThenTerminate

;++
;
; VOID
; BaseAttachCompleteThunk (
;     VOID
;     )
;
; Routine Description:
;
;    This function is called by a remote call after a successful debug
;    attach, with r4 the address of the context frame of the interrupted
;    thread (RtlRemoteCall), to break and continue in BaseAttachComplete.
;
;--

        LEAF_ENTRY BaseAttachCompleteThunk
        mov     r0, r4
        b       BaseAttachComplete
        LEAF_END BaseAttachCompleteThunk

        END
