# The module definition statements of the 16-bit and OS/2 linkers that the NT 3.5
# librarian ignored and that of today rejects: the initialization of a LIBRARY,
# PROTMODE, the attributes of the CODE and DATA segments and of the SEGMENTS, and
# resident names of exports.
s/^\(LIBRARY[[:space:]]\{1,\}[^[:space:]]*\)[[:space:]]\{1,\}INIT\(INSTANCE\|GLOBAL\)/\1/I
/^[[:space:]]*PROTMODE[[:space:]]*$/Id
/^[[:space:]]*\(DATA\|CODE\)[[:space:]]/Id
s/[[:space:]]\{1,\}RESIDENTNAME\>//I
/^[[:space:]]*SEGMENTS[[:space:]]*$/Id
/^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*\([[:space:]]\{1,\}\(PRELOAD\|LOADONCALL\|MOVEABLE\|MOVABLE\|FIXED\|DISCARDABLE\|NONDISCARDABLE\|PURE\|IMPURE\)\)\{1,\}[[:space:]]*$/Id
