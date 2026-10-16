;++
;
; Module Name:
;
;    cinitclk.asm
;
; Abstract:
;
;    The clock initializer and its start time (CRT32\TIME\I386\CINITCLK.ASM
;    for ARM).
;
;--

        IMPORT  __inittime

        AREA    |.CRT$XIC|, DATA, READWRITE, ALIGN=2
        DCD     __inittime

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  __itimeb
__itimeb                                        ; struct _timeb: four fields and padding
        DCD     0
        DCW     0, 0, 0, 0

        END
