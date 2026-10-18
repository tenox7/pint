# PI NT

Windows NT 3.5 ported to 32-bit ARM (ARMv7-A / Cortex-A7, Thumb-2) on the Raspberry Pi 2,
booted by its own ARC firmware the way NT booted on MIPS.

## Raspberry Pi models

- Supported: Pi 2 Model B v1.1 (BCM2836, Cortex-A7) and QEMU `raspi2b`.
- Planned:
  - Pi 2 v1.2, Pi 3B (BCM2837, Cortex-A53): needs the SD slot on SDHOST, serial vs Bluetooth, the I2C clock and the
    CPU name; the Pi 2 build already reaches Program Manager on QEMU `raspi3b`.
  - Pi 3B+, 3A+, Zero 2 W: the same plus their USB layouts (two hubs on the 3B+, no hub on the 3A+ and Zero 2 W).
  - CM4, Pi 4B, Pi 400 (BCM2711, Cortex-A72): needs the new peripheral window, GIC and EMMC2; the 4B and 400 also
    PCIe and an xHCI USB driver.
- Not planned:
  - Pi 1, Zero, Zero W: ARMv6 (ARM1176), no Thumb-2.
  - Pi 5, Pi 500: the Cortex-A76 runs 32-bit code only in user mode.

## Build

The NT 3.5 source (`PRIVATE`, `PUBLIC`) and DEC's ADK (`arc/adk`, for the ARC DOS shell) go beside this tree and are
not part of it; the port's versions of their files are edit scripts (`*.ntdiff`) the build applies to them
(`tools/ntdiff.pl`).

Everything builds in Docker with Microsoft C for ARM ([msvc-arm32](https://hub.docker.com/r/tenox7/msvc-arm32)),
installed once into `../msvc` beside the NT source; sources and outputs stay on the host, Docker only runs the tools:

    docker run --rm -e ACCEPT_MSVC_LICENSE=yes -v "$PWD/../msvc":/opt/msvc tenox7/msvc-arm32 msvc-setup
    build/build-all.sh      # everything from scratch -> obj/arcfw.{elf,bin}, obj/nt-arcfw-sd.img
    build/build-update.sh   # only what changed since the last build

`build-update.sh` runs only the steps whose files changed (`obj/stamp`) and copies what they made into the existing
system disk, keeping what NT wrote on it; the disk is made anew when the registry hives change.

## Run

    build/run.sh                  # console on serial in this terminal (Ctrl-A X quits)
    build/run.sh -device usb-kbd  # console on the HDMI window + USB keyboard

The firmware puts the console on HDMI when a USB keyboard is present, otherwise on serial. run.sh always attaches a USB
mouse (click the window to give it to NT, Ctrl+Alt+G releases it) and a DS1307-type RTC at 0x68 on I2C1 (GPIO 2/3),
where the HAL keeps the time.

The first run is NT's developer setup (three boots): Enter at the domain message, any key in the setup console,
then Ctrl+Alt+Del (QEMU monitor: Ctrl-A C, `sendkey ctrl-alt-delete`, Ctrl-A C) and Enter log on as Administrator.

For a real Pi 2, write `obj/nt-arcfw-sd.img` to an SD card. USB keyboards and mice, behind hubs too (and KVMs with an
absolute mouse, such as PiKVM), can be plugged in at any time; the boot menu lists the USB devices it found.

## Roadmap

- SMP: use all four cores (NT runs on one now).
- Networking: the onboard Ethernet and USB Ethernet adapters.

## Legal

This repository contains no Microsoft or Windows code. It is just a build toolchain and own ARM ARC firmware code. Written from scratch by Dave Clunker with U-Boot as the Raspberry Pi hardware reference, and is in the public domain.
