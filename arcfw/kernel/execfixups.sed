# Build-time per-file source fixups for the executive compile tail. PRIVATE/ stays
# pristine (same spirit as the RISC arch-gate sed + castlvalue.pl); applied to each
# staged TU after castlvalue.pl. Content-based (robust to line moves). Each clears a
# specific source defect that blocks a whole file - and with it many link-closure
# symbols. Found by exec-tail-rootcause analysis.

# ps/create.c: Windows-style relative include path -> resolve via the farm -I search.
# (unblocks PspCreateProcess / NtCreateProcess / NtCreateThread / PsCreateSystemThread)
s#"\.\.\\mm\\mi\.h"#"mi.h"#

# io/report.c + io/assign.c: header typo - ntiologc.h does not exist; it is ntiolog.h.
s#ntiologc\.h#ntiolog.h#

# rtl/string.c: TO_UPPER(Ch) modifies its arg in place; the (UCHAR) cast at the call
# sites makes a non-lvalue (MSVC cast-as-lvalue). Drop the cast (the arg is a CHAR).
# (unblocks RtlInitString / RtlInitAnsiString / RtlInitUnicodeString / RtlCopyString /
#  RtlEqualString / RtlPrefixString / RtlPrefixUnicodeString)
s#TO_UPPER((UCHAR)\([A-Za-z0-9_]*\))#TO_UPPER(\1)#g

# config/hivesync.c: RtlCheckBit is a 2-arg macro; the source passes a spurious 3rd arg.
# (unblocks HvSyncHive / HvWriteHive / HvRefreshHive / HvMark{Cell,}Dirty / HvMarkClean /
#  HvpDoWriteHive / HvpGrowLog2)
s#RtlCheckBit(&Hive->DirtyVector, p / HSECTOR_SIZE, 1)#RtlCheckBit(\&Hive->DirtyVector, p / HSECTOR_SIZE)#

# io/iosubs.c: (PIRP)(thread)->TopLevelIrp = Irp - the -> binds before the cast, so the
# cast applies to the field value -> non-lvalue. Assign to the PVOID field, cast the RHS.
# (unblocks the 22 Io*/Iof* manager-core symbols)
s#(PIRP) (PsGetCurrentThread())->TopLevelIrp = Irp;#(PsGetCurrentThread())->TopLevelIrp = (PVOID)Irp;#
