;++
;
; Module Name:
;
;    debugstb.asm
;
; Abstract:
;
;    The debug service stubs: the ARM port of RTL\MIPS\DEBUGSTB.S. Each puts the
;    debug service code in r12 and executes the debug service instruction (the
;    THUMB_DEBUG_SERVICE undefined opcode); the kernel debugger, or its stub when
;    none is attached, services it and resumes after it.
;
;--

#include "ksarm.h"
#include "kxarm.h"

        TEXTAREA

        LEAF_ENTRY DbgBreakPoint
        mov     r12, #DEBUG_STOP_BREAKPOINT
        EMIT_DEBUG_SERVICE
        bx      lr
        LEAF_END DbgBreakPoint

        LEAF_ENTRY DbgUserBreakPoint
        mov     r12, #DEBUG_STOP_BREAKPOINT
        EMIT_DEBUG_SERVICE
        bx      lr
        LEAF_END DbgUserBreakPoint

;
; ULONG DebugPrompt(IN PSTRING Output, IN PSTRING Input)
;

        LEAF_ENTRY DebugPrompt
        ldrh    r3, [r1, #StrMaximumLength]     ; maximum length of input string
        ldr     r2, [r1, #StrBuffer]            ; address of input string
        ldrh    r1, [r0, #StrLength]            ; length of output string
        ldr     r0, [r0, #StrBuffer]            ; address of output string
        mov     r12, #DEBUG_PROMPT_BREAKPOINT
        EMIT_DEBUG_SERVICE
        bx      lr
        LEAF_END DebugPrompt

;
; VOID DebugLoadImageSymbols(IN PSTRING FileName, IN PKD_SYMBOLS_INFO SymbolInfo)
;

        LEAF_ENTRY DebugLoadImageSymbols
        mov     r12, #DEBUG_LOAD_SYMBOLS_BREAKPOINT
        EMIT_DEBUG_SERVICE
        bx      lr
        LEAF_END DebugLoadImageSymbols

;
; VOID DebugUnLoadImageSymbols(IN PSTRING FileName, IN PKD_SYMBOLS_INFO SymbolInfo)
;

        LEAF_ENTRY DebugUnLoadImageSymbols
        mov     r12, #DEBUG_UNLOAD_SYMBOLS_BREAKPOINT
        EMIT_DEBUG_SERVICE
        bx      lr
        LEAF_END DebugUnLoadImageSymbols

;
; NTSTATUS DebugPrint(IN PSTRING Output)
;

        LEAF_ENTRY DebugPrint
        ldrh    r1, [r0, #StrLength]            ; length of output string
        ldr     r0, [r0, #StrBuffer]            ; address of output string
        mov     r12, #DEBUG_PRINT_BREAKPOINT
        EMIT_DEBUG_SERVICE
        bx      lr
        LEAF_END DebugPrint

        END
