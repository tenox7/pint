#!/bin/bash
# The container half of make-hives.sh: build REGINI, REGDMP and HIVEDMP as NT compiles,
# then the hives of each variant as MKHIVES.CMD and MKHIVE1.CMD make them: RCPP preprocesses
# the .INI with the hive options and REGINI -h loads it into a new hive file. REGINI
# of NT 4.0 writes the keys with the registry functions of ADVAPI32, which it takes
# here from their sources: the WINREG client and its local base functions (the
# server's, compiled LOCAL), over the registry services of ntreg.c.
# Usage: hives.sh VARIANT...
VARIANTS="$*"
set -eu
. /work/RASPI/build/msvc.sh
W=/work
T=$W/RASPI/tools/hive
H=$W/RASPI/obj/host
U=$W/RASPI/obj/um
HB=/tmp/hb
rm -rf $HB; mkdir -p $HB/obj
bash $B/mkfarm.sh /tmp/f > /dev/null
host() { bash $W/RASPI/tools/host.sh "$@"; }

# _NTSYSTEM_ (and _ADVAPI32_ for the registry functions): the calls bind to the
# definitions of the program, not to the build machine's DLLs.
DEFS="$NTDEFINES -D_NTSYSTEM_"
NTINC="-I$K/inc -I/tmp/f/ovr -I/tmp/f/config -I/tmp/f/se -I/tmp/f/rtl -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
UI=$(bash $B/incdirs.sh $W/private/ntos/config/utils "")
# the user mode components: the includes of their SOURCES and the SDK's
um() { echo "-I$K/inc -I/tmp/f/ovr $(bash $B/incdirs.sh $1 "$(bash $B/srcmacro.sh $1/sources INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"; }
RI=$(um $W/private/sdktools/regini)
WR=$W/private/windows/screg/winreg
CI=$(um $WR/client)
LI=$(um $WR/local)
# cc OBJECT SOURCE OPTION...: config\utils predates the CM of 3.5 (utils.pl)
cc() {
    local o=$1 s=$2; shift 2
    ntsrc $s $HB/obj/$o.c
    case $s in */config/utils/*) perl $T/utils.pl < $HB/obj/$o.c > $HB/obj/$o.u && mv $HB/obj/$o.u $HB/obj/$o.c ;; esac
    ntcc $HB/obj/$o.obj $HB/obj/$o.c $DEFS "$@"
}

echo ">> uconfig (config for user mode)"
# but the system services and the registry lock of the kernel (cmsubs3.c), which
# CMWRAPER has for user mode
for s in $(bash $B/srclist.sh config); do
    b=$(basename $s .c)
    case $b in ntapi|cmsubs3) continue;; esac
    cc cm_$b $s $NTINC
done
echo ">> tools"
for s in edithive cmwraper hivedmp regutil; do cc ut_$s $W/private/ntos/config/utils/$s.c $UI $NTINC; done
for s in regini regdmp regtool regutil regacl; do cc rg_$s $W/private/sdktools/regini/$s.c -DUNICODE -D_UNICODE -D_ADVAPI32_ $RI; done
echo ">> winreg (the registry functions of advapi32)"
for s in predefh regdkey regdval regekey regeval regkey regqkey regqval regsckey regsval; do
    cc wc_$s $WR/client/$s.c -DRPC_NO_WINDOWS_H -D_ADVAPI32_ -DREMOTE_NOTIFICATION_DISABLED $CI
done
for s in predefh regdkey regdval regekey regeval regkey reglukey regqkey regqval regsckey regsrkey regsval; do
    cc wl_$s $WR/server/$s.c -DRPC_NO_WINDOWS_H -DLOCAL -D_ADVAPI32_ $LI
done
cc wr_support $WR/lib/support.c -DRPC_NO_WINDOWS_H -D_ADVAPI32_ $(um $WR/lib)
echo ">> rtl"
for s in string nls nlsxlat sertl acledit cnvint time assert bitmap; do cc rtl_$s $W/private/ntos/rtl/$s.c $NTINC; done
cc rtl_largeint $K/rtl/largeint.c $NTINC
ntas $HB/obj/rtl_xxmvmem.obj $K/rtl/xxmvmem.asm $DEFS -I$W/RASPI/obj/gen $NTINC
cc dll_seurtl $W/private/ntos/dll/seurtl.c $(bash $B/incdirs.sh $W/private/ntos/dll "") $NTINC
cc ke_kernldat $W/private/ntos/ke/kernldat.c -I/tmp/f/ke $NTINC
echo ">> host"
cc hostnt $T/hostnt.c -I$T $NTINC
cc ntreg $T/ntreg.c -I$T $UI $NTINC

echo ">> link"
ntlib $HB/uconfig.lib $HB/obj/cm_*.obj
ntlib $HB/rtl.lib $HB/obj/rtl_*.obj $HB/obj/dll_*.obj $HB/obj/ke_*.obj
ntlib $HB/winreg.lib $HB/obj/wc_*.obj $HB/obj/wl_*.obj $HB/obj/wr_*.obj
# REGTOOL.LIB, the library of the registry tools (sdktools\regini SOURCES)
ntlib $HB/regtool.lib $HB/obj/rg_regtool.obj $HB/obj/rg_regacl.obj $HB/obj/rg_regutil.obj
BASE=$(tr -d "\r" < $W/public/sdk/lib/coffbase.txt | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
# The tools' own definitions come first, as the objects of the program. Of the objects
# of uconfig and the RTL they take, functions they do not call reach kernel services
# (CmLoadKey, CmQueryMultipleValueKey, CmpCreateKeyControlBlock, the security methods of
# the key object in CMSE.C, RtlCopySecurityDescriptor), as does the quota warning of
# CmpClaimGlobalQuota, which a hive under the quota never gives: those alone are left
# unresolved.
KERNEL="SeTokenType SeCreateClientSecurity ObfDereferenceObject ExAcquireFastMutex ExReleaseFastMutex RtlpQuerySecurityDescriptor
        SeLockSubjectContext SeUnlockSubjectContext SeAccessCheck SeQuerySecurityDescriptorInfo ObAssignObjectSecurityDescriptor
        ExRaiseAccessViolation ExRaiseDatatypeMisalignment ExQueueWorkItem ExRaiseHardError ExReadyForErrors"
# The registry functions reach a remote registry through its RPC stubs (BaseReg*) and
# the performance data of the predefined performance keys (PerfReg*, the data of
# LocalOpenPerformanceData): the keys of a hive file take neither path.
REMOTE="BaseRegCloseKey BaseRegCreateKey BaseRegDeleteKey BaseRegDeleteValue BaseRegEnumKey BaseRegEnumValue
        BaseRegFlushKey BaseRegGetKeySecurity BaseRegGetVersion BaseRegOpenKey BaseRegQueryInfoKey BaseRegQueryValue
        BaseRegSetKeySecurity BaseRegSetValue PerfRegCloseKey PerfRegEnumKey PerfRegEnumValue PerfRegQueryInfoKey
        PerfRegQueryValue PerfRegSetValue OpenExtensibleObjects GetBrowserStatistic IdentifyDisks BasicInfo NumberOfOpens
        ComputerNameLength pComputerName ProcessName ProcessBufSize pProcessBuffer ProcessorBufSize pProcessorBuffer
        pSysPageFileInfo dwSysPageFileInfoSize hDataSemaphore hEvent hMutex hRdr hSection hSemaphore hSrv"
lnk() {
    local out=$1 s bad=; shift
    link -nologo -machine:arm -release -brepro -force -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$HB/$out.EXE "$@" $HB/obj/ut_edithive.obj $HB/obj/ut_cmwraper.obj $HB/obj/hostnt.obj $HB/uconfig.lib $HB/rtl.lib \
         $U/libc.lib $U/ntdll.lib $U/kernel32.lib $U/advapi32.lib > $HB/$out.err 2>&1
    for s in $(tr -d "\r" < $HB/$out.err | sed -n "s/.*unresolved external symbol \([A-Za-z_0-9]*\).*/\1/p" | sort -u); do
        case " $(echo $KERNEL $REMOTE) " in *" $s "*) ;; *) bad="$bad $s" ;; esac
    done
    [ -z "$bad" ] || { echo "   LINK FAIL $out:$bad"; tr -d "\r" < $HB/$out.err | grep "unresolved external" | sed "s/^/      /"; return 1; }
    if tr -d "\r" < $HB/$out.err | grep "fatal error" | grep -qv LNK1120; then
        echo "   LINK FAIL $out:"; tr -d "\r" < $HB/$out.err | grep "fatal error"; return 1
    fi
    # the steps after take obj/host as an input: replace the program only when it changed
    cmp -s $HB/$out.EXE $H/$out.EXE || cp $HB/$out.EXE $H/$out.EXE
    echo "   obj/host/$out.EXE"
}
lnk HIVEDMP $HB/obj/ut_hivedmp.obj $HB/obj/ut_regutil.obj
lnk REGINI $HB/obj/rg_regini.obj $HB/obj/ntreg.obj $HB/regtool.lib $HB/winreg.lib
lnk REGDMP $HB/obj/rg_regdmp.obj $HB/obj/ntreg.obj $HB/regtool.lib $HB/winreg.lib

export NTNLS='Z:\work\private\windows\winnls\data'
G=$W/RASPI/obj/gen/hives
dump() { host hivedmp -f $1 | tr -d "\r"; }
# RETAIL: the hives of mkhives RETAIL; ARM: those of the Pi, from the .INI files of the
# port where it has them (ported/oak, made from their edit scripts)
for V in $VARIANTS; do
    case $V in
    RETAIL) OPT="-D_GENERAL_PURPOSE_ -D_RETAIL_SETUP_";;
    ARM)    OPT="-D_GENERAL_PURPOSE_ -D_RETAIL_SETUP_ -D_RASPI_";;
    VALIDATE) continue;;
    *) echo "unknown variant $V"; exit 1;;
    esac
    echo ">> $V hives ($OPT)"
    D=$G/$V; rm -rf $D; mkdir -p $D
    if [ $V = ARM ]; then rm -f $G/SYSTEM $G/SOFTWARE $G/DEFAULT $G/USERDIFF; fi
    for h in "SYSTEM System system" "SOFTWARE Software software" "DEFAULT .Default default" "USERDIFF Userdiff userdiff"; do
        set -- $h
        I=$W/public/oak/bin/$3.ini
        if [ $V = ARM ] && [ -f $W/RASPI/ported/oak/$3.ini ]; then I=$W/RASPI/ported/oak/$3.ini; fi
        (cd $D && host rcpp -R -P -I $W/public/oak/bin -f $I $OPT -g $1.PP > /dev/null) || echo "   rcpp $1 rc=$?"
        (cd $D && rc=0 && { host regini -h $1 $2 $1.PP > $1.OUT 2>&1 || rc=$?; } && tr -d "\r" < $1.OUT > $1.REG && rm $1.OUT &&
            [ $rc = 0 ]) || echo "   regini $1 failed"
        (cd $D && dump $1 > $1.DMP 2>&1) || echo "   hivedmp $1 rc=$?"
        echo "   $1: $(stat -c %s $D/$1) bytes, $(grep -c "" $D/$1.DMP) dump lines, $(grep -c "^REGINI:\|) : error:" $D/$1.REG) REGINI errors"
    done
    if [ $V = ARM ]; then cp $D/SYSTEM $D/SOFTWARE $D/DEFAULT $D/USERDIFF $G/; fi
done
mkdir -p $G/MS
for h in SYSTEM SOFTWARE DEFAULT; do
    cp $W/public/oak/bin/$(echo $h | tr A-Z a-z) $G/MS/$h; chmod u+w $G/MS/$h
    (cd $G/MS && dump $h > $h.DMP 2>&1) || true
done
case " $VARIANTS " in *" VALIDATE "*)
    # REGDMP writes a hive as REGINI reads it (of 4.0: HIVEDMP prints the resource lists
    # decoded and the binary data without the \ that continues a value)
    echo ">> VALIDATE: Microsoft's SYSTEM round trip (REGDMP -> REGINI -> REGDMP)"
    D=$G/VALIDATE; rm -rf $D; mkdir -p $D; cd $D
    cp $G/MS/SYSTEM MS; host regdmp -h MS System | tr -d "\r" > MS.INI
    # REGINI takes REG_RESOURCE_REQUIREMENTS_LIST for REG_RESOURCE_REQUIREMENTS (the name
    # of the .INI files) followed by no data: it reads the name of the .INI files
    sed "s/= REG_RESOURCE_REQUIREMENTS_LIST /= REG_RESOURCE_REQUIREMENTS /" MS.INI > RT.IN
    host regini -h RT System RT.IN | tr -d "\r" > RT.REG 2>&1 || echo "   regini rc=$?"
    host regdmp -h RT System | tr -d "\r" > RT.INI
    echo "   round trip: $(grep -c "" MS.INI) lines, $(diff MS.INI RT.INI | grep -c "^[<>]") differ"
    # the hives of RETAIL against Microsoft's (public\oak\bin)
    for h in "SYSTEM System" "SOFTWARE Software" "DEFAULT .Default"; do
        set -- $h
        [ -f $G/RETAIL/$1 ] || continue
        cp $G/MS/$1 MS.$1; cp $G/RETAIL/$1 RETAIL.$1
        host regdmp -h MS.$1 $2 | tr -d "\r" > MS.$1.INI; host regdmp -h RETAIL.$1 $2 | tr -d "\r" > RETAIL.$1.INI
        echo "   RETAIL $1 against Microsoft's: $(grep -c "" MS.$1.INI) lines, $(diff MS.$1.INI RETAIL.$1.INI | grep -c "^[<>]") differ"
    done
    cd /;;
esac
