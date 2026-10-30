#!/usr/bin/env bash
# Build HAL.DLL, the Raspberry Pi 2 HAL (TARGETTYPE HAL), with Microsoft C for ARM:
# hal/ and the shared NTHALS\DRIVESUP.C and BUSHND.C, linked with the ntoskrnl import
# library and the kernel C run-time, exporting hal.src.
#
# Usage:  cd RASPI/hal && ./make-hal.sh                 # -> obj/HAL.DLL
#         INSTALL=1 ./make-hal.sh                       # -> sdcard/root/os/winnt40/HAL.DLL (system partition)
# Needs obj/ntoskrnl.lib (make-execlink.sh) and obj/libcntpr.lib (../crt/make-crt.sh).
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
[ -f "$NT40/RASPI/obj/ntoskrnl.lib" ] || { echo "run make-execlink.sh first"; exit 1; }

docker run --rm -e CJ="${CJ:-4}" -e INSTALL="${INSTALL:-0}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/hal \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/build/msvc.sh
O=/work/RASPI/obj
SUBSYS=" " bash $B/mkfarm.sh /tmp/f
DIR=/tmp/hal; rm -rf $DIR; mkdir -p $DIR/obj $DIR/src
DEFS="$NTDEFINES"
INCS="-I/work/RASPI/hal -I/tmp/f/ovr -I/tmp/f/priv -I$K/inc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt -I$O/gen"

echo ">> compiling the HAL"
n=0; fail=0
for f in *.c /work/private/ntos/nthals/drivesup.c /work/private/ntos/nthals/bushnd.c; do
  b=$(basename "${f%.*}" | tr A-Z a-z)
  ntsrc $f $DIR/src/$b.c && ntcc $DIR/obj/$b.obj $DIR/src/$b.c -kernel $DEFS $INCS && n=$((n+1)) || fail=$((fail+1))
done
for s in *.asm; do
  ntas $DIR/obj/$(basename ${s%.asm})_asm.obj $s $DEFS $INCS && n=$((n+1)) || fail=$((fail+1))
done
echo "   $n compiled, $fail failed"
[ $fail -eq 0 ] || exit 1

tr -d "\032\r" < hal.src > $DIR/hal.src
cl -nologo -EP -D_ARM_ $DIR/hal.src > $DIR/hal.def 2>/dev/null
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/os/winnt40 && mkdir -p $OUT
link -nologo -machine:arm -release -opt:ref,noicf -dll -noentry -subsystem:native -base:0x81800000 -nodefaultlib $LINKVER \
     -debug -pdb:$O/hal.pdb -map:$O/hal.map -def:$DIR/hal.def -out:$DIR/hal.dll \
     $DIR/obj/*.obj $O/ntoskrnl.lib $O/libcntpr.lib > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
ntver $DIR/hal.dll || exit 1
cp $DIR/hal.dll $OUT/HAL.DLL
ntimplib $DIR/hal.def $DIR/hal.lib $O/hal.lib
echo "   wrote $OUT/HAL.DLL"
'
