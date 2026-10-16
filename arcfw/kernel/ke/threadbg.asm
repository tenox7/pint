;++
;
; Module Name:
;
;    threadbg.asm
;
; Abstract:
;
;    Thread startup: the ARM port of KE\MIPS\THREADBG.S. The first switch to
;    a new thread returns to KiThreadStartup with sp at the context switch
;    frame KiInitializeContextThread built and
;
;       r4 - the user mode context record, or NULL for a system thread
;       r5 - start context
;       r6 - start routine
;       r7 - system routine
;
;    IRQL is lowered to APC_LEVEL and the system routine called. If it
;    returns, a user thread exits to user mode; a system thread bug checks.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        IMPORT  KeLowerIrql
        IMPORT  KeBugCheck
        IMPORT  KiExceptionExit

        TEXTAREA

        LEAF_ENTRY KiThreadStartup
        add     sp, sp, #ExceptionFrameLength   ; deallocate the context switch frame
        movs    r0, #0
        vmsr    fpscr, r0                       ; clear floating status
        movs    r0, #APC_LEVEL
        bl      KeLowerIrql
        mov     r0, r6                          ; start routine
        mov     r1, r5                          ; start context
        blx     r7                              ; call the system routine
        cmp     r4, #0
        bne     KiExceptionExit                 ; exit to user mode
        movs    r0, #NO_USER_MODE_CONTEXT
        bl      KeBugCheck
        LEAF_END KiThreadStartup

        END
