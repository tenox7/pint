;++
;
; Module Name:
;
;    divby0.asm
;
; Abstract:
;
;    __helper_divide_by_0: trap an integer division by zero.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

        LEAF_ENTRY __helper_divide_by_0
        DCW     THUMB_DIVIDE_BY_0
        bx      lr
        LEAF_END __helper_divide_by_0

        END
