;++
;
; Module Name:
;
;    cachea.asm
;
; Abstract:
;
;    Glyph metrics of a realized font: the ARM port of GRE\MIPS\CACHEA.S. The
;    realized font object, the count, the glyph position and wide character
;    arrays and the current glyph run stay in nonvolatile registers across
;    the calls to the font cache.
;
;--

#include "kxarm.h"
#include "gdiarm.h"

        IMPORT  xprunFindRunRFONTOBJ
        IMPORT  xInsertMetricsRFONTOBJ
        IMPORT  xInsertMetricsPlusRFONTOBJ
        IMPORT  xInsertGlyphbitsRFONTOBJ
#ifdef FE_SB
        IMPORT  xwpgdGetLinkMetricsRFONTOBJ
        IMPORT  xwpgdGetLinkMetricsPlusRFONTOBJ
#else
        IMPORT  xpgdDefault
#endif

        TEXTAREA

;++
;
; BOOL
; xGetGlyphMetrics (
;    IN RFONTOBJ *pfobj,
;    IN COUNT c,
;    IN GLYPHPOS *pgp,
;    IN WCHAR *pwc
;    )
;
; Routine Description:
;
;    This function fills the glyph position array with the glyph handle and
;    the glyph data of each of an array of wide characters, from the glyph
;    runs of the realized font. Only the metrics of the glyph data are made
;    valid.
;
; Arguments:
;
;    pfobj (r0) - the realized font object.
;
;    c (r1) - the number of wide characters (at least one).
;
;    pgp (r2) - the glyph position array.
;
;    pwc (r3) - the wide character array.
;
; Return Value:
;
;    TRUE if every character was translated, else FALSE.
;
;--

        NESTED_ENTRY xGetGlyphMetrics
        PROLOG_PUSH {r4-r10, lr}

        mov     r4, r0                          ; realized font object
        mov     r5, r1                          ; characters left
        mov     r6, r2                          ; next glyph position
        mov     r7, r3                          ; next wide character
        ldr     r0, [r4, #rfo_prfnt]            ; realized font
        ldr     r0, [r0, #rf_wcgp]              ; its glyph runs
        ldr     r8, [r0, #gt_agpRun + gr_wcLow] ; the first run is the current run:
        ldr     r9, [r0, #gt_agpRun + gr_cGlyphs] ; its first character, number of
        ldr     r10, [r0, #gt_agpRun + gr_apgd] ; glyphs and glyph data pointers

10      ldrh    r1, [r7]                        ; next wide character
        sub     r2, r1, r8                      ; its index in the current run
        cmp     r2, r9
        bhs     %F50                            ; not in the current run
20      ldr     r0, [r10, r2, lsl #2]           ; its glyph data
        cmp     r0, #0
        beq     %F60                            ; no metrics yet
30      str     r0, [r6, #gp_pgdf]
        ldr     r0, [r0, #gd_hg]
        str     r0, [r6, #gp_hg]
        adds    r6, r6, #GLYPHPOS
        adds    r7, r7, #2
        subs    r5, r5, #1
        bne     %B10                            ; more characters
        movs    r0, #TRUE
40
        EPILOG_POP {r4-r10, pc}

;
; The character is not in the current run: the run that maps it is the
; current run.
;

50      mov     r0, r4                          ; (r1 the wide character)
        bl      xprunFindRunRFONTOBJ
        ldrh    r1, [r7]
        ldr     r8, [r0, #gr_wcLow]
        ldr     r9, [r0, #gr_cGlyphs]
        ldr     r10, [r0, #gr_apgd]
        sub     r2, r1, r8
        cmp     r2, r9
        blo     %B20                            ; it maps the character

;
; No run maps the character: its glyph is the default glyph, and the first
; run (of the glyph runs as they are now) is the current run again.
;

        mov     r0, r4
#ifdef FE_SB
        bl      xwpgdGetLinkMetricsRFONTOBJ     ; (r1 the wide character)
#else
        bl      xpgdDefault
#endif
        cmp     r0, #0
        beq     %B40                            ; none: FALSE
        ldr     r1, [r4, #rfo_prfnt]
        ldr     r1, [r1, #rf_wcgp]
        ldr     r8, [r1, #gt_agpRun + gr_wcLow]
        ldr     r9, [r1, #gt_agpRun + gr_cGlyphs]
        ldr     r10, [r1, #gt_agpRun + gr_apgd]
        b       %B30

;
; The glyph has no metrics yet: the font cache gets them.
;

60      add     r3, r10, r2, lsl #2             ; address of its glyph data pointer
        mov     r2, r1                          ; the wide character
        mov     r1, r3
        mov     r0, r4
        bl      xInsertMetricsRFONTOBJ
        cmp     r0, #0
        beq     %B40                            ; failed: FALSE
        ldrh    r1, [r7]
        sub     r2, r1, r8
        b       %B20

        NESTED_END xGetGlyphMetrics

;++
;
; BOOL
; xGetGlyphMetricsPlus (
;    IN RFONTOBJ *pfobj,
;    IN COUNT c,
;    IN GLYPHPOS *pgp,
;    IN WCHAR *pwc,
;    OUT BOOL *pbAccel
;    )
;
; Routine Description:
;
;    This function fills the glyph position array as xGetGlyphMetrics does,
;    and also tries to make the glyph bits of each glyph valid, until that
;    fails once; the font cache may be flushed for the first glyph only.
;
; Arguments:
;
;    pfobj (r0), c (r1), pgp (r2), pwc (r3) - as xGetGlyphMetrics.
;
;    pbAccel ([sp]) - receives TRUE if the glyph bits of every glyph are
;        valid, else FALSE.
;
; Return Value:
;
;    TRUE if every character was translated, else FALSE.
;
;--

;
; The frame: the first wide character and the value of pbAccel, then the
; saved registers and the fifth argument.
;

#define GpPwc 0
#define GpAccel 4
#define GpPbAccel (8 + 8 * 4)

        NESTED_ENTRY xGetGlyphMetricsPlus
        PROLOG_PUSH {r4-r10, lr}
        PROLOG_STACK_ALLOC 8

        mov     r4, r0                          ; realized font object
        mov     r5, r1                          ; characters left
        mov     r6, r2                          ; next glyph position
        mov     r7, r3                          ; next wide character
        str     r3, [sp, #GpPwc]
        movs    r0, #TRUE
        str     r0, [sp, #GpAccel]              ; every glyph bits valid so far
        ldr     r0, [r4, #rfo_prfnt]
        ldr     r0, [r0, #rf_wcgp]
        ldr     r8, [r0, #gt_agpRun + gr_wcLow]
        ldr     r9, [r0, #gt_agpRun + gr_cGlyphs]
        ldr     r10, [r0, #gt_agpRun + gr_apgd]

10      ldrh    r1, [r7]                        ; next wide character
        sub     r2, r1, r8                      ; its index in the current run
        cmp     r2, r9
        bhs     %F50                            ; not in the current run
20      ldr     r0, [r10, r2, lsl #2]           ; its glyph data
        cmp     r0, #0
        beq     %F60                            ; no metrics yet
30      str     r0, [r6, #gp_pgdf]
        ldr     r1, [r0, #gd_hg]
        ldr     r2, [r0, #gd_gdf]               ; its glyph bits
        str     r1, [r6, #gp_hg]
        adds    r6, r6, #GLYPHPOS
        cmp     r2, #0
        beq     %F70                            ; none yet
80      adds    r7, r7, #2
        subs    r5, r5, #1
        bne     %B10                            ; more characters
        movs    r0, #TRUE
40      ldr     r1, [sp, #GpAccel]
        ldr     r2, [sp, #GpPbAccel]
        str     r1, [r2]
        EPILOG_STACK_FREE 8
        EPILOG_POP {r4-r10, pc}

;
; The character is not in the current run: the run that maps it is the
; current run.
;

50      mov     r0, r4                          ; (r1 the wide character)
        bl      xprunFindRunRFONTOBJ
        ldrh    r1, [r7]
        ldr     r8, [r0, #gr_wcLow]
        ldr     r9, [r0, #gr_cGlyphs]
        ldr     r10, [r0, #gr_apgd]
        sub     r2, r1, r8
        cmp     r2, r9
        blo     %B20                            ; it maps the character

;
; No run maps the character: its glyph is the default glyph, and the first
; run (of the glyph runs as they are now) is the current run again.
;

        mov     r0, r4
#ifdef FE_SB
        mov     r1, r7                          ; the wide character
        add     r2, sp, #GpAccel                ; the value of pbAccel
        bl      xwpgdGetLinkMetricsPlusRFONTOBJ
#else
        bl      xpgdDefault
#endif
        cmp     r0, #0
        beq     %B40                            ; none: FALSE
        ldr     r1, [r4, #rfo_prfnt]
        ldr     r1, [r1, #rf_wcgp]
        ldr     r8, [r1, #gt_agpRun + gr_wcLow]
        ldr     r9, [r1, #gt_agpRun + gr_cGlyphs]
        ldr     r10, [r1, #gt_agpRun + gr_apgd]
        b       %B30

;
; The glyph has no metrics yet: the font cache gets them.
;

60      add     r3, r10, r2, lsl #2             ; address of its glyph data pointer
        mov     r2, r1                          ; the wide character
        mov     r1, r3
        mov     r0, r4
        bl      xInsertMetricsPlusRFONTOBJ
        cmp     r0, #0
        beq     %B40                            ; failed: FALSE
        ldrh    r1, [r7]
        sub     r2, r1, r8
        b       %B20

;
; The glyph has no glyph bits yet: the font cache gets them, unless that failed
; for an earlier glyph.
;

70      ldr     r1, [sp, #GpAccel]
        cmp     r1, #0
        beq     %B80                            ; failed before
        ldr     r2, [sp, #GpPwc]
        cmp     r2, r7                          ; the first glyph: may flush the cache
        ite     hs
        movhs   r2, #TRUE
        movlo   r2, #FALSE
        mov     r1, r0                          ; its glyph data
        mov     r0, r4
        bl      xInsertGlyphbitsRFONTOBJ
        cmp     r0, #0
        bne     %B80                            ; inserted
        str     r0, [sp, #GpAccel]              ; failed: not every glyph bits valid
        b       %B80

        NESTED_END xGetGlyphMetricsPlus

        END
