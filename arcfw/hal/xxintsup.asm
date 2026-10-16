;++
;
; Module Name:
;
;    xxintsup.asm
;
; Abstract:
;
;    The processor interrupt enable of the Raspberry Pi 2 HAL: disabling
;    interrupts and restoring them to a previous state, around the updates of
;    the interrupt controller state that the HAL interrupt dispatcher also
;    makes.
;
;--

#include "kxarm.h"

        TEXTAREA

;
; ULONG HalpDisableInterrupts(VOID)
;
; Disable interrupts and return the previous program status.
;

        LEAF_ENTRY HalpDisableInterrupts
        mrs     r0, cpsr
        cpsid   i
        bx      lr
        LEAF_END HalpDisableInterrupts

;
; VOID HalpRestoreInterrupts(ULONG Psr)
;
; Restore the interrupt enable of a previous program status.
;

        LEAF_ENTRY HalpRestoreInterrupts
        tst     r0, #0x80
        bne     %F10
        cpsie   i
10      bx      lr
        LEAF_END HalpRestoreInterrupts

        END
