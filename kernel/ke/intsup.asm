;++
;
; Module Name:
;
;    intsup.asm
;
; Abstract:
;
;    Interrupt object support: the ARM port of KE\MIPS\XXINTSUP.S. The
;    interrupt dispatch code and the code template copied into an interrupt
;    object.
;
;    A routine in PCR->InterruptRoutine[] is called by the HAL interrupt
;    dispatcher with r0 = the address of the routine and r1 = the trap frame
;    (the MIPS a0 and s8). For a connected interrupt object the routine is
;    the object's dispatch code, a copy of KiInterruptTemplate, which computes
;    the address of the object and transfers to its dispatcher with r0 = the
;    interrupt object and r1 = the trap frame. The dispatchers call the
;    service routine with (Interrupt, ServiceContext).
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  KeRaiseIrql
        IMPORT  KeLowerIrql

        TEXTAREA

;
; Save and restore the volatile floating state in the trap frame.
;

        MACRO
        SAVE_VOLATILE_FLOAT_STATE $Frame
        add     r12, $Frame, #TrVolatileD
        vstm    r12!, {d0-d7}
        vstm    r12, {d16-d31}
        vmrs    r12, fpscr
        str     r12, [$Frame, #TrFpscr]
        MEND

        MACRO
        RESTORE_VOLATILE_FLOAT_STATE $Frame
        ldr     r12, [$Frame, #TrFpscr]
        vmsr    fpscr, r12
        add     r12, $Frame, #TrVolatileD
        vldm    r12!, {d0-d7}
        vldm    r12, {d16-d31}
        MEND

;++
;
; BOOLEAN
; KeSynchronizeExecution (
;    IN PKINTERRUPT Interrupt,
;    IN PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
;    IN PVOID SynchronizeContext
;    )
;
; Routine Description:
;
;    This function synchronizes the execution of the specified routine with
;    the execution of the service routine associated with the specified
;    interrupt object: IRQL is raised to the synchronization level of the
;    object, the routine is called with the context, and IRQL is lowered to
;    its previous value. The value the routine returns is returned.
;
;--

        NESTED_ENTRY KeSynchronizeExecution
        PROLOG_PUSH {r4-r6, lr}
        PROLOG_STACK_ALLOC 8                    ; saved IRQL
        mov     r4, r1
        mov     r5, r2
        ldrb    r0, [r0, #InSynchronizeIrql]
        mov     r1, sp
        bl      KeRaiseIrql
        mov     r0, r5
        blx     r4                              ; call the synchronization routine
        mov     r4, r0
        ldrb    r0, [sp]
        bl      KeLowerIrql
        mov     r0, r4
        EPILOG_STACK_FREE 8
        EPILOG_POP {r4-r6, pc}
        NESTED_END KeSynchronizeExecution

;++
;
; KiChainedDispatch
;
; Routine Description:
;
;    An interrupt on a vector connected to more than one interrupt object:
;    the list of connected objects is walked and each service routine called
;    at the object's synchronization level. A level sensitive interrupt stops
;    at the first routine that handles it; a latched one traverses the whole
;    chain. If any object needs the volatile floating state saved, it is
;    saved once.
;
;--

        NESTED_ENTRY KiChainedDispatch
        PROLOG_PUSH {r4-r11, lr}
        PROLOG_STACK_ALLOC 12                   ; saved IRQL
        add     r4, r0, #InInterruptListEntry   ; list head
        mov     r5, r4                          ; current entry
        movs    r6, #0                          ; floating state not saved
        ldrb    r7, [r0, #InMode]
        ldrb    r8, [r0, #InIrql]
        mov     r10, r1                         ; trap frame
10      sub     r0, r5, #InInterruptListEntry
        cbnz    r6, %F20
        ldrb    r1, [r0, #InFloatingSave]
        cbz     r1, %F20
        SAVE_VOLATILE_FLOAT_STATE r10
        movs    r6, #1
        sub     r0, r5, #InInterruptListEntry
20      ldrb    r9, [r0, #InSynchronizeIrql]
        cmp     r8, r9
        beq     %F25
        mov     r0, r9
        mov     r1, sp
        bl      KeRaiseIrql                     ; raise to the synchronization level
        sub     r0, r5, #InInterruptListEntry
25      ldr     r2, [r0, #InServiceRoutine]
        ldr     r1, [r0, #InServiceContext]
        blx     r2                              ; call the service routine
        and     r11, r0, #0xff
        cmp     r8, r9
        beq     %F30
        mov     r0, r8
        bl      KeLowerIrql                     ; lower to the interrupt source level
30      ldr     r5, [r5, #LsFlink]
        cmp     r11, #0
        beq     %F40                            ; not handled
        cmp     r7, #InLevelSensitive
        beq     %F50                            ; level sensitive and handled
40      cmp     r5, r4
        bne     %B10
50      cbz     r6, %F60
        RESTORE_VOLATILE_FLOAT_STATE r10
60      EPILOG_STACK_FREE 12
        EPILOG_POP {r4-r11, pc}
        NESTED_END KiChainedDispatch

;++
;
; KiFloatingDispatch
;
; Routine Description:
;
;    An interrupt on a vector connected to an interrupt object that needs the
;    volatile floating state saved: it is saved, the service routine called
;    at the synchronization level, and the state restored.
;
;--

        NESTED_ENTRY KiFloatingDispatch
        PROLOG_PUSH {r4-r6, lr}
        PROLOG_STACK_ALLOC 8                    ; saved IRQL
        mov     r4, r0                          ; interrupt object
        mov     r5, r1                          ; trap frame
        SAVE_VOLATILE_FLOAT_STATE r5
        ldrb    r0, [r4, #InSynchronizeIrql]
        ldrb    r1, [r4, #InIrql]
        cmp     r0, r1
        beq     %F10
        mov     r1, sp
        bl      KeRaiseIrql                     ; raise to the synchronization level
10      mov     r0, r4
        ldr     r2, [r0, #InServiceRoutine]
        ldr     r1, [r0, #InServiceContext]
        blx     r2                              ; call the service routine
        ldrb    r0, [r4, #InIrql]
        ldrb    r1, [r4, #InSynchronizeIrql]
        cmp     r0, r1
        beq     %F20
        bl      KeLowerIrql                     ; lower to the interrupt source level
20      RESTORE_VOLATILE_FLOAT_STATE r5
        EPILOG_STACK_FREE 8
        EPILOG_POP {r4-r6, pc}
        NESTED_END KiFloatingDispatch

;++
;
; KiInterruptDispatchRaise
;
; Routine Description:
;
;    An interrupt on a vector connected to an interrupt object whose
;    synchronization level is above the interrupt source level: the service
;    routine is called at the synchronization level.
;
;--

        NESTED_ENTRY KiInterruptDispatchRaise
        PROLOG_PUSH {r4, lr}
        PROLOG_STACK_ALLOC 8                    ; saved IRQL
        mov     r4, r0
        ldrb    r0, [r4, #InSynchronizeIrql]
        mov     r1, sp
        bl      KeRaiseIrql                     ; raise to the synchronization level
        mov     r0, r4
        ldr     r2, [r0, #InServiceRoutine]
        ldr     r1, [r0, #InServiceContext]
        blx     r2                              ; call the service routine
        ldrb    r0, [sp]
        bl      KeLowerIrql                     ; lower to the previous level
        EPILOG_STACK_FREE 8
        EPILOG_POP {r4, pc}
        NESTED_END KiInterruptDispatchRaise

;++
;
; KiInterruptDispatchSame
;
; Routine Description:
;
;    An interrupt on a vector connected to an interrupt object whose
;    synchronization level is the interrupt source level: control transfers
;    directly to the service routine.
;
;--

        LEAF_ENTRY KiInterruptDispatchSame
        ldr     r2, [r0, #InServiceRoutine]
        ldr     r1, [r0, #InServiceContext]
        bx      r2
        LEAF_END KiInterruptDispatchSame

;++
;
; KiInterruptTemplate
;
; Routine Description:
;
;    The dispatch code copied into each interrupt object (DISPATCH_LENGTH
;    words): entered with r0 = its address, with the Thumb state bit set, it
;    computes the address of the object and transfers to its dispatcher.
;
;--

        LEAF_ENTRY_NO_PDATA KiInterruptTemplate
        ldr     r12, [r0, #InDispatchAddress - InDispatchCode - 1]
        sub     r0, r0, #InDispatchCode + 1
        bx      r12
        nop
        LEAF_END_NO_PDATA KiInterruptTemplate

;++
;
; KiUnexpectedInterrupt
;
; Routine Description:
;
;    An interrupt on a vector not connected to an interrupt object is
;    dismissed.
;
;--

        LEAF_ENTRY KiUnexpectedInterrupt
        bx      lr
        LEAF_END KiUnexpectedInterrupt

        END
