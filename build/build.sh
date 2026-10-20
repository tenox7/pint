#!/usr/bin/env bash
# Build the ARC firmware with Microsoft C for ARM: the firmware (arcfw/arm) linked
# with the boot library (FATBOOT, CDFSBOOT: boot.lib), the boot run-time library
# (bldrrtl.lib) and the C run-time (libcntpr.lib), laid out at 0x8000, where the
# Raspberry Pi firmware loads it: obj/arcfw.bin, and obj/arcfw.elf for QEMU.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

docker run --rm -e CJ="${CJ:-4}" -v "$ROOT/../msvc":/opt/msvc -v "$ROOT/..":/work -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/arcfw/kernel/msvc.sh
A=/work/RASPI/arcfw
O=/work/RASPI/obj
bash $K/mkfarm.sh /tmp/f > /dev/null
DIR=/tmp/fw; rm -rf $DIR; mkdir -p $DIR
INCS="-I$A/arm -I$A/drivers/bcmeth -I$K/inc -I/tmp/f/ovr $(bash $K/incdirs.sh /work/PRIVATE/NTOS/BOOT/LIB "..\inc;..\..\inc;..\..\fastfat;..\..\pinball;..\..\cdfs;..\..\ntfs;..\..\config") -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
DEFS="$NTDEFINES -D_NTSYSTEM_"
fail=0
for s in main init mmu mailbox pcie jxvendor jxmemory fwio fwprint jxserial jxdisp jxkbd usb sdcard config fwload \
         jxenvir jxboot fwtime jxsysid jxreboot fwusa arcdos_rt; do
  par ntcc $DIR/$s.obj $A/arm/$s.c $DEFS $INCS
done
# the USB host and its xHCI, shared with the keyboard and pointer port driver
ntcc $DIR/usbhost.obj $A/drivers/bcmusb/usbhost.c $DEFS -DUSB_FIRMWARE $INCS || fail=1
ntcc $DIR/xhci.obj $A/drivers/bcmusb/xhci.c $DEFS -DUSB_FIRMWARE $INCS || fail=1
# the VGA and 8514 OEM fonts of the display (the Jazz vgaoem.obj and 8514oem.obj)
perl ../tools/fon2fw.pl FwFont8x12 /work/PRIVATE/WINDOWS/GDI/FONTS/VGAOEM.FON \
     FwFont10x20 /work/PRIVATE/WINDOWS/GDI/FONTS/8514OEM.FON > $DIR/fwfonts.c
ntcc $DIR/fwfonts.obj $DIR/fwfonts.c $DEFS $INCS || fail=1
# arcdos is written to the ARC API of the ADK: its own headers only
ntcc $DIR/arcdos.obj $A/ported/arcdos.c -D_ARM_ -I$A/arcdos-inc || fail=1
for s in start fwtrap; do ntas $DIR/$s.obj $A/arm/$s.asm -I$O/gen || fail=1; done
pwait; [ $PARFAIL -eq 0 ] || fail=1
[ $fail = 0 ] || exit 1
link -nologo -machine:arm -subsystem:native -entry:FwStart -base:0x10000 -fixed:no \
     -nodefaultlib -debug -pdb:$O/arcfw.pdb -map:$O/arcfw.map -out:$O/arcfw.exe $DIR/start.obj $(ls $DIR/*.obj | grep -v /start.obj) \
     $O/boot.lib $O/bldrrtl.lib $O/libcntpr.lib > $DIR/link.err 2>&1 || { cat $DIR/link.err; exit 1; }
bash /work/RASPI/tools/host.sh pe2img $O/arcfw.exe 0x8000 $O/arcfw.bin $O/arcfw.elf
'
