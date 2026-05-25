@ KE/ARM context-switch assembly cores - the ARMv7-A analog of the register
@ save/restore in KE/MIPS/X4CTXSW.S "SwapContext" and the thread startup /
@ idle entries in X4START.S / THREADBG.S. The thread bookkeeping is in ctxsw.c;
@ these are the pieces that must be assembly (callee-saved register save across
@ a stack switch, and the thread/idle entry points).

.syntax unified
.arm
.fpu vfpv3
.text

@ VOID KiSwapStacks(PVOID *OldKernelStack /*r0*/, PVOID NewKernelStack /*r1*/)
@
@ Save the AAPCS callee-saved state (r4-r11, lr, VFP d8-d15) on the current
@ kernel stack as a KSWITCH_FRAME, record sp in *OldKernelStack, load the new
@ thread's kernel stack, restore its callee-saved state, and return into it.
@ Control returns to the caller's continuation only when this thread is later
@ switched back in. The KSWITCH_FRAME layout (low->high: d8-d15, r4-r11, lr)
@ must match KiInitializeContextThread in ctxsw.c.
.global KiSwapStacks
KiSwapStacks:
    push    {r4-r11, lr}
    vpush   {d8-d15}
    str     sp, [r0]                @ *OldKernelStack = sp
    mov     sp, r1                  @ sp = NewKernelStack
    vpop    {d8-d15}
    pop     {r4-r11, lr}
    bx      lr

@ VOID KiThreadStartup(VOID) - first entry of a newly created thread.
@ KiInitializeContextThread seeds the initial KSWITCH_FRAME so that the first
@ KiSwapStacks restore lands here with r4=SystemRoutine, r5=StartRoutine,
@ r6=StartContext. Lower IRQL to PASSIVE and call the system routine.
.global KiThreadStartup
KiThreadStartup:
    mov     r0, #0                  @ PASSIVE_LEVEL
    bl      KeLowerIrql
    mov     r0, r5                  @ StartRoutine
    mov     r1, r6                  @ StartContext
    blx     r4                      @ SystemRoutine(StartRoutine, StartContext)
1:  b       1b                      @ system routine does not return

@ VOID KiIdleLoop(VOID) - the idle thread's body. Enable interrupts and wait
@ for the next interrupt; the scheduler resumes a ready thread from the trap
@ path. (Reached only once the scheduler/trap layer is wired.)
.global KiIdleLoop
KiIdleLoop:
    cpsie   i
1:  wfi
    b       1b

.section .note.GNU-stack,"",%progbits
