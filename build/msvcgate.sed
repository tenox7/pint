# NT's headers test the compiler, as well as the machine, for the RISC
# compilers (_M_MRX000 MIPS, _M_ALPHA): their SEH declarations, intrinsics, packing
# and run-time library forms. Microsoft C for ARM (_M_ARM) is one of them: add it
# next to _M_MRX000, the two in parentheses so that a negated test (!defined(_M_MRX000):
# none of the RISC compilers) or one joined by && keeps its sense - except where the
# test is that of the MIPS compiler alone (its intrinsic 64-bit arithmetic, the
# machine it implies). The test of the value alone (math.h: #if _M_MRX000, the run-time
# library forms that stdlib.h gives under defined(_M_MRX000)) is the same test.
/^[[:space:]]*#[[:space:]]*(if|elif)/{/!defined\(_MIPS_\)/!{/^[[:space:]]*#[[:space:]]*elif[[:space:]]+defined\(_M_MRX000\)[[:space:]]*$/!s/defined[[:space:]]*\(_M_MRX000\)/(defined(_M_MRX000) || defined(_M_ARM))/}}
s/^([[:space:]]*#[[:space:]]*(if|elif)[[:space:]]+)_M_MRX000[[:space:]]*$/\1(_M_MRX000 || defined(_M_ARM))/
# Where the PowerPC compiler takes the plain C forms of the 64-bit arithmetic (ntdef.h,
# winnt.h: the branch after that of Alpha), so does ARM; its other branches of the
# PowerPC compiler (jump buffer, argument lists) are not those of ARM.
/Alpha has native 64-bit operations/,/^#elif defined\(_M_PPC\)$/s/^#elif defined\(_M_PPC\)$/#elif defined(_M_PPC) || defined(_M_ARM)/
