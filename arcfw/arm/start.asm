;++
;
; Module Name:
;
;    start.asm
;
; Abstract:
;
;    The reset entry of the ARC firmware (the ARM analog of the x86 SU module):
;    the first bytes of the image, which the Raspberry Pi firmware loads at
;    0x8000 and enters in the ARM state, as QEMU does. Its two ARM instructions
;    switch to the Thumb state; the word after them is the size of the image,
;    which the build writes (pe2img). Every core enters here; the secondary ones
;    wait, and the first one leaves the hypervisor mode for the supervisor mode,
;    allows unaligned accesses, takes exceptions in the Thumb state, enables the
;    floating point unit, sets up its stack and calls the firmware.
;
;--

#include "kxarm.h"

        EXPORT  FwStart
        EXPORT  FwInitialStackTop
        IMPORT  cmain

        AREA    |.text|, CODE, READONLY, ALIGN=5
        THUMB

FwStart
        DCD     0xe28f0005                      ; add r0, pc, #5  (ARM state)
        DCD     0xe12fff10                      ; bx r0: to FwStart1, Thumb state
        DCD     0                               ; the size of the image

FwStart1
        mrc     p15, 0, r0, c0, c0, 5           ; multiprocessor affinity
        ands    r0, r0, #3
        beq     %F2
1       wfi                                     ; the secondary cores wait
        b       %B1

2       mrs     r0, cpsr                        ; leave the hypervisor mode
        and     r1, r0, #0x1f
        cmp     r1, #0x1a
        bne     %F3
        bic     r0, r0, #0x1f
        orr     r0, r0, #0xf3                   ; supervisor, Thumb, IRQ and FIQ masked
        msr     spsr_hyp, r0
        adr     r1, %F3
        msr     elr_hyp, r1
        eret
3       cpsid   if, #0x13

        mrc     p15, 0, r0, c1, c0, 0           ; system control register:
        bic     r0, r0, #(1 << 1)               ; unaligned accesses allowed,
        orr     r0, r0, #(1 << 30)              ; exceptions taken in Thumb state
        mcr     p15, 0, r0, c1, c0, 0
        isb

        mrc     p15, 0, r0, c1, c0, 2           ; coprocessors 10 and 11 enabled,
        orr     r0, r0, #(0xf << 20)            ; as the Jazz firmware enables
        mcr     p15, 0, r0, c1, c0, 2           ; coprocessor 1
        isb
        mov     r0, #0x40000000
        vmsr    fpexc, r0

        ldr     r0, =FwInitialStackTop
        mov     sp, r0
        mov     r0, #0
        mcr     p15, 0, r0, c7, c5, 0           ; invalidate the instruction cache
        mcr     p15, 0, r0, c7, c5, 6           ; and the branch predictor
        dsb
        isb

        bl      cmain
4       wfi
        b       %B4

        LTORG

;
; The stack of the firmware.
;

        AREA    |.bss|, NOINIT, READWRITE, ALIGN=3
FwInitialStack
        SPACE   0x10000
FwInitialStackTop

        END
