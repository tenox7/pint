#!/bin/sh
# Interactive QEMU run: the ARC firmware ELF (-kernel) and the system disk on the SD slot.
# A window shows the HDMI framebuffer console; the PL011 serial console + QEMU
# monitor share this terminal. Quit QEMU with Ctrl-A then X. A DS1307-type realtime
# clock (ds1338) on I2C1 holds the local time, as NT keeps it, and a USB mouse is always
# plugged in (NT's first run disables the mouse driver if none is). Extra arguments go to QEMU.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec qemu-system-arm -M raspi2b -m 1G -kernel "$ROOT/obj/arcfw.elf" \
    -drive if=sd,format=raw,file="$ROOT/obj/nt-arcfw-sd.img" -serial mon:stdio \
    -device ds1338,bus=i2c-bus.1,address=0x68 -rtc base=localtime -device usb-mouse "$@"
