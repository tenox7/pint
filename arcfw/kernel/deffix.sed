# The module definition statements of the 16-bit and OS/2 linkers that the NT 3.5
# librarian ignored and that of today rejects: the initialization of a LIBRARY,
# PROTMODE, and the attributes of the CODE and DATA segments.
s/^\(LIBRARY[[:space:]]\{1,\}[^[:space:]]*\)[[:space:]]\{1,\}INIT\(INSTANCE\|GLOBAL\)/\1/I
/^[[:space:]]*PROTMODE[[:space:]]*$/Id
/^[[:space:]]*\(DATA\|CODE\)[[:space:]]/Id
