;++
;
; Module Name:
;
;    ctxsw.asm
;
; Abstract:
;
;    The context switch: the ARM port of KE\MIPS\X4CTXSW.S (unlock dispatcher
;    database, swap thread, dispatch interrupt, swap context) and of the DPC
;    list retirement of KE\MIPS\X4START.S.
;
;    A context switch keeps the nonvolatile state (r4-r11, d8-d15, fpscr) of
;    a thread in an exception frame on its kernel stack, at which the thread's
;    KernelStack points. Each caller of SwapContext builds the frame with its
;    prologue, whose unwind data describes it; SwapContext saves the status
;    and continuation in it and loads the nonvolatile state of the next thread
;    from the frame of that thread, which for a new thread is the start up
;    frame KiInitializeContextThread built, and for the idle thread the frame
;    its idle loop keeps (ke\armstart.asm). Across SwapContext r4 holds the
;    PRCB, r5 the previous thread and r6 the next thread (the MIPS s0-s2).
;
;    The dispatcher database is locked at SYNCH_LEVEL, which is DISPATCH_LEVEL
;    on ARM (as on the PowerPC), and a context switch runs at that level. On
;    a multiprocessor SwapContext takes the context swap lock and then
;    releases the dispatcher database lock, once the next thread is marked
;    running: the previous thread, readied before, may then be selected by
;    another processor, which waits for the context swap lock until this one
;    has left the previous thread's stack and address space. The DPC list of
;    a processor is changed under its DPC lock.
;
;--

#include "ksarm.h"
#include "kxarm.h"
#include "kxspin.h"

        IMPORT  KeLowerIrql
        IMPORT  KiDeliverApc
        IMPORT  KiReadyThread
        IMPORT  KiQuantumEnd
        IMPORT  KiSwapAddressSpace
        IMPORT  KiRequestSoftwareInterrupt
        IMPORT  KiDisableInterrupts
        IMPORT  KiRestoreInterrupts
        IMPORT  KiDispatcherReadyListHead
        IMPORT  KiIdleSummary
        IMPORT  KiReadySummary

#if !defined(NT_UP)

        IMPORT  KeTickCount
        IMPORT  KiContextSwapLock
        IMPORT  KiDispatcherLock
        IMPORT  KiProcessIdWrapLock
        IMPORT  KiSynchronizeProcessIds

#endif

#define KernelMode 0

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
;    This routine is entered at SYNCH_LEVEL with the dispatcher database
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
        ldr     r12, [r2, #PbDpcRoutineActive]
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

;
; A thread has been selected to run and the previous IRQL is below
; DISPATCH_LEVEL: the current thread waits at that IRQL, readied, and the
; selected thread runs. The return is through the exit of KiSwapThread,
; whose exception frame this frame is.
;

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
        b       KiSwapThreadExit
        NESTED_END KxUnlockDispatcherDatabase

;++
;
; NTSTATUS
; KiSwapThread (
;    VOID
;    )
;
; Routine Description:
;
;    This routine is entered at SYNCH_LEVEL with the dispatcher database
;    locked. It selects the next thread to run on this processor and swaps
;    context to it: the thread selected already, or the first thread of the
;    highest priority ready queue that can run on this processor, or else
;    the idle thread, this processor then marked idle.
;
;    On a multiprocessor a thread that last ran on this processor is taken,
;    or one for which this is the ideal processor; failing that the first
;    thread is taken if its priority is high realtime or it has waited for
;    longer than READY_SKIP_QUANTUM ticks, and otherwise a thread further in
;    the queue that last ran here or prefers this processor, up to the first
;    that has waited that long.
;
;    When this thread runs again, at SYNCH_LEVEL, a kernel APC pending at a
;    wait IRQL of PASSIVE_LEVEL is delivered, IRQL is lowered to the wait
;    IRQL and the wait completion status is returned.
;
; Return Value:
;
;    The wait completion status.
;
;--

        NESTED_ENTRY KiSwapThread
        PROLOG_EXCEPTION_FRAME
        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r7, #PcPrcb]
        ldr     r5, [r7, #PcCurrentThread]      ; previous thread
        ldr     r6, [r4, #PbNextThread]
        cbz     r6, %F10
        movs    r0, #0
        str     r0, [r4, #PbNextThread]         ; a thread was selected
        b       %F120

;
; Scan the ready queues from the highest priority with a ready thread down:
;
;   r0 - priority of the queue
;   r1 - ready queue listhead
;   r2 - list entry of the thread
;   r3 - thread
;   r8 - ready summary
;   r9 - the ready queues not yet scanned
;   r10 - set member of this processor
;   r11 - number of this processor
;   r12 - low part of the tick count
;

10      ldr     r8, =KiReadySummary
        ldr     r8, [r8]
        mov     r9, r8

#if !defined(NT_UP)

        ldr     r10, [r7, #PcSetMember]
        ldrb    r11, [r4, #PbNumber]
        ldr     r12, =KeTickCount
        ldr     r12, [r12]

#endif

20      cmp     r9, #0
        beq     %F40                            ; no thread can run: the idle thread
        clz     r0, r9
        rsb     r0, r0, #31                     ; highest priority left
        ldr     r1, =KiDispatcherReadyListHead
        add     r1, r1, r0, lsl #3              ; its ready queue
        ldr     r2, [r1, #LsFlink]

#if defined(NT_UP)

        sub     r3, r2, #ThWaitListEntry
        b       %F110

#else

30      sub     r3, r2, #ThWaitListEntry
        ldr     lr, [r3, #ThAffinity]
        tst     lr, r10
        bne     %F50                            ; the thread can run here
        ldr     r2, [r2, #LsFlink]
        cmp     r2, r1
        bne     %B30
        movs    lr, #1
        lsl     lr, lr, r0
        bic     r9, r9, lr                      ; no thread of this queue can run here
        b       %B20

50      ldrb    lr, [r3, #ThNextProcessor]
        cmp     lr, r11
        beq     %F110                           ; it last ran here
        ldrb    lr, [r3, #ThIdealProcessor]
        cmp     lr, r11
        beq     %F100                           ; this is its ideal processor
        cmp     r0, #LOW_REALTIME_PRIORITY + 9
        bhs     %F100                           ; high realtime priority
        ldr     lr, [r3, #ThWaitTime]
        sub     lr, r12, lr
        cmp     lr, #READY_SKIP_QUANTUM
        bhi     %F100                           ; it has waited too long to be passed over

;
; Look further in the queue: r6 - list entry of the candidate, r9 - the
; candidate (the queues left are no longer needed).
;

        ldr     r6, [r2, #LsFlink]
60      cmp     r6, r1
        beq     %F100                           ; the end of the queue
        sub     r9, r6, #ThWaitListEntry
        ldr     lr, [r9, #ThAffinity]
        tst     lr, r10
        beq     %F70                            ; the candidate cannot run here
        ldrb    lr, [r9, #ThNextProcessor]
        cmp     lr, r11
        beq     %F80                            ; it last ran here
        ldrb    lr, [r9, #ThIdealProcessor]
        cmp     lr, r11
        beq     %F80                            ; this is its ideal processor
70      ldr     lr, [r9, #ThWaitTime]
        sub     lr, r12, lr
        cmp     lr, #READY_SKIP_QUANTUM
        bhi     %F100                           ; it has waited too long to be passed over
        ldr     r6, [r6, #LsFlink]
        b       %B60

80      mov     r2, r6                          ; take the candidate
        mov     r3, r9
100     strb    r11, [r3, #ThNextProcessor]     ; it runs on this processor

#endif

;
; Remove the thread from its ready queue, and if the queue is empty clear its
; ready summary bit.
;

110     ldr     r9, [r2, #LsFlink]
        ldr     lr, [r2, #LsBlink]
        str     r9, [lr, #LsFlink]
        str     lr, [r9, #LsBlink]
        cmp     r9, lr
        bne     %F115                           ; the queue is not empty
        movs    r9, #1
        lsl     r9, r9, r0
        bic     r8, r8, r9
        ldr     r9, =KiReadySummary
        str     r8, [r9]
115     mov     r6, r3
        b       %F120

;
; No ready thread can run on this processor: the idle thread runs, the
; processor marked idle.
;

40      ldr     r1, =KiIdleSummary

#if defined(NT_UP)

        movs    r0, #1

#else

        ldr     r0, [r1]
        orr     r0, r0, r10

#endif

        str     r0, [r1]
        ldr     r6, [r4, #PbIdleThread]

;
; Swap context to the next thread.
;

120     str     r6, [r4, #PbCurrentThread]
        bl      SwapContext                     ; releases the dispatcher database lock

;
; The thread runs again (r0 - the thread, r1 - kernel APC pending), at
; SYNCH_LEVEL. A kernel APC pending at a wait IRQL of PASSIVE_LEVEL is
; delivered here at APC_LEVEL, its software interrupt request cleared; then
; IRQL is lowered to the wait IRQL. The nonvolatile registers are free: the
; epilogue loads them from the frame.
;

KiSwapThreadExit
        ldr     r5, [r0, #ThWaitStatus]         ; wait completion status
        ldrb    r4, [r0, #ThWaitIrql]           ; wait IRQL
        cbnz    r4, %F130                       ; above PASSIVE_LEVEL
        cbz     r1, %F130                       ; no kernel APC pending
        mrs     r12, cpsr
        cpsid   i
        CP_READ r2, 15, 0, 13, 0, 4             ; PCR
        ldr     r3, [r2, #PcInterruptRequest]
        bic     r3, r3, #1 << APC_LEVEL
        str     r3, [r2, #PcInterruptRequest]   ; clear the APC interrupt request
        ldr     r2, [r2, #PcPrcb]
        ldr     r3, [r2, #PbApcBypassCount]
        adds    r3, r3, #1
        str     r3, [r2, #PbApcBypassCount]
        msr     cpsr_c, r12
        movs    r0, #APC_LEVEL
        bl      KeLowerIrql                     ; lower IRQL to APC_LEVEL
        movs    r0, #KernelMode
        movs    r1, #0                          ; no exception frame
        movs    r2, #0                          ; no trap frame
        bl      KiDeliverApc                    ; deliver the kernel mode APCs
130     mov     r0, r4
        bl      KeLowerIrql                     ; lower IRQL to the wait IRQL
        mov     r0, r5
        EPILOG_EXCEPTION_FRAME
        NESTED_END KiSwapThread

        LTORG

;++
;
; VOID
; KiDispatchInterrupt (
;    VOID
;    )
;
; Routine Description:
;
;    The DISPATCH_LEVEL software interrupt: retire the DPC list on the
;    interrupt stack, then process a quantum end or switch to the thread
;    selected to run on this processor. The volatile floating state of an
;    interrupted thread is preserved across the switch. Entered at
;    DISPATCH_LEVEL with the dispatcher database unlocked: it is locked, at
;    SYNCH_LEVEL, to switch (a quantum end that selects a thread returns it
;    locked), and on a multiprocessor if it is owned the DPC list is
;    processed again rather than spinning on it.
;
;--

        NESTED_ENTRY KiDispatchInterrupt
        PROLOG_VPUSH {d16-d31}                  ; save the volatile floating state
        PROLOG_VPUSH {d0-d7}
        PROLOG_EXCEPTION_FRAME
        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r7, #PcPrcb]

;
; Retire the DPC list with interrupts disabled.
;

10      bl      KiDisableInterrupts
        mov     r9, r0                          ; previous interrupt enable
        ldr     r1, [r7, #PcInterruptRequest]
        bic     r1, r1, #1 << DISPATCH_LEVEL
        str     r1, [r7, #PcInterruptRequest]   ; clear dispatch interrupt pending
        add     r1, r4, #PbDpcListHead
        ldr     r0, [r1, #LsFlink]
        cmp     r0, r1
        beq     %F20                            ; the DPC list is empty
        mov     r0, r9
        bl      KiProcessDpcList
20      mov     r0, r9
        bl      KiRestoreInterrupts

;
; Process a quantum end: KiQuantumEnd returns the thread selected to run with
; the dispatcher database locked, or NULL with it unlocked.
;

        ldr     r0, [r4, #PbQuantumEnd]
        cbz     r0, %F30
        movs    r0, #0
        str     r0, [r4, #PbQuantumEnd]
        bl      KiQuantumEnd                    ; process quantum end
        cbnz    r0, %F40                        ; a thread was selected, the database locked
        b       %F50

;
; Switch to the thread selected to run: lock the dispatcher database, with
; interrupts disabled, and raise IRQL to SYNCH_LEVEL.
;

30      ldr     r0, [r4, #PbNextThread]
        cbz     r0, %F50                        ; no thread was selected
        bl      KiDisableInterrupts
        mov     r9, r0

#if !defined(NT_UP)

        ldr     r1, =KiDispatcherLock
        SPIN_LOCK_TRY r1, r1, r2
        beq     %F35
        mov     r0, r9                          ; the dispatcher database is locked
        bl      KiRestoreInterrupts
        b       %B10
35

#endif

        movs    r0, #SYNCH_LEVEL
        strb    r0, [r7, #PcCurrentIrql]        ; raise IRQL to SYNCH_LEVEL
        mov     r0, r9
        bl      KiRestoreInterrupts
40      ldr     r5, [r7, #PcCurrentThread]      ; previous thread
        ldr     r6, [r4, #PbNextThread]         ; next thread
        movs    r0, #0
        str     r0, [r4, #PbNextThread]
        str     r6, [r4, #PbCurrentThread]
        mov     r0, r5
        bl      KiReadyThread                   ; reready the previous thread
        bl      SwapContext                     ; releases the dispatcher database lock
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
;    only thread id register; its TEB in the user read/write one). It is
;    entered at SYNCH_LEVEL with the dispatcher database locked, which on a
;    multiprocessor it releases once it holds the context swap lock; that is
;    released after the previous thread's stack and, if the next thread is in
;    another process, address space are left, the processor moved from the
;    active processor set of the old process to that of the new one. A
;    process id rollover is then synchronized with the other processors, with
;    interrupts enabled. If the next thread has a kernel mode APC pending, an
;    APC interrupt is requested.
;
;    SwapFromIdle is the entry of the idle loop, which has done the work of
;    SwapContext up to it: the idle thread saves no state in its frame.
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
;    r0 - the current thread, r1 - its kernel APC pending, with the nonvolatile
;    state of the thread loaded from its exception frame.
;
;--

        LEAF_ENTRY_NO_PDATA SwapContext
        movs    r0, #Running
        strb    r0, [r6, #ThState]              ; the next thread is running

#if !defined(NT_UP)

        ldr     r0, =KiContextSwapLock
        SPIN_LOCK_ACQUIRE r0, r6, r1
        ldr     r0, =KiDispatcherLock
        movs    r1, #0
        SPIN_LOCK_RELEASE r0, r1

#endif

        str     lr, [sp, #ExSwapReturn]
        vmrs    r0, fpscr
        str     r0, [sp, #ExFpscr]
        mrs     r0, cpsr
        str     r0, [sp, #ExPsr]

        ALTERNATE_ENTRY SwapFromIdle

;
; Swap kernel stacks with interrupts disabled, set the stack bounds and the
; current thread and TEB of the processor, and if the next thread is in
; another process, swap the address space. The PSR is kept in the exception
; frame.
;

        cpsid   i
        clrex                                   ; no exclusive access continues
        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r0, [r6, #ThInitialStack]
        ldr     r1, [r6, #ThStackLimit]
        str     sp, [r5, #ThKernelStack]        ; save old kernel stack pointer
        ldr     sp, [r6, #ThKernelStack]        ; get new kernel stack pointer
        str     r0, [r7, #PcInitialStack]
        str     r1, [r7, #PcStackLimit]
        str     r6, [r7, #PcCurrentThread]
        CP_WRITE r6, 15, 0, 13, 0, 3            ; user read only thread id = thread
        ldr     r1, [r6, #ThTeb]
        str     r1, [r7, #PcTeb]
        CP_WRITE r1, 15, 0, 13, 0, 2            ; user read/write thread id = TEB
        ldr     r8, [r6, #ThApcState + AsProcess] ; new process
        ldr     r9, [r5, #ThApcState + AsProcess] ; old process
        movs    r10, #0                         ; no process id rollover
        cmp     r8, r9
        beq     %F20

#if !defined(NT_UP)

        ldr     r0, [r7, #PcSetMember]
        ldr     r1, [r8, #PrActiveProcessors]
        orr     r1, r1, r0
        str     r1, [r8, #PrActiveProcessors]   ; this processor runs the new process
        ldr     r1, [r9, #PrActiveProcessors]
        bic     r1, r1, r0
        str     r1, [r9, #PrActiveProcessors]   ; and no longer the old one

#endif

        mov     r0, r8
        bl      KiSwapAddressSpace
        mov     r10, r0                         ; process id rollover
20

#if !defined(NT_UP)

        ldr     r0, =KiContextSwapLock
        movs    r1, #0
        SPIN_LOCK_RELEASE r0, r1

#endif

;
; If the new thread has a kernel mode APC pending, request an APC interrupt
; (it is only recorded, interrupts being disabled), and restore the PSR of the
; new thread.
;

        ldrb    r11, [r6, #ThApcState + AsKernelApcPending]
        cmp     r11, #0
        beq     %F30
        movs    r0, #APC_LEVEL
        bl      KiRequestSoftwareInterrupt
30      ldr     r0, [sp, #ExPsr]
        msr     cpsr_c, r0

#if !defined(NT_UP)

;
; Synchronize the process ids of the other processors after a rollover and
; release the process id wrap lock.
;

        cmp     r10, #0
        beq     %F40
        bl      KiSynchronizeProcessIds
        ldr     r0, =KiProcessIdWrapLock
        movs    r1, #0
        SPIN_LOCK_RELEASE r0, r1
40

#endif

;
; Count the context switch of the processor and of the thread, and load the
; nonvolatile state of the new thread.
;

        ldr     r0, [r4, #PbContextSwitches]
        adds    r0, r0, #1
        str     r0, [r4, #PbContextSwitches]
        ldr     r0, [r6, #ThContextSwitches]
        adds    r0, r0, #1
        str     r0, [r6, #ThContextSwitches]
        ldr     r0, [sp, #ExFpscr]
        vmsr    fpscr, r0
        mov     r0, r6                          ; current thread
        mov     r1, r11                         ; kernel APC pending
        ldr     lr, [sp, #ExSwapReturn]
        add     r12, sp, #ExR4
        ldm     r12, {r4-r11}
        vldm    sp, {d8-d15}
        bx      lr
        LEAF_END_NO_PDATA SwapContext

        LTORG

;++
;
; KiProcessDpcList
;
; Routine Description:
;
;    This routine retires the DPC list of the processor on the interrupt
;    stack, whose bounds become those of the processor meanwhile; then the
;    bounds of the current thread's stack are restored. It is entered and
;    left with interrupts disabled.
;
; Arguments:
;
;    r0 - interrupt enable at entry to the dispatch interrupt
;
;--

        NESTED_ENTRY KiProcessDpcList
        PROLOG_PUSH {r4, r5, r11, lr}
        PROLOG_STACK_SAVE r4                    ; the stack of the thread
        mov     r5, r0
        CP_READ r1, 15, 0, 13, 0, 4             ; PCR
        ldr     r0, [r1, #PcInterruptStack]
        str     r0, [r1, #PcInitialStack]
        sub     r2, r0, #KERNEL_STACK_SIZE
        str     r2, [r1, #PcStackLimit]
        mov     sp, r0                          ; switch to the interrupt stack
        str     sp, [r1, #PcOnInterruptStack]
        mov     r0, r5
        bl      KiRetireDpcList
        CP_READ r1, 15, 0, 13, 0, 4
        ldr     r0, [r1, #PcCurrentThread]
        ldr     r2, [r0, #ThInitialStack]
        ldr     r3, [r0, #ThStackLimit]
        str     r2, [r1, #PcInitialStack]       ; the bounds of the thread's stack
        str     r3, [r1, #PcStackLimit]
        movs    r0, #0
        str     r0, [r1, #PcOnInterruptStack]
        EPILOG_STACK_RESTORE r4
        EPILOG_POP {r4, r5, r11, pc}
        NESTED_END KiProcessDpcList

;++
;
; KiRetireDpcList
;
; Routine Description:
;
;    This routine retires the DPC list of the processor on the current stack
;    (from the dispatch interrupt the interrupt stack, from the idle loop the
;    idle thread's): each entry is removed under the DPC lock and its DPC
;    routine called with interrupts enabled as they were at entry to the
;    dispatch interrupt (from the idle loop enabled), the DPC routine active
;    flags of the PRCB and the PCR set. When the list is empty the flags and
;    the DPC interrupt requested flag are cleared, and the list is looked at
;    once more: a DPC queued while the flags were set requested no interrupt.
;    It is entered and left with interrupts disabled.
;
; Arguments:
;
;    r0 - interrupt enable for the DPC routines
;
;--

        NESTED_ENTRY KiRetireDpcList
        PROLOG_PUSH {r4-r8, lr}
        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r7, #PcPrcb]
        add     r5, r4, #PbDpcListHead
        add     r6, r4, #PbDpcLock
        mov     r8, r0
5       str     sp, [r4, #PbDpcRoutineActive]   ; set DPC routine active
        str     sp, [r7, #PcDpcRoutineActive]
10      ldr     r0, [r5, #LsFlink]
        cmp     r0, r5
        beq     %F60                            ; the DPC list is empty

#if !defined(NT_UP)

        SPIN_LOCK_ACQUIRE r6, r4, r1
        ldr     r0, [r5, #LsFlink]
        cmp     r0, r5
        beq     %F50                            ; the DPC list is empty

#endif

        ldr     r1, [r0, #LsFlink]
        str     r1, [r5, #LsFlink]
        str     r5, [r1, #LsBlink]
        sub     r0, r0, #DpDpcListEntry         ; DPC object
        ldr     r1, [r0, #DpDeferredContext]
        ldr     r2, [r0, #DpSystemArgument1]
        ldr     r3, [r0, #DpSystemArgument2]
        ldr     r12, [r0, #DpDeferredRoutine]
        movs    lr, #0
        str     lr, [r0, #DpLock]               ; clear DPC inserted state
        ldr     lr, [r4, #PbDpcQueueDepth]
        subs    lr, lr, #1
        str     lr, [r4, #PbDpcQueueDepth]

#if !defined(NT_UP)

        movs    lr, #0
        SPIN_LOCK_RELEASE r6, lr

#endif

        cmp     r8, #0
        beq     %F20
        cpsie   i
20      blx     r12                             ; call the DPC routine
        cpsid   i
        b       %B10

#if !defined(NT_UP)

50      movs    r0, #0
        SPIN_LOCK_RELEASE r6, r0

#endif

60      movs    r0, #0
        str     r0, [r4, #PbDpcRoutineActive]   ; clear DPC routine active
        str     r0, [r7, #PcDpcRoutineActive]
        str     r0, [r4, #PbDpcInterruptRequested] ; clear DPC interrupt requested
        dmb
        ldr     r0, [r5, #LsFlink]
        cmp     r0, r5
        bne     %B5                             ; the DPC list is not empty
        EPILOG_POP {r4-r8, pc}
        NESTED_END KiRetireDpcList

        END
