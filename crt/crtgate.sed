# The C run-time gates the code of the RISC machines with "defined(MIPS) ||
# defined(_ALPHA_)" (it predates the PowerPC): ARM, a RISC machine with IEEE
# double floating point hardware, takes that code too (the RtlMoveMemory of
# memcpy, the forced floating point initialization, the double conversions).
s/^#if defined(MIPS) || defined(_ALPHA_)$/#if defined(MIPS) || defined(_ALPHA_) || defined(_ARM_)/
s/^#if ( defined(_MIPS_) || defined(_ALPHA_) )$/#if ( defined(_MIPS_) || defined(_ALPHA_) || defined(_ARM_) )/
