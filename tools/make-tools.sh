#!/usr/bin/env bash
# Build NT's build tools into obj/host; they run on the 32-bit ARM Wine of the build
# image (tools/host.sh). The tools that make NT's headers (HEXTRACT, HSPLIT, WCSHDR,
# WS2HDR, ZWAPI, LISTMUNG, MC), KBDTOOL, pe2img, midlcpp and msvcexe (CL.EXE and LINK.EXE, which
# GUIDLIB runs) come first, with the C run-time and
# headers of the SDK of Microsoft C, as NT took them from the SDK of the machine it
# was built on. Then the header pass (build/mkgen.sh), ksarm.h, NT's C run-time (each
# made when missing) and the import libraries of kernel32, user32 and advapi32; then GENSRV, GENERR (and
# rtl\error.h), RC, RCPP, MIDL with its parser tools, SOLIDPP, UIXPORT, MAPMSG and GUIDLIB,
# NT console programs of NT's headers and static C run-time (RC is its front end and RCDLL
# in one program), and the import library of ntdll the registry tools take. MIBCC links
# snmp.lib, the import library of snmpapi.dll, which the build makes: it is made alone, after it.
#
# Usage:  tools/make-tools.sh            # all the tools but MIBCC
#         tools/make-tools.sh headers    # the tools of the headers and the header pass
#         tools/make-tools.sh mibcc      # those and MIBCC alone
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
B=$NT40/RASPI/build

docker run --rm -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -eu
H=/work/RASPI/obj/host
T=/tmp/tools
S=/work/private/sdktools
mkdir -p $H
export WINEDEBUG=-all
wineserver -p
# the versions of NT sources the port changes, made from their edit scripts (tools/ntdiff.pl)
perl /work/RASPI/tools/ntdiff.pl apply /work /work/RASPI
# tool NAME DIR [OPTION...]: the C sources of the SOURCES of DIR (else NAME.C) and its
# resource script, ^Z and CR stripped, and the versions of the port (portdir.sh); a tool
# newer than these is not made again (a tool removed from obj/host is made again)
tool() {
    local n=$1 d=$2 s f r=; shift 2
    f=$H/$(echo $n | tr a-z A-Z).EXE
    [ -f $f ] && [ -z "$(find $d $(bash portdir.sh $d) -type f -newer $f -print -quit 2>/dev/null)" ] && return 0
    mkdir -p $T/$n
    for f in $d/*.[CcHh] $d/*.[Rr][Cc] $(bash portdir.sh $d)/*.[CcHh]; do [ ! -f $f ] || tr -d "\032\r" < $f > $T/$n/$(basename $f | tr A-Z a-z); done
    s=$(bash srcmacro.sh $d/sources SOURCES 2>/dev/null | tr A-Z a-z | tr " " "\n" | grep "\.c$") || s=$n.c
    if [ -f $T/$n/$n.rc ]; then
        (cd $T/$n && rc -nologo -I/work/public/sdk/inc -fo $n.res $n.rc > rc.err 2>&1) || { tr -d "\r" < $T/$n/rc.err; exit 1; }
        r=$n.res
    fi
    (cd $T/$n && cl -nologo -O2 -GF- -MT -w -I. -D_ARM_ -D_CRTAPI1=__cdecl -D_CRT_NON_CONFORMING_SWPRINTFS "$@" $s $r \
        -Fe$H/$(echo $n | tr a-z A-Z).EXE user32.lib > cl.err 2>&1) || { tr -d "\r" < $T/$n/cl.err | grep -m5 -E "error|fatal"; exit 1; }
    echo "   obj/host/$(echo $n | tr a-z A-Z).EXE"
}
tool hextract $S/hextract
tool hsplit $S/hsplit
tool wcshdr $S/wcshdr -D_OS2_20_=0 -Dnear= -Dfar= -Dpascal=
tool ws2hdr $S/ws2hdr
tool zwapi $S/zwapi
tool listmung $S/listmung
tool mc $S/vctools/mc
# (the keyboard layouts, ntuser\kbd: the sources of each from its kbd*.txt)
tool kbdtool $S/kbdtool
# (the alias objects of oldnames.lib of the C run-time, crt/make-crt.sh OLDNAMES)
tool aliasobj $S/vctools/crt/libw32/tools/source
mkdir -p /tmp/src && cp -p /work/RASPI/tools/pe2img.c /work/RASPI/tools/midl/midlcpp.c /work/RASPI/tools/msvcexe.c /tmp/src/
tool pe2img /tmp/src
tool midlcpp /tmp/src
tool msvcexe /tmp/src
for t in CL LINK; do cmp -s $H/MSVCEXE.EXE $H/$t.EXE || cp $H/MSVCEXE.EXE $H/$t.EXE; done
bash mkfarm.sh /tmp/f > /dev/null
'
[ "${1:-}" != headers ] || exit 0

if [ "${1:-}" != mibcc ]; then
"$B/make-ksarm.sh"
for c in ST:libc MT:libcmt; do
    [ -f ../obj/um/${c#*:}.lib ] || (cd ../crt && ./make-crt32.sh ${c%:*})
done
# (the export list of user32 is made with the lists of ntuser\inc, a component of pass 0)
[ -f ../obj/gen/tree/ntos/w32/ntuser/inc/daytona/cb.lst ] || "$B/make-lib.sh" w32/ntuser/inc/daytona
for c in windows/base/client/daytona:kernel32 ntos/w32/ntuser/client/daytona:user32 windows/base/advapi/daytona:advapi32; do
    [ -f ../obj/um/${c#*:}.lib ] || IMPLIB=1 ROOT=/work/private "$B/make-dll.sh" ${c%:*}
done
fi

docker run --rm -e CJ="${CJ:-4}" -e ONLY="${1:-}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -eu
. ./msvc.sh
H=/work/RASPI/obj/host
U=/work/RASPI/obj/um
T=/tmp/tools
S=/work/private/sdktools
M=/work/private/rpc
SUBSYS="ke rtl" bash mkfarm.sh /tmp/f
BASE=$(tr -d "\r" < /work/public/sdk/lib/coffbase.txt | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
host() { bash /work/RASPI/tools/host.sh "$@"; }
macro() { bash srcmacro.sh $1/sources $2 | sed "s/\$([A-Za-z_0-9]*)//g"; }
# srcs DIR [EXT]: the sources of the SOURCES of DIR (C, or the extensions EXT)
srcs() { macro $1 SOURCES | tr " " "\n" | grep -iE "\.(${2:-c})$" | while read -r f; do ls -d $1/* | grep -ix "$1/$f"; done; }
# ntc OUT DIR [OPTION...] -- SOURCE...: compile as NT compiles a program of component DIR,
# the sources by the pool of msvc.sh (par, pwait); a failed one stops the build when all are done
ntc() {
    local o=$1 d=$2 a=() f; shift 2
    while [ "$1" != -- ]; do a+=("$1"); shift; done; shift
    mkdir -p $o
    a+=(-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $d "$(macro $d INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt $(bash relinc.sh "$@") -I$d)
    for f; do par ntc1 $f; done
    pwait; [ $PARFAIL -eq 0 ] || exit 1
}
# one source of ntc (its o and a), the version of the port when there is one (portdir.sh)
ntc1() {
    local f=$1 b=$(basename "${1%.*}" | tr A-Z a-z) p
    p=$(bash portdir.sh $(dirname $f))/$(basename $f | tr A-Z a-z)
    [ -f $p ] && f=$p
    case $f in
    *.[Cc][Xx][Xx]) ntsrc $f $o/$b.cxx && ntcc $o/$b.obj $o/$b.cxx $NTDEFINES "${a[@]}" ;;
    *) ntsrc $f $o/$b.c && ntcc $o/$b.obj $o/$b.c $NTDEFINES "${a[@]}" ;;
    esac
}
# prog NAME CRT OBJECT...: link the console program obj/host/NAME.EXE with NT C run-time CRT
# (FORCE: the force option of the link, -force:multiple unless set)
prog() {
    local n=$(echo $1 | tr a-z A-Z) c=$2; shift 2
    link -nologo -machine:arm -release ${FORCE:--force:multiple} -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$H/$n.EXE "$@" $U/$c.lib $U/advapi32.lib $U/kernel32.lib $U/user32.lib > $T/link.err 2>&1 \
      || { grep -E "error|unresolved" $T/link.err | head -20; exit 1; }
    echo "   obj/host/$n.EXE"
}
mkdir -p $T
if [ "$ONLY" != mibcc ]; then

ntc $T/gensrv $S/gensrv $(macro $S/gensrv C_DEFINES) -- $S/gensrv/gensrv.c
prog gensrv libc $T/gensrv/gensrv.obj

# GENERR, as rtl\user\makefile.inc builds and runs it: rtl\error.h
D=/work/private/ntos/rtl/user
ntc $T/generr $D $(macro $D C_DEFINES) -- /work/private/ntos/rtl/generr.c
prog generr libc $T/generr/generr.obj
host generr $T/error.h > /dev/null
tr -d "\r" < $T/error.h > /work/RASPI/obj/gen/error.h
echo "   obj/gen/error.h"

# RC: RCDLL, the compiler, with its front end, and the message table of RCDLL
# (rcmsgs.mc, by mc in mcinc.sh as rcdll\makefile.inc makes it). RC compiles its own
# resources.
V=$S/vctools
ntc $T/rc $V/rcdll $(macro $V/rcdll C_DEFINES) -- $(srcs $V/rcdll)
ntsrc $V/rc/rc.c $T/rc/rcmain.c && ntcc $T/rc/rcmain.obj $T/rc/rcmain.c $NTDEFINES -D_MT -I/tmp/f/dirs$V/rcdll -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt
prog rc libcmt $T/rc/*.obj
cp /tmp/f/mc/sdktools/vctools/rcdll/* $T/rc/ && tr -d "\032\r" < $V/rcdll/rcdll.rc > $T/rc/rcdll.rc
(cd $T/rc && host rc -r -fo rc.res -I. -I/tmp/f/pub -I/tmp/f/crt rcdll.rc > rc.err 2>&1) || { cat $T/rc/rc.err; exit 1; }
prog rc libcmt $T/rc/*.obj $T/rc/rc.res

# RCPP, the preprocessor of the hive build (public\oak\bin\mkhives.cmd)
ntc $T/rcpp $S/rcpp $(macro $S/rcpp C_DEFINES) -- $(srcs $S/rcpp)
tr -d "\032\r" < $S/rcpp/rcpp.rc > $T/rcpp/rcpp.rc
(cd $T/rcpp && host rc -r -fo rcpp.res -I. -I/tmp/f/pub -I/tmp/f/crt rcpp.rc > rc.err 2>&1) || { cat $T/rcpp/rc.err; exit 1; }
prog rcpp libc $T/rcpp/*.obj $T/rcpp/rcpp.res

# SOLIDPP, the id preprocessor of the Solitaire build (windows\shell\games\sol\makefile.inc)
D=/work/private/windows/shell/games/sol
ntc $T/solidpp $D -- $D/solidpp.c
prog solidpp libc $T/solidpp/solidpp.obj

# UIXPORT, the export lists of the NET UI DLLs from the symbols of their libraries
# (net\ui\common\src\dll3\makefile.inc)
ntc $T/uixport $S/uixport -- $S/uixport/uixport.c
prog uixport libc $T/uixport/uixport.obj

# MAPMSG, the message files of the message definitions of headers (net\messages\makefile.inc)
ntc $T/mapmsg $S/mapmsg -- $S/mapmsg/mapmsg.c
prog mapmsg libc $T/mapmsg/mapmsg.obj

# GUIDLIB, a library of an object for each GUID of a file (types\uuid\makefile.inc: uuid.lib),
# with its message table (messages.mc, by mc in mcinc.sh as its makefile.inc makes it)
ntc $T/guidlib $S/guidlib -- $(srcs $S/guidlib)
cp /tmp/f/mc/sdktools/guidlib/* $T/guidlib/ && tr -d "\032\r" < $S/guidlib/guidlib.rc > $T/guidlib/guidlib.rc
(cd $T/guidlib && host rc -r -fo guidlib.res -I. -I/tmp/f/pub -I/tmp/f/crt guidlib.rc > rc.err 2>&1) || { cat $T/guidlib/rc.err; exit 1; }
prog guidlib libc $T/guidlib/*.obj $T/guidlib/guidlib.res

# MIDL (rpc\midl20), the RPC interface compiler, and the parser tools of rpc\midlnew
# (midlyacc, midlpg, midleb) midl20\makefil0 makes its IDL and ACF grammars with.
# The parser tools fail by exit(): completing is success.
for t in yacc:midlyacc pg:midlpg erec:midleb; do
    d=$M/midlnew/${t%%:*}
    ntc $T/${t#*:} $d $(macro $d C_DEFINES) -- $(srcs $d)
    prog ${t#*:} libc $T/${t#*:}/*.obj
done
G=$T/midl
mkdir -p $G/front $G/include
for f in $M/midl20/front/*; do tr -d "\032\r" < $f > $G/front/$(basename $f | tr A-Z a-z); done
for g in grammar:xlatidl:IDL:idlerec acfgram:xlatacf:ACF:acferec; do
    IFS=: read n x t r <<< "$g"
    (cd $G/front && { host midlyacc -his -t "YYCONST " $n.y > /dev/null || true; } &&
     mv $n.C $n.c && mv $n.H $n.h && rm -f $n.I && grep -v "^#.*line" $n.c | tr -d "\r" > $n.tmp &&
     { host midlpg $n.tmp | tr -d "\r" > $n.cxx || true; } && { host midleb - $x.dat $t | tr -d "\r" > ../include/$r.h || true; })
done
# the grammars and their headers where the MIDL20 builds find generated sources
mkdir -p /tmp/f/gen/rpc/midl20/front /tmp/f/gen/rpc/midl20/include
cp $G/front/grammar.cxx $G/front/acfgram.cxx $G/front/grammar.h $G/front/acfgram.h /tmp/f/gen/rpc/midl20/front/
cp $G/include/idlerec.h $G/include/acferec.h /tmp/f/gen/rpc/midl20/include/
objs=
for d in analysis codegen expr support typelib front; do
    s=$(srcs $M/midl20/$d "c|cxx")
    [ $d = front ] && s="$s /tmp/f/gen/rpc/midl20/front/grammar.cxx /tmp/f/gen/rpc/midl20/front/acfgram.cxx"
    ntc $G/$d $M/midl20/$d $(macro $M/midl20/$d C_DEFINES) -- $s
    objs="$objs $G/$d/*.obj"
done
ntcc $G/newarray.obj /work/RASPI/crt/cxx/newarray.cxx
# MIDL links the uuid.lib of NT (front\sources LINKLIBS) for the one GUID of its type library
# generator, IID_ITypeInfo. types\uuid makes uuid.lib with MIDL: until it is made, MIDL is
# linked without it (no IDL of NT has the library block that runs the generator), and
# again once it is.
if [ -f $U/uuid.lib ]; then
    prog midl libc $objs $G/newarray.obj $U/uuid.lib
else
    FORCE=-force prog midl libc $objs $G/newarray.obj
fi
fi

# MIBCC, the MIB compiler of the SDK (net\snmp\mibs\makefile.inc: mib.bin): the library of
# agent\mibcc, its program and the MIB writer of mgmtapi (oidconv.c), as its sources link it
if [ "$ONLY" = mibcc ]; then
D=/work/private/net/snmp/agent
ntc $T/mibcc $D/mibcc $(bash incdirs.sh $D/mgmtapi "$(bash srcmacro.sh $D/mgmtapi/sources INCLUDES)") -- $(srcs $D/mibcc) $D/mibcc/mibcc.c $D/mgmtapi/oidconv.c
prog mibcc libc $T/mibcc/*.obj $U/snmp.lib
fi
'

# the import library of ntdll for the registry tools (its exports take GENSRV's list)
[ -f ../obj/um/ntdll.lib ] || IMPLIB=1 "$B/make-dll.sh" dll/daytona
