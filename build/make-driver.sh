#!/usr/bin/env bash
# Build an NT kernel-mode driver from its component's SOURCES (TARGETTYPE DRIVER,
# MINIPORT, EXPORT_DRIVER or GDI_DRIVER) as makefile.def does, with Microsoft C for ARM:
# compile with the C_DEFINES of the SOURCES against the generated driver headers, link
# with the import libraries of ntoskrnl, the HAL and the TARGETLIBS (an export driver's
# or a LIBRARY's obj/<name>.lib) at DRIVERBASE, entry DriverEntry, native subsystem, the
# sections on DRIVER_ALIGNMENT (0x20); a
# display driver (GDI_DRIVER) is a DLL of entry DrvEnableDriver that imports from
# win32k.sys alone; a HAL a DLL of entry HalInitSystem at HALBASE, of the exports of its
# DLLDEF, that imports from ntoskrnl (but with NOTARGETLIB) and its TARGETLIBS (the
# mcdsrv32.dll of OpenGL MCD: win32k.sys). Its
# ARM sources are those of kernel/<component>.arm, which also holds the macros of NT's
# <machine>\sources (NAME=value: DRIVERBASE); the files its MAKEFILE.INC makes are made
# first (nttarget.sh: NTTARGETFILE0 and the ARM sources it makes). An EXPORT_DRIVER's
# exports come from <name>.src preprocessed as makefile.def does; its import library is
# written to obj/<name>.lib for its clients. INSTALL=1 installs it where NT setup puts it
# (layout.inx: System32 or System32\Drivers).
#
# Usage:  cd RASPI/build && ./make-driver.sh FASTFAT                # private/ntos/fastfat
#         INSTALL=1 ./make-driver.sh DD/SCSIPORT                     # also install
#         ROOT=/work/RASPI/drivers ./make-driver.sh BCMEMMC          # a component outside private/ntos
#         IMPLIB=1 ROOT=/work/private/windows/setup/textmode ./make-driver.sh spddlang/sbcs   # its import library
# Needs obj/ntoskrnl.lib from make-execlink.sh and obj/hal.lib from make-hal.sh.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
COMP=${1:?usage: make-driver.sh COMPONENT}
[ -f "$NT40/RASPI/obj/ntoskrnl.lib" ] || { echo "run make-execlink.sh first"; exit 1; }
[ -f "$NT40/RASPI/obj/hal.lib" ] || { echo "run make-hal.sh first"; exit 1; }

docker run --rm -e CJ="${CJ:-4}" -e COMP="$COMP" -e ROOT="${ROOT:-/work/private/ntos}" -e INSTALL="${INSTALL:-0}" -e IMPLIB="${IMPLIB:-0}" \
    -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
SRC=$ROOT/$COMP
c=$(echo $COMP | tr A-Z a-z)
# the component and the directory above it (a DAYTONA build of the sources there)
SUBSYS="$COMP $([ $COMP = ${COMP%/*} ] || echo ${COMP%/*})" bash mkfarm.sh /tmp/f
if [ $ROOT != /work/private/ntos ]; then
  mkdir -p /tmp/f/$c
  for h in $SRC/*.[Hh]; do
    [ -e "$h" ] && ntsrc "$h" /tmp/f/$c/$(basename "$h" | tr A-Z a-z)
  done
fi
# the port versions of its headers
PH=
mkdir -p /tmp/f/$c
for h in /work/RASPI/ported/$(echo ${SRC#/work/private/} | tr A-Z a-z)/*.h; do
  [ -e "$h" ] && ntsrc "$h" /tmp/f/$c/$(basename "$h") && PH=1
done
DIR=/tmp/drv; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src

macro() { bash srcmacro.sh $SRC/sources $1 | sed -E "s/[\$][(][A-Za-z0-9_]+[)]//g"; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
TYPE=$(macro TARGETTYPE | tr -d " ")
case $TYPE in
DRIVER|EXPORT_DRIVER|MINIPORT) EXT=sys ENTRY="-entry:DriverEntry" ;;
GDI_DRIVER) EXT=dll ENTRY="-dll -entry:DrvEnableDriver" ;;
HAL) EXT=dll ENTRY="-dll -entry:HalInitSystem" ;;
*) echo "$COMP: not a driver ($TYPE)"; exit 1 ;;
esac
CDEFS="$(macro C_DEFINES) $(macro NET_C_DEFINES) $(macro NTCPPFLAGS)"
DEFS="$(ntdefines $SRC) $CDEFS"
# (RC: the defines alone, as makefile.def gives it the CDEFINES)
RCDEFS="$(ntdefines $SRC rc) $CDEFS"
# (the INCLUDES with their $(BASEDIR), which incdirs.sh resolves)
INCS="-I/tmp/f/$c -I/tmp/f/ovr -I$K/inc -I/tmp/f/priv $(bash incdirs.sh $SRC "$(bash srcmacro.sh $SRC/sources INCLUDES)") -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
srcs=$(bash srclist.sh $COMP $ROOT)
export RELINC=$(bash relinc.sh $INCS $(realpath -m $srcs)); INCS="$INCS $RELINC"

arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm
# the ARM sources of its list (beside it, else made by MAKEFILE.INC: arm\systable.asm
# of W32\KMODE, among the files the component made) and the macros of the list
armsrcs() {
  local l
  [ -f $arm ] || return 0
  grep -v "^#" $arm | grep -v "=" | while read -r l; do
    [ -n "$l" ] || continue
    case $l in /*) echo $l ;; *) if [ -f $K/$l ]; then echo $K/$l; else echo /tmp/f/gen/${SRC#/work/private/}/$l; fi ;; esac
  done
}
armmacro() { [ -f $arm ] && sed -n "s/^$1=//p" $arm; }
bash nttarget.sh $SRC pre $O || exit 1
g=$(armsrcs | grep "^/tmp/f/gen/" | sed "s|^/tmp/f/gen/${SRC#/work/private/}/||")
[ -z "$g" ] || bash nttarget.sh $SRC src $O $g || exit 1

DEF=""
if [ $TYPE = EXPORT_DRIVER ] || [ $TYPE = HAL ]; then
  # the exports: the .src preprocessed into the DLLDEF under obj (<name>.src, or the
  # name of the DLLDEF: class.src of class2; beside the SOURCES or above them, as the
  # inference rules of makefile.def find it: W32\KMODE of its DAYTONA build; the port
  # version first), or the DLLDEF of the SOURCES as it is (makefile.def: <name>.def)
  d=$(macro DLLDEF | tr -d " " | tr "\\\\" /); [ -n "$d" ] || d=$NAME.def
  s=
  for p in $SRC $(dirname $SRC); do
    for x in $NAME.src $(case $d in [Oo][Bb][Jj]/*) basename "${d%.*}.src" ;; esac); do
      [ -f $(bash portdir.sh $p)/$x ] && { s=$(bash portdir.sh $p)/$x; break 2; }
      x=$(ls $p | grep -ix "$x") && { s=$p/$x; break 2; }
    done
  done
  if [ -n "$s" ]; then
    tr -d "\032\r" < $s > $DIR/$NAME.src
    cl -nologo -EP $DEFS $DIR/$NAME.src > $DIR/$NAME.def 2>/dev/null
  else
    # (relative to the SOURCES: ..\tcpip.def of tdi\tcpip\tcp\mp)
    s=$(dirname $d)/$(ls $SRC/$(dirname $d) | grep -ix "$(basename $d)") || { echo "   no $d"; exit 1; }
    tr -d "\032\r" < $SRC/$s | sed -f deffix.sed > $DIR/$NAME.def
  fi
  grep -qi "^ *EXPORTS" $DIR/$NAME.def || { echo "   no EXPORTS in $NAME.def"; exit 1; }
  DEF="-def:$DIR/$NAME.def"
fi
# With IMPLIB=1 it writes the import library of the exports alone, as the library pass of
# makefile.def does: two export drivers that import from each other (setupdd.sys, spddlang.sys).
if [ "$IMPLIB" = 1 ]; then
  [ -n "$DEF" ] || { echo "$COMP: no exports"; exit 1; }
  lib -nologo -machine:arm -def:$DIR/$NAME.def -out:$DIR/$NAME.lib > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
  ntimplib $DIR/$NAME.def $DIR/$NAME.lib $O/$NAME.lib
  echo "   wrote $O/$NAME.lib (the import library of the .def)"
  exit 0
fi

echo ">> compiling $COMP ($NAME.$EXT, $TYPE)"
n=0; fail=0
# the farmed directory of a source of the tree (..\x.c of tdi\tcpip\tcp\mp, or the port
# version of one), where its quoted includes are found first, ahead of the include
# directories of the port: those of the component too (make-lib.sh)
srcinc() { local s=$(realpath $(dirname $1)); case $s in /work/RASPI/ported/*) s=/work/private/${s#/work/RASPI/ported/} ;; /work/RASPI/apps/*) s=/work/private/${s#/work/RASPI/apps/} ;; esac; case $s in /work/private/*) bash incdirs.sh $s "" | cut -d" " -f2 ;; esac; }
# one source, compiled by the pool of msvc.sh (par, pwait)
one() {
  local f=$1 b=$(basename "${1%.*}" | tr A-Z a-z)
  case $f in
  *.asm) ntas $DIR/obj/${b}_asm.obj $f $DEFS -I/work/RASPI/obj/gen $INCS ;;
  # a quoted include finds the header beside the source (#line): with port versions of
  # headers, the farmed ones
  *) ntsrc $f $DIR/src/$b.c && { [ -z "$PH" ] || sed -i "1s|.*/|#line 1 \"/tmp/f/$c/|" $DIR/src/$b.c; } &&
     ntcc $DIR/obj/$b.obj $DIR/src/$b.c -kernel $DEFS $(srcinc $f) $INCS ;;
  esac
}
for f in $srcs $(armsrcs); do par one $f; done
pwait; n=$PAROK; fail=$PARFAIL
echo "   $n compiled, $fail failed"
[ $fail -eq 0 ] || exit 1

# The import libraries of ntoskrnl, the HAL (of a display driver, win32k.sys) and the
# export drivers of TARGETLIBS, and the LIBRARY components of LINKLIBS and TARGETLIBS
# (tdi\tcpip\tcp: ip.lib of tdi\tcpip\ip). One of user mode comes from obj/um, as public\sdk\lib
# held both (ntdll.lib of asyncmac.sys, after ntoskrnl.lib: it resolves nothing). An object is
# the one its directory made (libpath.sh: rc4c.obj of the LSA crypt engine, of ndiswan.sys).
case $TYPE in GDI_DRIVER) LIBS=$O/win32k.lib ;; HAL) LIBS=; [ -n "$(macro NOTARGETLIB)" ] || LIBS=$O/ntoskrnl.lib ;; *) LIBS="$O/ntoskrnl.lib $O/hal.lib" ;; esac
set -f
for l in $(macro LINKLIBS) $(macro TARGETLIBS); do
  case $l in *.[Oo][Bb][Jj])
    a=$(bash libpath.sh $SRC "$l"); [ -n "$a" ] || { echo "   missing object $l (build its directory first)"; exit 1; }
    LIBS="$LIBS $a"; continue ;;
  esac
  b=$(basename "$(echo "$l" | tr "\\\\" / | tr A-Z a-z)" .lib)
  case $b in ntoskrnl|hal) continue ;; esac
  # (that of its directory first, with the $(BASEDIR) that macro drops: the RTL of user mode,
  # \private\ntos\rtl\obj\*\rtl.lib of setupdd.sys)
  case $l in \\*) a=$(bash libpath.sh $SRC "\$(BASEDIR)$l") ;; *) a=$(bash libpath.sh $SRC "$l") ;; esac
  [ -n "$a" ] || a=$O/$b.lib; [ -f $a ] || a=$O/um/$b.lib
  [ -f $a ] || { echo "   missing library $b (build it first)"; exit 1; }
  LIBS="$LIBS $a"
done
set +f
RC=$(bash srclist.sh $COMP $ROOT rc 2>/dev/null | head -1)
RES=""; [ -z "$RC" ] || RES=$(bash rc.sh $SRC $RC $RCDEFS $INCS) || exit 1
F=$(echo $NAME.$EXT | tr a-z A-Z)
# (a driver has no C run-time library but with USE_LIBCNTPR, as in makefile.def: the
# routines of the run-time and the helpers of the compiler are exports of ntoskrnl)
BASE=$(armmacro DRIVERBASE)
[ $TYPE = HAL ] && { BASE=$(armmacro HALBASE); BASE=${BASE:-0x80010000}; }
CRT=; [ -z "$(macro USE_LIBCNTPR)" ] || CRT=$O/libcntpr.lib
# (-opt:ref as makefile.def links, which -debug would turn off; noicf: the link of 1996 folds nothing)
link -nologo -machine:arm -release -opt:ref,noicf -driver -subsystem:native $ENTRY -base:${BASE:-0x10000} -nodefaultlib $LINKVER \
     -debug -pdb:$O/$NAME.pdb -map:$O/$NAME.map $DEF -out:$DIR/$NAME.$EXT \
     $DIR/obj/*.obj $LIBS $CRT $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
ntver $DIR/$NAME.$EXT $(macro SUBSYSTEM_VERSION | tr -d " ") || exit 1
# The sections aligned on DRIVER_ALIGNMENT, as makefile.def links every driver (link.exe
# for ARM takes no -align: tools/pe2img lays the image out as it does). Below a page the
# image is one subsection, as win32k.sys must be to be mapped as a view of system space
# at its base (MmLoadSystemImage, MiAddMappedPtes).
ALIGN=$(macro DRIVER_ALIGNMENT | tr -d " ")
bash /work/RASPI/tools/host.sh pe2img -align ${ALIGN:-0x20} $DIR/$NAME.$EXT || exit 1
OUT=$O
if [ "$INSTALL" = 1 ]; then
  # the directory of its line in the layout of NT setup: 2 System32, 4 its Drivers (also
  # the drivers of the port, which have no line)
  case $(grep -a -i "^@[^:]*:$NAME\.$EXT *=" /work/private/windows/setup/inf/win4/inf/layout.inx | head -1 | tr -d "\r " | cut -d= -f2 | cut -d, -f8) in
  2) OUT=/work/RASPI/sdcard/root/WINNT/System32 ;;
  *) OUT=/work/RASPI/sdcard/root/WINNT/System32/Drivers ;;
  esac
fi
mkdir -p $OUT
cp $DIR/$NAME.$EXT $OUT/$F
[ $TYPE = EXPORT_DRIVER ] && ntimplib $DIR/$NAME.def $DIR/$NAME.lib $O/$NAME.lib
echo "   wrote $OUT/$F"
'
