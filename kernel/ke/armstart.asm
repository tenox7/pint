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
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  KiArmInitialize
        IMPORT  KeLowerIrql
        IMPORT  KiSwapContext
        IMPORT  HalProcessorIdle

        TEXTAREA

        LEAF_ENTRY KiSystemStartup
        cpsid   aif
        ldr     sp, [r0, #LpbKernelStack]       ; idle thread stack
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
; loop.
;

        CP_READ r7, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r7, #PcPrcb]
        add     r5, r4, #PbDpcListHead
        ldr     r6, [r7, #PcCurrentThread]
        movs    r0, #DISPATCH_LEVEL
        strb    r0, [r6, #ThWaitIrql]
        bl      KeLowerIrql
        cpsie   i

;
; The idle thread executes at DISPATCH_LEVEL and polls for work: DPCs to
; process and a thread selected to run on this processor.
;

        EXPORT  KiIdleLoop
KiIdleLoop
        movs    r0, #0
        str     r0, [r4, #PbQuantumEnd]         ; clear quantum end indicator
        cpsid   i
        ldr     r1, [r7, #PcInterruptRequest]
        bic     r1, r1, #1 << DISPATCH_LEVEL
        str     r1, [r7, #PcInterruptRequest]   ; clear dispatch interrupt pending
        ldr     r0, [r5, #LsFlink]
        cmp     r0, r5
        beq     %F10

;
; Process an entry from the DPC list.
;

        ldr     r1, [r0, #LsFlink]
        str     r1, [r5, #LsFlink]
        str     r5, [r1, #LsBlink]
        sub     r0, r0, #DpDpcListEntry         ; DPC object
        ldr     r1, [r0, #DpDeferredContext]
        ldr     r2, [r0, #DpSystemArgument1]
        ldr     r3, [r0, #DpSystemArgument2]
        ldr     r12, [r0, #DpDeferredRoutine]
        mov     lr, #0
        str     lr, [r0, #DpLock]               ; clear DPC inserted state
        cpsie   i
        mov     lr, #1
        str     lr, [r7, #PcDpcRoutineActive]
        blx     r12                             ; call the DPC routine
        movs    r0, #0
        str     r0, [r7, #PcDpcRoutineActive]
        b       KiIdleLoop

;
; Switch to a thread selected to run on this processor.
;

10      ldr     r0, [r4, #PbNextThread]
        cbz     r0, %F20
        cpsie   i
        movs    r1, #0
        str     r1, [r4, #PbNextThread]
        bl      KiSwapContext                   ; swap context to the new thread
        b       KiIdleLoop

;
; No DPC and no thread selected: the HAL is called with interrupts disabled
; and returns with them enabled.
;

20      bl      HalProcessorIdle
        b       KiIdleLoop
        LEAF_END KiSystemStartup

        END
