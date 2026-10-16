#!/usr/bin/env bash
# Build an NT kernel-mode driver from its component's SOURCES (TARGETTYPE DRIVER or
# EXPORT_DRIVER) as makefile.def does, with Microsoft C for ARM: compile with the
# C_DEFINES of the SOURCES against the generated driver headers, link with the
# import libraries of ntoskrnl, the HAL and the TARGETLIBS (an export driver's or a
# LIBRARY's obj/<name>.lib) at DRIVERBASE, entry DriverEntry, native subsystem. An
# EXPORT_DRIVER's exports come from <name>.src preprocessed as its MAKEFILE.INC
# does; its import library is written to obj/<name>.lib for its clients.
#
# Usage:  cd RASPI/arcfw/kernel && ./make-driver.sh FASTFAT         # PRIVATE/NTOS/FASTFAT
#         INSTALL=1 ./make-driver.sh DD/SCSIPORT                     # also install
#         ROOT=/work/RASPI/arcfw/drivers ./make-driver.sh BCMEMMC    # a component outside PRIVATE/NTOS
# Needs obj/ntoskrnl.lib from make-execlink.sh and obj/hal.lib from make-hal.sh.
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
COMP=${1:?usage: make-driver.sh COMPONENT}
[ -f "$NT35/RASPI/obj/ntoskrnl.lib" ] || { echo "run make-execlink.sh first"; exit 1; }
[ -f "$NT35/RASPI/obj/hal.lib" ] || { echo "run make-hal.sh first"; exit 1; }

docker run --rm -e COMP="$COMP" -e ROOT="${ROOT:-/work/PRIVATE/NTOS}" -e INSTALL="${INSTALL:-0}" \
    -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel --entrypoint bash nt-msvc:latest -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
SRC=$ROOT/$COMP
c=$(echo $COMP | tr A-Z a-z)
SUBSYS=$COMP bash mkfarm.sh /tmp/f
if [ $ROOT != /work/PRIVATE/NTOS ]; then
  mkdir -p /tmp/f/$c
  for h in $SRC/*.[Hh]; do
    [ -e "$h" ] && ntsrc "$h" /tmp/f/$c/$(basename "$h" | tr A-Z a-z)
  done
fi
DIR=/tmp/drv; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src

macro() { bash srcmacro.sh $SRC/SOURCES $1 | sed -E "s/[\$][(][A-Za-z0-9_]+[)]//g"; }
NAME=$(macro TARGETNAME | tr -d " " | tr A-Z a-z)
TYPE=$(macro TARGETTYPE | tr -d " ")
case $TYPE in DRIVER|EXPORT_DRIVER|MINIPORT) ;; *) echo "$COMP: not a driver ($TYPE)"; exit 1 ;; esac
DEFS="$NTDEFINES $(macro C_DEFINES)"
INCS="-I/tmp/f/$c -I/tmp/f/ovr -I$K/inc -I/tmp/f/priv -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"

echo ">> compiling $COMP ($NAME.sys, $TYPE)"
n=0; fail=0
arm=$K/$(echo $COMP | tr / _ | tr A-Z a-z).arm
for f in $(bash srclist.sh $COMP $ROOT) $( [ -f $arm ] && grep -v "^#" $arm | sed "s|^[^/]|$K/&|" ); do
  b=$(basename "${f%.[Cc]}" | tr A-Z a-z)
  ntsrc $f $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c -kernel $DEFS $INCS && n=$((n+1)) || fail=$((fail+1))
done
echo "   $n compiled, $fail failed"
[ $fail -eq 0 ] || exit 1

# The import libraries of ntoskrnl, the HAL and the export drivers of TARGETLIBS,
# and the LIBRARY components of TARGETLIBS.
LIBS="$O/ntoskrnl.lib $O/hal.lib"
set -f
for l in $(macro TARGETLIBS); do
  b=$(basename "$(echo "$l" | tr "\\\\" /)" .lib | tr A-Z a-z)
  case $b in ntoskrnl|hal) continue ;; esac
  [ -f $O/$b.lib ] || { echo "   missing library $b (build it first)"; exit 1; }
  LIBS="$LIBS $O/$b.lib"
done
set +f

DEF=""
if [ $TYPE = EXPORT_DRIVER ]; then
  s=$(ls $SRC | grep -ix "$NAME.src")
  tr -d "\032\r" < $SRC/$s > $DIR/$NAME.src
  cl -nologo -EP -D_ARM_ $DIR/$NAME.src > $DIR/$NAME.def 2>/dev/null
  DEF="-def:$DIR/$NAME.def"
fi

RC=$(bash srclist.sh $COMP $ROOT rc 2>/dev/null | head -1)
RES=""; [ -z "$RC" ] || RES=$(bash rc.sh $SRC $RC $DEFS $INCS) || exit 1
F=$(echo $NAME | tr a-z A-Z).SYS
link -nologo -machine:arm -release -driver -subsystem:native -entry:DriverEntry -base:0x10000 -nodefaultlib \
     -debug -pdb:$O/$NAME.pdb -map:$O/$NAME.map $DEF -out:$DIR/$NAME.sys \
     $DIR/obj/*.obj $LIBS $O/libcntpr.lib $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/WINNT/System32/Drivers
mkdir -p $OUT
cp $DIR/$NAME.sys $OUT/$F
[ $TYPE = EXPORT_DRIVER ] && cp $DIR/$NAME.lib $O/$NAME.lib
echo "   wrote $OUT/$F"
'
