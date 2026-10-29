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

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  BaseFreeStackAndTerminate
        IMPORT  BaseAttachComplete

        TEXTAREA

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

;++
;
; VOID
; SwitchToFiber (
;    IN PFIBER Fiber
;    )
;
; Routine Description:
;
;    This function saves the state of the current fiber and switches to the
;    specified fiber: the stack limit, the nonvolatile registers (r4 - r11,
;    d8 - d15), the stack pointer and the return address of the current
;    fiber are saved in it, and the stack base, stack limit and deallocation
;    stack of the new fiber are set in the TEB before its state is restored.
;    A new fiber continues in BaseFiberStart (BaseInitializeContext).
;
; Arguments:
;
;    Fiber (r0) - Supplies the address of the new fiber.
;
;--

        LEAF_ENTRY SwitchToFiber
        CP_READ r12, 15, 0, 13, 0, 2                    ; current TEB
        ldr     r1, [r12, #TeFiberData]                 ; current fiber
        ldr     r2, [r12, #TeStackLimit]                ; save the stack limit
        str     r2, [r1, #FbStackLimit]
        add     r2, r1, #FbFiberContext + CxR4          ; save r4 - r11
        stm     r2, {r4-r11}
        add     r2, r1, #FbFiberContext + CxD + 8 * 8   ; save d8 - d15
        vstm    r2, {d8-d15}
        str     sp, [r1, #FbFiberContext + CxSp]        ; save the stack pointer
        bic     r2, lr, #1                              ; and the return address
        str     r2, [r1, #FbFiberContext + CxPc]

        ldr     r2, [r0, #FbStackBase]                  ; restore the stack base,
        str     r2, [r12, #TeStackBase]
        ldr     r2, [r0, #FbStackLimit]                 ; the stack limit
        str     r2, [r12, #TeStackLimit]
        ldr     r2, [r0, #FbDeallocationStack]          ; and the deallocation stack
        str     r2, [r12, #TeDeallocationStack]
        add     r2, r0, #FbFiberContext + CxD + 8 * 8   ; restore d8 - d15
        vldm    r2, {d8-d15}
        add     r2, r0, #FbFiberContext + CxR4          ; restore r4 - r11
        ldm     r2, {r4-r11}
        ldr     sp, [r0, #FbFiberContext + CxSp]        ; restore the stack pointer
        ldr     r2, [r0, #FbFiberContext + CxPc]        ; and the return address
        str     r0, [r12, #TeFiberData]                 ; set the current fiber
        orr     r2, r2, #1                              ; continue in Thumb state
        bx      r2
        LEAF_END SwitchToFiber

        END
