#!/usr/bin/env bash
# Build the system disk: MBR + one FAT16 partition holding the Pi GPU firmware,
# config.txt, the ARC firmware (arcfw.bin, the kernel= image) and the NT tree in
# sdcard/root (\WINNT\...) with the registry hives of obj/gen/hives, the NLS tables and fonts
# of the NT tree, the help files of the programs and the setup aid of a developer install
# (ported/oak/registry.ini, the program groups in \WINNT\IDW, the games of apps/games in \GAMES
# in the Games group and the VNC viewer and Internet Explorer in \APPS in Main: tools/grp.pl). FAT16 because both
# the GPU firmware and NT 3.5 read it.
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
docker run --rm -v "$ROOT":/work -v "$ROOT/../PRIVATE":/nt/PRIVATE:ro -v "$ROOT/../PUBLIC":/nt/PUBLIC:ro -w /work/sdcard -e IMG="$IMG" -e SIZE_MB="$SIZE_MB" \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -e
rm -rf /tmp/sd && mkdir -p /tmp/sd
cp firmware/bootcode.bin firmware/start.elf firmware/fixup.dat firmware/bcm2709-rpi-2-b.dtb firmware/start4.elf firmware/fixup4.dat /tmp/sd/
cp config.txt ../obj/arcfw.bin /tmp/sd/
cp -r root/* /tmp/sd/
mkdir -p /tmp/sd/WINNT/System /tmp/sd/WINNT/System32 /tmp/sd/TMP
cp /nt/PRIVATE/WINDOWS/WINNLS/DATA/{C_1252,C_437,CTYPE,L_INTL,LOCALE,SORTKEY,SORTTBLS,UNICODE}.NLS /tmp/sd/WINNT/System32/
# the fonts Setup copies to \system (TXTSETUP.SIF directory 5): the VGA resolution set the registry names,
# the TrueType fonts with Lucida Console, the DOS window fonts
F=/nt/PRIVATE/WINDOWS/GDI/FONTS
for f in VGASYS VGAFIX VGAOEM VGA850 VGA860 VGA861 VGA863 VGA865 SSERIFE SERIFE COURE SMALLE SMALLF SYMBOLE SYMBOLF \
         ROMAN SCRIPT MODERN APP850 DOSAPP CGA40850 CGA40WOA CGA80850 CGA80WOA EGA40850 EGA40WOA EGA80850 EGA80WOA \
         8514FIX 8514OEM 8514SYS; do cp $F/$f.FON /tmp/sd/WINNT/System/; done
for f in ARIAL ARIALBD ARIALBI ARIALI COUR COURBD COURBI COURI TIMES TIMESBD TIMESBI TIMESI SYMBOL WINGDING LUCON; do
  cp $F/$f.TTF $F/$f.FOT /tmp/sd/WINNT/System/
done
cp $F/L_10646.TTF /tmp/sd/WINNT/System/
mkdir -p /tmp/sd/WINNT/System32/config
cp ../obj/gen/hives/SYSTEM ../obj/gen/hives/SOFTWARE ../obj/gen/hives/DEFAULT /tmp/sd/WINNT/System32/config/
perl ../tools/ntdiff.pl apply /nt ../ported/oak/registry.ini.ntdiff
cp ../ported/oak/registry.ini /tmp/sd/WINNT/REGISTRY.INI
mkdir -p /tmp/sd/WINNT/IDW && cp /nt/PRIVATE/WINDOWS/SHELL/PROGMAN/*.GRP /tmp/sd/WINNT/IDW/
# the games of apps/games (\GAMES, on the system drive: HOMEDRIVE, as winlogon sets it without a home directory)
# in the Games group
perl ../tools/grp.pl /tmp/sd/WINNT/IDW/GAMES.GRP /tmp/sd/GAMES "%HOMEDRIVE%\\GAMES" 5 2048=2048 Breakout=breakout "Bubble Breaker=bubbles" \
    "Moon Lander=lander" "Missile Command=misl" Snake=ntsnake Tankz=tankz "NT Rigue=ntrigue" RISCque=riscque Sokoban=sokoban \
    RISCoban=riscoban WiNTown=wintown
# the VNC viewer of apps/tenoxvnc, Internet Explorer of apps/ie and ntfetch of apps/ntfetch (\APPS) in the Main group
perl ../tools/grp.pl /tmp/sd/WINNT/IDW/MAIN.GRP /tmp/sd/APPS "%HOMEDRIVE%\\APPS" 6 TenoxVNC=tenoxvnc "Internet Explorer=iexplore" \
    ntfetch=ntfetch
# the help files of the programs, where setup puts them
for h in WINHELP/HLP/WINHELP SHELL/PROGMAN/PROGMAN SHELL/WINFILE/WINFILE SHELL/CONTROL/CONTROL SHELL/ACCESORY/NOTEPAD/NOTEPAD \
         SHELL/ACCESORY/CALC/CALC SHELL/ACCESORY/SPECHARS/CHARMAP SHELL/GAMES/SOL/SOL EP/WINMINE/HELP/WINMINE EP/FREECELL/FREECELL \
         SHELL/ACCESORY/CARDFILE/CARDFILE SHELL/ACCESORY/PAINTBRS/PBRUSH SHELL/ACCESORY/TERMINAL/TERMINAL \
         SHELL/ACCESORY/CALENDAR/CALENDAR SHELL/ACCESORY/CLIPBRD/CLIPBRD SHELL/ACCESORY/ACCESS/ACCESS/ACCESS35 \
         EP/CRUEL/CRUEL EP/GOLF/GOLF EP/PEGGED/PEGGED MEDIA/MPLAYER2/HELP/MPLAYER SHELL/ACCESORY/WINCHAT/WINCHAT; do
  cp /nt/PRIVATE/WINDOWS/$h.HLP /tmp/sd/WINNT/System32/
done
cp /nt/PRIVATE/UTILS/FDISK/DISKMAN.HLP /tmp/sd/WINNT/System32/WINDISK.HLP
cp /nt/PRIVATE/UTILS/REGEDIT/SRC/REGEDIT.HLP /tmp/sd/WINNT/System32/REGEDT32.HLP
# the names and explanations of the performance counters (Perflib, Performance Monitor)
cp /nt/PRIVATE/SDKTOOLS/CNTRTEXT/PERFINI/PERFC009.DAT /nt/PRIVATE/SDKTOOLS/CNTRTEXT/PERFINI/PERFH009.DAT /tmp/sd/WINNT/System32/

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
printf "label: dos\nstart=2048, type=06, bootable\n" | sfdisk "$IMG" >/dev/null
psect=$(( SIZE_MB*2048 - 2048 ))
dd if=/dev/zero of=/tmp/part.img bs=512 count="$psect" status=none
mkfs.vfat -F 16 -n NTBOOT /tmp/part.img >/dev/null
mcopy -s -i /tmp/part.img /tmp/sd/* ::
dd if=/tmp/part.img of="$IMG" bs=512 seek=2048 conv=notrunc status=none

echo "--- partition table ---"; sfdisk -l "$IMG"
echo "--- FAT contents ---"; mdir -i /tmp/part.img ::
'
touch "$STAMP"
echo ">> built $IMG ($(du -h "$IMG" | cut -f1))."
