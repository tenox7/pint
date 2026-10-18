;++
;
; Module Name:
;
;    j4cache.asm
;
; Abstract:
;
;    Cache maintenance of the Raspberry Pi 2 HAL: the ARM analog of the Jazz
;    HAL's j4cache.s and j4flshbf.s. The Cortex-A7 and Cortex-A53 data caches
;    are physically indexed, so a page is reached through its KSEG0 alias and
;    there is no color aliasing; the instruction cache is virtually indexed, so
;    it is invalidated whole when a physical page changes.
;
;--

#include "kxarm.h"

#define KSEG0_BASE 0x80000000

        TEXTAREA

;
; The line size of the data cache (CTR.DminLine) in r2, and its mask in r3.
;

        MACRO
        DCACHE_LINE
        CP_READ r3, 15, 0, 0, 0, 1              ; CTR
        ubfx    r3, r3, #16, #4
        movs    r2, #4
        lsls    r2, r2, r3
        subs    r3, r2, #1
        MEND

;
; Write back, or write back and invalidate, the data caches by set and way, the
; levels below r3 / 2 (r0 = CLIDR): CRm 10 is DCCSW, 14 is DCCISW.
;

        MACRO
        DCACHE_SET_WAY $CRm
        mov     r10, #0                         ; cache level * 2
10      add     r2, r10, r10, lsr #1
        lsr     r1, r0, r2
        and     r1, r1, #7                      ; cache type of this level
        cmp     r1, #2
        blt     %F40                            ; no data cache
        CP_WRITE r10, 15, 2, 0, 0, 0            ; CSSELR
        isb
        CP_READ r1, 15, 1, 0, 0, 0              ; CCSIDR
        and     r2, r1, #7
        add     r2, r2, #4                      ; set shift
        movw    r4, #0x3ff
        ands    r4, r4, r1, lsr #3              ; maximum way
        clz     r5, r4                          ; way shift
        movw    r7, #0x7fff
        ands    r7, r7, r1, lsr #13             ; maximum set
20      mov     r9, r7
30      lsl     r12, r4, r5
        orr     r11, r10, r12
        lsl     r12, r9, r2
        orr     r11, r11, r12
        CP_WRITE r11, 15, 0, 7, $CRm, 2
        subs    r9, r9, #1
        bge     %B30
        subs    r4, r4, #1
        bge     %B20
40      add     r10, r10, #2
        cmp     r3, r10
        bgt     %B10
        MEND

;
; VOID HalChangeColorPage(PVOID NewColor, PVOID OldColor, ULONG PageFrame)
;
; A physically indexed data cache holds no lines under an old color.
;

        LEAF_ENTRY HalChangeColorPage
        bx      lr
        LEAF_END HalChangeColorPage

;
; VOID HalFlushDcachePage(PVOID Color, ULONG PageFrame, ULONG Length)
;
; Write back and invalidate up to a page of data from the data cache.
;

        LEAF_ENTRY HalFlushDcachePage
        ubfx    r0, r0, #0, #12
        orr     r0, r0, r1, lsl #12
        orr     r0, r0, #KSEG0_BASE
        add     r1, r0, r2
        DCACHE_LINE
        bic     r0, r0, r3
10      CP_WRITE r0, 15, 0, 7, 14, 1            ; DCCIMVAC
        add     r0, r0, r2
        cmp     r0, r1
        blo     %B10
        dsb
        bx      lr
        LEAF_END HalFlushDcachePage

;
; VOID HalPurgeDcachePage(PVOID Color, ULONG PageFrame, ULONG Length)
;
; Invalidate up to a page of data from the data cache.
;

        LEAF_ENTRY HalPurgeDcachePage
        ubfx    r0, r0, #0, #12
        orr     r0, r0, r1, lsl #12
        orr     r0, r0, #KSEG0_BASE
        add     r1, r0, r2
        DCACHE_LINE
        bic     r0, r0, r3
10      CP_WRITE r0, 15, 0, 7, 6, 1             ; DCIMVAC
        add     r0, r0, r2
        cmp     r0, r1
        blo     %B10
        dsb
        bx      lr
        LEAF_END HalPurgeDcachePage

;
; VOID HalPurgeIcachePage(PVOID Color, ULONG PageFrame, ULONG Length)
;
; Invalidate up to a page of instructions from the instruction cache: the whole
; virtually indexed cache and the branch predictor. The caller has written the
; page back from the data cache.
;

        LEAF_ENTRY HalPurgeIcachePage
        movs    r0, #0
        dsb
        CP_WRITE r0, 15, 0, 7, 5, 0             ; ICIALLU
        CP_WRITE r0, 15, 0, 7, 5, 6             ; BPIALL
        dsb
        isb
        bx      lr
        LEAF_END HalPurgeIcachePage

;
; VOID HalSweepDcache(VOID)
;
; Write back and invalidate the data caches by set and way, each level to the
; level of coherency.
;

        NESTED_ENTRY HalSweepDcache
        PROLOG_PUSH {r4-r11, lr}
        dmb
        CP_READ r0, 15, 1, 0, 0, 1              ; CLIDR
        ands    r3, r0, #0x07000000
        lsr     r3, r3, #23                     ; level of coherency * 2
        beq     %F50
        DCACHE_SET_WAY 14                       ; DCCISW
50      mov     r10, #0
        CP_WRITE r10, 15, 2, 0, 0, 0
        dsb
        isb
        EPILOG_POP {r4-r11, pc}
        NESTED_END HalSweepDcache

;
; VOID HalSweepDcacheRange(PVOID BaseAddress, ULONG Length)
;
; Write back and invalidate a range of virtual addresses from the data cache.
;

        LEAF_ENTRY HalSweepDcacheRange
        add     r1, r0, r1
        DCACHE_LINE
        bic     r0, r0, r3
10      CP_WRITE r0, 15, 0, 7, 14, 1            ; DCCIMVAC
        add     r0, r0, r2
        cmp     r0, r1
        blo     %B10
        dsb
        bx      lr
        LEAF_END HalSweepDcacheRange

;
; VOID HalSweepIcache(VOID)
;
; Make the instruction cache coherent with the data cache: write back the data
; caches to the point of unification by set and way (the levels below
; CLIDR.LoUU), then invalidate the instruction cache and the branch predictor.
;

        NESTED_ENTRY HalSweepIcache
        PROLOG_PUSH {r4-r11, lr}
        dmb
        CP_READ r0, 15, 1, 0, 0, 1              ; CLIDR
        ands    r3, r0, #0x38000000
        lsr     r3, r3, #26                     ; level of unification * 2
        beq     %F50
        DCACHE_SET_WAY 10                       ; DCCSW
50      mov     r10, #0
        CP_WRITE r10, 15, 2, 0, 0, 0
        dsb
        CP_WRITE r10, 15, 0, 7, 5, 0            ; ICIALLU
        CP_WRITE r10, 15, 0, 7, 5, 6            ; BPIALL
        dsb
        isb
        EPILOG_POP {r4-r11, pc}
        NESTED_END HalSweepIcache

;
; VOID HalSweepIcacheRange(PVOID BaseAddress, ULONG Length)
;
; Make a range of virtual addresses coherent between the data and instruction
; caches: write back the data to the point of unification, then invalidate
; the instructions and the branch predictor.
;

        LEAF_ENTRY HalSweepIcacheRange
        add     r1, r0, r1
        DCACHE_LINE
        bic     r12, r0, r3
10      CP_WRITE r12, 15, 0, 7, 11, 1           ; DCCMVAU
        add     r12, r12, r2
        cmp     r12, r1
        blo     %B10
        dsb
        CP_READ r3, 15, 0, 0, 0, 1              ; CTR.IminLine
        and     r3, r3, #0xf
        movs    r2, #4
        lsls    r2, r2, r3
        subs    r3, r2, #1
        bic     r0, r0, r3
20      CP_WRITE r0, 15, 0, 7, 5, 1             ; ICIMVAU
        add     r0, r0, r2
        cmp     r0, r1
        blo     %B20
        movs    r0, #0
        CP_WRITE r0, 15, 0, 7, 5, 6             ; BPIALL
        dsb
        isb
        bx      lr
        LEAF_END HalSweepIcacheRange

;
; VOID KeFlushWriteBuffer(VOID)
;
; Drain the write buffer: complete every outstanding memory access.
;

        LEAF_ENTRY KeFlushWriteBuffer
        dsb
        bx      lr
        LEAF_END KeFlushWriteBuffer

;
; VOID HalZeroPage(PVOID NewColor, PVOID OldColor, ULONG PageFrame)
;
; Zero a physical page through its KSEG0 alias.
;

        NESTED_ENTRY HalZeroPage
        PROLOG_PUSH {r4-r10, lr}
        lsl     r0, r2, #12
        orr     r0, r0, #KSEG0_BASE
        add     r1, r0, #4096
        movs    r2, #0
        movs    r3, #0
        movs    r4, #0
        movs    r5, #0
        movs    r6, #0
        movs    r7, #0
        mov     r8, #0
        mov     r9, #0
10      stm     r0!, {r2-r9}
        cmp     r0, r1
        blo     %B10
        EPILOG_POP {r4-r10, pc}
        NESTED_END HalZeroPage

        END
