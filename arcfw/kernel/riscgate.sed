# NT 3.5 gates RISC-only code with "#if defined(_MIPS_) (or MIPS) || defined(_ALPHA_) ||
# defined(_PPC_)". ARM is a RISC port of the same lineage (KSEG0, PCR, software
# TB management), so add _ARM_ to those tests - ahead of any trailing // comment
# - and to the negated "none of the RISC machines" form as && !defined(_ARM_), also
# written with spaces inside the parentheses. A negated test that also excludes x86 is
# the portable code of any other machine, which ARM takes as it is. A test of MIPS and
# PPC alone is that of the 32-bit RISC machines (Alpha's registers are 64-bit), which
# ARM is too.
/^[[:space:]]*#[[:space:]]*(if|elif)/{/!defined[[:space:]]*\([[:space:]]*_MIPS_[[:space:]]*\)/!{/MIPS/{/_ALPHA_/{/_PPC_/s#[[:space:]]*(//.*)?$# || defined(_ARM_) \1#}}}}
/^[[:space:]]*#[[:space:]]*(if|elif)/{/!defined[[:space:]]*\([[:space:]]*_MIPS_[[:space:]]*\)/{/!defined[[:space:]]*\([[:space:]]*_ALPHA_[[:space:]]*\)/{/!defined[[:space:]]*\([[:space:]]*_PPC_[[:space:]]*\)/{/_X86_|i386/!s#[[:space:]]*(//.*)?$# \&\& !defined(_ARM_) \1#}}}}
/^[[:space:]]*#[[:space:]]*(if|elif)/{/!defined[[:space:]]*\([[:space:]]*_MIPS_[[:space:]]*\)/!{/MIPS/{/_PPC_/{/_ALPHA_|_X86_|i386/!s#[[:space:]]*(//.*)?$# || defined(_ARM_) \1#}}}}
/^[[:space:]]*#[[:space:]]*(if|elif)/{/!defined[[:space:]]*\([[:space:]]*_MIPS_[[:space:]]*\)/{/!defined[[:space:]]*\([[:space:]]*_PPC_[[:space:]]*\)/{/_ALPHA_|_X86_|i386/!s#[[:space:]]*(//.*)?$# \&\& !defined(_ARM_) \1#}}}
