;++
;
; Module Name:
;
;    chkstk.asm
;
; Abstract:
;
;    __chkstk, the stack probe of a large frame Microsoft C for ARM calls: r4 the
;    size of a frame in words -> r4 its size in bytes. Each page of the frame is
;    touched in order down from the stack pointer, as the guard page of an NT thread
;    stack grows it (the MIPS _RtlCheckStack). Only r4 changes.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

        NESTED_ENTRY __chkstk
        PROLOG_PUSH {r5, r6}
        lsl     r4, r4, #2
        add     r5, sp, #8
        sub     r6, r5, r4
1       sub     r5, r5, #0x1000
        cmp     r5, r6
        blo     %F2
        ldr     r12, [r5]
        b       %B1
2       ldr     r12, [r6]
        EPILOG_POP {r5, r6}
        EPILOG_RETURN
        NESTED_END __chkstk

        END
