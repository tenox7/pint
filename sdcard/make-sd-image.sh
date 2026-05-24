#!/usr/bin/env bash
# Build a flashable SD image that boots the NT 3.5 ARC loader on a real Raspberry
# Pi 2 with HDMI output. Layout: MBR + a single FAT32 partition holding the Pi GPU
# firmware, config.txt, our arcfw.bin (as kernel=arcfw.bin), and ramdisk.img.
#
# ramdisk.img is the FAT16 Arc disk image (arcfw/ramdisk/make-ramdisk.sh). config.txt's
# "initramfs ramdisk.img 0x00800000" makes the firmware stage it in RAM before entering
# the loader, exactly as an initrd is loaded for Linux - so the loader reads its files
# from RAM and needs no in-loader SD driver. The boot partition stays FAT32 because the
# loader reads the *contents* of ramdisk.img (its own FAT16), never this partition's FS.
# To use the staged image, build the loader with RAMDISK_INITRAMFS=1 (see build/Makefile);
# a default embedded-blob loader still boots here but ignores the staged copy.
#
# Modeled on /Users/tenox/VM/RPI/make-sd-image.sh (the proven U-Boot recipe), but
# boots our raw loader directly instead of U-Boot. The loader is linked at 0x8000
# (the firmware's native 32-bit load address), so no kernel_address is needed.
#
# Usage:  cd ARM32/arcfw/ramdisk && ./make-ramdisk.sh   # -> ../../obj/ramdisk.img
#         cd ARM32/build && ./build.sh RAMDISK_INITRAMFS=1  # -> ../obj/arcfw.bin
#         cd ../sdcard && ./make-sd-image.sh                # -> ../obj/nt-arcfw-sd.img
# Flash:  sudo dd if=../obj/nt-arcfw-sd.img of=/dev/rdiskN bs=4m   (diskutil list)
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LOADER="../obj/arcfw.bin"
RAMDISK="../obj/ramdisk.img"
IMG="../obj/nt-arcfw-sd.img"
FWCACHE="firmware"
SIZE_MB=64

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1"; exit 1; }; }
need docker
[[ -f "$LOADER" ]] || { echo "missing $LOADER - build it first: (cd ../build && ./build.sh RAMDISK_INITRAMFS=1)"; exit 1; }
[[ -f "$RAMDISK" ]] || { echo "missing $RAMDISK - build it first: (cd ../arcfw/ramdisk && ./make-ramdisk.sh)"; exit 1; }

# Cache the Pi firmware locally so repeated builds do not re-download.
mkdir -p "$FWCACHE"
base="https://raw.githubusercontent.com/raspberrypi/firmware/stable/boot"
for f in bootcode.bin start.elf fixup.dat bcm2709-rpi-2-b.dtb; do
  [[ -s "$FWCACHE/$f" ]] || { echo ">> fetching $f"; curl -fsSL -o "$FWCACHE/$f" "$base/$f"; }
done

echo ">> assembling $IMG (firmware + config.txt + arcfw.bin, MBR + FAT32) in Docker"
# Mount the ARM32 root so ../obj/arcfw.bin is visible inside the container; run
# from /work/sdcard so the relative paths below match the host layout.
ARM32ROOT="$(cd .. && pwd)"
docker run --rm -v "$ARM32ROOT":/work -w /work/sdcard \
  -e IMG="$IMG" -e SIZE_MB="$SIZE_MB" debian:bookworm bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq dosfstools mtools fdisk >/dev/null

rm -rf /tmp/sd && mkdir -p /tmp/sd
cp firmware/bootcode.bin firmware/start.elf firmware/fixup.dat firmware/bcm2709-rpi-2-b.dtb /tmp/sd/
cp config.txt /tmp/sd/config.txt
cp ../obj/arcfw.bin /tmp/sd/arcfw.bin
cp ../obj/ramdisk.img /tmp/sd/ramdisk.img

dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
printf "label: dos\nstart=2048, type=c, bootable\n" | sfdisk "$IMG" >/dev/null
psect=$(( SIZE_MB*2048 - 2048 ))
dd if=/dev/zero of=/tmp/part.img bs=512 count="$psect" status=none
mkfs.vfat -F 32 -n NTBOOT /tmp/part.img >/dev/null
mcopy -i /tmp/part.img /tmp/sd/* ::
dd if=/tmp/part.img of="$IMG" bs=512 seek=2048 conv=notrunc status=none

echo "--- partition table ---"; sfdisk -l "$IMG"
echo "--- FAT contents ---"; mdir -i /tmp/part.img ::
'
echo ">> built $IMG ($(du -h "$IMG" | cut -f1))."
echo "   Flash with Raspberry Pi Imager / balenaEtcher, or:"
echo "     sudo dd if=$IMG of=/dev/rdiskN bs=4m    (find N via 'diskutil list')"
echo "   Then: SD into a Pi 2, connect HDMI (+ USB-serial on GPIO14/15 for the log), power on."
