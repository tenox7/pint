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
#   DLL40   msvcrt40.dll (libw32\lib\dll40): the iostreams of the run-time (crtw32\iostream,
#           its LINKLIBS) and the forwarders of crt40.def to msvcrt.dll (ARM has the list of
#           PowerPC without the names the msvcrt.dll of ARM lacks: the crt40.def of the port);
#           its import library is obj/um/msvcirt.lib, as vctools\crt\makefile copies it
#   POSIX   obj/um/libcpsx.lib, the run-time of the programs of the POSIX subsystem
#           (libw32\lib\posix)
#   MT, ST  obj/um/libcmt.lib, obj/um/libc.lib (libw32\lib\mt, st), the static run-time of
#           USE_LIBCMT and of the images that name no run-time (makefile.def)
#   OLDNAMES obj/um/oldnames.lib, the old names of the run-time (stricmp for _stricmp, and
#           __imp_stricmp for its import), as libw32\makefile makes that of MIPS: an alias
#           object (tools\source\aliasobj.c, obj/host/ALIASOBJ.EXE) for each of its names
#
# Usage:  cd RASPI/crt && ./make-crt.sh                -> obj/libcntpr.lib
#         UM=1 ./make-crt.sh                          -> obj/um/libcntpr.lib (user mode)
#         ./make-crt.sh DLL && ./make-crt.sh DLLLIB   -> msvcrt.dll, obj/um/msvcrt.lib
#         INSTALL=1 ./make-crt.sh DLL                 (msvcrt.dll into System32)
#         ./make-crt.sh DLL40                         -> msvcrt40.dll, obj/um/msvcirt.lib
#         ./make-crt.sh POSIX                         -> obj/um/libcpsx.lib
#         ./make-crt.sh MT                            -> obj/um/libcmt.lib
#         ./make-crt.sh ST                            -> obj/um/libc.lib
#         ./make-crt.sh OLDNAMES                      -> obj/um/oldnames.lib
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
TYPE=${1:-NT}
case $TYPE in NT|DLL|DLLLIB|DLL40|POSIX|MT|ST|OLDNAMES) ;; *) echo "usage: make-crt.sh [NT|DLL|DLLLIB|DLL40|POSIX|MT|ST|OLDNAMES]"; exit 1 ;; esac

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

# OLDNAMES: the names are the objects of OLDNAMES_SHORT_OBJECTS1 and 2 (oldnames\obj\*\NAME.obj);
# the rules of MIPS (no leading _ on its C names, as on ARM) give FROM and TO of the alias
# of each object (.obj for $@) and of its import (.obi): those of the names ($(@B)), then
# those of the special objects (fileleng.obj: filelength)
if [ $TYPE = OLDNAMES ]; then
  [ -f $O/host/ALIASOBJ.EXE ] || { echo "   no obj/host/ALIASOBJ.EXE (tools/make-tools.sh headers)"; exit 1; }
  M=$DIR/makefile; tr -d "\r" < $V/libw32/makefile > $M
  o=$DIR/oldnames; mkdir -p $o
  names=$(awk "/^OLDNAMES_SHORT_OBJECTS[12][[:space:]]*=/ {p = 1; next} p && /^[[:space:]]*\$/ {p = 0} p" $M | grep -oE "[A-Za-z0-9_]+\.obj" | sed "s/\.obj\$//")
  awk "/^OLDNAMES_SHORT_OBJECTS1[[:space:]]*=/ {s = 1} s && index(\$0, \"!IF \$(MIPS)\") == 1 {m = 1} m && /^lib.mips.oldnames.lib/ {exit}
       m && /^[^[:space:]#].*:/ {t = \$1} m && \$1 ~ /mips.aliasobj\$/ {print t, \$2, \$3, \$4}" $M > $DIR/rules
  [ -n "$names" ] && [ -s $DIR/rules ] || { echo "   no OLDNAMES objects or MIPS rules in libw32/makefile"; exit 1; }
  while read -r t a b w; do
    x=obi; [ "$w" = "\$@" ] && x=obj
    case $t in
    "\$(OLDNAMES_SHORT_OBJECTS):") for n in $names; do bash /work/RASPI/tools/host.sh aliasobj ${a//\$(@B)/$n} ${b//\$(@B)/$n} $o/$n.$x || exit 1; done ;;
    *) n=${t##*\\}; n=${n%.obj:}; bash /work/RASPI/tools/host.sh aliasobj $a $b $o/$n.$x || exit 1 ;;
    esac
  done < $DIR/rules
  rm -f $U/oldnames.lib
  lib -nologo -machine:arm -out:$U/oldnames.lib $o/*.obj $o/*.obi > $DIR/lib.err 2>&1 || { echo "   LIB FAIL oldnames.lib: $(grep -m1 -E "error|fatal" $DIR/lib.err)"; exit 1; }
  echo "   wrote obj/um/oldnames.lib ($(ls $o/*.obj | wc -l) names)"
  exit 0
fi

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

# one source of the library build() makes (its globals L, o, defs, libc, adefs, incs), compiled
# by the pool of msvc.sh (par, pwait); C with the LIBC_DEFINES of makefile.def (NDEBUG, and
# _DLL and _MT for the USE_CRTDLL of the iostreams of msvcrt40.dll, which call msvcrt.dll)
one() {
  local p f b
  p=$(norm $L/$1)
  f=$(src $p) || { echo "   missing $p"; return 1; }
  b=$(basename "${p%.*}" | tr A-Z a-z)
  case $p in
  *.asm) ntas $o/$b.obj $f $NTDEFINES $adefs -I/work/RASPI/obj/gen ;;
  *) ntcc $o/$b.obj $DIR/src/$(dirname $p)/$(basename $p | tr A-Z a-z) $MODE $NTDEFINES -DNDEBUG $libc $defs $flags -I$DIR/src/$(dirname $p) $incs ;;
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
  libc=; bash $B/srcmacro.sh $D/sources "*" | grep -qE "^USE_(CRTDLL|MSVCRT)=" && libc="-D_DLL=1 -D_MT=1"
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

# the libraries of the type (libw32\lib\<type>\sources OBJLIBFILES; LINKLIBS for DLL40), as
# their directories: the components of the run-time (the import library of msvcrt.dll in that
# of DLLLIB is the one DLL made)
S=$V/libw32/lib/$T/sources
M=OBJLIBFILES; [ $TYPE = DLL40 ] && M=LINKLIBS
LIBS=$(bash $B/srcmacro.sh $S $M | tr " \\\\" "\n/" | grep -iE "\.(lib|obj)$" | sed -e "s|/[^/]*/[^/]*\$||" -e "s|^\(\.\./\)*||" | grep -E "^(crtw32|fpw32)/")
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
MT|ST)
  ntlib $OUT/$NAME.lib $objs || exit 1
  echo "   wrote obj/um/$NAME.lib" ;;
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
  link -nologo -machine:arm -release -opt:ref,noicf -dll -entry:$ENTRY -base:$BASE -subsystem:windows -nodefaultlib $LINKVER \
       -debug -pdb:$DIR/$NAME.pdb -pdbaltpath:"Z:$(echo $O/$NAME.pdb | tr / "\\\\")" -map:$DIR/$NAME.map \
       -def:$DIR/$NAME.def -implib:$DIR/$NAME.lib -out:$DIR/$F $objs $RES $U/kernel32.lib $U/user32.lib > $DIR/link.err 2>&1 \
    || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
  cp $DIR/$NAME.pdb $DIR/$NAME.map $O/
  ntver $DIR/$F || exit 1
  mkdir -p $I && cp $DIR/$NAME.lib $DIR/$NAME.def $I/
  X=$O; [ "$INSTALL" = 1 ] && X=/work/RASPI/sdcard/root/WINNT/System32 && mkdir -p $X
  cp $DIR/$F $X/$F
  echo "   wrote ${X#/work/RASPI/}/$F" ;;
DLL40)
  [ -f $U/msvcrt.lib ] || { echo "   build msvcrt.dll first (make-crt.sh DLL, DLLLIB)"; exit 1; }
  # its exports: crt40.def, made into the .def by the C preprocessor (a free build: no DBG);
  # the base of its name in coffbase.txt (makefile.def DLLBASE), its client run-time msvcrt.lib
  ntsrc $(src libw32/lib/dll40/crt40.def) $DIR/$NAME.src
  # (its forwarders of the data of msvcrt.dll marked DATA: the linker of today looks for the name
  # a forwarder names in the libraries, and the import library of msvcrt.dll gives data as its
  # __imp_ name alone)
  link -dump -linkermember:1 $I/msvcrt.lib | tr -d "\r" | awk "\$2 ~ /^__imp_/ { i[substr(\$2, 7)] } { c[\$2] } END { for (n in i) if (!(n in c)) print n }" > $DIR/data.txt
  (cd $DIR && cl -nologo -EP -D_ARM_ $NAME.src 2>/dev/null) | sed -f $B/deffix.sed \
    | awk "NR == FNR { d[\$1]; next } match(\$0, /=[ \t]*msvcrt\.[^ \t]+/) { n = substr(\$0, RSTART, RLENGTH); sub(/.*\./, \"\", n); if (n in d) sub(/[ \t]+PRIVATE/, \" DATA PRIVATE\") } { print }" $DIR/data.txt - > $DIR/$NAME.def
  grep -q "^EXPORTS" $DIR/$NAME.def || { echo "   no exports in $NAME.def"; exit 1; }
  D=$V/libw32/lib/dll40
  RES=$(bash $B/rc.sh $D $D/crt40.rc $NTDEFINES -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt) || exit 1
  ENTRY=$(bash $B/srcmacro.sh $S DLLENTRY | tr -d " ")
  F=$(echo $NAME.dll | tr a-z A-Z)
  echo ">> linking $F"
  link -nologo -machine:arm -release -opt:ref,noicf -dll -entry:$ENTRY -base:@/work/public/sdk/lib/coffbase.txt,$NAME -subsystem:windows \
       -nodefaultlib $LINKVER -debug -pdb:$DIR/$NAME.pdb -pdbaltpath:"Z:$(echo $O/$NAME.pdb | tr / "\\\\")" -map:$DIR/$NAME.map \
       -def:$DIR/$NAME.def -implib:$DIR/$NAME.lib -out:$DIR/$F $objs $RES $U/msvcrt.lib $U/kernel32.lib > $DIR/link.err 2>&1 \
    || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
  cp $DIR/$NAME.pdb $DIR/$NAME.map $O/
  ntver $DIR/$F || exit 1
  cp $DIR/$NAME.lib $U/msvcirt.lib
  X=$O; [ "$INSTALL" = 1 ] && X=/work/RASPI/sdcard/root/WINNT/System32 && mkdir -p $X
  cp $DIR/$F $X/$F
  echo "   wrote obj/um/msvcirt.lib, ${X#/work/RASPI/}/$F" ;;
esac
'
