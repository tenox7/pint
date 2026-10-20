;++
;
; Module Name:
;
;    cinittmp.asm
;
; Abstract:
;
;    The temporary file terminator (CRT32\STDIO\I386\CINITTMP.ASM for ARM).
;
;--

        IMPORT  _rmtmp

        AREA    |.CRT$XPX|, DATA, READWRITE, ALIGN=2
        DCD     _rmtmp

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  _tmpoff
        EXPORT  _tempoff
        EXPORT  _old_pfxlen
_tmpoff DCD     1
_tempoff DCD    1
_old_pfxlen DCD 0

        END
