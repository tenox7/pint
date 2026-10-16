;++
;
; Module Name:
;
;    mutexs.asm
;
; Abstract:
;
;    Handle manager fast mutex: the ARM part of GDI\GRE\MIPS\MUTEXS.S, the
;    entries that default the fast mutex to the handle manager lock. The fast
;    mutex itself is the portable one of LOCK.C.
;
;--

#include "kxarm.h"

        IMPORT  gfmHmgr
        IMPORT  AcquireFastMutex
        IMPORT  ReleaseFastMutex

        TEXTAREA

;
; VOID AcquireHmgrFastMutex(VOID)
;

        LEAF_ENTRY AcquireHmgrFastMutex
        ldr     r0, =gfmHmgr                    ; set address of handle manager lock
        b       AcquireFastMutex
        LEAF_END AcquireHmgrFastMutex

;
; VOID ReleaseHmgrFastMutex(VOID)
;

        LEAF_ENTRY ReleaseHmgrFastMutex
        ldr     r0, =gfmHmgr                    ; set address of handle manager lock
        b       ReleaseFastMutex
        LEAF_END ReleaseHmgrFastMutex

        END
