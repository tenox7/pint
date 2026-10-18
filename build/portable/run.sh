#!/bin/sh
cd "$(dirname "$0")" || exit 1
case "$(uname -s)-$(uname -m)" in
Darwin-arm64) q=bin/macos-arm64/qemu-system-arm; xattr -cr bin/macos-arm64 2>/dev/null ;;
Linux-x86_64) q=bin/linux-x86_64/qemu-system-arm ;;
Linux-aarch64) q=bin/linux-aarch64/qemu-system-arm ;;
*) echo "No QEMU binary for $(uname -sm)" >&2; exit 1 ;;
esac
exec "$q" \
 -M raspi2b -m 1G \
 -kernel arcfw.elf \
 -drive file=nt-arcfw-sd.img,if=sd,format=raw \
 -serial mon:stdio \
 -device ds1338,bus=i2c-bus.1,address=0x68 -rtc base=localtime \
 -device usb-mouse -device usb-kbd \
 "$@"
