;++
;
; Module Name:
;
;    huge.asm
;
; Abstract:
;
;    _HUGE - the overflow value of the math functions, +infinity
;    (FP32\TRAN\MIPS\HUGE.S for ARM).
;
;--

        AREA    |.data|, DATA, READWRITE, ALIGN=3
#ifdef CRTDLL
        EXPORT  _HUGE_dll
_HUGE_dll
#else
        EXPORT  _HUGE
_HUGE
#endif
        DCD     0, 0x7ff00000

        END
