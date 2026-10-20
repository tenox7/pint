#!/usr/bin/env bash
# Build an NT user mode DLL from its component's SOURCES (TARGETTYPE DYNLINK) as
# makefile.def does, with Microsoft C for ARM: compile its sources and its ARM
# sources (kernel/<component>.arm or the sources.arm of its port directory,
# portdir.sh), link the LINKLIBS and the import libraries and libraries of
# TARGETLIBS (obj/um/<name>.lib) at its COFFBASE.TXT base, entry DLLENTRY. ntdll's exports are NTDLLDEF.SRC + dll/armdef.src +
# GENSRV's ntdll.xtr, preprocessed as its MAKEFILE.INC does; another DLL's come
# from its <name>.def or <name>.src, and it imports ntdll, as makefile.def links
# every DLL. The import library is obj/um/<name>.lib, that of a DLL of a TARGETPATH
# other than the public one is that of its directory (libpath.sh); with PRIVATE exports
# there is also <name>p.lib, which has all of them.
#
# Usage:  cd RASPI/build && ./make-dll.sh DLL/DAYTONA                # ntdll
#         INSTALL=1 ./make-dll.sh DLL/DAYTONA                         # also install
#         INSTALL=1 DEST=SDKTOOLS ROOT=/work/PRIVATE ./make-dll.sh SDKTOOLS/PSAPI # install into \SDKTOOLS
#         ROOT=/work/PRIVATE ./make-dll.sh CSR/SERVER                 # csrsrv
#         IMPLIB=1 ROOT=/work/PRIVATE/WINDOWS ./make-dll.sh USER/CLIENT # the import library alone
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"
COMP=${1:?usage: make-dll.sh COMPONENT}

docker run --rm -e CJ="${CJ:-4}" -e COMP="$COMP" -e ROOT="${ROOT:-/work/PRIVATE/NTOS}" -e INSTALL="${INSTALL:-0}" -e DEST="${DEST:-WINNT/System32}" -e IMPLIB="${IMPLIB:-0}" \
    -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
U=$O/um
D=$ROOT/$COMP
SUBSYS="$(echo $COMP | cut -d/ -f1)" bash mkfarm.sh /tmp/f
DIR=/tmp/dll; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src $U

macro() { bash srcmacro.sh $D/SOURCES $1; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
# the DLLDEF, by default the <name>.def
DEFPATH=$(macro DLLDEF | tr -d " " | tr "\\\\" / | sed "s/\$(TARGET_DIRECTORY)/arm/g"); DEF=$(basename "${DEFPATH:-$NAME.def}")
# The .def file: one under obj\ that the component made (NTTARGETFILE0, nttarget.sh),
# else the version of the port, else that of the tree, else one the component made
# beside its sources (netui0.def).
deffile() {
  local l=$(echo $DEF | tr A-Z a-z) p=$(bash portdir.sh $D) g=/tmp/f/gen/${D#/work/PRIVATE/} f
  case $DEFPATH in [Oo][Bb][Jj]/*) [ -f $g/$l ] && { echo $g/$l; return; } ;; esac
  [ -f $p/$l ] && { echo $p/$l; return; }
  f=$(ls $D | grep -ix "$DEF") && { echo $D/$f; return; }
  [ -f $g/$l ] && echo $g/$l
}
bash mcinc.sh $D
# (the NTTARGETFILE0 of ntdll is the .def made below)
[ $NAME = ntdll ] || bash nttarget.sh $D pre $U || exit 1
EXT=$(macro TARGETEXT | tr -d " " | tr A-Z a-z); EXT=${EXT:-dll}
[ "$(macro TARGETTYPE | tr -d " ")" = DYNLINK ] || { echo "$COMP: not a DYNLINK"; exit 1; }
# the C run-time of the component: libcmt (USE_LIBCMT) or crtdll (USE_CRTDLL)
LIBCDEFS=""
[ -z "$(macro USE_LIBCMT)" ] || LIBCDEFS="-D_MT=1"
[ -z "$(macro USE_CRTDLL)" ] || LIBCDEFS="-D_DLL=1 -D_MT=1"
DEFS="$NTDEFINES $LIBCDEFS $(macro C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g") $(macro NET_C_DEFINES)"
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro USER_INCLUDES);$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/csr -I/tmp/f/crt"

if [ $NAME = ntdll ]; then
  bash gensrv.sh /tmp/gen > /dev/null || exit 1
  cat /work/PRIVATE/NTOS/DLL/NTDLLDEF.SRC $K/dll/armdef.src /tmp/gen/ntdll.xtr > $DIR/ntdll.in
  ntsrc $DIR/ntdll.in $DIR/$NAME.src
  cl -nologo -EP -D_ARM_ -DDBG=0 -DDEVL=1 $DIR/$NAME.src 2>/dev/null | sed -f deffix.sed > $DIR/$NAME.def
elif f=$(deffile); then
  tr -d "\032\r" < $f | sed -f deffix.sed > $DIR/$NAME.def
else
  # obj\*\<name>.def, which makefile.def makes from <name>.src (or a .def in the
  # parent directory) with the C preprocessor
  f=$(ls $D | grep -ix "${DEF%.*}.src") && f=$D/$f || f=$D/../$(ls $D/.. | grep -ix "${DEF%.*}.src\|$DEF" | head -1)
  ntsrc $f $DIR/$NAME.src
  cl -nologo -EP -D_ARM_ -DDBG=0 -DDEVL=1 -DNT_UP=1 $DEFS $DIR/$NAME.src 2>/dev/null | sed -f deffix.sed > $DIR/$NAME.def
fi
# The import library, of the public TARGETPATH in obj/um, else that of its TARGETPATH
# (libpath.sh), as build.exe left it. It leaves out the PRIVATE exports of the DLL;
# when it has any, the private import library <name>p (user32p.lib, gdi32p.lib) has them all.
T=$(bash libpath.sh -d $D "$(macro TARGETPATH)")
I=$U; case $T in /work/PUBLIC/*) ;; *) I=$U/dir/${T#/work/} ;; esac
mkdir -p $I
privlib() {
  grep -q PRIVATE $DIR/$NAME.def || return 0
  sed "s/[[:space:]]PRIVATE//" $DIR/$NAME.def > $DIR/${NAME}p.def
  lib -nologo -machine:arm -def:$DIR/${NAME}p.def -name:$NAME.$EXT -out:$DIR/${NAME}p.lib > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
  ntimplib $DIR/${NAME}p.def $DIR/${NAME}p.lib $I/${NAME}p.lib
}
if [ "$IMPLIB" = 1 ]; then
  lib -nologo -machine:arm -def:$DIR/$NAME.def -name:$NAME.$EXT -out:$DIR/$NAME.lib > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
  ntimplib $DIR/$NAME.def $DIR/$NAME.lib $I/$NAME.lib
  privlib
  echo "   wrote ${I#/work/RASPI/}/$NAME.lib (the import library of the .def)"
  exit 0
fi

echo ">> compiling $COMP ($NAME.$EXT)"
n=0; fail=0
# the farmed directory of a source of the tree outside the component (..\x.c), where
# its quoted includes are found first
srcinc() { local s=$(realpath $(dirname $1)); [ "$s" = "$(realpath $D)" ] && return; case $s in /work/PRIVATE/*) bash incdirs.sh $s "" | cut -d" " -f2 ;; esac; }
srcs=$(bash srclist.sh $COMP $ROOT "(c|cxx|cpp)")
# the directories of the relative includes of the sources, last
INCS="$INCS $(bash relinc.sh $INCS $srcs)"
arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm; [ -f $arm ] || arm=$(bash portdir.sh $D)/sources.arm
# one source, compiled by the pool of msvc.sh (par, pwait)
one() {
  local f=$1 b=$(basename "${1%.*}" | tr A-Z a-z)
  case $f in
  *.asm) ntas $DIR/obj/${b}_asm.obj $f $DEFS -I$O/gen $INCS ;;
  *.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) ntsrc $f $DIR/src/$b.cxx && ntcc $DIR/obj/$b.obj $DIR/src/$b.cxx $DEFS $(srcinc $f) $INCS ;;
  *) ntsrc $f $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c $DEFS $(srcinc $f) $INCS ;;
  esac
}
for f in $srcs $( [ -f $arm ] && grep -v "^#" $arm | sed "s|^[^/]|$(dirname $arm)/&|" ); do par one $f; done
pwait; n=$((n+PAROK)); fail=$((fail+PARFAIL))
echo "   $n compiled, $fail failed"
[ $fail -eq 0 ] || exit 1
# the objects as those of its directory, which another image links (..\imagehlp\obj\*\map.obj
# of psapi.dll); an object of an assembler source by the name of its source (chksum.obj)
mkdir -p $U/dir/${D#/work/} && cp $DIR/obj/*.obj $U/dir/${D#/work/}/
for o in $DIR/obj/*_asm.obj; do [ -f "$o" ] && [ ! -f ${o%_asm.obj}.obj ] && cp $o $U/dir/${D#/work/}/$(basename ${o%_asm.obj}).obj; done
# the array new and delete of the C++ of today (MSVC 2.0 had none), for the C++ of
# the DLL and its libraries
ntcc $DIR/newarray.obj /work/RASPI/crt/cxx/newarray.cxx && ntlib $DIR/newarray.lib $DIR/newarray.obj || exit 1

# In the order of makefile.def: LINKLIBS, the TARGETLIB of a DLL (crtdll.lib with
# USE_CRTDLL, ntdll.lib), TARGETLIBS. A library is the one make-lib.sh made for the
# directory named, else that of its name: a library or an import library.
LIBS=""; seen=""; RESLIBS=""
set -f
for l in $(macro LINKLIBS) $([ -z "$(macro USE_CRTDLL)" ] || echo crtdll.lib) $([ $NAME = ntdll ] || echo ntdll.lib) $(macro TARGETLIBS); do
  case $l in *.[Rr][Ee][Ss]) RESLIBS="$RESLIBS $l"; continue ;; esac
  b=$(basename "$(echo "$l" | tr "\\\\" /)" | tr A-Z a-z); b=${b%.lib}
  case " $seen " in *" $b "*) continue ;; esac; seen="$seen $b"
  a=$(bash libpath.sh $D "$l"); [ -n "$a" ] || a=$U/$b.lib
  [ -f $a ] || { echo "   missing library $b (build it first)"; exit 1; }
  LIBS="$LIBS $a"
done
# The objects the makefile.inc adds (OBJECTS = $(OBJECTS) ...\obj\$(TARGET_DIRECTORY)\x.obj):
# those make-lib.sh made for that directory.
MKI=$(ls $D | grep -ix makefile.inc) && \
for l in $(tr -d "\r" < $D/$MKI | grep -iE "^[[:space:]]*OBJECTS[[:space:]]*=[[:space:]]*\\\$\(OBJECTS\)" | sed -E "s/^[^)]*\)//; s/\\\$\(TARGET_DIRECTORY\)/*/g"); do
  a=$(bash libpath.sh $D "$l")
  [ -n "$a" ] || { echo "   missing object $l (build it first)"; exit 1; }
  LIBS="$LIBS $a"
done
set +f

# DLLBASE: an address, or @coffbase.txt,KEY; the TARGETNAME by default
KEY=$NAME; BASE=""; DB=$(macro DLLBASE | tr -d " ")
case $DB in @*,*) KEY=$(echo ${DB##*,} | tr A-Z a-z) ;; 0x*|[0-9]*) BASE=$DB ;; esac
[ -n "$BASE" ] || BASE=$(tr -d "\r" < /work/PUBLIC/SDK/LIB/COFFBASE.TXT | awk -v n=$KEY "tolower(\$1) == n {print \$2; exit}")
[ -n "$BASE" ] || { echo "   $KEY has no COFFBASE.TXT entry"; exit 1; }
ENTRY=$(macro DLLENTRY | tr -d " ")
E=-noentry; [ -z "$ENTRY" ] || E=-entry:$ENTRY
RC=$(bash srclist.sh $COMP $ROOT rc 2>/dev/null | head -1)
RES=""; [ -z "$RC" ] || RES=$(bash rc.sh $D $RC $DEFS $INCS) || exit 1
# The resource files the libraries name: one another component made (..\xlate\obj\*\x.res,
# make-lib.sh) as it is, one of the component (obj\*\x.res) from its x.rc.
set -f
for l in $RESLIBS; do
  a=$(bash libpath.sh $D "$l")
  case $a in "") ;; */dir/${D#/work/}/*) ;; *) RES="$RES $a"; continue ;; esac
  r=$(basename "$(echo "$l" | tr "\\\\" /)")
  n=$(ls $D | grep -ix "${r%.*}.rc") || { echo "   no ${r%.*}.rc for $l"; exit 1; }
  [ "$D/$n" = "$RC" ] || RES="$RES $(bash rc.sh $D $D/$n $DEFS $INCS)" || exit 1
done
set +f
F=$(echo $NAME.$EXT | tr a-z A-Z)
# (the .pdb and .map are made beside the image, which names the obj/<name>.pdb they then are:
# two images of a name, msacm32.dll and msacm32.drv, may link at once)
link -nologo -machine:arm -release -force:multiple -dll $E -base:$BASE -subsystem:console -nodefaultlib \
     -debug -pdb:$DIR/$NAME.pdb -pdbaltpath:"Z:$(echo $O/$NAME.pdb | tr / "\\\\")" -map:$DIR/$NAME.map \
     -def:$DIR/$NAME.def -implib:$DIR/$NAME.lib -out:$DIR/$F $DIR/obj/*.obj $LIBS $DIR/newarray.lib $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
cp $DIR/$NAME.pdb $DIR/$NAME.map $O/
bash /work/RASPI/tools/host.sh pe2img -version 3.50 $DIR/$F || exit 1
ntimplib $DIR/$NAME.def $DIR/$NAME.lib $I/$NAME.lib
privlib
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/$DEST && mkdir -p $OUT
cp $DIR/$F $OUT/$F
echo "   wrote $OUT/$F"
'
