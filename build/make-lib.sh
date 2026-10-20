#!/usr/bin/env bash
# Build an NT LIBRARY component from its SOURCES (TARGETNAME, C_DEFINES, INCLUDES,
# SOURCES) with Microsoft C for ARM, as build.exe does, plus its ARM sources listed
# in kernel/<component>.arm or the sources.arm of its port directory (portdir.sh)
# (a line per source, relative to the list; the ARM_SOURCES NT's SOURCES would carry).
# Output obj/<targetname>.lib, user mode libraries in obj/um.
#
# Usage:  cd RASPI/build && ./make-lib.sh RTL/BOOT               # -> obj/bldrrtl.lib
#         ROOT=/work/PRIVATE ./make-lib.sh URTL         # a component outside PRIVATE/NTOS
#         LOCAL=1 ROOT=/work/PRIVATE ./make-lib.sh SDKTOOLS/SLMNEW/UTIL   # a library of TARGETPATH obj
#                       only in obj/um/dir/<component>, where build.exe left it (not obj/um/util.lib)
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"
COMP=${1:?usage: make-lib.sh COMPONENT}

docker run --rm -e CJ="${CJ:-4}" -e COMP="$COMP" -e ROOT="${ROOT:-/work/PRIVATE/NTOS}" -e LOCAL="${LOCAL:-0}" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work \
    -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
D=$ROOT/$COMP
SUBSYS="$(echo $COMP | cut -d/ -f1)" bash mkfarm.sh /tmp/f
bash mcinc.sh $D
DIR=/tmp/lib; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src

macro() { bash srcmacro.sh $D/SOURCES $1; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
# The libraries of NTOS but its user mode run-time library and the disk registry library
# (of Disk Administrator) are of the kernel and the loader; the others, of user mode, go
# to obj/um.
case $ROOT/$COMP in
/work/PRIVATE/NTOS/RTL/USER|/work/PRIVATE/NTOS/DISKREG) MODE=""; OUT=/work/RASPI/obj/um ;;
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
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro USER_INCLUDES);$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"

echo ">> compiling $COMP ($NAME.lib)"
srcs=$(macro SOURCES | tr " " "\n" | grep -iE "\.(c|cxx|cpp)$" | tr "\\\\" / | while read -r s; do
  d=$(cd $D && cd $(dirname $s) && pwd); b=$(basename $s | tr A-Z a-z); p=$(bash portdir.sh $d)/$b
  { [ -f $p ] && echo $p; } || { n=$(ls $d | grep -ix $b) && echo $d/$n; } ||
  { n=$(ls $d/.. | grep -ix $b) && echo $d/../$n; } || echo /tmp/f/gen/${d#/work/PRIVATE/}/$b; done)
# the farmed directory of a source of the tree outside the component (..\x.c),
# where its quoted includes are found first
srcinc() { local s=$(realpath $(dirname $1)); [ "$s" = "$(realpath $D)" ] && return; case $s in /work/PRIVATE/*) bash incdirs.sh $s "" | cut -d" " -f2 ;; esac; }
# and the directories of the relative includes of the sources, last
INCS="$INCS $(bash relinc.sh $INCS $srcs)"
arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm; [ -f $arm ] || arm=$(bash portdir.sh $D)/sources.arm
n=0; nr=0; fail=0
# one source, compiled by the pool of msvc.sh (par, pwait)
one() {
  local f=$1 b=$(basename "${1%.*}" | tr A-Z a-z)
  [ -f $f ] || { echo "   no source $f"; return 1; }
  case $f in
  *.asm) ntas $DIR/obj/${b}_asm.obj $f $DEFS -I/work/RASPI/obj/gen $INCS ;;
  *.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) ntsrc $f $DIR/src/$b.cxx && ntcc $DIR/obj/$b.obj $DIR/src/$b.cxx $MODE $DEFS $(srcinc $f) $INCS ;;
  *) ntsrc $f $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c $MODE $DEFS $(srcinc $f) $INCS ;;
  esac
}
for f in $srcs $( [ -f $arm ] && grep -v "^#" $arm | sed "s|^[^/]|$(dirname $arm)/&|" ); do par one $f; done
pwait; n=$PAROK; fail=$PARFAIL
# the resources of its resource scripts (obj\*\<name>.res), which the image of another
# component links (..\xlate\obj\*\acledit.res of NET UI)
for r in $(macro SOURCES | tr " " "\n" | grep -iE "\.rc$" | tr "\\\\" /); do
  p=$(bash portdir.sh $D)/$(basename $r | tr A-Z a-z)
  [ -f $p ] || p=$D/$(ls $D | grep -ix "$(basename $r)") || continue
  res=$(bash rc.sh $D $p $DEFS $INCS) && mkdir -p $OUT/dir/${D#/work/} && cp $res $OUT/dir/${D#/work/}/ && nr=$((nr+1)) || fail=$((fail+1))
done
echo "   $((n+nr)) compiled, $fail failed"
[ $fail -eq 0 ] || exit 1
# (a library of no sources is made of libraries by its makefile.inc: uimisc.lib of NET UI)
if [ $n -gt 0 ]; then
  # (LOCAL=1: a library of TARGETPATH obj only as that of its directory)
  T=$(bash libpath.sh -d $D "$(macro TARGETPATH)")
  L=$OUT; [ "$LOCAL" = 1 ] && [ "$T" = $D ] && L=$OUT/dir/${D#/work/}
  mkdir -p $OUT/dir/${D#/work/} && ntlib $L/$NAME.lib $DIR/obj/*.obj || exit 1
  # and as the library and objects of its directory (libpath.sh), where two
  # components share a name or a program links an object of another, and as the
  # library of its TARGETPATH (..\bin)
  cp $DIR/obj/*.obj $OUT/dir/${D#/work/}/ && { [ $L != $OUT ] || cp $L/$NAME.lib $OUT/dir/${D#/work/}/; }
  case $T in /work/PUBLIC/*|$D) ;; *) mkdir -p $OUT/dir/${T#/work/} && cp $L/$NAME.lib $OUT/dir/${T#/work/}/ ;; esac
fi
bash nttarget.sh $D post $OUT || exit 1
[ $n -eq 0 ] || echo "   wrote ${L#/work/RASPI/}/$NAME.lib"
'
