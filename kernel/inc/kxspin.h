;++
;
; Module Name:
;
;    kxspin.h
;
; Abstract:
;
;    The spin lock sequences of the ARM kernel assembly code: the MIPS ll/sc
;    loops of XXSPINLK.S as ldrex/strex loops, with the memory barriers of the
;    Alpha and Windows on ARM ones (a barrier after the lock is taken and
;    before it is released), spinning on plain loads with yield while the lock
;    is owned. A lock holds zero when free, its owner (any nonzero value, the
;    thread or the lock itself) when owned. A release signals an event (the
;    ARMv7 sequence of Linux), which an idle processor waits for
;    (HalProcessorIdle): a thread is readied for it, or a DPC queued, under a
;    lock. Under NT_UP they expand to nothing.
;
;    The labels 80 to 89 are theirs: the code around them must not use them.
;
;--

;
; SPIN_LOCK_ACQUIRE $Lock, $Owner, $Temp: acquire the lock at [$Lock].
;

        MACRO
        SPIN_LOCK_ACQUIRE $Lock, $Owner, $Temp
#if !defined(NT_UP)
80      ldrex   $Temp, [$Lock]
        cmp     $Temp, #0
        bne     %F81
        strex   $Temp, $Owner, [$Lock]
        cmp     $Temp, #0
        bne     %B80
        dmb
        b       %F82
81      yield
        ldr     $Temp, [$Lock]
        cmp     $Temp, #0
        bne     %B81
        b       %B80
82
#endif
        MEND

;
; SPIN_LOCK_TRY $Lock, $Owner, $Temp: try to acquire the lock at [$Lock]: on
; return Z is set if it was acquired and clear if it is owned. Under NT_UP it
; sets Z.
;

        MACRO
        SPIN_LOCK_TRY $Lock, $Owner, $Temp
#if !defined(NT_UP)
83      ldrex   $Temp, [$Lock]
        cmp     $Temp, #0
        bne     %F84
        strex   $Temp, $Owner, [$Lock]
        cmp     $Temp, #0
        bne     %B83
        dmb
84
#else
        cmp     $Lock, $Lock
#endif
        MEND

;
; SPIN_LOCK_RELEASE $Lock, $Zero: release the lock at [$Lock] and signal an event;
; $Zero holds 0.
;

        MACRO
        SPIN_LOCK_RELEASE $Lock, $Zero
#if !defined(NT_UP)
        dmb
        str     $Zero, [$Lock]
        dsb     ishst
        sev
#endif
        MEND
