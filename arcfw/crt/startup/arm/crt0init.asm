;++
;
; Module Name:
;
;    crt0init.asm
;
; Abstract:
;
;    The bounds of the C initializer and terminator tables (CRT32\STARTUP\
;    I386\CRT0INIT.ASM for ARM). The linker orders the .CRT$X sections by
;    name, so the entries of a table lie between its A and Z words.
;
;--

        AREA    |.CRT$XIA|, DATA, READWRITE, ALIGN=2
        EXPORT  __xi_a
__xi_a  DCD     0

        AREA    |.CRT$XIZ|, DATA, READWRITE, ALIGN=2
        EXPORT  __xi_z
__xi_z  DCD     0

        AREA    |.CRT$XCA|, DATA, READWRITE, ALIGN=2
        EXPORT  __xc_a
__xc_a  DCD     0

        AREA    |.CRT$XCZ|, DATA, READWRITE, ALIGN=2
        EXPORT  __xc_z
__xc_z  DCD     0

        AREA    |.CRT$XPA|, DATA, READWRITE, ALIGN=2
        EXPORT  __xp_a
__xp_a  DCD     0

        AREA    |.CRT$XPZ|, DATA, READWRITE, ALIGN=2
        EXPORT  __xp_z
__xp_z  DCD     0

        AREA    |.CRT$XTA|, DATA, READWRITE, ALIGN=2
        EXPORT  __xt_a
__xt_a  DCD     0

        AREA    |.CRT$XTZ|, DATA, READWRITE, ALIGN=2
        EXPORT  __xt_z
__xt_z  DCD     0

        END
