#!/usr/bin/env bash
# Build the Win32 C run-time of CRT32 and FP32 as BUILDCRT and CRTLIB\MAKEFILE do,
# with Microsoft C for ARM: TYPE ST -> obj/um/libc.lib, MT -> libcmt.lib, DLL ->
# libcdll.lib. Each component compiles its SOURCES (CRTLIBTYPE TYPE) and its ARM
# sources (crt32.arm); the FP32 CONV and TRAN objects join the library, as CRTLIB
# merges conv.lib and tran.lib into it. ST also makes the link option objects
# obj/um/{binmode,commode,setargv}.obj; DLL makes obj/um/crtdlls.lib, the startup
# objects crtdll.lib gives the images that use crtdll.dll (make-crtdll.sh).
#
# Usage:  cd RASPI/crt && ./make-crt32.sh ST|MT|DLL
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"
TYPE=${1:?usage: make-crt32.sh ST|MT|DLL}

docker run --rm -e CJ="${CJ:-4}" -e TYPE="$TYPE" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/crt \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/build/msvc.sh
A=/work/RASPI/crt
OUT=/work/RASPI/obj/um
SUBSYS=" " bash $B/mkfarm.sh /tmp/f
DIR=/tmp/crt32; rm -rf $DIR; mkdir -p $DIR $OUT
export SRCMACROS="CRTLIBTYPE=$TYPE"

COMPS="CONVERT MISC STARTUP STDIO STRING TIME WINHEAP DIRECT DOS EXEC LOWIO MBSTRING HELPER"
case $TYPE in
ST) LIB=libc; COMPS="$COMPS IOSTREAM LINKOPTS" ;;
MT) LIB=libcmt; COMPS="$COMPS IOSTREAM" ;;
DLL) LIB=libcdll; COMPS="$COMPS DLLSTUFF" ;;
*) echo "make-crt32: TYPE is ST, MT or DLL"; exit 1 ;;
esac

# one source of the component build() makes (its locals D, o, defs, incs), compiled by the
# pool of msvc.sh (par, pwait)
one() {
  local f=$1
  local p b
  p=/work/RASPI/ported/$(echo ${D#/work/PRIVATE/} | tr A-Z a-z)/$(echo $f | tr A-Z a-z)
  if [ -f $A/$f ]; then f=$A/$f; elif [ -f $p ]; then f=$p; else f=$D/$(ls $D | grep -ix "$f"); fi
  b=$(basename "${f%.*}" | tr A-Z a-z)
  case $f in
  *.asm) ntas $o/$b.obj $f $NTDEFINES $defs -I/work/RASPI/obj/gen ;;
  *.[Cc][Xx][Xx]) ntsrc $f $o/src/$b.cxx && sed -i -f $A/crtgate.sed $o/src/$b.cxx && ntcc $o/$b.obj $o/src/$b.cxx -Oi- $NTDEFINES $defs $incs ;;
  *) ntsrc $f $o/src/$b.c && sed -i -f $A/crtgate.sed $o/src/$b.c && ntcc $o/$b.obj $o/src/$b.c -Oi- $NTDEFINES $defs $incs ;;
  esac
}

# build COMPONENTDIR: its SOURCES and ARM sources into $DIR/<component>
build() {
  local D=$1 c=$(basename $1) o=$DIR/$(basename $1 | tr A-Z a-z) defs rel incs f b n=0 fail=0
  mkdir -p $o/src
  defs=$(bash $B/srcmacro.sh $D/SOURCES C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g")
  # the directories of the relative includes ("..\\string\\strcat.c"), which the compiler
  # resolves from the include path when the source is compiled from its farmed copy
  rel=$(cat $D/*.[CcHh] 2>/dev/null | tr -d "\r" | sed -nE "s/^[[:space:]]*#[[:space:]]*include[[:space:]]*[\"<](\.\.[\\\\/][^\">]*)[\\\\/][^\\\\/\">]+[\">].*/\1/p" | sort -u | tr "\n" ";")
  incs="-I$A/h -I/work/RASPI/ported/fp32/include -I/tmp/f/ovr $(bash $B/incdirs.sh $D "$(bash $B/srcmacro.sh $D/SOURCES INCLUDES)") -I$K/inc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt -I$D $(bash $B/incdirs.sh $D "$rel" | cut -d" " -f3-)"
  for f in $(bash $B/srcmacro.sh $D/SOURCES SOURCES | tr " " "\n" | grep -iE "\.(c|cxx)$") $(awk -v c=$c "\$1 == c {print \$2}" $A/crt32.arm); do
    par one $f
  done
  pwait; n=$PAROK; fail=$PARFAIL
  echo "   $c: $n compiled, $fail failed"
  [ $fail -eq 0 ]
}

echo ">> CRT32 $TYPE ($LIB.lib)"
ok=1
for c in $COMPS; do build /work/PRIVATE/CRT32/$c || ok=0; done
for c in CONV TRAN; do build /work/PRIVATE/FP32/$c || ok=0; done
[ $ok = 1 ] || exit 1

# The DLL library leaves out the startup of the client images: CRT32\MAKEFILE
# lists its startup objects, and dllstuff gives it only crtlib (the DLL entry).
# crtdll.lib takes txtmode, ncommode and tlssup from LIBCMT (CRTLIB\MAKEFILE):
# the _fmode and _commode of the image, which crtexe hands to the DLL, and dllsupp its
# _fltused (DLLSUPP.ASM of x86).
case $TYPE in
DLL) [ -f $OUT/libcmt.lib ] || { echo "   build MT first: crtdll.lib takes txtmode, ncommode, tlssup from libcmt.lib"; exit 1; }
     mkdir -p /tmp/mt && (cd /tmp/mt && for m in txtmode ncommode tlssup; do
         lib -nologo -extract:$(lib -nologo -list $OUT/libcmt.lib | tr -d "\r" | grep -i "[\\\\/]$m.obj$") -out:$m.obj $OUT/libcmt.lib > /dev/null || exit 1; done) || exit 1
     ntas $DIR/dllstuff/cinitexe.obj $A/dllstuff/arm/cinitexe.asm && ntas $DIR/dllstuff/dllsupp.obj $A/dllstuff/arm/dllsupp.asm || exit 1
     ntlib $OUT/crtdlls.lib $DIR/dllstuff/{crtexe,crtexew,crtdll,dllargv,cinitexe,dllsupp}.obj $DIR/startup/dllmain.obj \
         /tmp/mt/{txtmode,ncommode,tlssup}.obj || exit 1
     rm -f $DIR/dllstuff/{crtexe,crtexew,crtdll,dllargv,cinitexe,dllsupp}.obj $DIR/startup/{crt0,wincrt0,dllcrt0,dllmain,tlssup}.obj
     echo "   wrote obj/um/crtdlls.lib" ;;
ST)  for o in binmode commode setargv; do mv $DIR/linkopts/$o.obj $OUT/$o.obj; done ;;
esac
ntlib $OUT/$LIB.lib $(find $DIR -name "*.obj" | sort) || exit 1
echo "   wrote obj/um/$LIB.lib"
'
