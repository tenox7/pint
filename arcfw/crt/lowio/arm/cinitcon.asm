;++
;
; Module Name:
;
;    cinitcon.asm
;
; Abstract:
;
;    The console handle initializer and terminator, and the console handles
;    (CRT32\LOWIO\I386\CINITCON.ASM for ARM).
;
;--

        IMPORT  _initcon
        IMPORT  _termcon

        AREA    |.CRT$XIC|, DATA, READWRITE, ALIGN=2
        DCD     _initcon

        AREA    |.CRT$XPX|, DATA, READWRITE, ALIGN=2
        DCD     _termcon

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  _coninpfh
        EXPORT  _confh
_coninpfh DCD   -1                              ; console input
_confh  DCD     -1                              ; console output

        END
