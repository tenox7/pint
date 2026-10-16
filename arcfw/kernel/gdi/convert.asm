;++
;
; Module Name:
;
;    convert.asm
;
; Abstract:
;
;    Convert single floating to long: the ARM port of GDI\MATH\MIPS\CONVERT.S,
;    single floating IEEE conversion to long with directed rounding, and
;    fraction extraction. Each conversion runs with the desired rounding mode
;    and no enables in the floating status; an invalid operation (a value out
;    of the range of a long) is an error. Values are passed in s0.
;
;--

#include "kxarm.h"

#define ROUND_TO_NEAREST 0x0
#define ROUND_TO_ZERO 0xc00000
#define FPSCR_IOC 1                             ; invalid operation cumulative flag

        TEXTAREA

;++
;
; FLOAT
; eFraction (
;    IN FLOAT Value
;    )
;
; Routine Description:
;
;    This function extracts the fractional part of a single floating value.
;
;--

        LEAF_ENTRY eFraction
        vmrs    r1, fpscr                       ; save current floating status
        mov     r2, #ROUND_TO_ZERO
        vmsr    fpscr, r2                       ; set rounding mode with no enables
        vcvt.s32.f32 s1, s0                     ; convert to long with truncation
        vmrs    r2, fpscr                       ; get floating status of result
        vcvt.f32.s32 s1, s1                     ; convert long to single floating
        vmsr    fpscr, r1                       ; restore previous floating status
        tst     r2, #FPSCR_IOC                  ; any error?
        bne     %F10
        vsub.f32 s0, s0, s1                     ; subtract out integer part
        bx      lr
10      movs    r0, #0                          ; set fraction value to zero
        vmov    s0, r0
        bx      lr
        LEAF_END eFraction

;++
;
; BOOL
; bFToFixRound (
;    IN FLOAT Value,
;    OUT PFIX Fixed
;    )
;
; Routine Description:
;
;    This function converts a single floating value to a fixed (28.4) value
;    with rounding toward nearest. TRUE is returned if the conversion is
;    successful, FALSE otherwise.
;
;--

        LEAF_ENTRY bFToFixRound
        vmov    r2, s0                          ; get floating value
        add     r3, r2, #0x2000000              ; bias exponent by 4
        eors    r2, r3, r2                      ; check for a change in sign
        bmi     %F10                            ; sign change
        vmov    s0, r3                          ; set biased floating value
        b       bFToLRound
10      movs    r0, #0                          ; FALSE
        bx      lr
        LEAF_END bFToFixRound

;++
;
; BOOL
; bFToLRound (
;    IN FLOAT Value,
;    OUT PLONG Long
;    )
;
; Routine Description:
;
;    This function converts a single floating value to a long with rounding
;    toward nearest. TRUE is returned if the conversion is successful, FALSE
;    otherwise.
;
;--

        LEAF_ENTRY bFToLRound
        vmrs    r1, fpscr                       ; save current floating status
        mov     r2, #ROUND_TO_NEAREST
        vmsr    fpscr, r2                       ; set rounding mode with no enables
        vcvtr.s32.f32 s1, s0                    ; convert to long with rounding
        vmrs    r2, fpscr                       ; get floating status of result
        vmsr    fpscr, r1                       ; restore previous floating status
        tst     r2, #FPSCR_IOC                  ; any error?
        bne     %F10
        vstr    s1, [r0]                        ; set result value
        movs    r0, #1                          ; TRUE
        bx      lr
10      movs    r0, #0                          ; FALSE
        bx      lr
        LEAF_END bFToLRound

;++
;
; BOOL
; bFToLTrunc (
;    IN FLOAT Value,
;    OUT PLONG Long
;    )
;
; Routine Description:
;
;    This function converts a single floating value to a long with rounding
;    toward zero. TRUE is returned if the conversion is successful, FALSE
;    otherwise.
;
;--

        LEAF_ENTRY bFToLTrunc
        vmrs    r1, fpscr                       ; save current floating status
        mov     r2, #ROUND_TO_ZERO
        vmsr    fpscr, r2                       ; set rounding mode with no enables
        vcvt.s32.f32 s1, s0                     ; convert to long with truncation
        vmrs    r2, fpscr                       ; get floating status of result
        vmsr    fpscr, r1                       ; restore previous floating status
        tst     r2, #FPSCR_IOC                  ; any error?
        bne     %F10
        vstr    s1, [r0]                        ; set result value
        movs    r0, #1                          ; TRUE
        bx      lr
10      movs    r0, #0                          ; FALSE
        bx      lr
        LEAF_END bFToLTrunc

        END
