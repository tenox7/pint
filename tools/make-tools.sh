#!/usr/bin/env bash
# Build NT's build tools into obj/host; they run on the 32-bit ARM Wine of the build
# image (tools/host.sh). The tools that make NT's headers (HEXTRACT, HSPLIT, WCSHDR,
# ZWAPI, LISTMUNG, MC), pe2img and midlcpp come first, with the C run-time and
# headers of the SDK of Microsoft C, as NT took them from the SDK of the machine it
# was built on. Then the header pass (arcfw/kernel/mkgen.sh), ksarm.h, NT's C run-time
# and the import libraries of kernel32, user32 and advapi32; then GENSRV, GENERR (and
# rtl\error.h), RC, RCPP, MIDL with its parser tools and SOLIDPP, NT console programs of NT's
# headers and static C run-time (RC is its front end and RCDLL in one program), and the
# import library of ntdll the registry tools take.
#
# Usage:  tools/make-tools.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"
K=$NT35/RASPI/arcfw/kernel

docker run --rm -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel --entrypoint bash nt-msvc:latest -c '
set -eu
H=/work/RASPI/obj/host
T=/tmp/tools
S=/work/PRIVATE/SDKTOOLS
mkdir -p $H
export WINEDEBUG=-all
wineserver -p
# tool NAME DIR [OPTION...]: the C sources of the SOURCES of DIR (else NAME.C) and its
# resource script, ^Z and CR stripped
tool() {
    local n=$1 d=$2 s f r=; shift 2
    mkdir -p $T/$n
    for f in $d/*.[CcHh] $d/*.[Rr][Cc]; do [ -f $f ] && tr -d "\032\r" < $f > $T/$n/$(basename $f | tr A-Z a-z); done
    s=$(bash srcmacro.sh $d/SOURCES SOURCES 2>/dev/null | tr A-Z a-z | tr " " "\n" | grep "\.c$") || s=$n.c
    if [ -f $T/$n/$n.rc ]; then
        (cd $T/$n && rc -nologo -I/work/PUBLIC/SDK/INC -fo $n.res $n.rc > rc.err 2>&1) || { tr -d "\r" < $T/$n/rc.err; exit 1; }
        r=$n.res
    fi
    (cd $T/$n && cl -nologo -O2 -GF- -MT -w -I. -D_ARM_ -D_CRTAPI1=__cdecl -D_CRT_NON_CONFORMING_SWPRINTFS "$@" $s $r \
        -Fe$H/$(echo $n | tr a-z A-Z).EXE user32.lib > cl.err 2>&1) || { tr -d "\r" < $T/$n/cl.err | grep -m5 -E "error|fatal"; exit 1; }
    echo "   obj/host/$(echo $n | tr a-z A-Z).EXE"
}
tool hextract $S/HEXTRACT
tool hsplit $S/HSPLIT
tool wcshdr $S/WCSHDR -D_OS2_20_=0 -Dnear= -Dfar= -Dpascal=
tool zwapi $S/ZWAPI
tool listmung $S/LISTMUNG
tool mc $S/VCTOOLS/MC
mkdir -p /tmp/src && cp /work/RASPI/tools/pe2img.c /work/RASPI/tools/midl/midlcpp.c /tmp/src/
tool pe2img /tmp/src
tool midlcpp /tmp/src
bash mkfarm.sh /tmp/f > /dev/null
'

"$K/make-ksarm.sh"
(cd ../arcfw/crt && ./make-crt32.sh ST && ./make-crt32.sh MT)
for c in BASE/CLIENT/DAYTONA:kernel32 USER/CLIENT:user32 BASE/ADVAPI:advapi32; do
    [ -f ../obj/um/${c#*:}.lib ] || IMPLIB=1 ROOT=/work/PRIVATE/WINDOWS "$K/make-dll.sh" ${c%:*}
done

docker run --rm -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel --entrypoint bash nt-msvc:latest -c '
set -eu
. ./msvc.sh
H=/work/RASPI/obj/host
U=/work/RASPI/obj/um
T=/tmp/tools
S=/work/PRIVATE/SDKTOOLS
M=/work/PRIVATE/RPC
SUBSYS="KE RTL" bash mkfarm.sh /tmp/f
BASE=$(tr -d "\r" < /work/PUBLIC/SDK/LIB/COFFBASE.TXT | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
host() { bash /work/RASPI/tools/host.sh "$@"; }
macro() { bash srcmacro.sh $1/SOURCES $2 | sed "s/\$([A-Za-z_0-9]*)//g"; }
# srcs DIR [EXT]: the sources of the SOURCES of DIR (C, or the extensions EXT)
srcs() { macro $1 SOURCES | tr " " "\n" | grep -iE "\.(${2:-c})$" | while read -r f; do ls -d $1/* | grep -ix "$1/$f"; done; }
# ntc OUT DIR [OPTION...] -- SOURCE...: compile as NT compiles a program of component DIR
ntc() {
    local o=$1 d=$2 a=() f b; shift 2
    while [ "$1" != -- ]; do a+=("$1"); shift; done; shift
    mkdir -p $o
    a+=(-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $d "$(macro $d INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt $(bash relinc.sh "$@") -I$d)
    for f; do
        b=$(basename "${f%.*}" | tr A-Z a-z)
        case $f in
        *.[Cc][Xx][Xx]) ntsrc $f $o/$b.cxx && ntcc $o/$b.obj $o/$b.cxx $NTDEFINES "${a[@]}" ;;
        *) ntsrc $f $o/$b.c && ntcc $o/$b.obj $o/$b.c $NTDEFINES "${a[@]}" ;;
        esac || exit 1
    done
}
# prog NAME CRT OBJECT...: link the console program obj/host/NAME.EXE with NT C run-time CRT
prog() {
    local n=$(echo $1 | tr a-z A-Z) c=$2; shift 2
    link -nologo -machine:arm -release -force:multiple -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$H/$n.EXE "$@" $U/$c.lib $U/advapi32.lib $U/kernel32.lib $U/user32.lib > $T/link.err 2>&1 \
      || { grep -E "error|unresolved" $T/link.err | head -20; exit 1; }
    echo "   obj/host/$n.EXE"
}
mkdir -p $T

ntc $T/gensrv $S/GENSRV $(macro $S/GENSRV C_DEFINES) -- $S/GENSRV/GENSRV.C
prog gensrv libc $T/gensrv/gensrv.obj

# GENERR, as RTL\USER\MAKEFILE.INC builds and runs it: rtl\error.h
D=/work/PRIVATE/NTOS/RTL/USER
ntc $T/generr $D $(macro $D C_DEFINES) -- /work/PRIVATE/NTOS/RTL/GENERR.C
prog generr libc $T/generr/generr.obj
host generr $T/error.h > /dev/null
tr -d "\r" < $T/error.h > /work/RASPI/obj/gen/error.h
echo "   obj/gen/error.h"

# RC: RCDLL, the compiler, with its front end, and the message table of RCDLL
# (RCMSGS.MC, by mc in mcinc.sh as RCDLL\MAKEFILE.INC makes it). RC compiles its own
# resources.
V=$S/VCTOOLS
ntc $T/rc $V/RCDLL $(macro $V/RCDLL C_DEFINES) -- $(srcs $V/RCDLL)
ntsrc $V/RC/RC.C $T/rc/rcmain.c && ntcc $T/rc/rcmain.obj $T/rc/rcmain.c $NTDEFINES -D_MT -I/tmp/f/dirs$V/RCDLL -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt
prog rc libcmt $T/rc/*.obj
cp /tmp/f/mc/sdktools/vctools/rcdll/* $T/rc/ && tr -d "\032\r" < $V/RCDLL/RCDLL.RC > $T/rc/rcdll.rc
(cd $T/rc && host rc -r -fo rc.res -I. -I/tmp/f/pub -I/tmp/f/crt rcdll.rc > rc.err 2>&1) || { cat $T/rc/rc.err; exit 1; }
prog rc libcmt $T/rc/*.obj $T/rc/rc.res

# RCPP, the preprocessor of the hive build (PUBLIC\OAK\BIN\MKHIVES.CMD)
ntc $T/rcpp $S/RCPP $(macro $S/RCPP C_DEFINES) -- $(srcs $S/RCPP)
tr -d "\032\r" < $S/RCPP/RCPP.RC > $T/rcpp/rcpp.rc
(cd $T/rcpp && host rc -r -fo rcpp.res -I. -I/tmp/f/pub -I/tmp/f/crt rcpp.rc > rc.err 2>&1) || { cat $T/rcpp/rc.err; exit 1; }
prog rcpp libc $T/rcpp/*.obj $T/rcpp/rcpp.res

# SOLIDPP, the id preprocessor of the Solitaire build (WINDOWS\SHELL\GAMES\SOL\MAKEFILE.INC)
D=/work/PRIVATE/WINDOWS/SHELL/GAMES/SOL
ntc $T/solidpp $D -- $D/SOLIDPP.C
prog solidpp libc $T/solidpp/solidpp.obj

# MIDL (RPC\MIDL20), the RPC interface compiler, and the parser tools of RPC\MIDLNEW
# (midlyacc, midlpg, midleb) MIDL20\MAKEFIL0 makes its IDL and ACF grammars with.
# The parser tools fail by exit(): completing is success.
for t in YACC:midlyacc PG:midlpg EREC:midleb; do
    d=$M/MIDLNEW/${t%%:*}
    ntc $T/${t#*:} $d $(macro $d C_DEFINES) -- $(srcs $d)
    prog ${t#*:} libc $T/${t#*:}/*.obj
done
G=$T/midl
mkdir -p $G/front $G/include
for f in $M/MIDL20/FRONT/*; do tr -d "\032\r" < $f > $G/front/$(basename $f | tr A-Z a-z); done
for g in grammar:xlatidl:IDL:idlerec acfgram:xlatacf:ACF:acferec; do
    IFS=: read n x t r <<< "$g"
    (cd $G/front && { host midlyacc -his -t "YYCONST " $n.y > /dev/null || true; } &&
     mv $n.C $n.c && mv $n.H $n.h && rm -f $n.I && grep -v "^#.*line" $n.c | tr -d "\r" > $n.tmp &&
     { host midlpg $n.tmp | tr -d "\r" > $n.cxx || true; } && { host midleb - $x.dat $t | tr -d "\r" > ../include/$r.h || true; })
done
# the grammars and their headers where the MIDL20 builds find generated sources
mkdir -p /tmp/f/gen/RPC/MIDL20/FRONT /tmp/f/gen/RPC/MIDL20/INCLUDE
cp $G/front/grammar.cxx $G/front/acfgram.cxx $G/front/grammar.h $G/front/acfgram.h /tmp/f/gen/RPC/MIDL20/FRONT/
cp $G/include/idlerec.h $G/include/acferec.h /tmp/f/gen/RPC/MIDL20/INCLUDE/
objs=
for d in ANALYSIS CODEGEN EXPR SUPPORT FRONT; do
    s=$(srcs $M/MIDL20/$d "c|cxx")
    [ $d = FRONT ] && s="$s /tmp/f/gen/RPC/MIDL20/FRONT/grammar.cxx /tmp/f/gen/RPC/MIDL20/FRONT/acfgram.cxx"
    ntc $G/$d $M/MIDL20/$d $(macro $M/MIDL20/$d C_DEFINES) -- $s
    objs="$objs $G/$d/*.obj"
done
ntcc $G/newarray.obj /work/RASPI/arcfw/crt/cxx/newarray.cxx
prog midl libc $objs $G/newarray.obj
'

# the import library of ntdll for the registry tools (its exports take GENSRV's list)
[ -f ../obj/um/ntdll.lib ] || IMPLIB=1 "$K/make-dll.sh" DLL/DAYTONA
