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
;    above it, the address of the PRCB of the sending processor, which holds
;    the packet, updated together with ldrexd/strexd (the MIPS lld/scd). A
;    target signals that it is done with a packet by clearing its member in
;    the target set of the packet. The packet is made visible before the
;    requests, and the requests before the HAL interrupts the targets. In the
;    NT_UP build requests are never sent, as in the NT_UP build of the MIPS
;    routines.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  KiFreezeTargetExecution
        IMPORT  KiRequestSoftwareInterrupt
        IMPORT  HalRequestIpi
        IMPORT  KiProcessorBlock

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
;    worker routine of a ready packet with the address of the PRCB of the
;    sending processor, which the worker passes to KiIpiSignalPacketDone, and
;    requests APC and DPC software interrupts.
;
;--

        NESTED_ENTRY KiIpiProcessRequests
        PROLOG_PUSH {r4-r6, lr}
        CP_READ r0, 15, 0, 13, 0, 4             ; PCR
        ldr     r0, [r0, #PcPrcb]
        add     r0, r0, #PbRequestSummary
10      ldrexd  r4, r5, [r0]                    ; request summary, sending PRCB
        movs    r2, #0
        movs    r3, #0
        strexd  r1, r2, r3, [r0]                ; zero both
        cmp     r1, #0
        bne     %B10
        dmb                                     ; the packet after the request
        tst     r4, #IPI_PACKET_READY
        beq     %F20
        add     r0, r5, #PbCurrentPacket
        ldm     r0, {r1-r3}                     ; worker parameters
        ldr     r12, [r5, #PbWorkerRoutine]
        mov     r0, r5                          ; packet context: the sending PRCB
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
;    function, on the target processors. A packet is posted in the PRCB of the
;    sending processor: the target set, the worker and its parameters; each
;    target receives the address of that PRCB with its request and clears its
;    member in the target set when done (KiIpiSignalPacketDone), which the
;    sender waits for (KiIpiStallOnPacketTargets). A target with a packet
;    pending is waited for.
;
;--

#if defined(NT_UP)

        LEAF_ENTRY KiIpiSend
        ALTERNATE_ENTRY KiIpiSendPacket
        bx      lr
        LEAF_END KiIpiSend

#else

        NESTED_ENTRY KiIpiSend
        PROLOG_PUSH {r4-r7, lr}
        mov     r4, r0                          ; target set
        ldr     r5, =KiProcessorBlock
10      tst     r4, #1
        beq     %F30
        ldr     r6, [r5]                        ; target PRCB
        add     r6, r6, #PbRequestSummary
20      ldrexd  r2, r3, [r6]                    ; request summary, packet entry
        orr     r2, r2, r1                      ; merge the request
        strexd  r7, r2, r3, [r6]
        cmp     r7, #0
        bne     %B20
30      add     r5, r5, #4
        lsrs    r4, r4, #1
        bne     %B10
        dsb                                     ; requests visible before the interrupt
        EPILOG_POP {r4-r7, lr}
        EPILOG_BRANCH HalRequestIpi             ; interrupt the targets (r0)
        NESTED_END KiIpiSend

        NESTED_ENTRY KiIpiSendPacket
        PROLOG_PUSH {r4-r8, lr}
        ldr     r12, [sp, #6 * 4]               ; Parameter3
        CP_READ r4, 15, 0, 13, 0, 4             ; PCR
        ldr     r4, [r4, #PcPrcb]               ; sending PRCB
        str     r0, [r4, #PbTargetSet]
        str     r1, [r4, #PbWorkerRoutine]
        str     r2, [r4, #PbCurrentPacket]
        str     r3, [r4, #PbCurrentPacket + 4]
        str     r12, [r4, #PbCurrentPacket + 8]
        dmb                                     ; the packet before the requests
        ldr     r5, =KiProcessorBlock
        mov     r6, r0                          ; target set
10      tst     r6, #1
        beq     %F30
        ldr     r7, [r5]                        ; target PRCB
        add     r7, r7, #PbRequestSummary
20      ldrexd  r2, r3, [r7]                    ; request summary, sending PRCB
        tst     r2, #IPI_PACKET_READY
        bne     %B20                            ; the target has a packet pending
        orr     r2, r2, #IPI_PACKET_READY
        mov     r3, r4                          ; sending PRCB
        strexd  r8, r2, r3, [r7]
        cmp     r8, #0
        bne     %B20
30      add     r5, r5, #4
        lsrs    r6, r6, #1
        bne     %B10
        dsb                                     ; requests visible before the interrupt
        EPILOG_POP {r4-r8, lr}
        EPILOG_BRANCH HalRequestIpi             ; interrupt the targets (r0)
        NESTED_END KiIpiSendPacket

#endif

;++
;
; VOID
; KiIpiSignalPacketDone (
;    IN PKIPI_CONTEXT SignalDone
;    )
;
; Routine Description:
;
;    This routine signals that the current processor is done with a packet:
;    it clears its member in the target set of the packet, in the PRCB of the
;    sending processor, after its own accesses to the packet and the data of
;    the request.
;
; Arguments:
;
;    SignalDone (r0) - Supplies the address of the PRCB of the sending
;        processor.
;
;--

        LEAF_ENTRY KiIpiSignalPacketDone
        CP_READ r1, 15, 0, 13, 0, 4             ; PCR
        ldr     r1, [r1, #PcNotMember]          ; all but the current processor
        add     r0, r0, #PbTargetSet
        dmb                                     ; the work before the signal
10      ldrex   r2, [r0]                        ; target set
        ands    r2, r2, r1                      ; clear the current processor
        strex   r3, r2, [r0]
        cmp     r3, #0
        bne     %B10
        bx      lr
        LEAF_END KiIpiSignalPacketDone

        END
