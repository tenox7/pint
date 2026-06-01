# Build-time MSVC -> GCC cast-as-lvalue compatibility filter for the NT executive
# port. Run as `perl -p castlvalue.pl` over each staged executive TU (and mm/mi.h)
# by make-exec.sh.
#
# WHY: GCC dropped the "cast yields an lvalue" extension (removed in gcc 4.0) that
# MSVC keeps and that NT's *portable* MM/RTL/IO source still uses, e.g.
#   (PCONTROL_AREA)(File->...->ImageSectionObject) = ControlArea;   // cast on LHS
#   &((PHARDWARE_PTE)PteFlushList->FlushPte[0])                     // address-of-cast
#   *((USHORT *)(BitIo->pbBB))++                                    // post-inc of cast
# This is a toolchain-portability gap (a cousin of the SEH wall), not an arch issue,
# so - exactly like make-exec.sh's RISC arch-gate sed and Ctrl-Z/CR stripping - it is
# fixed by a staged source transform, leaving PRIVATE/ pristine ("compile as-is").
#
# HOW: for an lvalue L, `(T)L` is exactly `*(T *)&(L)` on a flat 32-bit ABI (same
# size, same bytes). Crucially a *false* match would take `&(rvalue)`, which fails
# to compile - so this filter can never silently miscompile; a bad rewrite is loud.
# Rules are kept narrow (specific type + operand shape) so they cannot disturb the
# valid deref-of-cast forms ((T)p)->f and ((T)p)[i], which are real lvalues already.
# Validated by a before/after per-file compile diff: +8 files, 0 regressions.

# address-of-cast:  &((PHARDWARE_PTE)X)  ->  (PHARDWARE_PTE *)&(X)
#   (not when the cast is dereferenced afterwards: &((T)p)[i] / ->f / .f)
s{&\(\(PHARDWARE_PTE\)([^()]+)\)(?!\s*(?:->|\[|\.))}{(PHARDWARE_PTE *)&($1)}g;

# cast on the LHS of (or read of) a SectionObjectPointer field. Rewrite only the
# cast token via look-ahead so the operand's / wrappers' parens are never disturbed
# (handles ((T)(X)), (T)((X)) and (T)(X) alike, assignment or rvalue read):
s{\(PCONTROL_AREA\)(?=\()}{*(PCONTROL_AREA *)&}g;

# cast on the LHS of a += / -=  (pointer-advance idioms):
s{\(PUCHAR\)([A-Za-z_]\w*)(\s*[-+]=)}{*(PUCHAR *)&($1)$2}g;
s{\(PUNICODE_STRING\)\(([^()]+)\)(\s*[-+]=)}{*(PUNICODE_STRING *)&($1)$2}g;

# post-increment of a cast pointer:  *((USHORT *)(p))++  ->  *((*(USHORT **)&(p))++)
s{\*\(\(USHORT\s*\*\)\(([^()]+)\)\)\+\+}{*((*(USHORT **)&($1))++)}g;

# the MI_SET_BIT / MI_CLEAR_BIT bitmap macros in mm/mi.h:  (ULONG)ARRAY[..] |=/&=
s{\(ULONG\)(ARRAY\[[^\]]*\])(\s*[|&]=)}{*(ULONG *)&($1)$2}g;
