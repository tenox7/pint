#!/usr/bin/env bash
# Build an NT user mode DLL from its component's SOURCES (TARGETTYPE DYNLINK) as
# makefile.def does, with Microsoft C for ARM: compile its sources and its ARM
# sources (kernel/<component>.arm or the sources.arm of its port directory,
# portdir.sh), link the LINKLIBS and the import libraries and libraries of
# TARGETLIBS (obj/um/<name>.lib) at its coffbase.txt base, entry DLLENTRY. ntdll's exports are ntdlldef.src + dll/armdef.src +
# GENSRV's ntdll.xtr, preprocessed as its makefile.inc does; another DLL's come
# from its <name>.def or <name>.src, and it imports ntdll, as makefile.def links
# every DLL. The import library is obj/um/<name>.lib, that of a DLL of a TARGETPATHLIB
# (else TARGETPATH) other than the public one is that of its directory (libpath.sh); with PRIVATE exports
# there is also <name>p.lib, which has all of them.
#
# Usage:  cd RASPI/build && ./make-dll.sh dll/daytona                # ntdll
#         INSTALL=1 ./make-dll.sh dll/daytona                         # also install
#         INSTALL=1 DEST=SDKTOOLS ROOT=/work/private ./make-dll.sh sdktools/psapi # install into \SDKTOOLS
#         ROOT=/work/private ./make-dll.sh csr/server                 # csrsrv
#         IMPLIB=1 ROOT=/work/private ./make-dll.sh ntos/w32/ntuser/client/daytona # the import library alone
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
COMP=${1:?usage: make-dll.sh COMPONENT}

docker run --rm -e CJ="${CJ:-4}" -e COMP="$COMP" -e ROOT="${ROOT:-/work/private/ntos}" -e INSTALL="${INSTALL:-0}" -e DEST="${DEST:-WINNT/System32}" -e IMPLIB="${IMPLIB:-0}" \
    -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
U=$O/um
D=$ROOT/$COMP
SUBSYS="$(echo $COMP | cut -d/ -f1)" bash mkfarm.sh /tmp/f
DIR=/tmp/dll; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src $U

macro() { bash srcmacro.sh $D/sources $1; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
# the forced include of makefile.def (msvc.sh), or that of the SOURCES
cw=$(bash srcmacro.sh $D/sources "*" | grep "^COMPILER_WARNINGS=") && COMPILER_WARNINGS=${cw#COMPILER_WARNINGS=}
# the DLLDEF, by default the <name>.def; one set to nothing (ulib.dll) is none: the DLL exports what
# its objects export (__declspec(dllexport)), and its import library is made of them (LIBRARY_OBJS)
DEFPATH=$(macro DLLDEF | tr -d " " | tr "\\\\" / | sed "s/\$(TARGET_DIRECTORY)/arm/g"); DEF=$(basename "${DEFPATH:-$NAME.def}")
NODEF=; [ -n "$DEFPATH" ] || ! bash srcmacro.sh $D/sources "*" | grep -qx "DLLDEF=" || NODEF=1
# The .def file: for one under obj\, the one the component made (NTTARGETFILE0, nttarget.sh);
# else, in its directory (that of the component, or another: ..\riched32.def of a component of
# the port alone), the version of the port, else that of the tree; else one the component made
# beside its sources (netui0.def).
deffile() {
  local l=$(echo $DEF | tr A-Z a-z) d=$(realpath -m $D/$(dirname "${DEFPATH:-$DEF}")) g=/tmp/f/gen/${D#/work/private/} f p
  case $DEFPATH in [Oo][Bb][Jj]/*) [ -f $g/$l ] && echo $g/$l; return ;; esac
  p=$(bash portdir.sh $d)
  [ -f $p/$l ] && { echo $p/$l; return; }
  f=$(ls $d 2>/dev/null | grep -ix "$DEF") && { echo $d/$f; return; }
  [ -f $g/$l ] && echo $g/$l
}
bash mcinc.sh $D
# (the .def of ntdll, of its NTTARGETFILE0, is made below; its makefile.inc copies the property set
# sources of dcomidl)
NTTARGET_SKIP=$([ $NAME != ntdll ] || echo obj/arm/ntdll.def) bash nttarget.sh $D pre $U || exit 1
# a .def of NTTARGETFILE1 (netui0.def, of the symbols of the NET UI libraries, uixport) first: the
# import library depends on its DLLDEF, so nmake made it before (and not again with NTTARGETFILE1)
DEF1=; case " $(macro NTTARGETFILE1 | tr A-Z a-z) " in *" $(echo $DEF | tr A-Z a-z) "*) DEF1=$(echo $DEF | tr A-Z a-z)
  bash nttarget.sh $D src $U $DEF || exit 1 ;; esac
EXT=$(macro TARGETEXT | tr -d " " | tr A-Z a-z); EXT=${EXT:-dll}
[ "$(macro TARGETTYPE | tr -d " ")" = DYNLINK ] || { echo "$COMP: not a DYNLINK"; exit 1; }
# (a \" of the C_DEFINES is a quote, as the command line of NT gave it to cl: the
# -D__GLS_RELEASE=\"0\" of glmf32); RC gets the defines alone, as makefile.def gives it the
# CDEFINES: not the MFC_FLAGS and USER_C_FLAGS of the compiler
CDEFS="$(macro C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g; s/\\\\\"/\"/g") $(macro NET_C_DEFINES)"
DEFS="$(ntdefines $D) $CDEFS"
RCDEFS="$(ntdefines $D rc) $CDEFS"
CXXC=$(ntcxx $D)
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro MFC_INCLUDES);$(macro USER_INCLUDES);$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/csr -I/tmp/f/crt"

if [ $NAME = ntdll ]; then
  bash gensrv.sh /tmp/gen > /dev/null || exit 1
  cat /work/private/ntos/dll/ntdlldef.src $K/dll/armdef.src /tmp/gen/ntdll.xtr > $DIR/ntdll.in
  ntsrc $DIR/ntdll.in $DIR/$NAME.src
  cl -nologo -EP -D_ARM_ -DDEVL=1 $DIR/$NAME.src 2>/dev/null | sed -f deffix.sed > $DIR/$NAME.def
elif [ -n "$NODEF" ]; then
  :
elif f=$(deffile); then
  tr -d "\032\r" < $f | sed -f deffix.sed > $DIR/$NAME.def
else
  # obj\*\<name>.def, which makefile.def makes from the <name>.def or <name>.src of the
  # directory (else of its parent) with the C preprocessor; the version of the port first
  f=$(ls $D 2>/dev/null | grep -ix "$DEF\|${DEF%.*}.src" | head -1)
  [ -n "$f" ] && f=$D/$f || f=$D/../$(ls $D/.. | grep -ix "${DEF%.*}.src\|$DEF" | head -1)
  # (with the include directories, as makefile.def runs the C preprocessor, the directory of the
  # file first: oleaut32.def includes switches.hxx)
  t=$(dirname $f)
  p=$(bash portdir.sh $(realpath -m $(dirname $f)))/$(basename $f | tr A-Z a-z); [ ! -f $p ] || f=$p
  ntsrc $f $DIR/$NAME.src
  cl -nologo -EP -D_ARM_ -DDEVL=1 -DNT_UP=1 $DEFS -I$t $INCS $DIR/$NAME.src 2>/dev/null | sed -f deffix.sed > $DIR/$NAME.def
fi
# The import library, of the public TARGETPATHLIB (else TARGETPATH, as makefile.def takes
# it) in obj/um, else that of its directory (libpath.sh), as build.exe left it. It leaves
# out the PRIVATE exports of the DLL; when it has any, the private import library
# <name>p (user32p.lib, gdi32p.lib) has them all.
T=$(macro TARGETPATHLIB | tr -d " "); T=$(bash libpath.sh -d $D "${T:-$(macro TARGETPATH)}")
I=$U; case $T in /work/public/*) ;; *) I=$U/dir/${T#/work/} ;; esac
mkdir -p $I
# The import library, before the image, as makefile.def makes it (TARGETLIBFILES): lib -def:DLLDEF
# with the LIBRARY_OBJS, the objects and LINKLIBS, whose __declspec(dllexport) exports it takes
# too (the FUNC_DECLSPEC functions of netcfg.dll, which ncpa.cpl calls); of a DLL of no .def, those
# alone. Made of the .def alone with IMPLIB=1, before the compile (the objects of a DLL of no .def
# first). The key of ntimplib: the .def and the names the library exports.
privlib() {
  grep -q PRIVATE $DIR/$NAME.def || return 0
  sed "s/[[:space:]]PRIVATE//" $DIR/$NAME.def > $DIR/${NAME}p.def
  lib -nologo -machine:arm -nodefaultlib -def:$DIR/${NAME}p.def -name:$NAME.$EXT -out:$DIR/${NAME}p.lib "$@" > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
  { cat $DIR/${NAME}p.def; MSVC_ENV=0 link -dump -exports $DIR/${NAME}p.lib | grep -v "^Dump of file"; } > $DIR/${NAME}p.key
  ntimplib $DIR/${NAME}p.key $DIR/${NAME}p.lib $I/${NAME}p.lib
}
implib() {
  local d=-def; [ -n "$NODEF" ] || d=-def:$DIR/$NAME.def
  # (-nodefaultlib of the LIBRARIAN_FLAGS: the libraries the objects ask for, uuid3.lib of the
  # docobj.h of MFC, are not opened)
  lib -nologo -machine:arm -nodefaultlib $d -name:$NAME.$EXT -out:$DIR/$NAME.lib "$@" > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
  { [ -n "$NODEF" ] || cat $DIR/$NAME.def; MSVC_ENV=0 link -dump -exports $DIR/$NAME.lib | grep -v "^Dump of file"; } > $DIR/$NAME.key
  ntimplib $DIR/$NAME.key $DIR/$NAME.lib $I/$NAME.lib
  [ -n "$NODEF" ] || privlib "$@"
}
if [ "$IMPLIB" = 1 ] && [ -z "$NODEF" ]; then
  implib
  echo "   wrote ${I#/work/RASPI/}/$NAME.lib (the import library of the .def)"
  exit 0
fi

echo ">> compiling $COMP ($NAME.$EXT)"
n=0; fail=0
# the farmed directory of a source of the tree (..\x.c, or the port version of one), where
# its quoted includes are found first, ahead of the include directories of the port: those
# of the component too (make-lib.sh)
srcinc() { local s=$(realpath $(dirname $1)); case $s in /work/RASPI/ported/*) s=/work/private/${s#/work/RASPI/ported/} ;; /work/RASPI/apps/*) s=/work/private/${s#/work/RASPI/apps/} ;; esac; case $s in /work/private/*) bash incdirs.sh $s "" | cut -d" " -f2 ;; esac; }
srcs=$(bash srclist.sh $COMP $ROOT "(c|cxx|cpp)" 2> $DIR/srclist.err)
# (a source not found stops the build: a DLL without it is not that DLL)
[ ! -s $DIR/srclist.err ] || { cat $DIR/srclist.err; exit 1; }
# the subdirectories of include directories the sources name in angle brackets, after the include
# directories of the SOURCES (relinc.sh -a), and the directories of their relative includes, last
# (RELINC: both, which rc.sh can leave out)
SUBINC=$(bash relinc.sh -a $INCS $srcs); INCS=$(echo "$INCS" | sed "s| -I/tmp/f/oak | $SUBINC -I/tmp/f/oak |")
RELINC=$(bash relinc.sh $INCS $srcs); INCS="$INCS $RELINC"; export RELINC="$SUBINC $RELINC"
# and the precompiled include of the SOURCES, which its sources include (ntpch)
INCS="$INCS $(ntpch $D /tmp/f/pch)"
arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm; [ -f $arm ] || arm=$(bash portdir.sh $D)/sources.arm
# one source, compiled by the pool of msvc.sh (par, pwait)
one() {
  local f=$1 b=$(basename "${1%.*}" | tr A-Z a-z)
  case $f in
  *.asm) ntas $DIR/obj/${b}_asm.obj $f $DEFS -I$O/gen $INCS ;;
  *.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) ntsrc $f $DIR/src/$b.cxx && ntyu $D cxx $DIR/src/$b.cxx && ntcc $DIR/obj/$b.obj $DIR/src/$b.cxx $DEFS $(srcinc $f) $INCS ;;
  *) ntsrc $f $DIR/src/$b.c && ntyu $D c $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c $CXXC $DEFS $(srcinc $f) $INCS ;;
  esac
}
# (and the object of the precompiled include, ntpchobj)
for f in $srcs $( [ -f $arm ] && grep -v "^#" $arm | sed "s|^[^/]|$(dirname $arm)/&|" ) $(ntpchobj $D /tmp/f/pchobj); do par one $f; done
pwait; n=$((n+PAROK)); fail=$((fail+PARFAIL))
echo "   $n compiled, $fail failed"
[ $fail -eq 0 ] || exit 1
# the objects as those of its directory, which another image links (..\imagehlp\obj\*\map.obj
# of psapi.dll); an object of an assembler source by the name of its source (chksum.obj)
# (a DLL of no sources of its own is made of its libraries: winsrv.dll)
OBJS=$(ls $DIR/obj/*.obj 2>/dev/null)
mkdir -p $U/dir/${D#/work/} && { [ -z "$OBJS" ] || cp $OBJS $U/dir/${D#/work/}/; }
for o in $DIR/obj/*_asm.obj; do [ -f "$o" ] && [ ! -f ${o%_asm.obj}.obj ] && cp $o $U/dir/${D#/work/}/$(basename ${o%_asm.obj}).obj; done
# the array new and delete of the C++ of today (MSVC 2.0 had none), for the C++ of
# the DLL and its libraries
ntcc $DIR/newarray.obj /work/RASPI/crt/cxx/newarray.cxx && ntlib $DIR/newarray.lib $DIR/newarray.obj || exit 1

# In the order of makefile.def: LINKLIBS, the TARGETLIB of a DLL, TARGETLIBS. TARGETLIB is the
# C run-time (libcpsx.lib for UMTYPE posix, whatever the rest says; LIBC_LIB: msvcrt.lib and
# msvcirt.lib, the iostreams of msvcrt40.dll, with USE_CRTDLL, USE_MSVCRT, USE_MSVCRT20 or
# USE_MSVCRT40, all USE_MSVCRT on a RISC machine; libcmt.lib with USE_LIBCMT, libcntpr.lib with
# USE_LIBCNTPR, none with USE_NTDLL, USE_SYSDLL or USE_NOLIBS, else libc.lib) and ntdll.lib, but
# ntdll.lib alone with USE_NTDLL or USE_SYSDLL, the run-time alone with NO_NTDLL (USE_NOLIBS),
# none for ntdll. A library is the one make-lib.sh made for the directory named, else that of
# its name: a library or an import library.
NONT=$(macro NO_NTDLL)
if [ "$(macro UMTYPE | tr -d " ")" = posix ]; then CRT=libcpsx.lib; NONT=
elif [ -n "$(macro USE_CRTDLL)$(macro USE_MSVCRT)$(macro USE_MSVCRT20)$(macro USE_MSVCRT40)" ]; then CRT="msvcrt.lib msvcirt.lib"
elif [ -n "$(macro USE_LIBCMT)" ]; then CRT=libcmt.lib
elif [ -n "$(macro USE_LIBCNTPR)" ]; then CRT=libcntpr.lib
elif [ -n "$(macro USE_NTDLL)$(macro USE_SYSDLL)" ]; then CRT=
elif [ -n "$(macro USE_NOLIBS)" ]; then CRT=; NONT=1
else CRT=libc.lib; fi
# (with the libraries of MFC ahead of it: LIBC_LIB = $(MFC_LIBS) $(LIBC_LIB))
case $CRT in ""|libcpsx.lib) ;; *) CRT="$(macro MFC_LIBS) $CRT" ;; esac
if [ $NAME = ntdll ]; then TL=
elif [ -n "$(macro USE_NTDLL)$(macro USE_SYSDLL)" ]; then TL=ntdll.lib
elif [ -n "$NONT" ]; then TL=$CRT
else TL="$CRT ntdll.lib"; fi
LIBS=""; seen=""; RESLIBS=""; LL=""
# a list of libraries with those of makefile.def it names by macro (olecnv32): WIN32_LIBS,
# the C run-time (LIBC_LIB), advapi32 and kernel32, and GUI32_LIBS, gdi32 and user32
libs() { local v=$(macro $1); v=${v//\$(WIN32_LIBS)/$CRT advapi32.lib kernel32.lib}; echo "${v//\$(GUI32_LIBS)/gdi32.lib user32.lib}"; }
set -f
# the LINKLIBS of the import library, after the static library of MFC (LIBRARY_OBJS)
for l in $(macro MFC_STATIC_LIB) $(libs LINKLIBS); do
  case $l in *.[Rr][Ee][Ss]) continue ;; esac
  a=$(bash libpath.sh $D "$l"); [ -n "$a" ] || a=$U/$(basename "$(echo "$l" | tr "\\\\" /)" | tr A-Z a-z)
  [ -f $a ] && LL="$LL $a"
done
[ "$IMPLIB" = 1 ] && { implib $OBJS $LL; echo "   wrote ${I#/work/RASPI/}/$NAME.lib (the import library of its objects)"; exit 0; }
for l in $(libs LINKLIBS) $TL $(libs TARGETLIBS); do
  case $l in *.[Rr][Ee][Ss]) RESLIBS="$RESLIBS $l"; continue ;; esac
  b=$(basename "$(echo "$l" | tr "\\\\" /)" | tr A-Z a-z); b=${b%.lib}
  a=$(bash libpath.sh $D "$l"); [ -n "$a" ] || a=$U/$b.lib
  # (else one of kernel mode, which the client links too: efloat.lib of gdi32 and win32k.sys)
  [ -f $a ] || [ ! -f $O/$b.lib ] || a=$O/$b.lib
  [ -f $a ] || { echo "   missing library $b (build it first)"; exit 1; }
  # (each once: two of one name are two libraries, the util.lib of OLE com and of ole232)
  case " $seen " in *" $a "*) continue ;; esac; seen="$seen $a"
  LIBS="$LIBS $a"
done
# The objects the makefile.inc adds (OBJECTS = $(OBJECTS) ...\obj\$(TARGET_DIRECTORY)\x.obj):
# those make-lib.sh made for that directory.
MKI=$(ls $D 2>/dev/null | grep -ix makefile.inc) && \
for l in $(tr -d "\r" < $D/$MKI | grep -iE "^[[:space:]]*OBJECTS[[:space:]]*=[[:space:]]*\\\$\(OBJECTS\)" | sed -E "s/^[^)]*\)//; s/\\\$\(TARGET_DIRECTORY\)/*/g"); do
  a=$(bash libpath.sh $D "$l")
  [ -n "$a" ] || { echo "   missing object $l (build it first)"; exit 1; }
  LIBS="$LIBS $a"
done
set +f

# DLLBASE: an address, or @coffbase.txt,KEY; the TARGETNAME by default
KEY=$NAME; BASE=""; DB=$(macro DLLBASE | tr -d " ")
case $DB in @*,*) KEY=$(echo ${DB##*,} | tr A-Z a-z) ;; 0x*|[0-9]*) BASE=$DB ;; esac
[ -n "$BASE" ] || BASE=$(tr -d "\r" < /work/public/sdk/lib/coffbase.txt | awk -v n=$KEY "tolower(\$1) == n {print \$2; exit}")
[ -n "$BASE" ] || { echo "   $KEY has no COFFBASE.TXT entry"; exit 1; }
ENTRY=$(macro DLLENTRY | tr -d " ")
E=-noentry; [ -z "$ENTRY" ] || E=-entry:$ENTRY
# The DLL entry of the run-time calls DllMain, that of the DLL: the link of today resolves the entry
# first and would take the dummy DllMain of the run-time library before one in a library of LINKLIBS
# (that of ole32, com\class compobj.obj), which then never ran.
[ "$ENTRY" != _DllMainCRTStartup ] || E="$E -include:DllMain"
RC=$(bash srclist.sh $COMP $ROOT rc 2>/dev/null | head -1)
RES=""; [ -z "$RC" ] || RES=$(bash rc.sh $D $RC $RCDEFS $INCS) || exit 1
# The resource files the libraries name: one another component made (..\xlate\obj\*\x.res,
# make-lib.sh) as it is, one of the component (obj\*\x.res) from its x.rc.
set -f
for l in $RESLIBS; do
  a=$(bash libpath.sh $D "$l")
  case $a in "") ;; */dir/${D#/work/}/*) ;; *) RES="$RES $a"; continue ;; esac
  r=$(basename "$(echo "$l" | tr "\\\\" /)")
  n=$(ls $D 2>/dev/null | grep -ix "${r%.*}.rc") || { echo "   no ${r%.*}.rc for $l"; exit 1; }
  [ "$D/$n" = "$RC" ] || RES="$RES $(bash rc.sh $D $D/$n $RCDEFS $INCS)" || exit 1
done
set +f
# The import library, then NTTARGETFILE1, which makefile.def makes between it and the image
# (the public shell32.lib of windows\shell\shelldll\base: the import library, shguid.obj and
# shellprv.obj); NTTARGETFILES after the image.
implib $OBJS $LL
NTTARGET_SKIP=$DEF1 bash nttarget.sh $D file1 $U || exit 1
F=$(echo $NAME.$EXT | tr a-z A-Z)
# (the .pdb and .map are made beside the image, which names the obj/<name>.pdb they then are:
# two images of a name, msacm32.dll and msacm32.drv, may link at once; -opt:ref as makefile.def
# links, which -debug would turn off, and noicf: the link of 1996 folds no functions)
link -nologo -machine:arm -release -opt:ref,noicf -force:multiple -dll $E -base:$BASE -subsystem:console -nodefaultlib $LINKVER $(ntlinkflags $D) \
     -debug -pdb:$DIR/$NAME.pdb -pdbaltpath:"Z:$(echo $O/$NAME.pdb | tr / "\\\\")" -map:$DIR/$NAME.map \
     $([ -n "$NODEF" ] || echo -def:$DIR/$NAME.def) -implib:$DIR/$NAME.lib -out:$DIR/$F $OBJS $LIBS $DIR/newarray.lib $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
cp $DIR/$NAME.pdb $DIR/$NAME.map $O/
ntver $DIR/$F $(macro SUBSYSTEM_VERSION | tr -d " ") || exit 1
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/$DEST && mkdir -p $OUT
cp $DIR/$F $OUT/$F
echo "   wrote $OUT/$F"
# NTTARGETFILES as in the link pass of BUILD (MAKEDLL), of the image where makefile.def leaves it
# (obj\<machine>, the generated files of the component; not kept by gensave.sh): the codecs copy
# theirs to an .acm, which they binplace. An image placefil.txt does not list went to dump.
G=/tmp/f/gen/${D#/work/private/}; g=$G/$(echo $F | tr A-Z a-z)
mkdir -p $G && cp $DIR/$F $g && { [ ! -f /tmp/f/.genbase ] || touch -r /tmp/f/.genbase $g; }
rm -f /tmp/nttarget.placed
NTTARGET_PLACE=$([ "$INSTALL" = 1 ] && echo $OUT) SRCMACROS="${SRCMACROS:+$SRCMACROS }MAKEDLL=1" bash nttarget.sh $D files $U || exit 1
for p in $(cat /tmp/nttarget.placed 2>/dev/null); do echo "   wrote $OUT/$(echo $p | tr a-z A-Z) (binplace)"; done
if [ -s /tmp/nttarget.placed ] && ! tr -d "\r" < /work/public/sdk/lib/placefil.txt | awk -v n=$(echo $F | tr A-Z a-z) "tolower(\$1) == n {f = 1} END {exit !f}"; then
  rm -f $OUT/$F; echo "   removed $OUT/$F (not in placefil.txt: binplace put it in dump)"
fi
'
