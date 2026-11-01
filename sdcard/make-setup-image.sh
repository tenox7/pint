#!/usr/bin/env bash
# Build the setup disk: an SD card that installs Windows NT 4.0 as its CD installs it on an
# ARC machine. MBR + one FAT16 partition with the Pi GPU firmware, config.txt, the ARC firmware
# (arcfw.bin), the tag file of the CD (\cdrom_w.40, the tree's bom\tagfile.tag) and the setup
# source in \arm (setupfiles.pl): SETUPLDR, TXTSETUP.SIF and the files of LAYOUT.INF, the
# hives of a retail setup with the services of the Pi's drivers (obj/gen/hives/SETUP, from
# tools/hive/make-hives.sh SETUP) and SETUPREG.HIV among them; LAYOUT.INF gets the file sizes (bom\infsize).
# In the firmware, "Run a program" arm\setupldr starts text setup. Its source is this partition
# (txtsetup.sif SetupSourceDevice = \device\harddisk0\partition1); installed on it, in \WINNT,
# the system boots with the boot selection text setup adds to the environment of the firmware,
# which the card keeps (arcenv.pl: the system partition and the search path, no boot selection).
#
# Usage:  cd RASPI/sdcard && ./make-setup-image.sh      # -> ../obj/nt-setup-sd.img
# Needs the build of the system disk (../build/build-update.sh) and the INFs (../tools/inf/make-infs.sh).
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMG="../obj/nt-setup-sd.img"
FWCACHE="firmware"
SIZE_MB=${SIZE_MB:-1024}
ROOT="$(cd .. && pwd)"
G=obj/gen/tree/windows/setup/inf/win4/inf/daytona/wks

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1"; exit 1; }; }
need docker
for f in ../obj/arcfw.bin ../obj/SETUPLDR ../obj/USETUP.EXE ../obj/SETUPDD.SYS ../$G/txtsetup.sif \
         ../obj/gen/hives/SETUP/SYSTEM ../obj/gen/hives/SETUPREG.HIV ../obj/inf/host/INFSIZE.EXE; do
  [[ -f "$f" ]] || { echo "missing $f - build it first"; exit 1; }
done

mkdir -p "$FWCACHE"
base="https://raw.githubusercontent.com/raspberrypi/firmware/stable/boot"
for f in bootcode.bin start.elf fixup.dat bcm2709-rpi-2-b.dtb start4.elf fixup4.dat; do
  [[ -s "$FWCACHE/$f" ]] || { echo ">> fetching $f"; curl -fsSL -o "$FWCACHE/$f" "$base/$f"; }
done

echo ">> assembling $IMG (MBR + FAT16: firmware, config.txt, arcfw.bin, \\cdrom_w.40, \\arm)"
docker run --rm -v "$ROOT":/work -v "$ROOT/../private":/nt/private:ro -v "$ROOT/../public":/nt/public:ro -w /work/sdcard -e IMG="$IMG" -e SIZE_MB="$SIZE_MB" \
    -e G="/work/$G" --entrypoint bash tenox7/msvc-arm32 -c '
set -e
rm -rf /tmp/sd /tmp/gen && mkdir -p /tmp/sd /tmp/gen
cp firmware/bootcode.bin firmware/start.elf firmware/fixup.dat firmware/bcm2709-rpi-2-b.dtb firmware/start4.elf firmware/fixup4.dat /tmp/sd/
cp config.txt ../obj/arcfw.bin /tmp/sd/
cp /nt/private/windows/setup/bom/tagfile.tag /tmp/sd/cdrom_w.40
# desktop.ini of the Fonts folder, as the CD has it (fontext bld\desktop.inx with CR LF)
sed "s/\$/\r/" /nt/private/windows/shell/fontfldr/fontext/bld/desktop.inx > /tmp/gen/desktop.ini
perl setupfiles.pl $G/layout.inf $G/txtsetup.sif desktop.ini=/tmp/gen/desktop.ini > /tmp/files.txt 2> /tmp/missing.txt ||
  { grep -v "^missing [^(]*$" /tmp/missing.txt || true; exit 1; }
n=0
while read -r s d; do mkdir -p /tmp/sd/$(dirname $d); cp $s /tmp/sd/$d; n=$((n+1)); done < /tmp/files.txt
echo "   $n files in \\arm, $(grep -c "^missing" /tmp/missing.txt) files of layout.inf not on the disk (obj/setup-missing.txt)"
sed "s/^missing //" /tmp/missing.txt > ../obj/setup-missing.txt
# the file sizes of layout.inf, as the release put them in (bom\infsize over the flat shares: \arm is the
# local one and that of ARM; no share of another machine here, so its sizes read 0); log: obj/infsize.log
mkdir -p /tmp/ir/RASPI/obj && ln -sfn /work/obj/inf/host /tmp/ir/RASPI/obj/host
NTROOT=/tmp/ir bash /work/tools/host.sh infsize /tmp/infsize.log /tmp/sd/arm /tmp/no /tmp/no /tmp/no /tmp/no /tmp/sd/arm > /dev/null
rm -f /tmp/sd/arm/LAYOUT.TMP; tr -d "\r" < /tmp/infsize.log > ../obj/infsize.log

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
printf "label: dos\nstart=2048, type=06, bootable\n" | sfdisk "$IMG" >/dev/null
psect=$(( SIZE_MB*2048 - 2048 ))
dd if=/dev/zero of=/tmp/part.img bs=512 count="$psect" status=none
mkfs.vfat -F 16 -n NTSETUP /tmp/part.img >/dev/null
mcopy -s -i /tmp/part.img /tmp/sd/* ::
dd if=/tmp/part.img of="$IMG" bs=512 seek=2048 conv=notrunc status=none
perl arcenv.pl "$IMG" "SYSTEMPARTITION=scsi(0)disk(0)rdisk(0)partition(1)" "FWSEARCHPATH=scsi(0)disk(0)rdisk(0)partition(1)\\"

echo "--- partition table ---"; sfdisk -l "$IMG"
echo "--- FAT contents ---"; mdir -i /tmp/part.img ::; minfo -i /tmp/part.img :: | grep -i "cluster size\|sectors per cluster" || true
'
echo ">> built $IMG ($(du -h "$IMG" | cut -f1))."
