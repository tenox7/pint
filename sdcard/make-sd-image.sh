#!/usr/bin/env bash
# Build the system disk: MBR + one FAT16 partition holding the Pi GPU firmware,
# config.txt, the ARC firmware (arcfw.bin, the kernel= image) and the NT tree in
# sdcard/root (\os\nt: OSLOADER and the HAL; \WINNT\...) with the registry hives of
# obj/gen/hives and the NLS tables and fonts of the NT tree where NT 4.0 Setup puts them
# (\WINNT\System32, \WINNT\System32\config, \WINNT\Fonts), the hive of the Default User profile,
# and the setup aid of a developer install for the first boot (ported/oak/registry.ini, the
# program groups in \WINNT\IDW).
# FAT16 because both the GPU firmware and NT read it.
# Flash it for a real Pi 2, 3 or 4, or attach it in QEMU with -drive if=sd. With -u the disk of
# the last build is updated in place, keeping what NT wrote on it: the files built since
# go in (a new disk when the hives or this script changed).
#
# Usage:  cd RASPI/sdcard && ./make-sd-image.sh [-u]           # -> ../obj/nt-arcfw-sd.img
# Flash:  sudo dd if=../obj/nt-arcfw-sd.img of=/dev/rdiskN bs=4m   (diskutil list)
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LOADER="../obj/arcfw.bin"
IMG="../obj/nt-arcfw-sd.img"
STAMP="../obj/stamp/sd-image"
FWCACHE="firmware"
SIZE_MB=256
ROOT="$(cd .. && pwd)"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1"; exit 1; }; }
need docker
[[ -f "$LOADER" ]] || { echo "missing $LOADER - build it first: (cd ../build && ./build.sh)"; exit 1; }
[[ -f ../obj/gen/hives/SYSTEM ]] || { echo "missing the hives - build them first: (cd ../tools/hive && ./make-hives.sh)"; exit 1; }
mkdir -p ../obj/stamp

mkdir -p "$FWCACHE"
base="https://raw.githubusercontent.com/raspberrypi/firmware/stable/boot"
for f in bootcode.bin start.elf fixup.dat bcm2709-rpi-2-b.dtb start4.elf fixup4.dat; do
  [[ -s "$FWCACHE/$f" ]] || { echo ">> fetching $f"; curl -fsSL -o "$FWCACHE/$f" "$base/$f"; }
done

if [[ "${1:-}" = -u && -f "$IMG" && -f "$STAMP" && -z "$(find ../obj/gen/hives ../ported/oak make-sd-image.sh -newer "$STAMP" -print -quit)" ]]; then
  new=$(cd .. && find sdcard/root sdcard/config.txt sdcard/firmware obj/arcfw.bin -type f -newer obj/stamp/sd-image)
  echo ">> updating $IMG: $(echo $new | wc -w | tr -d ' ') files"
  [[ -z "$new" ]] || docker run --rm -v "$ROOT":/work -w /work --entrypoint bash tenox7/msvc-arm32 -c '
set -e
I=obj/nt-arcfw-sd.img@@1M
for f in "$@"; do
  case $f in sdcard/root/*) d=${f#sdcard/root/} ;; *) d=${f##*/} ;; esac
  p=; for c in $(dirname $d | tr / " "); do [ $c = . ] || { p=$p/$c; mmd -D s -i $I ::$p 2>/dev/null || true; }; done
  mcopy -D o -i $I $f ::/$d
  echo "   $d"
done' _ $new
  touch "$STAMP"
  exit 0
fi

echo ">> assembling $IMG (MBR + FAT16: firmware, config.txt, arcfw.bin, root/)"
docker run --rm -v "$ROOT":/work -v "$ROOT/../private":/nt/private:ro -v "$ROOT/../public":/nt/public:ro -w /work/sdcard -e IMG="$IMG" -e SIZE_MB="$SIZE_MB" \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -e
rm -rf /tmp/sd && mkdir -p /tmp/sd
cp firmware/bootcode.bin firmware/start.elf firmware/fixup.dat firmware/bcm2709-rpi-2-b.dtb firmware/start4.elf firmware/fixup4.dat /tmp/sd/
cp config.txt ../obj/arcfw.bin /tmp/sd/
cp -r root/* /tmp/sd/
mkdir -p /tmp/sd/WINNT/System32/config /tmp/sd/WINNT/Fonts /tmp/sd/TMP
# the NLS tables of text setup ([nls] of TXTSETUP.SIF, USA) and of the base
N=/nt/private/windows/winnls/data
cp $N/c_1252.nls $N/c_437.nls $N/c_850.nls $N/c_10000.nls $N/l_intl.nls $N/ctype.nls $N/locale.nls $N/sortkey.nls \
   $N/sorttbls.nls $N/unicode.nls /tmp/sd/WINNT/System32/
# the fonts: the VGA resolution set (vgaoem.fon is the OEMHAL font OSLOADER loads from \WINNT\Fonts),
# the TrueType fonts with Lucida Console and Marlett (the window frame glyphs), the DOS window fonts
F=/nt/private/ntos/w32/ntgdi/fonts
for f in vgasys vgafix vgaoem vga850 vga860 vga861 vga863 vga865 sserife serife coure smalle smallf symbole symbolf \
         roman script modern app850 dosapp cga40850 cga40woa cga80850 cga80woa ega40850 ega40woa ega80850 ega80woa \
         8514fix 8514oem 8514sys; do cp $F/$f.fon /tmp/sd/WINNT/Fonts/; done
for f in arial arialbd arialbi ariali cour courbd courbi couri times timesbd timesbi timesi symbol wingding lucon marlett \
         l_10646; do cp $F/$f.ttf /tmp/sd/WINNT/Fonts/; done
# and desktop.ini of layout.inx (fontext bld\desktop.inx, with the CR LF of the CD), whose UICLSID
# makes Explorer show \WINNT\Fonts as the Fonts folder of fontext.dll
sed "s/\$/\r/" /nt/private/windows/shell/fontfldr/fontext/bld/desktop.inx > /tmp/sd/WINNT/Fonts/desktop.ini
cp ../obj/gen/hives/SYSTEM ../obj/gen/hives/SOFTWARE ../obj/gen/hives/DEFAULT ../obj/gen/hives/USERDIFF /tmp/sd/WINNT/System32/config/
# the counter names and explain texts of the performance counters, which the build of perfmon
# places from sdktools\cntrtext\perfini (perfmon\makefile.inc)
cp /nt/private/sdktools/cntrtext/perfini/perfc009.dat /nt/private/sdktools/cntrtext/perfini/perfh009.dat /tmp/sd/WINNT/System32/
# the separator pages of the spooler, which text setup copies to System32 (layout.inx)
S=/nt/private/windows/spooler/localspl
cp $S/pcl.sep $S/pscript.sep $S/sysprint.sep /tmp/sd/WINNT/System32/
# and the patches of the synthesizer (synth.dll) and the MIDI mapper configuration; the sounds
# in \WINNT\Media and the clock video in \WINNT
M=/nt/private/windows/media
cp $M/synth/synth.pat $M/midimap/midimap.cfg /tmp/sd/WINNT/System32/
mkdir -p /tmp/sd/WINNT/Media
cp $M/sounds/*.wav $M/sounds/*.mid /tmp/sd/WINNT/Media/
cp $M/avi/clock.avi /tmp/sd/WINNT/
# the hive of the profile of a new user, which GUI setup saves from the default hive (SaveHive
# of HKEY_USERS\.DEFAULT, syssetup.c); the profile directories are made at the first boot
mkdir -p "/tmp/sd/WINNT/Profiles/Default User"
cp ../obj/gen/hives/DEFAULT "/tmp/sd/WINNT/Profiles/Default User/NTUSER.DAT"
# the first boot: REGISTRY.INI (made from its edit script by every build) and the groups it names
cp ../ported/oak/registry.ini /tmp/sd/WINNT/REGISTRY.INI
mkdir -p /tmp/sd/WINNT/IDW
for g in main accesori games admin; do cp /nt/private/windows/shell/progman/$g.grp /tmp/sd/WINNT/IDW/; done

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
printf "label: dos\nstart=2048, type=06, bootable\n" | sfdisk "$IMG" >/dev/null
psect=$(( SIZE_MB*2048 - 2048 ))
dd if=/dev/zero of=/tmp/part.img bs=512 count="$psect" status=none
mkfs.vfat -F 16 -n NTBOOT /tmp/part.img >/dev/null
mcopy -s -i /tmp/part.img /tmp/sd/* ::
# GUI setup hides the fonts of [HiddenFontFiles] of FONT.INF, which the Fonts folder does not show
for f in $(tr -d "\r" < /nt/private/windows/setup/inf/win4/inf/font.inx | sed -n "/^\[HiddenFontFiles\]/,/^\[/p" | grep -a "^[a-z0-9_]*\.[a-z]*$"); do
  [ ! -f /tmp/sd/WINNT/Fonts/$f ] || mattrib -i /tmp/part.img +h ::/WINNT/Fonts/$f
done
# and makes the Fonts directory read-only and system, for Explorer to show it as the Fonts folder
mattrib -i /tmp/part.img +r +s ::/WINNT/Fonts
dd if=/tmp/part.img of="$IMG" bs=512 seek=2048 conv=notrunc status=none

echo "--- partition table ---"; sfdisk -l "$IMG"
echo "--- FAT contents ---"; mdir -i /tmp/part.img ::
'
touch "$STAMP"
echo ">> built $IMG ($(du -h "$IMG" | cut -f1))."
