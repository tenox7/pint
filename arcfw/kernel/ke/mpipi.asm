;++
;
; Module Name:
;
;    mpipi.asm
;
; Abstract:
;
;    Interprocessor interrupt support: the ARM port of KE\MIPS\X4MPIPI.S.
;
;    A request to a processor is posted in its PRCB: the request summary and,
;    above it, the address of the packet entry of the sending processor,
;    updated together with ldrexd/strexd (the MIPS lld/scd). The kernel is
;    built for a uniprocessor (NT_UP), so requests are never sent, as in the
;    NT_UP build of the MIPS routines.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  KiFreezeTargetExecution
        IMPORT  KiRequestSoftwareInterrupt

        TEXTAREA

;++
;
; VOID
; KeIpiInterrupt (
;    IN PKTRAP_FRAME TrapFrame
;    )
;
; Routine Description:
;
;    This routine is entered as the result of an interprocessor interrupt:
;    it processes all interprocessor immediate and packet requests and, if a
;    freeze is requested, freezes the execution of the processor with its
;    state in the trap frame and an exception frame.
;
;--

        NESTED_ENTRY KeIpiInterrupt
        PROLOG_PUSH {lr}
        PROLOG_STACK_ALLOC 12                   ; SwapReturn, Psr, Fpscr
        PROLOG_PUSH {r4-r11}
        PROLOG_VPUSH {d8-d15}
        mov     r4, r0                          ; trap frame
        bl      KiIpiProcessRequests
        tst     r0, #IPI_FREEZE
        beq     %F10
        add     r12, r4, #TrVolatileD           ; save the volatile floating state
        vstm    r12!, {d0-d7}
        vstm    r12, {d16-d31}
        vmrs    r12, fpscr
        str     r12, [r4, #TrFpscr]
        mov     r0, r4                          ; trap frame
        mov     r1, sp                          ; exception frame
        bl      KiFreezeTargetExecution
        ldr     r12, [r4, #TrFpscr]             ; restore the volatile floating state
        vmsr    fpscr, r12
        add     r12, r4, #TrVolatileD
        vldm    r12!, {d0-d7}
        vldm    r12, {d16-d31}
10      EPILOG_VPOP {d8-d15}
        EPILOG_POP {r4-r11}
        EPILOG_STACK_FREE 12
        EPILOG_POP {pc}
        NESTED_END KeIpiInterrupt

;++
;
; ULONG
; KiIpiProcessRequests (
;    VOID
;    )
;
; Routine Description:
;
;    This routine processes the interprocessor requests of the current
;    processor and returns a summary of the requests processed: it calls the
;    worker routine of a ready packet, and requests APC and DPC software
;    interrupts.
;
;--

        NESTED_ENTRY KiIpiProcessRequests
        PROLOG_PUSH {r4-r6, lr}
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r0, [r0, #PcPrcb]
        add     r0, r0, #PbRequestSummary
10      ldrexd  r4, r5, [r0]                    ; request summary, packet entry address
        movs    r2, #0
        movs    r3, #0
        strexd  r1, r2, r3, [r0]                ; zero both
        cmp     r1, #0
        bne     %B10
        dmb
        tst     r4, #IPI_PACKET_READY
        beq     %F20
        ldr     r12, [r5]                       ; sender PRCB
        add     r0, r12, #PbCurrentPacket
        ldm     r0, {r1-r3}                     ; worker parameters
        ldr     r12, [r12, #PbWorkerRoutine]
        mov     r0, r5                          ; packet context
        blx     r12                             ; call the worker routine
20      tst     r4, #IPI_APC
        beq     %F30
        movs    r0, #APC_LEVEL
        bl      KiRequestSoftwareInterrupt
30      tst     r4, #IPI_DPC
        beq     %F40
        movs    r0, #DISPATCH_LEVEL
        bl      KiRequestSoftwareInterrupt
40      mov     r0, r4
        EPILOG_POP {r4-r6, pc}
        NESTED_END KiIpiProcessRequests

;++
;
; VOID
; KiIpiSend (
;    IN KAFFINITY TargetProcessors,
;    IN KIPI_REQUEST IpiRequest
;    )
;
; VOID
; KiIpiSendPacket (
;    IN KAFFINITY TargetProcessors,
;    IN PKIPI_WORKER WorkerFunction,
;    IN PVOID Parameter1,
;    IN PVOID Parameter2,
;    IN PVOID Parameter3
;    )
;
; Routine Description:
;
;    These routines request an operation, or the execution of a worker
;    function, on the target processors. There are none on a uniprocessor.
;
;--

        LEAF_ENTRY KiIpiSend
        ALTERNATE_ENTRY KiIpiSendPacket
        bx      lr
        LEAF_END KiIpiSend

        END
