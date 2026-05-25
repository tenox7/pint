@ Interlocked operations for ARMv7-A, using the ldrex/strex exclusive monitor.
@ These are the architecture-specific implementations the kernel's portable
@ ExInterlocked*/Interlocked* interfaces map onto (the KE/MIPS XXSPINLK.S /
@ ll-sc analog). INTERLOCKED_RESULT values match arm.h: Zero=0, Negative=1,
@ Positive=2.

.syntax unified
.arm
.text

@ INTERLOCKED_RESULT ExArmInterlockedIncrementLong(PLONG Addend)
.global ExArmInterlockedIncrementLong
ExArmInterlockedIncrementLong:
1:  ldrex   r1, [r0]
    add     r1, r1, #1
    strex   r2, r1, [r0]
    teq     r2, #0
    bne     1b
    dmb
    cmp     r1, #0
    moveq   r0, #0                   @ ResultZero
    movlt   r0, #1                   @ ResultNegative
    movgt   r0, #2                   @ ResultPositive
    bx      lr

@ INTERLOCKED_RESULT ExArmInterlockedDecrementLong(PLONG Addend)
.global ExArmInterlockedDecrementLong
ExArmInterlockedDecrementLong:
1:  ldrex   r1, [r0]
    sub     r1, r1, #1
    strex   r2, r1, [r0]
    teq     r2, #0
    bne     1b
    dmb
    cmp     r1, #0
    moveq   r0, #0
    movlt   r0, #1
    movgt   r0, #2
    bx      lr

@ ULONG ExArmInterlockedExchangeUlong(PULONG Target, ULONG Value)
.global ExArmInterlockedExchangeUlong
ExArmInterlockedExchangeUlong:
1:  ldrex   r2, [r0]
    strex   r3, r1, [r0]
    teq     r3, #0
    bne     1b
    dmb
    mov     r0, r2
    bx      lr

@ LONG InterlockedIncrement(PLONG Addend)  -> new value
.global InterlockedIncrement
InterlockedIncrement:
1:  ldrex   r1, [r0]
    add     r1, r1, #1
    strex   r2, r1, [r0]
    teq     r2, #0
    bne     1b
    dmb
    mov     r0, r1
    bx      lr

@ LONG InterlockedDecrement(PLONG Addend)  -> new value
.global InterlockedDecrement
InterlockedDecrement:
1:  ldrex   r1, [r0]
    sub     r1, r1, #1
    strex   r2, r1, [r0]
    teq     r2, #0
    bne     1b
    dmb
    mov     r0, r1
    bx      lr

@ LONG InterlockedExchange(PLONG Target, LONG Value)  -> old value
.global InterlockedExchange
InterlockedExchange:
1:  ldrex   r2, [r0]
    strex   r3, r1, [r0]
    teq     r3, #0
    bne     1b
    dmb
    mov     r0, r2
    bx      lr

@ LONG InterlockedExchangeAdd(PLONG Addend, LONG Increment)  -> old value
.global InterlockedExchangeAdd
InterlockedExchangeAdd:
1:  ldrex   r2, [r0]
    add     r3, r2, r1
    strex   ip, r3, [r0]
    teq     ip, #0
    bne     1b
    dmb
    mov     r0, r2
    bx      lr

@ PVOID InterlockedCompareExchange(PVOID *Dest, PVOID Exch, PVOID Comp) -> old
.global InterlockedCompareExchange
InterlockedCompareExchange:
1:  ldrex   r3, [r0]
    teq     r3, r2
    bne     2f
    strex   ip, r1, [r0]
    teq     ip, #0
    bne     1b
2:  dmb
    mov     r0, r3
    bx      lr

.section .note.GNU-stack,"",%progbits
