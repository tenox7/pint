#!/usr/bin/env bash
# Build libcntpr.lib, the C run-time of the system: CRT32NT built as BUILDCRT NT builds
# it (CRTLIBTYPE NT) and the floating point library of FP32NT merged into it, as
# CRTLIB does, with Microsoft C for ARM. The objects are the NT list of
# CRT32NT\MAKEFILE plus the ARM CRT_OBJECTS (the portable C string routines), the ARM
# run-time helpers and the ARM sources of MISC (chandler.c, jmpuwind.asm, setjmp.asm,
# setjmpex.asm, longjmp.asm) here.
#
# Usage:  cd RASPI/crt && ./make-crt.sh                -> obj/libcntpr.lib
#         UM=1 ./make-crt.sh                          -> obj/um/libcntpr.lib (user mode)
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"

docker run --rm -e CJ="${CJ:-4}" -e UM="${UM:-0}" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/crt --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/build/msvc.sh
C=/work/PRIVATE/CRT32NT
A=/work/RASPI/crt
SUBSYS=" " bash $B/mkfarm.sh /tmp/f
DIR=/tmp/crt; rm -rf $DIR; mkdir -p $DIR/obj

MODE="-kernel -Oi-"; OUT=/work/RASPI/obj
[ "$UM" = 1 ] && { MODE="-Oi-"; OUT=/work/RASPI/obj/um; }
DEFS="$NTDEFINES -DNDEBUG -D_WIN32_=1 -D_NTSUBSET_=1 -D_NTSYSTEM_=1 -D_INTL"
INCS="-I$A/h -I/tmp/f/crth -I$K/inc -I/tmp/f/ovr -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt -I/work/RASPI/obj/gen"

# The sources, farmed as the build compiles them (some include a sibling .c), the
# versions of the port over them.
for d in CONVERT MISC STRING STDIO STARTUP HACK; do
  s=$DIR/src/$(echo $d | tr A-Z a-z); mkdir -p $s
  for f in $C/$d/*.[CcHh]; do ntsrc $f $s/$(basename $f | tr A-Z a-z); done
  for f in /work/RASPI/ported/$(echo ${C#/work/PRIVATE/}/$d | tr A-Z a-z)/*.[ch]; do [ -f $f ] && ntsrc $f $s/$(basename $f); done
done
bash crtlist.sh $C > $DIR/list
for f in MEMCMP MEMCPY MEMMOVE MEMSET STRCAT STRCHR STRCMP STRLEN STRRCHR; do echo $C/STRING/$f.C; done >> $DIR/list

n=0; fail=0
# one source of the list, compiled by the pool of msvc.sh (par, pwait)
crtc() { local f=$1 b=$(basename "${1%.[Cc]}" | tr A-Z a-z) d=$(basename $(dirname $1) | tr A-Z a-z); ntcc $DIR/obj/$b.obj $DIR/src/$d/$b.c $MODE $DEFS -I$DIR/src/$d $INCS; }
for f in $(cat $DIR/list); do par crtc $f; done
ntsrc helper/arm/rthelpc.c $DIR/rthelpc.c && ntcc $DIR/obj/rthelpc.obj $DIR/rthelpc.c $MODE && n=$((n+1)) || fail=$((fail+1))
ntsrc misc/arm/chandler.c $DIR/chandler.c && ntcc $DIR/obj/chandler.obj $DIR/chandler.c $MODE $DEFS $INCS && n=$((n+1)) || fail=$((fail+1))
for s in jmpuwind setjmp setjmpex longjmp; do par ntas $DIR/obj/$s.obj misc/arm/$s.asm -I/work/RASPI/obj/gen; done
for s in sdiv udiv sdiv64 udiv64 srsh ursh divby0 chkstk; do par ntas $DIR/obj/$s.obj helper/arm/$s.asm -I/work/RASPI/obj/gen; done
# The floating point library of FP32NT (trannt.lib), which CRTLIB merges into
# libcntpr.lib: its SOURCES (CRTLIBTYPE NT) and ARM sources (the TRAN of crt32.arm).
T=/work/PRIVATE/FP32NT/TRAN
export SRCMACROS="CRTLIBTYPE=NT"
TDEFS=$(bash $B/srcmacro.sh $T/SOURCES C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g")
TINCS="-I$A/h -I/work/RASPI/ported/fp32/include $(bash $B/incdirs.sh $T "$(bash $B/srcmacro.sh $T/SOURCES INCLUDES)") -I$K/inc -I/tmp/f/ovr -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
mkdir -p $DIR/tran
# one source of TRAN, compiled by the pool
tranc() {
  local f=$1 b
  if [ -f $A/$f ]; then f=$A/$f; else f=$T/$(ls $T | grep -ix "$f"); fi
  b=t_$(basename "${f%.*}" | tr A-Z a-z)
  case $f in
  *.asm) ntas $DIR/obj/$b.obj $f $NTDEFINES $TDEFS ;;
  *) ntsrc $f $DIR/tran/$b.c && sed -i -f $A/crtgate.sed $DIR/tran/$b.c && ntcc $DIR/obj/$b.obj $DIR/tran/$b.c -Oi- $NTDEFINES $TDEFS $TINCS ;;
  esac
}
for f in $(bash $B/srcmacro.sh $T/SOURCES SOURCES | tr " " "\n" | grep -iE "\.c$") $(awk "\$1 == \"TRAN\" {print \$2}" $A/crt32.arm); do
  par tranc $f
done
pwait; n=$((n+PAROK)); fail=$((fail+PARFAIL))
echo "   libcntpr: $n objects, $fail failed"
[ $fail -eq 0 ] || exit 1
mkdir -p $OUT
ntlib $OUT/libcntpr.lib $DIR/obj/*.obj || exit 1
echo "   wrote ${OUT#/work/RASPI/}/libcntpr.lib"
'
