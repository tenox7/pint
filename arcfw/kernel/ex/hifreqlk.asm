;++
;
; Module Name:
;
;    hifreqlk.asm
;
; Abstract:
;
;    The high frequency system spin locks (the ARM EX\MIPS\HIFREQLK.S). On a
;    multiprocessor each lock sits in its own cache line so a hot lock never
;    shares a line with another high use variable; the uniprocessor build
;    packs them.
;
;--

#include "kxarm.h"

        MACRO
        SPINLOCK $Name
        EXPORT  $Name
#if !defined(NT_UP)
        ALIGN   64
#endif
$Name   DCD     0
        MEND

        AREA    |.data|, DATA, READWRITE, ALIGN=6

        DCD     0
        SPINLOCK CcMasterSpinLock               ; cache manager master lock
        SPINLOCK CcVacbSpinLock                 ; cache manager VACB lock
        SPINLOCK ExpResourceSpinLock            ; resource package lock
        SPINLOCK HandleTableListLock            ; object manager handle table lock
        SPINLOCK IopCancelSpinLock              ; I/O cancel lock
        SPINLOCK IopCompletionLock              ; I/O completion lock
        SPINLOCK IopDatabaseLock                ; I/O database lock
        SPINLOCK IopFastLockSpinLock            ; fast I/O path lock
        SPINLOCK IopLargeIrpLock                ; large IRP lock
        SPINLOCK IopSmallIrpLock                ; small IRP lock
        SPINLOCK IopMdlLock                     ; MDL lock
        SPINLOCK IopVpbSpinLock                 ; I/O VPB lock
        SPINLOCK IoStatisticsLock               ; I/O statistics lock
        SPINLOCK KiApcQueueLock                 ; APC queue database lock
        SPINLOCK KiDispatcherLock               ; dispatcher database lock
        SPINLOCK LpcpLock                       ; LPC lock
        SPINLOCK MmChargeCommitmentLock         ; charge commitment lock
        SPINLOCK MmPfnLock                      ; page frame database lock
        SPINLOCK NonPagedPoolLock               ; nonpaged pool allocation lock
#if !defined(NT_UP)
        ALIGN   64
#endif
        DCD     0

        END
