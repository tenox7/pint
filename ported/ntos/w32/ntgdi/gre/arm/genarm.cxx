/*++

Module Name:

    genarm.cxx

Abstract:

    This module generates the structure offsets of the realized fonts and
    glyphs that the ARM assembler of the graphics engine uses (cachea.asm):
    the GRE\MIPS\GENMIPS.CXX analog. The build host does not run ARM programs:
    the recipe of gdiarm.h compiles this module to an assembly listing, and
    each constant becomes a #define of gdiarm.h (build/nttarget.sh).

--*/

#include "precomp.hxx"

#define DEF(Name, Value) extern "C" const ULONG GenArm_##Name = (ULONG)(Value)

#define OFF(Name, Type, Field) DEF(Name, FIELD_OFFSET(Type, Field))

DEF(TRUE, TRUE);
DEF(FALSE, FALSE);

//
// Wide character to glyph mapping structures (rfntobj.hxx).
//

OFF(gr_wcLow, GPRUN, wcLow);
OFF(gr_cGlyphs, GPRUN, cGlyphs);
OFF(gr_apgd, GPRUN, apgd);
OFF(gt_agpRun, WCGP, agpRun);

//
// Realized font structures (rfntobj.hxx).
//

OFF(rf_wcgp, RFONT, wcgp);
OFF(rfo_prfnt, RFONTOBJ, prfnt);

//
// Glyph data and glyph position structures (winddi.h).
//

OFF(gd_hg, GLYPHDATA, hg);
OFF(gd_gdf, GLYPHDATA, gdf);
OFF(gp_hg, GLYPHPOS, hg);
OFF(gp_pgdf, GLYPHPOS, pgdf);
DEF(GLYPHPOS, sizeof(GLYPHPOS));
