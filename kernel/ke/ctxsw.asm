;++
;
; Module Name:
;
;    ctxsw.asm
;
; Abstract:
;
;    The context switch: the ARM port of KE\MIPS\X4CTXSW.S (unlock dispatcher
;    database, swap context, dispatch interrupt, process DPC list).
;
;    A context switch keeps the nonvolatile state (r4-r11, d8-d15, fpscr) of
;    a thread in an exception frame on its kernel stack, at which the thread's
;    KernelStack points. Each caller of SwapContext builds the frame with its
;    prologue, whose unwind data describes it; SwapContext saves the status
;    and continuation in it and loads the nonvolatile state of the next thread
;    from the frame of that thread, which for a new thread is the start up
;    frame KiInitializeContextThread built. Across SwapContext r4 holds the
;    PRCB, r5 the previous thread and r6 the next thread (the MIPS s0-s2).
;
;    On a multiprocessor the dispatcher database lock is held into
;    SwapContext, which releases it once the previous thread's state is saved
;    and its stack left: the thread, readied before, may then run on another
;    processor. The DPC list of a processor is changed under its DPC lock.
;
;--

#include "ksarm.h"
#include "kxarm.h"
#include "kxspin.h"

        IMPORT  KeLowerIrql
        IMPORT  KiReadyThread
        IMPORT  KiQuantumEnd
        IMPORT  KiSwapProcess
        IMPORT  KiRequestSoftwareInterrupt
        IMPORT  KiDisableInterrupts
        IMPORT  KiRestoreInterrupts

#if !defined(NT_UP)

        IMPORT  KiDispatcherLock
        IMPORT  KiProcessIdWrapLock
        IMPORT  KiSynchronizeProcessIds

#endif

;
; Build and remove an exception frame: the link register, the continuation,
; status and floating status of a context switch, and the nonvolatile state.
;

        MACRO
        PROLOG_EXCEPTION_FRAME
        PROLOG_PUSH {lr}
        PROLOG_STACK_ALLOC 12                   ; SwapReturn, Psr, Fpscr
        PROLOG_PUSH {r4-r11}
        PROLOG_VPUSH {d8-d15}
        MEND

        MACRO
        EPILOG_EXCEPTION_FRAME
        EPILOG_VPOP {d8-d15}
        EPILOG_POP {r4-r11}
        EPILOG_STACK_FREE 12
        EPILOG_POP {pc}
        MEND

;
; Release the dispatcher database lock: the address of the lock in $Lock and
; zero in $Zero.
;

        MACRO
        RELEASE_DISPATCHER_LOCK $Lock, $Zero
#if !defined(NT_UP)
        ldr     $Lock, =KiDispatcherLock
        movs    $Zero, #0
        SPIN_LOCK_RELEASE $Lock, $Zero
#endif
        MEND

        TEXTAREA

;++
;
; VOID
; KiUnlockDispatcherDatabase (
;    IN KIRQL OldIrql
;    )
;
; Routine Description:
;
;    This routine is entered at DISPATCH_LEVEL with the dispatcher database
;    locked. Its function is to either unlock the dispatcher database and
;    return or initiate a context switch if another thread has been selected
;    for execution. A context switch cannot be initiated if the previous IRQL
;    is DISPATCH_LEVEL or above; then, unless a DPC routine is active, a
;    dispatch interrupt is requested.
;
;--

        LEAF_ENTRY KiUnlockDispatcherDatabase
        CP_READ r1, 15, 0, 13, 0, 4             ; PCR
        ldr     r2, [r1, #PcPrcb]
        ldr     r3, [r2, #PbNextThread]
        cbz     r3, %F10                        ; no new thread: restore the previous IRQL
        cmp     r0, #DISPATCH_LEVEL
        blo     KxUnlockDispatcherDatabase      ; a context switch can be initiated
        ldr     r12, [r1, #PcDpcRoutineActive]
        cmp     r12, #0
        bne     %F10                            ; a DPC routine is active
        RELEASE_DISPATCHER_LOCK r2, r3
        b       KxRequestDispatchInterrupt
10      RELEASE_DISPATCHER_LOCK r2, r3
        b       KeLowerIrql
        LEAF_END KiUnlockDispatcherDatabase

        NESTED_ENTRY KxRequestDispatchInterrupt
        PROLOG_PUSH {r0, lr}
        movs    r0, #DISPATCH_LEVEL
        bl      KiRequestSoftwareInterrupt      ; request a dispatch interrupt
        EPILOG_POP {r0, lr}
        EPILOG_BRANCH KeLowerIrql
        NESTED_END KxRequestDispatchInterrupt

        NESTED_ENTRY KxUnlockDispatcherDatabase
        PROLOG_EXCEPTION_FRAME
        CP_READ r1, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r1, #PcPrcb]
        ldr     r5, [r1, #PcCurrentThread]      ; previous thread
        ldr     r6, [r4, #PbNextThread]         ; next thread
        strb    r0, [r5, #ThWaitIrql]           ; save previous IRQL
        movs    r0, #0
        str     r0, [r4, #PbNextThread]
        str     r6, [r4, #PbCurrentThread]
        mov     r0, r5
        bl      KiReadyThread                   ; reready the previous thread
        bl      SwapContext                     ; releases the dispatcher database lock
        ldrb    r0, [r0, #ThWaitIrql]
        bl      KeLowerIrql                     ; restore the wait IRQL
        EPILOG_EXCEPTION_FRAME
        NESTED_END KxUnlockDispatcherDatabase

;++
;
; NTSTATUS
; KiSwapContext (
;    IN PKTHREAD Thread,
;    IN BOOLEAN Ready
;    )
;
; Routine Description:
;
;    This routine is entered at DISPATCH_LEVEL with the dispatcher database
;    locked. It switches to the specified thread, readying the previous one
;    if requested. When this thread runs again, IRQL is lowered to its wait
;    IRQL and the wait completion status is returned; SwapContext has
;    released the dispatcher database lock.
;
;--

        NESTED_ENTRY KiSwapContext
        PROLOG_EXCEPTION_FRAME
        CP_READ r2, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r2, #PcPrcb]
        ldr     r5, [r2, #PcCurrentThread]      ; previous thread
        mov     r6, r0                          ; next thread
        cbz     r1, %F10
        mov     r0, r5
        bl      KiReadyThread                   ; reready the previous thread
10      str     r6, [r4, #PbCurrentThread]
        bl      SwapContext
        ldr     r4, [r0, #ThWaitStatus]         ; wait completion status
        ldrb    r0, [r0, #ThWaitIrql]
        bl      KeLowerIrql                     ; lower IRQL to the wait IRQL
        mov     r0, r4
        EPILOG_EXCEPTION_FRAME
        NESTED_END KiSwapContext

;++
;
; VOID
; KiDispatchInterrupt (
;    VOID
;    )
;
; Routine Description:
;
;    The DISPATCH_LEVEL software interrupt: process the DPC list, then
;    process a quantum end or switch to the thread selected to run on this
;    processor. The volatile floating state of an interrupted thread is
;    preserved across the switch. Entered at DISPATCH_LEVEL with the
;    dispatcher database unlocked: on a multiprocessor it is locked to switch
;    (a quantum end that selects a thread returns it locked), and if it is
;    owned the DPC list is processed again rather than spinning on it.
;
;--

        NESTED_ENTRY KiDispatchInterrupt
        PROLOG_VPUSH {d16-d31}                  ; save the volatile floating state
        PROLOG_VPUSH {d0-d7}
        PROLOG_EXCEPTION_FRAME
        CP_READ r1, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r1, #PcPrcb]

;
; Process the DPC list with interrupts disabled, under its lock.
;

5       bl      KiDisableInterrupts
        mov     r6, r0                          ; previous interrupt enable
        CP_READ r1, 15, 0, 13, 0, 4
        ldr     r2, [r1, #PcInterruptRequest]
        bic     r2, r2, #1 << DISPATCH_LEVEL
        str     r2, [r1, #PcInterruptRequest]   ; clear dispatch interrupt pending
        add     r5, r4, #PbDpcListHead
        add     r10, r4, #PbDpcLock
        SPIN_LOCK_ACQUIRE r10, r10, r0
        ldr     r0, [r5, #LsFlink]
        cmp     r0, r5
        beq     %F10
        bl      KiProcessDpcList
10      movs    r0, #0
        SPIN_LOCK_RELEASE r10, r0
        mov     r0, r6
        bl      KiRestoreInterrupts

;
; Process a quantum end, or switch to the thread selected to run.
;

        ldr     r0, [r4, #PbQuantumEnd]
        cbz     r0, %F30
        movs    r0, #0
        str     r0, [r4, #PbQuantumEnd]
        bl      KiQuantumEnd                    ; process quantum end
        cbnz    r0, %F40                        ; a thread was selected, the database locked
        b       %F50
30      ldr     r0, [r4, #PbNextThread]
        cbz     r0, %F50

#if !defined(NT_UP)

        ldr     r8, =KiDispatcherLock
        SPIN_LOCK_TRY r8, r8, r9
        bne     %B5                             ; the dispatcher database is locked

#endif

40      CP_READ r1, 15, 0, 13, 0, 4
        ldr     r5, [r1, #PcCurrentThread]      ; previous thread
        ldr     r6, [r4, #PbNextThread]         ; next thread
        movs    r0, #0
        str     r0, [r4, #PbNextThread]
        str     r6, [r4, #PbCurrentThread]
        mov     r0, r5
        bl      KiReadyThread                   ; reready the previous thread
        bl      SwapContext
50      EPILOG_VPOP {d8-d15}
        EPILOG_POP {r4-r11}
        EPILOG_STACK_FREE 12
        EPILOG_POP {lr}
        EPILOG_VPOP {d0-d7}
        EPILOG_VPOP {d16-d31}                   ; restore the volatile floating state
        EPILOG_RETURN
        NESTED_END KiDispatchInterrupt

;++
;
; SwapContext
;
; Routine Description:
;
;    This routine swaps context from the previous thread to the next, which
;    becomes this processor's current thread (in the PCR and in the user read
;    only thread id register; its TEB in the user read/write one). On a
;    multiprocessor it releases the dispatcher database lock, after the
;    previous thread's state is saved, its stack left and the address space
;    swapped; a process id rollover is first synchronized with the other
;    processors, with interrupts enabled.
;
; Arguments:
;
;    r4 - PRCB
;    r5 - previous thread
;    r6 - next thread
;    sp - the exception frame of the previous thread, built by the caller
;
; Return Value:
;
;    r0 - the current thread, with the nonvolatile state of the thread loaded
;    from its exception frame.
;
;--

        LEAF_ENTRY_NO_PDATA SwapContext
        str     lr, [sp, #ExSwapReturn]
        vmrs    r0, fpscr
        str     r0, [sp, #ExFpscr]
        ldr     r0, [r4, #PbContextSwitches]
        adds    r0, r0, #1
        str     r0, [r4, #PbContextSwitches]
        ldr     r0, [r6, #ThContextSwitches]
        adds    r0, r0, #1
        str     r0, [r6, #ThContextSwitches]
        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        str     r6, [r7, #PcCurrentThread]
        CP_WRITE r6, 15, 0, 13, 0, 3            ; user read only thread id = thread
        movs    r0, #Running
        strb    r0, [r6, #ThState]

;
; Swap kernel stacks and, if the new thread is in another process, the address
; space. The PSR is kept in the exception frame.
;

        ldr     r8, [r6, #ThApcState + AsProcess] ; new process
        ldr     r9, [r5, #ThApcState + AsProcess] ; old process
        mrs     r0, cpsr
        str     r0, [sp, #ExPsr]
        cpsid   i
        clrex                                   ; no exclusive access continues
        ldr     r1, [r6, #ThInitialStack]
        str     sp, [r5, #ThKernelStack]        ; save old kernel stack pointer
        ldr     sp, [r6, #ThKernelStack]        ; get new kernel stack pointer
        str     r1, [r7, #PcInitialStack]
        movs    r10, #0                         ; no process id rollover
        cmp     r8, r9
        beq     %F20
        mov     r0, r8
        mov     r1, r9
        bl      KiSwapProcess
        mov     r10, r0                         ; process id rollover

;
; If the new thread has a kernel mode APC pending, request an APC interrupt.
; Set the user TEB.
;

20      ldrb    r0, [r6, #ThApcState + AsKernelApcPending]
        cbz     r0, %F30
        movs    r0, #APC_LEVEL
        bl      KiRequestSoftwareInterrupt
30      ldr     r1, [r6, #ThTeb]
        str     r1, [r7, #PcTeb]
        CP_WRITE r1, 15, 0, 13, 0, 2            ; user read/write thread id = TEB
        ldr     r0, [sp, #ExPsr]
        msr     cpsr_c, r0                      ; restore the new thread's PSR

#if !defined(NT_UP)

;
; Synchronize the process ids of the other processors after a rollover and
; release the process id wrap lock, then release the dispatcher database lock.
;

        cmp     r10, #0
        beq     %F40
        bl      KiSynchronizeProcessIds
        ldr     r0, =KiProcessIdWrapLock
        movs    r1, #0
        SPIN_LOCK_RELEASE r0, r1
40      RELEASE_DISPATCHER_LOCK r0, r1

#endif

        ldr     r0, [sp, #ExFpscr]
        vmsr    fpscr, r0
        mov     r0, r6                          ; current thread
        ldr     lr, [sp, #ExSwapReturn]
        add     r12, sp, #ExR4
        ldm     r12, {r4-r11}
        vldm    sp, {d8-d15}
        bx      lr
        LEAF_END_NO_PDATA SwapContext

;++
;
; KiProcessDpcList
;
; Routine Description:
;
;    This routine processes the DPC list on the interrupt stack. It is
;    entered and left with interrupts disabled and the DPC lock held; each
;    DPC routine runs with the lock released and interrupts enabled as they
;    were at entry to the dispatch interrupt.
;
; Arguments:
;
;    r0 - first entry of the DPC list
;    r4 - PRCB
;    r5 - DPC listhead
;    r6 - interrupt enable at entry to the dispatch interrupt
;    r10 - DPC lock
;
;--

        NESTED_ENTRY KiProcessDpcList
        PROLOG_PUSH {r7-r9, lr}
        PROLOG_STACK_SAVE r8                    ; the stack of the thread
        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r9, [r7, #PcInitialStack]
        ldr     r1, [r7, #PcInterruptStack]
        str     r1, [r7, #PcInitialStack]
        mov     sp, r1                          ; switch to the interrupt stack
        str     sp, [r7, #PcOnInterruptStack]
10      ldr     r1, [r0, #LsFlink]
        str     r1, [r5, #LsFlink]
        str     r5, [r1, #LsBlink]
        sub     r0, r0, #DpDpcListEntry         ; DPC object
        ldr     r1, [r0, #DpDeferredContext]
        ldr     r2, [r0, #DpSystemArgument1]
        ldr     r3, [r0, #DpSystemArgument2]
        ldr     r12, [r0, #DpDeferredRoutine]
        mov     lr, #0
        str     lr, [r0, #DpLock]               ; clear DPC inserted state
        SPIN_LOCK_RELEASE r10, lr
        cbz     r6, %F20
        cpsie   i
20      mov     lr, #1
        str     lr, [r7, #PcDpcRoutineActive]
        blx     r12                             ; call the DPC routine
        movs    r0, #0
        str     r0, [r7, #PcDpcRoutineActive]
        cpsid   i
        SPIN_LOCK_ACQUIRE r10, r10, r0
        ldr     r0, [r5, #LsFlink]
        cmp     r0, r5
        bne     %B10
        str     r9, [r7, #PcInitialStack]
        movs    r1, #0
        str     r1, [r7, #PcOnInterruptStack]
        EPILOG_STACK_RESTORE r8
        EPILOG_POP {r7-r9, pc}
        NESTED_END KiProcessDpcList

        END
