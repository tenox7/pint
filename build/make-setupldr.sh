#!/usr/bin/env bash
# Build SETUPLDR, the ARC program that starts NT Setup, as boot\setup\mipsldr.rsp links
# the MIPS one: the setup loader (obj/setupldr.lib: setup.c, arcdisp.c, arcdtect.c,
# config.c, oemdisk.c, parseini.c), the boot library (obj/boot.lib, with its ARM
# sources), the kernel's configuration routines (ke\config.c, kernldat.c), the boot
# RTL (obj/bldrrtl.lib) and the C run-time (obj/libcntpr.lib), fixed at 0x80600000 with
# entry SlInit and the msgs.usa message table, made a ROM image (link -rom, as
# tools/pe2img -rom makes it). It goes in the setup source, next to txtsetup.sif.
#
# Usage:  cd RASPI/build && ./make-lib.sh boot/setup && ./make-setupldr.sh   # -> obj/SETUPLDR
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"

docker run --rm -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
SUBSYS="ke boot" bash mkfarm.sh /tmp/f
DIR=/tmp/sldr; rm -rf $DIR; mkdir -p $DIR
INCS="-I$K/inc -I/tmp/f/ovr -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
DEFS="$NTDEFINES -D_NTSYSTEM_"
for b in config kernldat; do
  ntsrc /work/private/ntos/ke/$b.c $DIR/$b.c && ntcc $DIR/$b.obj $DIR/$b.c -kernel $DEFS $INCS || exit 1
done
L=$O/dir/private/ntos/boot/setup
link -nologo -machine:arm -subsystem:native -entry:SlInit -base:0x80600000 \
     -nodefaultlib -debug -pdb:$O/setupldr.pdb -map:$O/setupldr.map -out:$O/setupldr.pe $DIR/config.obj $DIR/kernldat.obj \
     $L/setupldr.lib $L/setupldr.res $O/boot.lib $O/bldrrtl.lib $O/libcntpr.lib > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|warning LNK4" $DIR/link.err | head -60; exit 1; }
bash /work/RASPI/tools/host.sh pe2img -rom $O/setupldr.pe $O/SETUPLDR || exit 1
echo "   wrote $O/SETUPLDR"
'
