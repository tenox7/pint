;++
;
; Module Name:
;
;    cinitone.asm
;
; Abstract:
;
;    The onexit table initializer (CRT32\MISC\I386\CINITONE.ASM for ARM).
;
;--

        IMPORT  _onexitinit

        AREA    |.CRT$XIC|, DATA, READWRITE, ALIGN=2
        DCD     _onexitinit

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  __c_onexit
__c_onexit DCD  0

        END
