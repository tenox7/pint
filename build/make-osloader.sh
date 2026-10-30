#!/usr/bin/env bash
# Build OSLOADER.EXE, the ARC program the firmware starts to load NT, as
# boot\bldr\mipsldr.rsp links the MIPS one: the loader (obj/bldr.lib: osloader.c
# regboot.c), the boot library (obj/boot.lib, with its ARM sources), the boot
# configuration manager (obj/bconfig.lib), the kernel's configuration routines
# (ke\config.c, kernldat.c), the boot RTL (obj/bldrrtl.lib) and the C run-time
# (obj/libcntpr.lib), fixed at 0x80600000 with entry BlOsLoader and the msg.usa
# message table, made a ROM image (link -rom, as tools/pe2img -rom makes it).
#
# Usage:  cd RASPI/build && ./make-lib.sh rtl/boot && ./make-lib.sh boot/lib &&
#         ./make-lib.sh config/boot && ./make-lib.sh boot/bldr && INSTALL=1 ./make-osloader.sh
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"

docker run --rm -e INSTALL="${INSTALL:-0}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
SUBSYS="ke boot" bash mkfarm.sh /tmp/f
bash mcinc.sh /work/private/ntos/boot/bldr
DIR=/tmp/osl; rm -rf $DIR; mkdir -p $DIR
INCS="-I$K/inc -I/tmp/f/ovr -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
DEFS="$NTDEFINES -D_NTSYSTEM_"
for b in config kernldat; do
  ntsrc /work/private/ntos/ke/$b.c $DIR/$b.c && ntcc $DIR/$b.obj $DIR/$b.c -kernel $DEFS $INCS || exit 1
done
RES=$(bash rc.sh /work/private/ntos/boot/bldr /work/private/ntos/boot/bldr/osloader.rc $DEFS $INCS) || exit 1
link -nologo -machine:arm -subsystem:native -entry:BlOsLoader -base:0x80600000 \
     -nodefaultlib -debug -pdb:$O/osloader.pdb -map:$O/osloader.map -out:$O/osloader.pe $DIR/config.obj $DIR/kernldat.obj \
     $O/bldr.lib $O/boot.lib $O/bconfig.lib $O/bldrrtl.lib $O/libcntpr.lib $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|warning LNK4" $DIR/link.err | head -60; exit 1; }
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/os/winnt40 && mkdir -p $OUT
bash /work/RASPI/tools/host.sh pe2img -rom $O/osloader.pe $OUT/OSLOADER.EXE || exit 1
echo "   wrote $OUT/OSLOADER.EXE"
'
