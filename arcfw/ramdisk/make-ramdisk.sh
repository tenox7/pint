#!/usr/bin/env bash
# Build the FAT disk image the loader serves as its Arc disk. The file contents are a
# real, editable source tree at arcfw/ramdisk/root/ - EDIT FILES THERE, then re-run
# this script. (This is also where future kernel-build output should be placed, e.g.
# root/OS/NTOSKRNL.EXE, so it gets packaged into the image.)
#
# The image reaches RAM two ways (one build, one image): under QEMU it is embedded in
# the loader as a blob; on a real Pi 2 the GPU firmware stages it via config.txt's
# `initramfs ramdisk.img 0x00800000` (build the loader RAMDISK_INITRAMFS=1). Either way
# ramdisk.c serves it behind the same BL_DEVICE_ENTRY_TABLE.
#
# Layout mirrors the real SD card on purpose (MBR + one partition starting at LBA
# 2048), so AEOpen's MBR-parse / partition-offset path is the SAME code on QEMU and
# on hardware. The partition is FAT16, not FAT32: NT 3.5 predates FAT32 (1996), so
# BOOT/LIB/FATBOOT.C reads only FAT12/16. Sized > 4085 clusters so it is
# unambiguously FAT16 (below that it would be FAT12). Bump SIZE_MB below if root/
# grows past ~6 MiB (e.g. once a real kernel image lands in it).
#
# Output: ../../obj/ramdisk.img  (embedded as ramdisk_blob.o for QEMU; copied onto the
# SD card by sdcard/make-sd-image.sh for real HW). Run in Docker like make-sd-image.sh;
# the arc-rpi-build image has no mkfs.vfat.
#
# Usage:  cd ARM32/arcfw/ramdisk && ./make-ramdisk.sh
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMG="../../obj/ramdisk.img"
SIZE_MB=6
PART_START=2048          # match the SD card (make-sd-image.sh) so the MBR path is identical

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1"; exit 1; }; }
need docker

mkdir -p ../../obj
ARM32ROOT="$(cd ../.. && pwd)"

echo ">> assembling $IMG (MBR + FAT16 partition from arcfw/ramdisk/root/) in Docker"
docker run --rm -v "$ARM32ROOT":/work -w /work/arcfw/ramdisk \
  -e IMG="/work/obj/ramdisk.img" -e SIZE_MB="$SIZE_MB" -e PART_START="$PART_START" \
  debian:bookworm bash -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq dosfstools mtools fdisk >/dev/null

# The FAT volume contents are a REAL editable tree at arcfw/ramdisk/root/ (mounted
# here at /work/arcfw/ramdisk/root), not generated inline - edit/add files there and
# re-run this script. 8.3 names only (FATBOOT.C has no VFAT/long names). This is what
# arcdos "dir"/"type" lists and reads, and where future kernel-build output should land
# (e.g. root/OS/NTOSKRNL.EXE). Fail loudly if it is missing or empty.
SRCROOT=/work/arcfw/ramdisk/root
[ -d "$SRCROOT" ] && [ -n "$(ls -A "$SRCROOT" 2>/dev/null)" ] || \
  { echo "ERROR: $SRCROOT is missing or empty - it holds the disk files"; exit 1; }

# Whole-disk image, MBR, one FAT16 partition (type 0x06) starting at PART_START.
psect=$(( SIZE_MB*2048 - PART_START ))
dd if=/dev/zero of="$IMG" bs=1M count="$SIZE_MB" status=none
printf "label: dos\nstart=%s, type=06, bootable\n" "$PART_START" | sfdisk "$IMG" >/dev/null

# Build the FAT16 partition separately, then splice it in at PART_START.
dd if=/dev/zero of=/tmp/part.img bs=512 count="$psect" status=none
mkfs.vfat -F 16 -s 1 -n NTBOOT /tmp/part.img >/dev/null
mcopy -s -i /tmp/part.img "$SRCROOT"/* ::
dd if=/tmp/part.img of="$IMG" bs=512 seek="$PART_START" conv=notrunc status=none

echo "--- partition table ---"; sfdisk -l "$IMG"
echo "--- FAT type + contents ---"; file - < /tmp/part.img 2>/dev/null || true
mdir -i /tmp/part.img -/ ::
'
echo ">> built $IMG ($(du -h "$IMG" | cut -f1)). Rebuild the loader to embed it."
