#!/usr/bin/env bash
# Build the stand-in NT kernel and wrap it as a PE the OS loader can load.
#
# Output: arcfw/ramdisk/root/OS/NTOSKRNL.EXE - a real PE32 (machine 0x1c0, ARM).
# Run this BEFORE make-ramdisk.sh so the PE is packaged into the FAT image the
# loader reads. Pipeline (all in the arc-rpi-build Docker - no host toolchain):
#
#   start.S + kernel.c  --gcc-->  kernel.elf  --objcopy-->  kernel.bin (flat)
#   kernel.bin          --mkpe.py (PE32 wrapper)-->  NTOSKRNL.EXE
#
# The arm-linux-gnueabihf binutils has no PE backend (ELF only), so mkpe.py
# hand-builds the PE headers; peldr.c (the real BOOT/LIB PE loader) reads it.
#
# Usage:  cd ARM32/arcfw/kernel && ./make-kernel.sh
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARM32ROOT="$(cd ../.. && pwd)"
IMAGE="${IMAGE:-arc-rpi-build:latest}"

echo ">> building stand-in kernel + wrapping as NTOSKRNL.EXE (Docker: $IMAGE)"
docker run --rm -v "$ARM32ROOT":/work -w /work/arcfw/kernel "$IMAGE" bash -c '
set -e
CROSS=arm-linux-gnueabihf-
CFLAGS="-mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic \
        -fno-builtin -fno-stack-protector -fno-unwind-tables \
        -fno-asynchronous-unwind-tables -O2 -Wall -Wextra"
OUT=/work/obj
mkdir -p "$OUT"

${CROSS}gcc $CFLAGS -c start.S  -o "$OUT/k_start.o"
${CROSS}gcc $CFLAGS -c kernel.c -o "$OUT/k_kernel.o"
${CROSS}gcc $CFLAGS -nostdlib -no-pie -Wl,-T,kernel.ld \
        -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-z,noexecstack \
        "$OUT/k_start.o" "$OUT/k_kernel.o" -o "$OUT/kernel.elf"

# The PE carries no zero-fill region (mkpe sets VirtualSize == SizeOfRawData), so a
# non-empty .bss would be uninitialized garbage at run time. Fail loudly if present.
# (size prints text/data/bss in decimal; the 3rd column of the data line is .bss.)
bss=$(${CROSS}size "$OUT/kernel.elf" | awk "NR==2 {print \$3}")
bss=${bss:-0}
if [ "$bss" -ne 0 ]; then
    echo "ERROR: kernel .bss is $bss bytes (must be 0 - initialize all globals non-zero)"; exit 1
fi

${CROSS}objcopy -O binary "$OUT/kernel.elf" "$OUT/kernel.bin"

# Entry virtual address straight from the ELF header; mkpe derives AddressOfEntryPoint.
entry=$(${CROSS}readelf -h "$OUT/kernel.elf" | awk "/Entry point/ {print \$NF}")
echo "   kernel.elf entry = $entry, .bss = $bss bytes, kernel.bin = $(stat -c%s "$OUT/kernel.bin") bytes"

mkdir -p /work/arcfw/ramdisk/root/OS
python3 mkpe.py "$OUT/kernel.bin" /work/arcfw/ramdisk/root/OS/NTOSKRNL.EXE \
        --image-base 0x01000000 --section-rva 0x1000 --entry "$entry" --machine 0x1c0
'
echo ">> NTOSKRNL.EXE written to arcfw/ramdisk/root/OS/. Next: make-ramdisk.sh, then build.sh."
