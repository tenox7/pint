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
# Usage:  cd RASPI/arcfw/kernel && ROOT=/work/PRIVATE ./make-exe.sh SM/SERVER smss
#         INSTALL=1 ...                                           # also install
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
COMP=${1:?usage: make-exe.sh COMPONENT PROGRAM}
PROG=${2:?usage: make-exe.sh COMPONENT PROGRAM}

docker run --rm -e COMP="$COMP" -e PROG="$PROG" -e ROOT="${ROOT:-/work/PRIVATE/NTOS}" -e INSTALL="${INSTALL:-0}" \
    -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel --entrypoint bash tenox7/msvc-arm32 -c '
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
INCS="-I$K/inc -I/tmp/f/ovr $(bash incdirs.sh $D "$(macro INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/csr -I/tmp/f/crt"
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
# A PROGRAM is its SOURCES; another program is its source: the version of the port,
# else the one in the component directory or its parent, and the objects of an
# UMAPPL_NOLIB are the SOURCES its UMLIBS names (obj\*\name.obj).
TTYPE=$(macro TARGETTYPE | tr -d " ")
prog=; srcs=
p=/work/RASPI/arcfw/ported/$(echo ${D#/work/PRIVATE/} | tr A-Z a-z)/$PROG.c
[ "$TTYPE" = PROGRAM ] || { [ -f $p ] && prog=$p; } || for p in $D $D/..; do n=$(ls $p | grep -ix "$PROG.c") && { prog=$p/$n; break; }; done
[ "$TTYPE" = PROGRAM ] || [ -n "$prog" ] || { echo "   no $PROG.c"; exit 1; }
case $TTYPE in PROGRAM|UMAPPL_NOLIB) srcs=$(bash srclist.sh $COMP $ROOT c) ;; esac
# the directories of the relative includes of the sources, last
INCS="$INCS $(bash relinc.sh $prog $srcs)"
comp() { local b=$(basename "${2%.*}" | tr A-Z a-z); ntsrc $2 $DIR/src/$b.c && ntcc $1/$b.obj $DIR/src/$b.c $DEFS $INCS; }
o=$DIR/lobj; [ "$TTYPE" = PROGRAM ] && o=$DIR/obj
for f in $prog; do comp $DIR/obj $f || exit 1; done
for f in $srcs; do comp $o $f || exit 1; done

# The UMLIBS, the TARGETLIBS of a PROGRAM and the libraries of the UMTYPE. A
# library or object is the one make-lib.sh made for the directory named, else
# an object of the program, else that of its name: a library, an import
# library or an object of the C run-time.
LIBS=""; seen=""
set -f
for l in $(macro UMLIBS | tr " " "\n" | grep -iE "\.(lib|obj)$") $([ "$TTYPE" != PROGRAM ] || macro TARGETLIBS) $TYPELIBS; do
  b=$(basename "$(echo "$l" | tr "\\\\" /)" | tr A-Z a-z); case $b in *.obj) ;; *) b=${b%.lib}.lib ;; esac
  case " $seen " in *" $b "*) continue ;; esac; seen="$seen $b"
  a=$(bash libpath.sh $D "$l"); [ -n "$a" ] || { [ -f $DIR/lobj/$b ] && a=$DIR/lobj/$b; } || a=$U/$b
  [ -f $a ] || { echo "   missing $b (build it first)"; exit 1; }
  LIBS="$LIBS $a"
done
set +f

BASE=$(tr -d "\r" < /work/PUBLIC/SDK/LIB/COFFBASE.TXT | awk -v n=$PROG "tolower(\$1) == n {print \$2; exit}")
[ -n "$BASE" ] || BASE=$(tr -d "\r" < /work/PUBLIC/SDK/LIB/COFFBASE.TXT | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
STACK=$(echo "$(macro LINKER_FLAGS) $(amacro MIPS_LNKFLAGS)" | grep -oiE "stack:[0-9a-fx,]+" | tail -1 | cut -d: -f2)
RC=$(bash srclist.sh $COMP $ROOT rc 2>/dev/null | head -1)
RES=""; [ -z "$RC" ] || RES=$(bash rc.sh $D $RC $DEFS $INCS) || exit 1
F=$(echo $PROG | tr a-z A-Z).EXE
link -nologo -machine:arm -release -force:multiple -subsystem:$SUB -entry:$ENTRY -base:$BASE ${STACK:+-stack:$STACK} -nodefaultlib \
     -debug -pdb:$O/$PROG.pdb -map:$O/$PROG.map -out:$DIR/$F $DIR/obj/*.obj $LIBS $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
bash /work/RASPI/tools/host.sh pe2img -version 3.50 $DIR/$F || exit 1
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/WINNT/System32
cp $DIR/$F $OUT/$F
echo "   wrote $OUT/$F"
'
