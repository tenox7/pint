#!/usr/bin/env bash
# Build an NT LIBRARY component from its sources file (TARGETNAME, C_DEFINES, INCLUDES,
# SOURCES) with Microsoft C for ARM, as build.exe does, plus its ARM sources listed
# in kernel/<component>.arm or the sources.arm of its port directory (portdir.sh)
# (a line per source, relative to the list; the ARM_SOURCES NT's SOURCES would carry).
# Output obj[/um]/dir/<component>/<targetname>.lib, where build.exe left it (user mode
# libraries in obj/um), also obj/<targetname>.lib in kernel mode and obj/um/<targetname>.lib
# for a library of the public TARGETPATH.
#
# Usage:  cd RASPI/build && ./make-lib.sh rtl/boot               # -> obj/bldrrtl.lib
#         ROOT=/work/private ./make-lib.sh urtl         # a component outside private/ntos
#         INSTALL=1 ROOT=/work/private ./make-lib.sh net/snmp/mibs   # also install what it binplaces as retail (mib.bin)
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
COMP=${1:?usage: make-lib.sh COMPONENT}

docker run --rm -e CJ="${CJ:-4}" -e COMP="$COMP" -e ROOT="${ROOT:-/work/private/ntos}" -e INSTALL="${INSTALL:-0}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro \
    -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
D=$ROOT/$COMP
SUBSYS="$(echo $COMP | cut -d/ -f1)" bash mkfarm.sh /tmp/f
bash mcinc.sh $D
DIR=/tmp/lib; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src

macro() { bash srcmacro.sh $D/sources $1; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
# the forced include of makefile.def (msvc.sh), or that of the SOURCES (none: LSA subauth)
cw=$(bash srcmacro.sh $D/sources "*" | grep "^COMPILER_WARNINGS=") && COMPILER_WARNINGS=${cw#COMPILER_WARNINGS=}
# The libraries of NTOS but its user mode run-time library and the disk registry library
# (of Disk Administrator) are of kernel mode: those of the kernel and the loader define
# _NTSYSTEM_ in their SOURCES, those of drivers (tdi\tcpip\ip of tcpip.sys) do not; the
# others, of user mode, go to obj/um. Of W32, those of win32k.sys (USER and GDI of kernel mode, the font drivers,
# the math and halftone libraries) and of the display drivers and the printer drivers (their km
# libraries) are of kernel mode, without _NTSYSTEM_; the rest (the clients, the servers of csrss)
# is of user mode. So are those of the drivers of RAS (net\ras\src\nt: rascomp and coherent of asyncmac.sys).
case $ROOT/$COMP in
/work/private/ntos/rtl/user|/work/private/ntos/diskreg) MODE=""; OUT=/work/RASPI/obj/um ;;
/work/private/ntos/w32/ntuser/kernel*|/work/private/ntos/w32/ntuser/rtl/kernel*|/work/private/ntos/w32/ntgdi/gre*|/work/private/ntos/w32/ntgdi/fondrv/*|\
/work/private/ntos/w32/ntgdi/math*|/work/private/ntos/w32/ntgdi/halftone/ht*|/work/private/ntos/w32/ntgdi/displays/*|/work/private/ntos/w32/kmode*|\
/work/private/ntos/w32/ntgdi/printers/*/km|/work/private/net/ras/src/nt/*)
  MODE="-kernel"; OUT=/work/RASPI/obj ;;
/work/private/ntos/w32/*) MODE=""; OUT=/work/RASPI/obj/um ;;
/work/private/ntos/*) MODE="-kernel"; OUT=/work/RASPI/obj ;;
*) MODE=""; OUT=/work/RASPI/obj/um ;;
esac
mkdir -p $OUT
# (with INSTALL=1, the files its recipes binplace as retail go to System32, as make-dll.sh: mib.bin)
export NTTARGET_PLACE=$([ "$INSTALL" = 1 ] && echo /work/RASPI/sdcard/root/WINNT/System32)
rm -f /tmp/nttarget.placed
bash nttarget.sh $D pre $OUT || exit 1
# (a component of pass 0 alone, PASS0ONLY, compiles nothing: ntuser\kbd\us_kbd)
[ -z "$(macro PASS0ONLY)" ] || { echo ">> $COMP: pass 0 done"; exit 0; }
# (a \" of the C_DEFINES is a quote, as the command line of NT gave it to cl: -DX=\"0\"); RC
# gets the defines alone, as makefile.def gives it the CDEFINES: not the MFC_FLAGS of the compiler
CDEFS="$(macro C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g; s/\\\\\"/\"/g") $(macro NET_C_DEFINES)"
DEFS="$(ntdefines $D) $CDEFS"
RCDEFS="$(ntdefines $D rc) $CDEFS"
CXXC=$(ntcxx $D)
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro MFC_INCLUDES);$(macro USER_INCLUDES);$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"

echo ">> compiling $COMP ($NAME.lib)"
srcs=$(macro SOURCES | tr " " "\n" | grep -iE "\.(c|cxx|cpp)$" | tr "\\\\" / | while read -r s; do
  # (of a component of the port alone, which the tree has no directory for: srcmacro.sh)
  d=$(cd $D 2>/dev/null && cd $(dirname $s) && pwd || realpath -m $D/$(dirname $s)); b=$(basename $s | tr A-Z a-z); p=$(bash portdir.sh $d)/$b
  { [ -f $p ] && echo $p; } || { n=$(ls $d 2>/dev/null | grep -ix $b) && echo $d/$n; } ||
  { n=$(ls $d/.. | grep -ix $b) && echo $d/../$n; } || echo /tmp/f/gen/${d#/work/private/}/$b; done)
# the farmed directory of a source of the tree (..\x.c, or the port version of one), where
# its quoted includes are found first, ahead of the include directories of the port: those
# of the component too (the arc.h of glu\nurbs\core, not the ARC arc.h of the port)
srcinc() { local s=$(realpath $(dirname $1)); case $s in /work/RASPI/ported/*) s=/work/private/${s#/work/RASPI/ported/} ;; /work/RASPI/apps/*) s=/work/private/${s#/work/RASPI/apps/} ;; esac; case $s in /work/private/*) bash incdirs.sh $s "" | cut -d" " -f2 ;; esac; }
# and the subdirectories of include directories the sources name in angle brackets, after the include
# directories of the SOURCES (relinc.sh -a), and the directories of their relative includes, last
# (RELINC: both, which rc.sh can leave out)
SUBINC=$(bash relinc.sh -a $INCS $srcs); INCS=$(echo "$INCS" | sed "s| -I/tmp/f/oak | $SUBINC -I/tmp/f/oak |")
RELINC=$(bash relinc.sh $INCS $srcs); INCS="$INCS $RELINC"; export RELINC="$SUBINC $RELINC"
# and the precompiled include of the SOURCES, which its sources include (ntpch)
INCS="$INCS $(ntpch $D /tmp/f/pch)"
arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm; [ -f $arm ] || arm=$(bash portdir.sh $D)/sources.arm
n=0; nr=0; fail=0
# one source, compiled by the pool of msvc.sh (par, pwait)
one() {
  local f=$1 b=$(basename "${1%.*}" | tr A-Z a-z)
  [ -f $f ] || { echo "   no source $f"; return 1; }
  case $f in
  *.asm) ntas $DIR/obj/${b}_asm.obj $f $DEFS -I/work/RASPI/obj/gen $INCS ;;
  *.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) ntsrc $f $DIR/src/$b.cxx && ntyu $D cxx $DIR/src/$b.cxx && ntcc $DIR/obj/$b.obj $DIR/src/$b.cxx $MODE $DEFS $(srcinc $f) $INCS ;;
  *) ntsrc $f $DIR/src/$b.c && ntyu $D c $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c $CXXC $MODE $DEFS $(srcinc $f) $INCS ;;
  esac
}
# the ARM sources of its list: beside the list, else among the files the component made
# (arm/usrstubs.asm of W32\UMODE, by its makefile.inc)
armsrcs() {
  local l
  [ -f $arm ] || return 0
  grep -v "^#" $arm | while read -r l; do
    [ -n "$l" ] || continue
    case $l in /*) echo $l ;; *) if [ -f $(dirname $arm)/$l ]; then echo $(dirname $arm)/$l; else echo /tmp/f/gen/${D#/work/private/}/$l; fi ;; esac
  done
}
# (and the object of the precompiled include, ntpchobj)
for f in $srcs $(armsrcs) $(ntpchobj $D /tmp/f/pchobj); do par one $f; done
pwait; n=$PAROK; fail=$PARFAIL
# the resources of its resource scripts (obj\*\<name>.res), which the image of another
# component links (..\xlate\obj\*\acledit.res of NET UI); a script of another directory is there
# (..\winnet.rc of shellui\xlate\daytona)
for r in $(macro SOURCES | tr " " "\n" | grep -iE "\.rc$" | tr "\\\\" /); do
  s=$D; for c in $(dirname $r | tr / " "); do case $c in .) ;; ..) s=$(dirname $s) ;; *) s=$s/$(ls $s | grep -ix "$c" | head -1) ;; esac; done
  b=$(basename $r | tr A-Z a-z); p=$(bash portdir.sh $s)/$b
  # (else one the build made: the rtmsg.rc of mc in utils\ntlib\src)
  [ -f $p ] || p=$s/$(ls $s 2>/dev/null | grep -ix "$b") || { p=/tmp/f/gen/${s#/work/private/}/$b; [ -f $p ]; } ||
  { p=/tmp/f/mc/$(echo ${s#/work/private/} | tr A-Z a-z)/$b; [ -f $p ]; } || { echo "   no $r"; fail=$((fail+1)); continue; }
  res=$(bash rc.sh $D $p $RCDEFS $INCS) && mkdir -p $OUT/dir/${D#/work/} && cp $res $OUT/dir/${D#/work/}/ && nr=$((nr+1)) || fail=$((fail+1))
done
echo "   $((n+nr)) compiled, $fail failed"
[ $fail -eq 0 ] || exit 1
# (a library of no sources is made of libraries by its makefile.inc: uimisc.lib of NET UI)
if [ $n -gt 0 ]; then
  # the library and objects of its directory (libpath.sh), where two components share a
  # name or a program links an object of another, the library also that of its TARGETPATH
  # (..\bin) and of obj (kernel mode) or obj/um (the public TARGETPATH)
  L=$OUT/dir/${D#/work/}
  T=$(bash libpath.sh -d $D "$(macro TARGETPATH)")
  mkdir -p $L && ntlib $L/$NAME.lib $DIR/obj/*.obj && cp $DIR/obj/*.obj $L/ || exit 1
  case $T in /work/public/*|$D) ;; *) mkdir -p $OUT/dir/${T#/work/} && cp $L/$NAME.lib $OUT/dir/${T#/work/}/ ;; esac
  case $OUT:$T in /work/RASPI/obj:*|*:/work/public/*) cp $L/$NAME.lib $OUT/ ;; esac
fi
bash nttarget.sh $D post $OUT || exit 1
[ $n -eq 0 ] || echo "   wrote ${L#/work/RASPI/}/$NAME.lib"
for p in $(cat /tmp/nttarget.placed 2>/dev/null); do echo "   wrote $NTTARGET_PLACE/$(echo $p | tr a-z A-Z) (binplace)"; done
'
