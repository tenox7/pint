;++
;
; Module Name:
;
;    dllsupp.asm
;
; Abstract:
;
;    The public constants of the C library that an image using crtdll.dll
;    references and that the DLL cannot give it (CRT32\DLLSTUFF\I386\DLLSUPP.ASM
;    for ARM): _fltused, which the compiler references from code that uses
;    floating point, and _ldused. _except_list is of the x86 exceptions only.
;
;--

        AREA    |.data|, DATA, READWRITE, ALIGN=2
        EXPORT  _fltused
        EXPORT  _ldused
_fltused DCD    0x9876
_ldused DCD     0x9876

        END
