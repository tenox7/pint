;++
;
; Module Name:
;
;    invoke.asm
;
; Abstract:
;
;    The low-level dispatching of the default implementation of
;    IDispatch::Invoke for ARM, as invoke.s is for MIPS, Alpha and PowerPC:
;    InvokeStdCall calls a virtual method of an object with the actuals of a
;    VARIANTARG array and stores its result in a VARIANT.
;
; Environment:
;
;    User mode.
;
;--

#include "kxarm.h"

        IMPORT  g_S_OK
        IMPORT  g_E_INVALIDARG

;
; The VARTYPEs, as variant.h has them.
;

VT_EMPTY        EQU     0
VT_NULL         EQU     1
VT_I2           EQU     2
VT_I4           EQU     3
VT_R4           EQU     4
VT_R8           EQU     5
VT_CY           EQU     6
VT_DATE         EQU     7
VT_BSTR         EQU     8
VT_DISPATCH     EQU     9
VT_ERROR        EQU     10
VT_BOOL         EQU     11
VT_VARIANT      EQU     12
VT_UNKNOWN      EQU     13
VT_MAX          EQU     14
VT_UI1          EQU     17

VT_BYREF        EQU     0x4000
VT_ARRAY        EQU     0x2000
VT_NOMODE       EQU     0x00ff

VARIANT_DATA    EQU     8                       ; offset of the union of data

;
; The frame, below r11: the hidden argument of a structure return, the images
; of the core argument registers (r0 - r3) and of the floating-point argument
; registers (s0 - s15 = d0 - d7), and three of the arguments. At r11: the
; saved r11 and lr (the frame chain), then r3 - r10 and the arguments the
; caller passed on the stack. The actuals go below, from sp, which r11 restores.
;

HiddenParm      EQU     16                      ; r11 - 16: 16 bytes
CoreArgs        EQU     32                      ; r11 - 32: r0 - r3
VfpArgs         EQU     96                      ; r11 - 96: s0 - s15
SavedThis       EQU     100
SavedOvft       EQU     104
SavedVtReturn   EQU     108
LocalSize       EQU     112

ArgRgvt         EQU     40                      ; r11 + 40
ArgRgpvarg      EQU     44
ArgPvargResult  EQU     48

        TEXTAREA

;++
;
; HRESULT
; InvokeStdCall (
;    IN  PVOID _this,
;    IN  unsigned int oVft,
;    IN  unsigned int vtReturn,
;    IN  unsigned int cActuals,
;    IN  VARTYPE *rgvt,
;    IN  VARIANTARG **rgpvarg,
;    OUT VARIANT *pvargResult
;    )
;
; Routine Description:
;
;    Invoke a virtual method through the vtable of _this, at offset oVft,
;    with the actuals rgpvarg[0] - rgpvarg[cActuals - 1] of the types rgvt,
;    and store its result, of the type vtReturn, in pvargResult.
;
; Arguments:
;
;    _this (r0), oVft (r1), vtReturn (r2), cActuals (r3); rgvt, rgpvarg and
;    pvargResult on the stack.
;
; Return Value:
;
;    g_S_OK, or g_E_INVALIDARG for a VARTYPE it cannot pass or return.
;
; Implementation Note:
;
;    The method is called as Microsoft C for ARM calls one (the procedure call
;    standard of the ARM architecture, with floating-point arguments in VFP
;    registers):
;
;    1.  _this is the first argument, in r0. A method that returns a structure
;        (VT_CY, VT_VARIANT) takes the address of the result as the next one,
;        in r1, and returns it in r0.
;
;    2.  The actuals follow from left to right. One of a word or less (VT_I2,
;        VT_BOOL and VT_UI1 widened to a word, VT_I4, VT_ERROR, pointers) goes
;        in the next of r0 - r3, else on the stack.
;
;    3.  VT_CY (a structure of 8 bytes, aligned as an __int64) goes in an even
;        register and the next (r2 and r3), else on the stack at an 8-byte
;        boundary; after it the remaining core registers are not used.
;
;    4.  A VARIANT (16 bytes, 8-byte aligned) starts at an even register; what
;        the registers cannot hold goes on the stack, when nothing has gone on
;        the stack before, else the whole VARIANT goes on the stack.
;
;    5.  VT_R4 goes in the lowest free single register (s0 - s15), VT_R8 and
;        VT_DATE in the lowest free double register (d0 - d7); a single fills a
;        register a double alignment left free. A floating-point actual that
;        does not fit goes on the stack, and so does every one after it.
;
;    6.  The stack is 8-byte aligned at the call.
;
;    7.  The result: VT_UI1, VT_I2, VT_BOOL, VT_I4, VT_ERROR and pointers in
;        r0, VT_R4 in s0, VT_R8 and VT_DATE in d0, VT_CY and VT_VARIANT in the
;        hidden argument.
;
;    As the other versions do, it checks the VARTYPE of the result for a by
;    reference result before the call and for an unknown one after it.
;
;--

        NESTED_ENTRY InvokeStdCall

        PROLOG_PUSH {r3-r10}
        PROLOG_PUSH {r11, lr}
        PROLOG_STACK_SAVE r11

        sub     sp, sp, #LocalSize
        str     r0, [r11, #-SavedThis]
        str     r1, [r11, #-SavedOvft]
        str     r2, [r11, #-SavedVtReturn]

        tst     r2, #VT_BYREF                   ; cannot return by reference
        bne.w   InvalidArg

;
; The stack area of the actuals: 20 bytes an actual at most (a VARIANT and
; its alignment). Each page of it is touched in turn, as the stack grows
; through its guard page.
;

        mov     r6, r3                          ; r6 = actuals left
        add     r12, r3, r3, lsl #2
        lsl     r12, r12, #2                    ; 20 * cActuals
        sub     r12, sp, r12
        bic     r12, r12, #7
        mov     r0, sp
10      sub     r0, r0, #0x1000
        cmp     r0, r12
        bls     %F20
        ldr     r1, [r0]
        b       %B10
20      mov     sp, r12

        ldr     r4, [r11, #ArgRgvt]             ; r4 = &rgvt[i]
        ldr     r5, [r11, #ArgRgpvarg]          ; r5 = &rgpvarg[i]
        sub     r10, r11, #CoreArgs             ; r10 = core register image
        movs    r8, #0                          ; r8 = single registers taken
        movs    r9, #0                          ; r9 = stack bytes taken

        ldr     r0, [r11, #-SavedThis]
        str     r0, [r10]                       ; r0 = _this
        movs    r7, #1                          ; r7 = next core register

        ldr     r2, [r11, #-SavedVtReturn]
        tst     r2, #VT_BYREF :OR: VT_ARRAY     ; a pointer is no structure
        bne.w   ArgLoop
        and     r2, r2, #VT_NOMODE
        cmp     r2, #VT_CY
        beq     %F30
        cmp     r2, #VT_VARIANT
        bne.w   ArgLoop
30      sub     r0, r11, #HiddenParm
        str     r0, [r10, #4]                   ; r1 = address of the result
        movs    r7, #2

ArgLoop
        cmp     r6, #0
        beq.w   DoCall
        subs    r6, r6, #1
        ldrh    r0, [r4], #2                    ; r0 = rgvt[i]
        ldr     r1, [r5], #4                    ; r1 = rgpvarg[i]
        tst     r0, #VT_BYREF :OR: VT_ARRAY
        bne.w   PushI4                          ; a pointer
        and     r0, r0, #VT_NOMODE
        cmp     r0, #VT_UI1
        beq.w   PushUI1
        cmp     r0, #VT_MAX
        bhs.w   InvalidArg
        cmp     r0, #VT_EMPTY
        beq.w   ArgLoop                         ; nothing
        cmp     r0, #VT_I2
        beq.w   PushI2
        cmp     r0, #VT_BOOL
        beq.w   PushI2
        cmp     r0, #VT_R4
        beq.w   PushR4
        cmp     r0, #VT_R8
        beq.w   PushR8
        cmp     r0, #VT_DATE
        beq.w   PushR8
        cmp     r0, #VT_CY
        beq.w   PushCy
        cmp     r0, #VT_VARIANT
        beq.w   PushVar
                                                ; VT_NULL, VT_I4, VT_BSTR,
                                                ; VT_DISPATCH, VT_ERROR, VT_UNKNOWN
PushI4
        ldr     r0, [r1, #VARIANT_DATA]
        b       PushWord

PushI2
        ldrsh   r0, [r1, #VARIANT_DATA]
        b       PushWord

PushUI1
        ldrb    r0, [r1, #VARIANT_DATA]

PushWord                                        ; r0: a core register, else the stack
        cmp     r7, #4
        bhs     %F40
        str     r0, [r10, r7, lsl #2]
        adds    r7, r7, #1
        b       ArgLoop
40      str     r0, [sp, r9]
        adds    r9, r9, #4
        b       ArgLoop

PushCy                                          ; 8 bytes: an even register pair, else the stack
        ldr     r0, [r1, #VARIANT_DATA]
        ldr     r2, [r1, #VARIANT_DATA + 4]
        adds    r7, r7, #1
        bic     r7, r7, #1
        cmp     r7, #2
        bhi     %F50
        add     r3, r10, r7, lsl #2
        str     r0, [r3]
        str     r2, [r3, #4]
        adds    r7, r7, #2
        b       ArgLoop
50      movs    r7, #4
        adds    r9, r9, #7
        bic     r9, r9, #7
        add     r3, sp, r9
        str     r0, [r3]
        str     r2, [r3, #4]
        adds    r9, r9, #8
        b       ArgLoop

PushVar                                         ; the 16 bytes of the VARIANT
        adds    r7, r7, #1
        bic     r7, r7, #1
        cmp     r7, #4
        bhs     %F65
        cmp     r9, #0                          ; split only while the stack is empty
        bne     %F65
        movs    r3, #4                          ; r3 = words left
60      ldr     r0, [r1], #4
        str     r0, [r10, r7, lsl #2]
        adds    r7, r7, #1
        subs    r3, r3, #1
        beq.w   ArgLoop
        cmp     r7, #4
        blo     %B60
62      ldr     r0, [r1], #4
        str     r0, [sp, r9]
        adds    r9, r9, #4
        subs    r3, r3, #1
        bne     %B62
        b       ArgLoop
65      movs    r7, #4
        adds    r9, r9, #7
        bic     r9, r9, #7
        add     lr, sp, r9
        ldm     r1, {r0, r2, r3, r12}
        stm     lr, {r0, r2, r3, r12}
        adds    r9, r9, #16
        b       ArgLoop

PushR4                                          ; a single register, else the stack
        ldr     r0, [r1, #VARIANT_DATA]
        mvn     r2, r8
        ubfx    r2, r2, #0, #16                 ; r2 = free single registers
        cbz     r2, %F70
        rbit    r2, r2
        clz     r2, r2                          ; the lowest
        movs    r3, #1
        lsl     r3, r3, r2
        orr     r8, r8, r3
        sub     r3, r11, #VfpArgs
        str     r0, [r3, r2, lsl #2]
        b       ArgLoop
70      str     r0, [sp, r9]
        adds    r9, r9, #4
        b       ArgLoop

PushR8                                          ; a double register, else the stack
        ldr     r0, [r1, #VARIANT_DATA]
        ldr     r2, [r1, #VARIANT_DATA + 4]
        mvn     r3, r8
        ubfx    r3, r3, #0, #16
        and     r3, r3, r3, lsr #1
        and     r3, r3, #0x55555555             ; r3 = free double registers
        cbz     r3, %F80
        rbit    r3, r3
        clz     r3, r3                          ; the lowest, as its first single
        movs    r12, #3
        lsl     r12, r12, r3
        orr     r8, r8, r12
        sub     r12, r11, #VfpArgs
        add     r12, r12, r3, lsl #2
        str     r0, [r12]
        str     r2, [r12, #4]
        b       ArgLoop
80      movw    r8, #0xffff                     ; no more floating-point registers
        adds    r9, r9, #7
        bic     r9, r9, #7
        add     r12, sp, r9
        str     r0, [r12]
        str     r2, [r12, #4]
        adds    r9, r9, #8
        b       ArgLoop

DoCall
        ldr     r0, [r11, #-SavedThis]
        ldr     r1, [r0]                        ; the vtable
        ldr     r2, [r11, #-SavedOvft]
        ldr     r12, [r1, r2]                   ; the method
        sub     r3, r11, #VfpArgs
        vldm    r3, {d0-d7}
        ldm     r10, {r0-r3}
        blx     r12

;
; The result, as vtReturn describes it (no coercion).
;

        ldr     r2, [r11, #-SavedVtReturn]
        ldr     r3, [r11, #ArgPvargResult]
        strh    r2, [r3]                        ; pvargResult->vt
        tst     r2, #VT_BYREF :OR: VT_ARRAY
        bne.w   RetI4                           ; a pointer
        and     r2, r2, #VT_NOMODE
        cmp     r2, #VT_UI1
        beq.w   RetUI1
        cmp     r2, #VT_MAX
        bhs.w   InvalidArg
        cmp     r2, #VT_EMPTY
        beq.w   Done
        cmp     r2, #VT_I2
        beq.w   RetI2
        cmp     r2, #VT_BOOL
        beq.w   RetI2
        cmp     r2, #VT_R4
        beq.w   RetR4
        cmp     r2, #VT_R8
        beq.w   RetR8
        cmp     r2, #VT_DATE
        beq.w   RetR8
        cmp     r2, #VT_CY
        beq.w   RetCy
        cmp     r2, #VT_VARIANT
        beq.w   RetVar
                                                ; VT_NULL, VT_I4, VT_BSTR,
                                                ; VT_DISPATCH, VT_ERROR, VT_UNKNOWN
RetI4
        str     r0, [r3, #VARIANT_DATA]
        b       Done

RetI2
        strh    r0, [r3, #VARIANT_DATA]
        b       Done

RetUI1
        strb    r0, [r3, #VARIANT_DATA]
        b       Done

RetR4
        vstr    s0, [r3, #VARIANT_DATA]
        b       Done

RetR8
        vstr    d0, [r3, #VARIANT_DATA]
        b       Done

RetCy
        sub     r1, r11, #HiddenParm
        ldr     r0, [r1]
        ldr     r2, [r1, #4]
        str     r0, [r3, #VARIANT_DATA]
        str     r2, [r3, #VARIANT_DATA + 4]
        b       Done

RetVar
        sub     r1, r11, #HiddenParm
        ldm     r1, {r0, r1, r2, r12}
        stm     r3, {r0, r1, r2, r12}

Done
        ldr     r0, =g_S_OK
        ldr     r0, [r0]
        b       Exit

InvalidArg
        ldr     r0, =g_E_INVALIDARG
        ldr     r0, [r0]

Exit
        EPILOG_STACK_RESTORE r11
        EPILOG_POP {r11, lr}
        EPILOG_POP {r3-r10}
        EPILOG_RETURN

        NESTED_END InvokeStdCall

        END
