#!/bin/sh
cd "$(dirname "$0")" || exit 1
case "$(uname -s)-$(uname -m)" in
Darwin-arm64) q=bin/macos-arm64/qemu-system-arm; xattr -cr bin/macos-arm64 2>/dev/null ;;
Linux-x86_64) q=bin/linux-x86_64/qemu-system-arm; export LD_LIBRARY_PATH=bin/linux-x86_64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
Linux-aarch64) q=bin/linux-aarch64/qemu-system-arm; export LD_LIBRARY_PATH=bin/linux-aarch64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ;;
*) echo "No QEMU binary for $(uname -sm)" >&2; exit 1 ;;
esac
RES=${RES:-1920x1080}
exec "$q" \
 -M raspi2b -m 1G \
 -kernel arcfw.elf \
 -drive file=nt-arcfw-sd.img,if=sd,format=raw \
 -serial mon:stdio \
 -device ds1338,bus=i2c-bus.1,address=0x68 -rtc base=localtime \
 -device usb-mouse -device usb-kbd \
 -netdev user,id=n0 -device usb-net,netdev=n0 \
 -global bcm2835-fb.xres=${RES%x*} -global bcm2835-fb.yres=${RES#*x} \
 "$@"
