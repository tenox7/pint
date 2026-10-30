;++
;
; Module Name:
;
;    olecall_.asm
;
; Abstract:
;
;    The ARM version of _AfxDispatchCall, the lowest level of the IDispatch
;    implementation of the class library (olecall.cpp has those of the other
;    machines, alpha\olecall_.s and ppc\olecall_.s those of Alpha and
;    PowerPC): it calls a member function with the arguments
;    CCmdTarget::PushStackArgs laid out for it (oledisp1.cpp).
;
; Environment:
;
;    User mode.
;
;--

#include "kxarm.h"

        TEXTAREA

;++
;
; DWORD
; AFXAPI
; _AfxDispatchCall (
;    IN AFX_PMSG pfn,
;    IN void *pArgs,
;    IN UINT nSizeArgs
;    )
;
; Routine Description:
;
;    Call the member function pfn with the arguments of pArgs, as Microsoft C
;    for ARM calls one (the procedure call standard of the ARM architecture,
;    with floating-point arguments in VFP registers): the 64 bytes before pArgs
;    are s0 - s15 (d0 - d7), the floating-point arguments, its first 16 bytes
;    r0 - r3, the 'this' pointer first, and the rest the arguments on the
;    stack.
;
; Arguments:
;
;    pfn (r0) - the member function.
;
;    pArgs (r1) - the arguments.
;
;    nSizeArgs (r2) - the bytes of the arguments: 64, 16 and those of the
;        stack, a multiple of 8.
;
; Return Value:
;
;    That of the member function: in r0 (r0 and r1), s0 or d0, which this
;    function leaves as they are.
;
;--

        NESTED_ENTRY _AfxDispatchCall

        PROLOG_PUSH {r11, lr}
        PROLOG_STACK_SAVE r11

        mov     r12, r0                         ; r12 = the member function
        sub     r3, r1, #64
        vldm    r3, {d0-d7}                     ; the floating-point arguments

;
; The stack arguments, below the stack (which stays 8-byte aligned); each page
; of them is touched in turn, as the stack grows through its guard page.
;

        subs    r2, r2, #64+16                  ; r2 = bytes of stack arguments
        beq     %F30
        sub     r3, sp, r2                      ; r3 = the stack at the call
        mov     r0, sp
10      sub     r0, r0, #0x1000
        cmp     r0, r3
        bls     %F20
        ldr     lr, [r0]
        b       %B10
20      mov     sp, r3
        add     lr, r1, #16
25      ldr     r0, [lr], #4
        str     r0, [r3], #4
        subs    r2, r2, #4
        bne     %B25

30      ldm     r1, {r0-r3}                     ; the core register arguments
        blx     r12

        EPILOG_STACK_RESTORE r11
        EPILOG_POP {r11, pc}

        NESTED_END _AfxDispatchCall

        END
