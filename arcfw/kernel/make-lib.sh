#!/usr/bin/env bash
# Build an NT LIBRARY component from its SOURCES (TARGETNAME, C_DEFINES, INCLUDES,
# SOURCES) with Microsoft C for ARM, as build.exe does, plus its ARM sources listed
# in arcfw/kernel/<component>.arm (a line per source; the ARM_SOURCES NT's SOURCES
# would carry). Output obj/<targetname>.lib, user mode libraries in obj/um.
#
# Usage:  cd RASPI/arcfw/kernel && ./make-lib.sh RTL/BOOT        # -> obj/bldrrtl.lib
#         ROOT=/work/PRIVATE ./make-lib.sh URTL         # a component outside PRIVATE/NTOS
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
COMP=${1:?usage: make-lib.sh COMPONENT}

docker run --rm -e COMP="$COMP" -e ROOT="${ROOT:-/work/PRIVATE/NTOS}" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work \
    -w /work/RASPI/arcfw/kernel --entrypoint bash nt-msvc:latest -c '
set -u
. ./msvc.sh
D=$ROOT/$COMP
SUBSYS="$(echo $COMP | cut -d/ -f1)" bash mkfarm.sh /tmp/f
bash mcinc.sh $D
DIR=/tmp/lib; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src

macro() { bash srcmacro.sh $D/SOURCES $1; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
# The libraries of NTOS but its user mode run-time library are of the kernel and
# the loader; the others, of user mode, go to obj/um.
case $ROOT/$COMP in
/work/PRIVATE/NTOS/RTL/USER) MODE=""; OUT=/work/RASPI/obj/um ;;
/work/PRIVATE/NTOS/*) MODE="-kernel -D_NTSYSTEM_"; OUT=/work/RASPI/obj ;;
*) MODE=""; OUT=/work/RASPI/obj/um ;;
esac
mkdir -p $OUT
bash nttarget.sh $D pre $OUT || exit 1
# the C run-time of the component: libcmt (USE_LIBCMT) or crtdll (USE_CRTDLL)
LIBCDEFS=""
[ -z "$(macro USE_LIBCMT)" ] || LIBCDEFS="-D_MT=1"
[ -z "$(macro USE_CRTDLL)" ] || LIBCDEFS="-D_DLL=1 -D_MT=1"
DEFS="$NTDEFINES $LIBCDEFS $(macro C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g") $(macro NET_C_DEFINES)"
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"

echo ">> compiling $COMP ($NAME.lib)"
srcs=$(macro SOURCES | tr " " "\n" | grep -iE "\.(c|cxx)$" | tr "\\\\" / | while read -r s; do
  d=$(cd $D && cd $(dirname $s) && pwd); b=$(basename $s | tr A-Z a-z); p=/work/RASPI/arcfw/ported/$(echo ${d#/work/PRIVATE/} | tr A-Z a-z)/$b
  { n=$(ls $d | grep -ix $b) && echo $d/$n; } || { n=$(ls $d/.. | grep -ix $b) && echo $d/../$n; } ||
  { [ -f $p ] && echo $p; } || echo /tmp/f/gen/${d#/work/PRIVATE/}/$b; done)
# the farmed directory of a source of the tree outside the component (..\x.c),
# where its quoted includes are found first
srcinc() { local s=$(realpath $(dirname $1)); [ "$s" = "$(realpath $D)" ] && return; case $s in /work/PRIVATE/*) bash incdirs.sh $s "" | cut -d" " -f2 ;; esac; }
# and the directories of the relative includes of the sources, last
INCS="$INCS $(bash relinc.sh $srcs)"
arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm
n=0; fail=0
for f in $srcs $( [ -f $arm ] && grep -v "^#" $arm | sed "s|^[^/]|$K/&|" ); do
  b=$(basename "${f%.*}" | tr A-Z a-z)
  [ -f $f ] || { fail=$((fail+1)); echo "   no source $f"; continue; }
  case $f in
  *.asm) ntas $DIR/obj/${b}_asm.obj $f $DEFS -I/work/RASPI/obj/gen $INCS ;;
  *.[Cc][Xx][Xx]) ntsrc $f $DIR/src/$b.cxx && ntcc $DIR/obj/$b.obj $DIR/src/$b.cxx $MODE $DEFS $(srcinc $f) $INCS ;;
  *) ntsrc $f $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c $MODE $DEFS $(srcinc $f) $INCS ;;
  esac && n=$((n+1)) || fail=$((fail+1))
done
echo "   $n compiled, $fail failed"
[ $fail -eq 0 ] || exit 1
ntlib $OUT/$NAME.lib $DIR/obj/*.obj || exit 1
# and as the library and objects of its directory (libpath.sh), where two
# components share a name or a program links an object of another
mkdir -p $OUT/dir/${D#/work/} && cp $OUT/$NAME.lib $DIR/obj/*.obj $OUT/dir/${D#/work/}/
bash nttarget.sh $D post $OUT || exit 1
echo "   wrote ${OUT#/work/RASPI/}/$NAME.lib"
'
