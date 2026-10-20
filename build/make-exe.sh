#!/usr/bin/env bash
# Build an NT user mode program (a SOURCES UMAPPL) as makefile.def does, with
# Microsoft C for ARM: compile the program's source and link it with its UMLIBS (the
# component's LIBRARY from make-lib.sh) and the libraries of its UMTYPE at its
# COFFBASE.TXT base. A native (nt) program links ntdll and nt (URTL startup, entry
# NtProcessStartup), ntss also smdll; a Win32 one (windows, console) the C run-time -
# libc, or libcmt (USE_LIBCMT) or crtdll (USE_CRTDLL), entry WinMainCRTStartup
# (UMENTRY winmain) or mainCRTStartup - advapi32 and kernel32, windows also gdi32 and
# user32.
#
# Usage:  cd RASPI/build && ROOT=/work/PRIVATE ./make-exe.sh SM/SERVER smss
#         INSTALL=1 ...                                           # also install
#         INSTALL=1 DEST=SDKTOOLS ...                             # install into \SDKTOOLS
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"
COMP=${1:?usage: make-exe.sh COMPONENT PROGRAM}
PROG=${2:?usage: make-exe.sh COMPONENT PROGRAM}

docker run --rm -e CJ="${CJ:-4}" -e COMP="$COMP" -e PROG="$PROG" -e ROOT="${ROOT:-/work/PRIVATE/NTOS}" -e INSTALL="${INSTALL:-0}" -e DEST="${DEST:-WINNT/System32}" \
    -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
U=$O/um
D=$ROOT/$COMP
SUBSYS="$(echo $COMP | cut -d/ -f1)" bash mkfarm.sh /tmp/f
bash mcinc.sh $D
bash nttarget.sh $D pre $U || exit 1
DIR=/tmp/exe; rm -rf $DIR; mkdir -p $DIR/obj $DIR/lobj $DIR/src

macro() { bash srcmacro.sh $D/SOURCES $1; }
# The MIPS SOURCES of the component (its mips\sources), the template of the ARM build.
amacro() { local m=$(ls -d $D/[Mm][Ii][Pp][Ss] 2>/dev/null); [ -f "$m/SOURCES" ] && bash srcmacro.sh $m/SOURCES $1; }
CRT=libc; LIBCDEFS=""
[ -z "$(macro USE_LIBCMT)" ] || { CRT=libcmt; LIBCDEFS="-D_MT=1"; }
[ -z "$(macro USE_CRTDLL)" ] || { CRT=crtdll; LIBCDEFS="-D_DLL=1 -D_MT=1"; }
DEFS="$NTDEFINES $LIBCDEFS $(macro C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g") $(macro NET_C_DEFINES)"
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro USER_INCLUDES);$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/csr -I/tmp/f/crt"
UMTYPE=$(macro UMTYPE | tr -d " ")
ENTRY=mainCRTStartup; [ "$(macro UMENTRY | tr -d " ")" = winmain ] && ENTRY=WinMainCRTStartup
case ${UMTYPE:-nt} in
ntss) ENTRY=NtProcessStartup; SUB=native; TYPELIBS="ntdll nt smdll" ;;
nt) ENTRY=NtProcessStartup; SUB=native; TYPELIBS="ntdll nt" ;;
windows) SUB=windows; TYPELIBS="$CRT advapi32 kernel32 gdi32 user32" ;;
console) SUB=console; TYPELIBS="$CRT advapi32 kernel32" ;;
*) echo "$COMP: UMTYPE $UMTYPE not supported"; exit 1 ;;
esac

echo ">> compiling $COMP $PROG.exe (${UMTYPE:-nt})"
# A PROGRAM is its SOURCES (and the source of its UMAPPL that they do not name: pch.c);
# another program is its source (C or C++): the version of the port, else the one in the
# component directory or its parent, and the objects of an UMAPPL_NOLIB are the SOURCES its
# UMLIBS names (obj\*\name.obj).
TTYPE=$(macro TARGETTYPE | tr -d " ")
prog=; srcs=
umappl() { case " $(macro UMAPPL | tr "*" " ") " in *" $PROG "*) ! macro SOURCES | tr " " "\n" | tr "\\\\" / | grep -qiE "(^|/)$PROG\.(c|cxx|cpp)$" ;; *) false ;; esac; }
if [ "$TTYPE" != PROGRAM ] || umappl; then
  p=$(bash portdir.sh $D)
  for x in c cxx cpp; do
    [ -f $p/$PROG.$x ] && { prog=$p/$PROG.$x; break; }
    for d in $D $D/..; do n=$(ls $d | grep -ix "$PROG.$x") && { prog=$d/$n; break 2; }; done
  done
  [ -n "$prog" ] || { echo "   no $PROG.c"; exit 1; }
fi
case $TTYPE in PROGRAM|UMAPPL_NOLIB) srcs=$(bash srclist.sh $COMP $ROOT "(c|cxx|cpp)") ;; esac
# the directories of the relative includes of the sources, last
INCS="$INCS $(bash relinc.sh $INCS $prog $srcs)"
comp() {
  local b=$(basename "${2%.*}" | tr A-Z a-z) x=$(echo ${2##*.} | tr A-Z a-z)
  ntsrc $2 $DIR/src/$b.$x && ntcc $1/$b.obj $DIR/src/$b.$x $DEFS $INCS
}
o=$DIR/lobj; [ "$TTYPE" = PROGRAM ] && o=$DIR/obj
for f in $prog; do par comp $DIR/obj $f; done
for f in $srcs; do par comp $o $f; done
pwait; [ $PARFAIL -eq 0 ] || exit 1

# The objects of UMRES and UMOBJS, the UMLIBS, the libraries of the UMTYPE, the TARGETLIBS
# of a PROGRAM and the LINKLIBS, in the order of makefile.def (srvmgr links crtdll.lib after
# libc.lib). A library or object is the one make-lib.sh made for the directory named, else
# an object of the program, else that of its name: a library, an import library or an
# object of the C run-time. The library of a component whose SOURCES are resources only
# (cntfs.lib) has nothing in it.
OWN=$(macro TARGETNAME | tr -d " " | tr A-Z a-z).lib
NSRC=$(macro SOURCES | tr " " "\n" | grep -ciE "\.(c|cxx|cpp)$")
LIBS=""; seen=""
set -f
for l in $(echo "$(macro UMRES) $(macro UMOBJS)" | tr " " "\n" | grep -iE "\.obj$") $(macro UMLIBS | tr " " "\n" | grep -iE "\.(lib|obj)$") $TYPELIBS \
         $([ "$TTYPE" != PROGRAM ] || macro TARGETLIBS | tr " " "\n" | grep -iE "\.(lib|obj)$") $(macro LINKLIBS | tr " " "\n" | grep -iE "\.(lib|obj)$"); do
  b=$(basename "$(echo "$l" | tr "\\\\" /)" | tr A-Z a-z); case $b in *.obj) ;; *) b=${b%.lib}.lib ;; esac
  a=$(bash libpath.sh $D "$l"); [ -n "$a" ] || { [ -f $DIR/lobj/$b ] && a=$DIR/lobj/$b; } || a=$U/$b
  case " $seen " in *" $a "*) continue ;; esac; seen="$seen $a"
  [ -f $a ] || { [ $b = $OWN ] && [ $NSRC = 0 ] && continue; echo "   missing $b (build it first)"; exit 1; }
  LIBS="$LIBS $a"
done
set +f

BASE=$(tr -d "\r" < /work/PUBLIC/SDK/LIB/COFFBASE.TXT | awk -v n=$PROG "tolower(\$1) == n {print \$2; exit}")
[ -n "$BASE" ] || BASE=$(tr -d "\r" < /work/PUBLIC/SDK/LIB/COFFBASE.TXT | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
STACK=$(echo "$(macro LINKER_FLAGS) $(amacro MIPS_LNKFLAGS)" | grep -oiE "stack:[0-9a-fx,]+" | tail -1 | cut -d: -f2)
# the MS-DOS stub, in the component directory
STUB=$(macro LINKER_FLAGS | grep -oiE "stub:[^ ]+" | tail -1 | cut -d: -f2)
[ -z "$STUB" ] || STUB=$D/$(ls $D | grep -ix "$STUB")
# the resource scripts of the SOURCES and those of the resource files UMRES (that
# of the program, $(@R).res: bind.res) and the libraries name (obj\*\name.res; the
# TARGETLIBS of a PROGRAM, those of another program are of its DLL: imagehlp.res), in the
# component directory; a resource file another component made (..\xlate\obj\*\eventvwr.res,
# make-lib.sh) is linked as it is
RCS=$(bash srclist.sh $COMP $ROOT rc 2>/dev/null | tr "\n" " ")
RES=""
set -f
for l in $(echo "$(macro UMRES | sed "s/\$(@R)/$PROG/g") $(macro UMLIBS) $([ "$TTYPE" != PROGRAM ] || macro TARGETLIBS)" | tr " " "\n" | grep -iE "\.res$"); do
  r=$(basename "$(echo "$l" | tr "\\\\" /)")
  a=$(bash libpath.sh $D "$l")
  case $a in "") ;; */dir/${D#/work/}/*) ;; *) RES="$RES $a"; continue ;; esac
  n=$(ls $D | grep -ix "${r%.*}.rc") || continue
  # (once: the port version of a script of the SOURCES, winmsd.rc, is that of WINMSD.RC)
  case " $(echo $RCS | tr A-Z a-z) " in *"/$(echo $n | tr A-Z a-z) "*) continue ;; esac
  p=$(bash portdir.sh $D)/$(echo $n | tr A-Z a-z); [ -f $p ] && RCS="$RCS $p" || RCS="$RCS $D/$n"
done
set +f
# A script another one includes goes in with that one only (the linker of 1994 kept
# the first of two identical resources).
incl() {
  local o
  for o in $RCS; do
    [ $o != $1 ] && tr -d "\r" < $o | grep -qiE "^[[:space:]]*(rcinclude|#[[:space:]]*include)[[:space:]]+\"?$(basename $1)" && return 0
  done
  return 1
}
for rc in $RCS; do incl $rc || RES="$RES $(bash rc.sh $D $rc $DEFS $INCS)" || exit 1; done
# the program file, .exe or its UMAPPLEXT; its .pdb and .map made beside it, which names the
# obj/<program>.pdb they then are (a DLL of its name, winmsd.dll, may link at once)
X=$(macro UMAPPLEXT | tr -d " "); F=$(echo $PROG${X:-.exe} | tr a-z A-Z)
link -nologo -machine:arm -release -force:multiple -subsystem:$SUB -entry:$ENTRY -base:$BASE ${STACK:+-stack:$STACK} ${STUB:+-stub:$STUB} -nodefaultlib \
     -debug -pdb:$DIR/$PROG.pdb -pdbaltpath:"Z:$(echo $O/$PROG.pdb | tr / "\\\\")" -map:$DIR/$PROG.map -out:$DIR/$F $DIR/obj/*.obj $LIBS $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
cp $DIR/$PROG.pdb $DIR/$PROG.map $O/
bash /work/RASPI/tools/host.sh pe2img -version 3.50 $DIR/$F || exit 1
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/$DEST && mkdir -p $OUT
cp $DIR/$F $OUT/$F
echo "   wrote $OUT/$F"
'
