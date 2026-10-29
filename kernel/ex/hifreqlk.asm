;++
;
; Module Name:
;
;    hifreqlk.asm
;
; Abstract:
;
;    The high frequency system spin locks and the clock data (the ARM
;    EX\MIPS\HIFREQLK.S). On a multiprocessor each lock sits in its own cache
;    line so a hot lock never shares a line with another high use variable;
;    the uniprocessor build packs them. The tick count, the clock increments
;    and the DPC queue tuning values the clock interrupt reads on every tick
;    share a cache line of their own.
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

        MACRO
        VALUE   $Name, $Value
        EXPORT  $Name
$Name   DCD     $Value
        MEND

        AREA    |.data|, DATA, READWRITE, ALIGN=6

        DCD     0
        SPINLOCK CcMasterSpinLock               ; cache manager master lock
        SPINLOCK CcVacbSpinLock                 ; cache manager VACB lock
        SPINLOCK ExpResourceSpinLock            ; resource package lock
        SPINLOCK IopCancelSpinLock              ; I/O cancel lock
        SPINLOCK IopCompletionLock              ; I/O completion lock
        SPINLOCK IopDatabaseLock                ; I/O database lock
        SPINLOCK IopFastLockSpinLock            ; fast I/O path lock
        SPINLOCK IopVpbSpinLock                 ; I/O VPB lock
        SPINLOCK IoStatisticsLock               ; I/O statistics lock
        SPINLOCK KiContextSwapLock              ; context swap lock
        SPINLOCK KiDispatcherLock               ; dispatcher database lock
        SPINLOCK MmChargeCommitmentLock         ; charge commitment lock
        SPINLOCK MmPfnLock                      ; page frame database lock
        SPINLOCK NonPagedPoolLock               ; nonpaged pool allocation lock

;
; The clock data. KeTickCount is a KSYSTEM_TIME (low part, high part 1, high
; part 2): the clock ticks since the system started.
;

        ALIGN   64
        EXPORT  KeTickCount
KeTickCount DCD 0, 0, 0
        VALUE   KeMaximumIncrement, 0           ; longest clock interval of the HAL (100 ns)
        VALUE   KeTimeAdjustment, 0             ; system time added on each tick (100 ns)
        VALUE   KiTickOffset, 0                 ; time left to the next tick (100 ns)
        VALUE   KiMaximumDpcQueueDepth, 4       ; DPCs queued before a medium DPC interrupts
        VALUE   KiMinimumDpcRate, 3             ; DPC requests per tick that start batching
        VALUE   KiAdjustDpcThreshold, 20        ; ticks between rises of the queue depth
        VALUE   KiIdealDpcRate, 20              ; request rate that keeps the queue depth
#if !defined(NT_UP)
        ALIGN   64
#endif
        DCD     0

        END
