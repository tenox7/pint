#!/usr/bin/env bash
# Build the C run-time as NT 4.0 builds it: the run-time of Visual C++ 4.2
# (sdktools\vctools\crt) for a CRTLIBTYPE, the libraries libw32\lib\<type> merges (its
# OBJLIBFILES: crtw32 and fpw32 components), each compiled as its <type>\sources says
# (crt32.nt, fp32.nt) with its ARM sources (crtw32.arm), with Microsoft C for ARM. The
# port's versions of the files of the run-time are crt/<path> (build/portdir.sh).
#   NT      libcntpr.lib, the run-time of the system (libw32\lib\nt)
#   DLL     msvcrt.dll (libw32\lib\dll): its objects, the exports of libw32\msvcrt.src, its
#           resources, kernel32 and user32, entry _CRTDLL_INIT; its import library goes to
#           its directory (TARGETPATHLIB obj), where DLLLIB takes it
#   DLLLIB  obj/um/msvcrt.lib (libw32\lib\dlllib): that import library and the run-time
#           the clients of msvcrt.dll link into themselves (startup, helpers), which a RISC
#           machine links for USE_CRTDLL and USE_MSVCRT (makefile.def); also as crtdll.lib,
#           where libw32 puts it (DESTFILE)
#   POSIX   obj/um/libcpsx.lib, the run-time of the programs of the POSIX subsystem
#           (libw32\lib\posix)
#
# Usage:  cd RASPI/crt && ./make-crt.sh                -> obj/libcntpr.lib
#         UM=1 ./make-crt.sh                          -> obj/um/libcntpr.lib (user mode)
#         ./make-crt.sh DLL && ./make-crt.sh DLLLIB   -> msvcrt.dll, obj/um/msvcrt.lib
#         INSTALL=1 ./make-crt.sh DLL                 (msvcrt.dll into System32)
#         ./make-crt.sh POSIX                         -> obj/um/libcpsx.lib
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
TYPE=${1:-NT}
case $TYPE in NT|DLL|DLLLIB|POSIX) ;; *) echo "usage: make-crt.sh [NT|DLL|DLLLIB|POSIX]"; exit 1 ;; esac

docker run --rm -e CJ="${CJ:-4}" -e UM="${UM:-0}" -e TYPE="$TYPE" -e INSTALL="${INSTALL:-0}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/crt --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/build/msvc.sh
V=/work/private/sdktools/vctools/crt
A=/work/RASPI/crt
O=/work/RASPI/obj
U=$O/um
SUBSYS=" " bash $B/mkfarm.sh /tmp/f
DIR=/tmp/crt; rm -rf $DIR; mkdir -p $DIR
export SRCMACROS="CRTLIBTYPE=$TYPE"
T=$(echo $TYPE | tr A-Z a-z)
MODE="-Oi-"; OUT=$U
[ $TYPE = NT ] && [ "$UM" != 1 ] && { MODE="-kernel -Oi-"; OUT=$O; }

# a path of the run-time (from sdktools\vctools\crt), its .. resolved
norm() { local p; p=$(realpath -m "/vctools-crt/$1"); echo ${p#/vctools-crt/}; }
# the file of a path of the run-time: that of the port (crt/<path>), else that of the tree
src() { [ -f $A/$1 ] && { echo $A/$1; return; }; bash $B/srcmacro.sh --resolve $V "$1"; }
# the farm of a directory of the run-time: its C and C++ sources and headers as ntsrc makes
# them, those of the port over them (a source includes a sibling, "strspn.c", from there)
farm() {
  local d=$1 t f
  [ -d $DIR/src/$d ] && return
  mkdir -p $DIR/src/$d
  t=$(bash $B/srcmacro.sh --resolve $V $d) || t=
  for f in ${t:+$t/*} $A/$d/*; do
    case $(echo $f | tr A-Z a-z) in *.c|*.h|*.cpp|*.hpp|*.cxx|*.hxx|*.inl) [ -f $f ] && ntsrc $f $DIR/src/$d/$(basename $f | tr A-Z a-z) ;; esac
  done
}

# one source of the library build() makes (its globals L, o, defs, adefs, incs), compiled
# by the pool of msvc.sh (par, pwait); C with the LIBC_DEFINES of makefile.def (NDEBUG:
# the run-time names no USE_ library)
one() {
  local p f b
  p=$(norm $L/$1)
  f=$(src $p) || { echo "   missing $p"; return 1; }
  b=$(basename "${p%.*}" | tr A-Z a-z)
  case $p in
  *.asm) ntas $o/$b.obj $f $NTDEFINES $adefs -I/work/RASPI/obj/gen ;;
  *) ntcc $o/$b.obj $DIR/src/$(dirname $p)/$(basename $p | tr A-Z a-z) $MODE $NTDEFINES -DNDEBUG $defs $flags -I$DIR/src/$(dirname $p) $incs ;;
  esac
}

# build L: the library of the directory L (crtw32/convert/dll), its SOURCES and ARM
# sources: the lines of crtw32.arm for L, and those for its component when L has
# MIPS_SOURCES (the ARM sources stand for them: no library type without them, misc of
# DLLLIB, takes any); the include directories of its SOURCES (the farm of ntos\inc has
# the port ntos.h), then the headers of the port (inc: nt.h) and the common ones
build() {
  local D=$V/$1 s f m
  L=$1; o=$DIR/obj/$(echo $1 | tr / _); mkdir -p $o
  m=$(bash $B/srcmacro.sh $D/sources MIPS_SOURCES)
  s=$(bash $B/srcmacro.sh $D/sources SOURCES | tr " \\\\" "\n/" | grep -iE "\.(c|cpp|asm)$"
      awk -v l=$L -v c=${L%/*} -v m="$m" "\$1 == l || (\$1 == c && m != \"\") {print \$2}" $A/crtw32.arm)
  defs=$(bash $B/srcmacro.sh $D/sources C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g")
  adefs=$(bash $B/srcmacro.sh $D/sources ASM_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g")
  flags=$(bash $B/srcmacro.sh $D/sources USER_C_FLAGS | sed "s/\$([A-Za-z_0-9]*)//g")
  incs="$(bash $B/incdirs.sh $D "$(bash $B/srcmacro.sh $D/sources INCLUDES)") -I$K/inc -I/tmp/f/ovr -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt -I/work/RASPI/obj/gen"
  for f in $s; do farm $(dirname $(norm $L/$f)); done
  # the directories of the sources a source includes by a path (mbscat.c: ..\string\strcat.c),
  # farmed, last
  for f in $(for f in $s; do p=$(norm $L/$f); f=$(src $p) || continue
               tr -d "\r" < $f | sed -nE "s/^[[:space:]]*#[[:space:]]*include[[:space:]]*\"(\.\.[^\"]*)\".*/\1/p" | tr "\\\\" / |
               while read -r i; do dirname $(norm $(dirname $p)/$i); done; done | sort -u); do
    farm $f; incs="$incs -I$DIR/src/$f"
  done
  for f in $s; do par one $f; done
  pwait
  echo "   $L: $PAROK compiled, $PARFAIL failed"
  [ $PARFAIL -eq 0 ]
}

# the libraries of the type (libw32\lib\<type>\sources OBJLIBFILES), as their directories:
# the components of the run-time (the import library of msvcrt.dll in that of DLLLIB is
# the one DLL made)
S=$V/libw32/lib/$T/sources
LIBS=$(bash $B/srcmacro.sh $S OBJLIBFILES | tr " \\\\" "\n/" | grep -iE "\.(lib|obj)$" | sed -e "s|/[^/]*/[^/]*\$||" -e "s|^\(\.\./\)*||" | grep -E "^(crtw32|fpw32)/")
[ -n "$LIBS" ] || { echo "   no libraries in libw32/lib/$T/sources"; exit 1; }
ok=1; objs=
for L in $LIBS; do
  build $L || ok=0
  objs="$objs $DIR/obj/$(echo $L | tr / _)/*.obj"
done
[ $ok = 1 ] || exit 1
mkdir -p $OUT
NAME=$(bash $B/srcmacro.sh $S TARGETNAME | tr -d " " | tr A-Z a-z)
I=$U/dir/private/sdktools/vctools/crt/libw32/lib/dll
case $TYPE in
NT)
  ntlib $OUT/libcntpr.lib $objs || exit 1
  echo "   wrote ${OUT#/work/RASPI/}/libcntpr.lib" ;;
POSIX)
  ntlib $OUT/libcpsx.lib $objs || exit 1
  echo "   wrote obj/um/libcpsx.lib" ;;
DLLLIB)
  [ -f $I/msvcrt.lib ] || { echo "   build msvcrt.dll first (make-crt.sh DLL)"; exit 1; }
  # (in the order of OBJLIBFILES: a name the client defines itself, atexit, before the import)
  ntlib $U/msvcrt.lib $objs $I/msvcrt.lib || exit 1
  cp $U/msvcrt.lib $U/crtdll.lib
  echo "   wrote obj/um/msvcrt.lib (and crtdll.lib)" ;;
DLL)
  # its exports: libw32\msvcrt.src, which makefile.inc includes with LIBRARYNAME DLLNAME, made
  # into the .def by the C preprocessor with its C_DEFINES (DLLNAME, NT_BUILD)
  ntsrc $(src libw32/msvcrt.src) $DIR/msvcrt.in
  printf "#define LIBRARYNAME DLLNAME\n#include \"msvcrt.in\"\n" > $DIR/$NAME.src
  (cd $DIR && cl -nologo -EP -D_ARM_ -DDLLNAME=$NAME.dll -DNT_BUILD $NAME.src 2>/dev/null) | sed -f $B/deffix.sed > $DIR/$NAME.def
  grep -q "^EXPORTS" $DIR/$NAME.def || { echo "   no exports in $NAME.def"; exit 1; }
  D=$V/libw32/lib/dll
  RES=$(bash $B/rc.sh $D $D/msvcrt.rc $NTDEFINES "-DDLLNAME=\"$NAME.dll\"" -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt) || exit 1
  BASE=$(bash $B/srcmacro.sh $S DLLBASE | tr -d " ")
  ENTRY=$(bash $B/srcmacro.sh $S DLLENTRY | tr -d " ")
  F=$(echo $NAME.dll | tr a-z A-Z)
  echo ">> linking $F"
  link -nologo -machine:arm -release -dll -entry:$ENTRY -base:$BASE -subsystem:windows -nodefaultlib $LINKVER \
       -debug -pdb:$DIR/$NAME.pdb -pdbaltpath:"Z:$(echo $O/$NAME.pdb | tr / "\\\\")" -map:$DIR/$NAME.map \
       -def:$DIR/$NAME.def -implib:$DIR/$NAME.lib -out:$DIR/$F $objs $RES $U/kernel32.lib $U/user32.lib > $DIR/link.err 2>&1 \
    || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
  cp $DIR/$NAME.pdb $DIR/$NAME.map $O/
  ntver $DIR/$F || exit 1
  mkdir -p $I && cp $DIR/$NAME.lib $DIR/$NAME.def $I/
  X=$O; [ "$INSTALL" = 1 ] && X=/work/RASPI/sdcard/root/WINNT/System32 && mkdir -p $X
  cp $DIR/$F $X/$F
  echo "   wrote ${X#/work/RASPI/}/$F" ;;
esac
'
