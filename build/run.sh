#!/bin/sh
# Interactive QEMU run: the ARC firmware ELF (-kernel) and the system disk on the SD slot.
# A window shows the HDMI framebuffer console; the PL011 serial console + QEMU
# monitor share this terminal. Quit QEMU with Ctrl-A then X. A DS1307-type realtime
# clock (ds1338) on I2C1 holds the local time, as NT keeps it, and a USB mouse is always
# plugged in (NT's first run disables the mouse driver if none is), and a USB Ethernet
# adapter on QEMU's user network (NONET=1: none; FWD: a hostfwd= of it). M=raspi3b or raspi3ap runs a Pi 3B or 3A+.
# RES=WxH: the resolution (default 1920x1080; on a Pi framebuffer_width/height of config.txt).
# Extra arguments go to QEMU.
case "$1" in -h|-help|--help)
    cat <<'EOF'
Usage: [M=raspi3b|raspi3ap] [RES=1024x768] [NONET=1] [FWD=hostfwd=tcp::1139-:139] build/run.sh [QEMU options]

Boots obj/arcfw.elf with obj/nt-arcfw-sd.img in QEMU raspi2b (M=raspi3b: a Pi 3B, M=raspi3ap: a
3A+ with 512 MB). Serial console and QEMU monitor in this terminal: Ctrl-A X quits, Ctrl-A C
switches to the monitor.
RES=WxH sets the resolution (default 1920x1080), as framebuffer_width/height of config.txt on a Pi.

  -device usb-kbd       console on the HDMI window, with a USB keyboard
  -device usb-tablet    absolute pointer (no mouse grab)
  -display none         no window
  -snapshot             leave the SD image unchanged
  -gdb tcp::1235        gdb server for lldb (gdb-remote 1235)
EOF
    exit 0 ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
Q=qemu-system-arm G=1G
case ${M:=raspi2b} in raspi3*) Q="qemu-system-aarch64 -cpu cortex-a53,aarch64=off" ;; esac
[ $M = raspi3ap ] && G=512M
RES=${RES:-1920x1080}
FB="-global bcm2835-fb.xres=${RES%x*} -global bcm2835-fb.yres=${RES#*x}"
NET="-netdev user,id=n0${FWD:+,$FWD} -device usb-net,netdev=n0"; [ -n "${NONET:-}" ] && NET=
exec $Q -M $M -m $G -kernel "$ROOT/obj/arcfw.elf" \
    -drive if=sd,format=raw,file="$ROOT/obj/nt-arcfw-sd.img" -serial mon:stdio \
    -device ds1338,bus=i2c-bus.1,address=0x68 -rtc base=localtime -device usb-mouse $NET $FB "$@"
