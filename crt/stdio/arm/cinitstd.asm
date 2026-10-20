;++
;
; Module Name:
;
;    cinitstd.asm
;
; Abstract:
;
;    The stdio terminator (CRT32\STDIO\I386\CINITSTD.ASM for ARM).
;
;--

        IMPORT  _endstdio

        AREA    |.CRT$XPX|, DATA, READWRITE, ALIGN=2
        DCD     _endstdio

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  _cflush
_cflush DCD     0

        END
