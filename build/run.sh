#!/bin/sh
# Interactive QEMU run. Uses the ELF: QEMU honors its 0x8000 LMA, while the raw
# arcfw.bin is for real-hardware SD boot (firmware loads it at the native 0x8000).
# A window shows the HDMI framebuffer console; the PL011 serial console + QEMU
# monitor share this terminal. Quit QEMU with Ctrl-A then X.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec qemu-system-arm -M raspi2b -m 1G -kernel "$ROOT/obj/arcfw.elf" -serial mon:stdio
