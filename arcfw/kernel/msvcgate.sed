# The NT 3.5 headers test the compiler, as well as the machine, for the RISC
# compilers (_M_MRX000 MIPS, _M_ALPHA): their SEH declarations, intrinsics, packing
# and run-time library forms. Microsoft C for ARM (_M_ARM) is one of them: add it
# next to _M_MRX000 - except where the test is that of the MIPS compiler alone
# (its intrinsic 64-bit arithmetic, the machine it implies).
/^[[:space:]]*#[[:space:]]*(if|elif)/{/!defined\(_MIPS_\)/!{/^[[:space:]]*#[[:space:]]*elif[[:space:]]+defined\(_M_MRX000\)[[:space:]]*$/!s/defined[[:space:]]*\(_M_MRX000\)/defined(_M_MRX000) || defined(_M_ARM)/}}
# Where the PowerPC compiler takes the plain C forms (the 64-bit arithmetic of
# ntdef.h and winnt.h), so does ARM.
s/^#elif defined\(_M_PPC\)$/#elif defined(_M_PPC) || defined(_M_ARM)/
