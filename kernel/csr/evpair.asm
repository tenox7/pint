;++
;
; Module Name:
;
;    evpair.asm
;
; Abstract:
;
;    Fast event pair support: the ARM port of CSR\SERVER\MIPS\EVPAIR.S, the
;    system call interface of the fast event pair service used from the
;    server side.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

;++
;
; NTSTATUS
; XySetHighWaitLowThread (
;    )
;
; Routine Description:
;
;    This function sets the high event of the event pair of the current
;    thread and waits on its low event. STATUS_NO_EVENT_PAIR is returned if no
;    event pair is associated with the thread, otherwise the status of the
;    wait.
;
;--

        LEAF_ENTRY XySetHighWaitLowThread
        ldr     r12, =SET_HIGH_WAIT_LOW
        svc     #1
        bx      lr
        LEAF_END XySetHighWaitLowThread

        END
