#!/bin/bash
# The container half of make-hives.sh: build REGINI and HIVEDMP as NT compiles, then
# the hives of each variant as MKHIVES.CMD and MKHIVE1.CMD make them: RCPP preprocesses
# the .INI with the hive options and REGINI -h loads it into a new hive file.
# Usage: hives.sh VARIANT...
VARIANTS="$*"
set -eu
. /work/RASPI/arcfw/kernel/msvc.sh
W=/work
T=$W/RASPI/tools/hive
H=$W/RASPI/obj/host
U=$W/RASPI/obj/um
B=/tmp/hb
rm -rf $B; mkdir -p $B/obj
bash $K/mkfarm.sh /tmp/f > /dev/null
host() { bash $W/RASPI/tools/host.sh "$@"; }

# The ARM headers, with the TEB of the build machine: Windows on ARM keeps it in
# TPIDRURW, NT on the Pi in TPIDRURO.
mkdir -p $B/inc && cp $K/inc/*.h $B/inc/
sed "s/_MoveFromCoprocessor(15, 0, 13, 0, 3)/_MoveFromCoprocessor(15, 0, 13, 0, 2)/" $K/inc/ntarm.h > $B/inc/ntarm.h
DEFS="$NTDEFINES -D_NTSYSTEM_"
NTINC="-I$B/inc -I/tmp/f/ovr -I/tmp/f/config -I/tmp/f/se -I/tmp/f/rtl -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
UI=$(bash $K/incdirs.sh $W/PRIVATE/NTOS/CONFIG/UTILS "")
RI=$(bash $K/incdirs.sh $W/PRIVATE/SDKTOOLS/REGINI "")
# cc OBJECT SOURCE OPTION...: CONFIG\UTILS predates the CM of 3.5 (utils.pl)
cc() {
    local o=$1 s=$2; shift 2
    ntsrc $s $B/obj/$o.c
    case $s in */CONFIG/UTILS/*) perl $T/utils.pl < $B/obj/$o.c > $B/obj/$o.u && mv $B/obj/$o.u $B/obj/$o.c ;; esac
    ntcc $B/obj/$o.obj $B/obj/$o.c $DEFS "$@"
}

echo ">> uconfig (CONFIG for user mode)"
for s in $(bash $K/srclist.sh CONFIG); do
    b=$(basename $s .C)
    case $b in NTAPI) continue;; esac
    cc cm_$b $s $NTINC
done
echo ">> tools"
for s in EDITHIVE CMWRAPER HIVEDMP REGUTIL; do cc ut_$s $W/PRIVATE/NTOS/CONFIG/UTILS/$s.C $UI $NTINC; done
for s in REGINI REGUTIL REGACL; do cc rg_$s $W/PRIVATE/SDKTOOLS/REGINI/$s.C $RI $NTINC; done
echo ">> rtl"
for s in STRING NLS NLSXLAT SERTL ACLEDIT CNVINT TIME ASSERT BITMAP; do cc rtl_$s $W/PRIVATE/NTOS/RTL/$s.C $NTINC; done
cc rtl_LARGEINT $K/rtl/largeint.c $NTINC
cc dll_SEURTL $W/PRIVATE/NTOS/DLL/SEURTL.C $NTINC
cc ke_KERNLDAT $W/PRIVATE/NTOS/KE/KERNLDAT.C -I/tmp/f/ke $NTINC
echo ">> host"
cc hostnt $T/hostnt.c -I$T $NTINC
cc ntreg $T/ntreg.c -I$T $UI $NTINC

echo ">> link"
ntlib $B/uconfig.lib $B/obj/cm_*.obj
ntlib $B/rtl.lib $B/obj/rtl_*.obj $B/obj/dll_*.obj $B/obj/ke_*.obj
BASE=$(tr -d "\r" < $W/PUBLIC/SDK/LIB/COFFBASE.TXT | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
# The tools' own definitions come first, as the objects of the program. Of the objects
# of uconfig and the RTL they take, functions they do not call reach kernel services
# (CmLoadKey, CmpCreateKeyControlBlock, the security methods of the key object in CMSE.C,
# RtlCopySecurityDescriptor): those alone are left unresolved.
KERNEL="SeTokenType SeCreateClientSecurity ObfDereferenceObject ExAcquireFastMutex ExReleaseFastMutex RtlpQuerySecurityDescriptor
        SeLockSubjectContext SeUnlockSubjectContext SeAccessCheck SeQuerySecurityDescriptorInfo ObAssignObjectSecurityDescriptor"
lnk() {
    local out=$1 s; shift
    link -nologo -machine:arm -release -brepro -force -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$B/$out.EXE "$@" $B/obj/ut_EDITHIVE.obj $B/obj/ut_CMWRAPER.obj $B/obj/hostnt.obj $B/uconfig.lib $B/rtl.lib \
         $U/libc.lib $U/ntdll.lib $U/kernel32.lib $U/advapi32.lib > $B/$out.err 2>&1
    for s in $(tr -d "\r" < $B/$out.err | sed -n "s/.*unresolved external symbol \([A-Za-z_0-9]*\).*/\1/p" | sort -u); do
        case " $(echo $KERNEL) " in *" $s "*) ;; *) echo "   LINK FAIL $out: $s"; return 1 ;; esac
    done
    if tr -d "\r" < $B/$out.err | grep "fatal error" | grep -qv LNK1120; then
        echo "   LINK FAIL $out:"; tr -d "\r" < $B/$out.err | grep "fatal error"; return 1
    fi
    # the steps after take obj/host as an input: replace the program only when it changed
    cmp -s $B/$out.EXE $H/$out.EXE || cp $B/$out.EXE $H/$out.EXE
    echo "   obj/host/$out.EXE"
}
lnk HIVEDMP $B/obj/ut_HIVEDMP.obj $B/obj/ut_REGUTIL.obj
lnk REGINI $B/obj/rg_REGINI.obj $B/obj/rg_REGUTIL.obj $B/obj/rg_REGACL.obj $B/obj/ntreg.obj

export NTNLS='Z:\work\PRIVATE\WINDOWS\WINNLS\DATA'
G=$W/RASPI/obj/gen/hives
dump() { host hivedmp -f $1 | tr -d "\r"; }
for V in $VARIANTS; do
    case $V in
    GENERAL) OPT="-D_GENERAL_PURPOSE_";;
    RETAIL)  OPT="-D_GENERAL_PURPOSE_ -D_RETAIL_SETUP_";;
    NETIDW)  OPT="-D_GENERAL_PURPOSE_ -D_NETIDW_SETUP_";;
    ARM)     OPT="-D_GENERAL_PURPOSE_ -D_RASPI_";;
    ARMNETIDW) OPT="-D_GENERAL_PURPOSE_ -D_RASPI_ -D_NETIDW_SETUP_";;
    VALIDATE) continue;;
    *) echo "unknown variant $V"; exit 1;;
    esac
    echo ">> $V hives ($OPT)"
    D=$G/$V; rm -rf $D; mkdir -p $D
    if [ $V = ARM ]; then rm -f $G/SYSTEM $G/SOFTWARE $G/DEFAULT; fi
    for h in "SYSTEM System system" "SOFTWARE Software software" "DEFAULT .Default default"; do
        set -- $h
        (cd $D && host rcpp -R -P -I $W/RASPI/arcfw/ported/oak -f $W/RASPI/arcfw/ported/oak/$3.ini $OPT -g $1.PP > /dev/null) || echo "   rcpp $1 rc=$?"
        (cd $D && host regini -h $1 $2 $1.PP | tr -d "\r" > $1.REG 2>&1) || echo "   regini $1 rc=$?"
        (cd $D && dump $1 > $1.DMP 2>&1) || echo "   hivedmp $1 rc=$?"
        echo "   $1: $(stat -c %s $D/$1) bytes, $(grep -c "" $D/$1.DMP) dump lines, $(grep -c "^REGINI" $D/$1.REG) REGINI errors"
    done
    if [ $V = ARM ]; then cp $D/SYSTEM $D/SOFTWARE $D/DEFAULT $G/; fi
done
mkdir -p $G/MS
for h in SYSTEM SOFTWARE DEFAULT; do
    cp $W/PUBLIC/OAK/BIN/$h $G/MS/$h; chmod u+w $G/MS/$h
    (cd $G/MS && dump $h > $h.DMP 2>&1) || true
done
case " $VARIANTS " in *" VALIDATE "*)
    echo ">> VALIDATE: Microsoft's SYSTEM round trip (HIVEDMP -> REGINI -> HIVEDMP)"
    D=$G/VALIDATE; rm -rf $D; mkdir -p $D; cd $D
    cp $G/MS/SYSTEM MS; dump MS > MS.DMP
    awk '/Unknown registry type/{skip=1; next} skip && (/^ *$/ || /^ *(Data:)? *([0-9a-f][0-9a-f] )+ *$/){next} {skip=0; print}' MS.DMP | sed "1s/.*/SYSTEM/" > TYPED.INI
    host regini -h TYPED System TYPED.INI > TYPED.REG 2>&1; dump TYPED > TYPED.DMP
    sed "1d" TYPED.INI > a; sed "1d" TYPED.DMP > b; echo "   typed round trip: $(grep -c "" a) lines, $(diff a b | grep -c "^[<>]") differ"
    rm -f a b
    cd /;;
esac
