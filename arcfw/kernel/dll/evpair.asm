;++
;
; Module Name:
;
;    evpair.asm
;
; Abstract:
;
;    Fast event pair support: the ARM port of DLL\MIPS\EVPAIR.S, the system
;    call interface of the fast event pair service used from the client side.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

;++
;
; NTSTATUS
; XySetLowWaitHighThread (
;    )
;
; Routine Description:
;
;    This function sets the low event of the event pair of the current thread
;    and waits on its high event. STATUS_NO_EVENT_PAIR is returned if no event
;    pair is associated with the thread, otherwise the status of the wait.
;
;--

        LEAF_ENTRY XySetLowWaitHighThread
        ldr     r12, =SET_LOW_WAIT_HIGH
        svc     #1
        bx      lr
        LEAF_END XySetLowWaitHighThread

        END
