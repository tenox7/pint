;++
;
; Module Name:
;
;    fpctrl.asm
;
; Abstract:
;
;    The floating point control routines (FPW32\TRAN\MIPS\FPCTRL.S for ARM).
;    The internal control word is the FPSCR with its exception enables
;    inverted into masks (trans.h).
;
;--

#include "kxarm.h"

#define ENABLES 0x1f00                          ; the IEEE exception trap enables
#define FLAGS 0x9f                              ; the cumulative exception flags
#define FZ 0x1000000                            ; flush to zero

        TEXTAREA

;
; unsigned _ctrlfp(unsigned newctrl, unsigned mask) - returns the old word
;

        LEAF_ENTRY _ctrlfp
        vmrs    r2, fpscr                       ; old control word
        eor     r2, r2, #ENABLES                ; inverse exception enable bits
        and     r0, r0, r1                      ; newctrl & mask
        bic     r3, r2, r1                      ; oldCw & ~mask
        orr     r3, r3, r0                      ; newctrl & mask | oldCw & ~mask
        eor     r3, r3, #ENABLES                ; inverse exception masks
        vmsr    fpscr, r3                       ; set new control word
        mov     r0, r2
        bx      lr
        LEAF_END _ctrlfp

;
; unsigned _statfp(void)
;

        LEAF_ENTRY _statfp
        vmrs    r0, fpscr
        bx      lr
        LEAF_END _statfp

;
; unsigned _clrfp(void) - clears the exception flags, returns the old word
;

        LEAF_ENTRY _clrfp
        vmrs    r0, fpscr
        bic     r1, r0, #FLAGS
        vmsr    fpscr, r1
        bx      lr
        LEAF_END _clrfp

;
; void _FPreset(void) - the default state, keeping flush to zero
;

        LEAF_ENTRY _FPreset
        vmrs    r0, fpscr
        and     r0, r0, #FZ
        vmsr    fpscr, r0
        bx      lr
        LEAF_END _FPreset

;
; void _set_statfp(unsigned sw)
;

        LEAF_ENTRY _set_statfp
        vmrs    r1, fpscr
        orr     r1, r1, r0
        vmsr    fpscr, r1
        bx      lr
        LEAF_END _set_statfp

;
; unsigned _get_fpscr(void) / void _set_fpscr(unsigned)
;

        LEAF_ENTRY _get_fpscr
        vmrs    r0, fpscr
        bx      lr
        LEAF_END _get_fpscr

        LEAF_ENTRY _set_fpscr
        vmsr    fpscr, r0
        bx      lr
        LEAF_END _set_fpscr

        END
