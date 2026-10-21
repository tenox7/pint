;++
;
; Module Name:
;
;    armstart.asm
;
; Abstract:
;
;    The kernel entry and the idle loop: the ARM port of KE\MIPS\X4START.S.
;
;    The OS Loader transfers control to KiSystemStartup at the kernel image
;    in KSEG0 with r0 = the loader parameter block and translation on.
;    Interrupts are disabled (the fast interrupt input is not used and stays
;    masked), the idle thread's stack is loaded, the floating point unit is
;    enabled, and the kernel is initialized; control then returns as the idle
;    thread, which lowers IRQL to DISPATCH_LEVEL and enters the idle loop.
;
;    Every other processor enters at KiInitializeSystem, from the restart block
;    the HAL filled (KeStartAllProcessors, HalStartNextProcessor), with the
;    loader parameter block describing its stacks, PCR and processor block,
;    translation on through the firmware's table and its caches off. Before it
;    writes memory it takes the kernel-only translation table and turns its
;    caches on, which makes them coherent with the other processors': a write
;    with them off would go to memory beneath a line another processor caches.
;    Its data and instruction caches are invalidated first, the level 1 only:
;    the level 2 is shared and holds the other processors' data.
;
;--

#include "ksarm.h"
#include "kxarm.h"
#include "kxspin.h"

        IMPORT  KiArmInitialize
        IMPORT  KeLowerIrql
        IMPORT  KiSwapContext
        IMPORT  HalProcessorIdle

#if !defined(NT_UP)

        IMPORT  KiArmKernelTtbr
        IMPORT  KiBarrierWait
        IMPORT  KiDispatcherLock
        IMPORT  KiProcessorBlock
        IMPORT  KeNumberProcessors
        IMPORT  HalAllProcessorsStarted
        IMPORT  KeBugCheck

#endif

#define SCTLR_C     (1 << 2)
#define SCTLR_Z     (1 << 11)
#define SCTLR_I     (1 << 12)

#define HAL1_INITIALIZATION_FAILED 0x61

        TEXTAREA

        LEAF_ENTRY KiSystemStartup
        ALTERNATE_ENTRY KiInitializeSystem
        cpsid   aif

#if !defined(NT_UP)

        ldr     r1, [r0, #LpbPrcb]
        ldrb    r1, [r1, #PbNumber]
        cmp     r1, #0
        beq     %F10                            ; the boot processor

;
; A secondary processor: invalidate its level 1 data cache by set and way, its
; instruction cache, branch predictor and TB, load the kernel-only table with
; the translation table control and domain access of the kernel, and turn the
; caches on.
;

        movs    r1, #0
        CP_WRITE r1, 15, 2, 0, 0, 0             ; CSSELR: level 1 data cache
        isb
        CP_READ r1, 15, 1, 0, 0, 0              ; CCSIDR
        and     r2, r1, #7
        adds    r2, r2, #4                      ; log2 of the line size
        ubfx    r3, r1, #3, #10                 ; ways - 1
        ubfx    r12, r1, #13, #15               ; sets - 1
        clz     r1, r3                          ; the shift of the way
2       mov     r4, r12                         ; set
3       lsl     r5, r3, r1
        lsl     r6, r4, r2
        orr     r5, r5, r6
        CP_WRITE r5, 15, 0, 7, 6, 2             ; DCISW
        subs    r4, r4, #1
        bpl     %B3
        subs    r3, r3, #1
        bpl     %B2
        dsb
        movs    r1, #0
        CP_WRITE r1, 15, 0, 7, 5, 0             ; ICIALLU
        CP_WRITE r1, 15, 0, 7, 5, 6             ; BPIALL
        CP_WRITE r1, 15, 0, 2, 0, 2             ; TTBCR: TTBR0 only
        movs    r1, #1
        CP_WRITE r1, 15, 0, 3, 0, 0             ; DACR: domain 0 client
        ldr     r1, =KiArmKernelTtbr
        ldr     r1, [r1]
        CP_WRITE r1, 15, 0, 2, 0, 0             ; TTBR0: the kernel-only table
        isb
        movs    r1, #0
        CP_WRITE r1, 15, 0, 13, 0, 1            ; CONTEXTIDR: ASID 0
        CP_WRITE r1, 15, 0, 8, 7, 0             ; TLBIALL
        dsb
        isb
        CP_READ r1, 15, 0, 1, 0, 0              ; SCTLR
        orr     r1, r1, #SCTLR_C
        orr     r1, r1, #SCTLR_Z | SCTLR_I
        CP_WRITE r1, 15, 0, 1, 0, 0
        isb

#endif

10      ldr     sp, [r0, #LpbKernelStack]       ; idle thread stack
        CP_READ r1, 15, 0, 1, 0, 2              ; CPACR: cp10 and cp11 full access
        orr     r1, r1, #0xf00000
        CP_WRITE r1, 15, 0, 1, 0, 2
        isb
        mov     r1, #0x40000000                 ; FPEXC.EN
        vmsr    fpexc, r1
        bl      KiArmInitialize                 ; initialize the processor and the kernel

;
; Control is returned to the idle thread with IRQL at HIGH_LEVEL. Lower IRQL
; to DISPATCH_LEVEL, set the wait IRQL of the idle thread, enable interrupts,
; and enter the idle loop. r4 = PRCB, r5 = DPC listhead and r7 = PCR for the
; loop (the idle thread runs only on its processor).
;

        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r7, #PcPrcb]
        add     r5, r4, #PbDpcListHead
        ldr     r6, [r7, #PcCurrentThread]
        movs    r0, #DISPATCH_LEVEL
        strb    r0, [r6, #ThWaitIrql]
        bl      KeLowerIrql
        cpsie   i

#if !defined(NT_UP)

;
; A processor that starts does not enter the idle loop until all have been
; started; then each but the boot processor performs the platform specific
; operations of a started processor. r8 = the index of the processor block
; whose DPC list the idle loop looks at next.
;

20      ldr     r0, =KiBarrierWait
        ldr     r0, [r0]
        cbz     r0, %F25
        yield
        b       %B20
25      dmb
        ldrb    r0, [r7, #PcNumber]
        cbz     r0, %F30
        bl      HalAllProcessorsStarted
        cbnz    r0, %F30
        movs    r0, #HAL1_INITIALIZATION_FAILED
        bl      KeBugCheck
30      movs    r8, #0

#endif

;
; The idle thread executes at DISPATCH_LEVEL and polls for work: DPCs to
; process and a thread selected to run on this processor. On a multiprocessor
; an idle processor also processes the DPCs of another processor, one in turn
; each time round.
;

        EXPORT  KiIdleLoop
KiIdleLoop
        movs    r0, #0
        str     r0, [r4, #PbQuantumEnd]         ; clear quantum end indicator
        cpsid   i
        ldr     r1, [r7, #PcInterruptRequest]
        bic     r1, r1, #1 << DISPATCH_LEVEL
        str     r1, [r7, #PcInterruptRequest]   ; clear dispatch interrupt pending
        mov     r9, r5                          ; DPC listhead
        add     r10, r4, #PbDpcLock             ; DPC lock
        ldr     r0, [r9, #LsFlink]
        cmp     r0, r9
        bne     %F50                            ; the list of this processor has an entry

#if !defined(NT_UP)

        ldr     r0, =KeNumberProcessors
        ldrb    r0, [r0]
        cmp     r8, r0
        it      hs
        movhs   r8, #0                          ; wrap the processor block index
        ldr     r0, =KiProcessorBlock
        ldr     r0, [r0, r8, lsl #2]            ; processor block
        adds    r8, r8, #1
        cmp     r0, #0
        beq     %F70                            ; processor not started
        cmp     r0, r4
        beq     %F70                            ; this processor
        add     r10, r0, #PbDpcLock
        ldr     r1, [r10]
        cmp     r1, #0
        bne     %F70                            ; its DPC lock is owned
        add     r9, r0, #PbDpcListHead
        ldr     r0, [r9, #LsFlink]
        cmp     r0, r9
        beq     %F70                            ; its DPC list is empty

#else

        b       %F70

#endif

;
; Process an entry from the DPC list r9, under its lock r10.
;

50      SPIN_LOCK_ACQUIRE r10, r10, r1
        ldr     r0, [r9, #LsFlink]
        cmp     r0, r9
        beq     %F60                            ; the list is empty
        ldr     r1, [r0, #LsFlink]
        str     r1, [r9, #LsFlink]
        str     r9, [r1, #LsBlink]
        sub     r0, r0, #DpDpcListEntry         ; DPC object
        ldr     r1, [r0, #DpDeferredContext]
        ldr     r2, [r0, #DpSystemArgument1]
        ldr     r3, [r0, #DpSystemArgument2]
        ldr     r12, [r0, #DpDeferredRoutine]
        movs    r6, #0
        str     r6, [r0, #DpLock]               ; clear DPC inserted state
        SPIN_LOCK_RELEASE r10, r6
        cpsie   i
        movs    r6, #1
        str     r6, [r7, #PcDpcRoutineActive]
        blx     r12                             ; call the DPC routine
        movs    r0, #0
        str     r0, [r7, #PcDpcRoutineActive]
        b       KiIdleLoop

60      movs    r1, #0
        SPIN_LOCK_RELEASE r10, r1

;
; Switch to a thread selected to run on this processor. On a multiprocessor
; the dispatcher database lock is taken, and the thread read again, as it may
; have changed; if the lock is owned the DPC lists are looked at again rather
; than spinning on it.
;

70      ldr     r0, [r4, #PbNextThread]
        cbz     r0, %F90
        cpsie   i

#if !defined(NT_UP)

        ldr     r1, =KiDispatcherLock
        SPIN_LOCK_TRY r1, r1, r2
        bne     KiIdleLoop                      ; the dispatcher database is locked
        ldr     r0, [r4, #PbNextThread]

#endif

        movs    r1, #0
        str     r1, [r4, #PbNextThread]
        bl      KiSwapContext                   ; swap context to the new thread
        b       KiIdleLoop

;
; No DPC and no thread selected: the HAL is called with interrupts disabled
; and returns with them enabled.
;

90      bl      HalProcessorIdle
        b       KiIdleLoop
        LEAF_END KiSystemStartup

        END
