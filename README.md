# PINT - Raspberry PI Windows NT

Windows NT 4.0 (build 1381) ported to 32-bit ARM (ARMv7-A) on the Raspberry Pi 2, 3 and 4, with own ARC firmware.

PINT 4.0 is work in progress. PINT 3.5, Windows NT 3.5 (build 782), is on the branch `782`.

## Status

- Runs in QEMU `raspi2b` on four CPUs: NT's developer first run, logon, the Explorer desktop and Start menu, Control
  Panel, Task Manager, the accessories and games, TCP/IP networking, printing, WinHelp 4, POSIX and OpenGL.
- Not yet: OLE (and with it Control Panel > Network), a real Pi.

## Raspberry Pi models

- Tested with PINT 3.5: Pi 2 Model B v1.1 (BCM2836, Cortex-A7), Pi 2 Model B v1.2, Pi 3 Model B v1.2, v1.3 and B+
  v1.3 (BCM2837, Cortex-A53), Pi 4 Model B v1.4 and Pi 400 (BCM2711, Cortex-A72: USB-A ports and Ethernet; NT uses the
  first GB of memory) and QEMU `raspi2b`, `raspi3b`, `raspi3ap`, `raspi4b`.
- Untested, should work: 3A+, Zero 2 W (BCM2837), CM4 (BCM2711).
- NT runs its multiprocessor kernel on all four cores of every model.
- Not planned:
  - Pi 1, Zero, Zero W: ARMv6 (ARM1176), no Thumb-2.
  - Pi 5, Pi 500: the Cortex-A76 runs 32-bit code only in user mode.

## Why not Pi 1 or Pi 5

Microsoft's compiler, the only 32-bit ARM compiler with NT's exception handling, makes only Thumb-2 code, not the ARMv6
code the Pi 1 and Zero need, and the Pi 5 cannot run a 32-bit kernel at all.

## Build

The NT 4.0 source (`private`, `public`) goes beside this tree, and the NT 3.5 tree (`nt35`: a few NT 3.5 files and
DEC's ADK in `arc/adk`, for the ARC DOS shell) beside the NT 4.0 tree; neither is part of this repository. The port's
versions of their files are edit scripts (`*.ntdiff`) the build applies to them (`tools/ntdiff.pl`).

Everything builds in Docker with Microsoft C for ARM ([msvc-arm32](https://hub.docker.com/r/tenox7/msvc-arm32)),
installed once into `msvc` of the NT 3.5 tree; sources and outputs stay on the host, Docker only runs the tools:

    docker run --rm -e ACCEPT_MSVC_LICENSE=yes -v "$PWD/../../nt35/msvc":/opt/msvc tenox7/msvc-arm32 msvc-setup
    build/build-all.sh      # everything from scratch -> obj/arcfw.{elf,bin}, obj/nt-arcfw-sd.img
    build/build-update.sh   # only what changed since the last build

`build-update.sh` runs only the steps whose files changed (`obj/stamp`) and copies what they made into the existing
system disk, keeping what NT wrote on it; the disk is made anew when the registry hives change. Builds run `J` steps
at once (default 8, `J=1` in order) and `CJ` compiles per step (default 4).

    build/portable-qemu-image.sh  # obj/pint-qemu.tar.gz: the system disk, firmware and QEMU for macOS, Linux, Windows

Run it on a Mac with Homebrew `qemu` and `dylibbundler`; `IMG=disk.img` packs another disk, `FORCE=1` rebuilds the
bundled QEMU.

## Run

    build/run.sh                  # console on serial in this terminal (Ctrl-A X quits)
    build/run.sh -device usb-kbd  # console on the HDMI window + USB keyboard
    M=raspi3b build/run.sh        # a Pi 3B (M=raspi3ap: a 3A+, M=raspi4b: a Pi 4B, QEMU 11.1 or later)
    IMG=disk.img build/run.sh     # another system disk
    build/run.sh -h               # the common options; the others go to QEMU

The firmware puts the console on HDMI when a USB keyboard is present, otherwise on serial. run.sh always attaches a USB
mouse (click the window to give it to NT, Ctrl+Alt+G releases it) and a DS1307-type RTC at 0x68 on I2C1 (GPIO 2/3),
where the HAL keeps the time. Without an RTC the clock starts each boot at the firmware build date.

The first run is NT's developer setup (three boots). In the second, OK the boxes (Detected Display, Display Properties)
and press a key at the console's pause; then Ctrl+Alt+Del (QEMU monitor: Ctrl-A C, `sendkey ctrl-alt-delete`, Ctrl-A
C) and Enter log on as Administrator. Each boot says that a service failed: RpcSs, part of OLE, which is not built yet.

The resolution is `framebuffer_width` and `framebuffer_height` of the SD card's `config.txt` (1920x1080; without them the
display's mode), in QEMU `RES=1280x1024 build/run.sh`.

For a real Pi 2, 3 or 4, write `obj/nt-arcfw-sd.img` to an SD card. USB keyboards and mice, behind hubs too (and KVMs
with an absolute mouse, such as PiKVM), can be plugged in at any time; the boot menu lists the USB devices it found.

## Networking

TCP/IP with DHCP and NetBIOS over TCP/IP, and the server and workstation services. It uses the onboard Ethernet
(LAN9514 on the Pi 2/3B, LAN7800 on the 3B+, the BCM2711's GENET on the Pi 4), in QEMU a USB Ethernet adapter
(`NONET=1 build/run.sh`: none). Control Panel > Network needs OLE, which is not built yet.

## Roadmap

- OLE, and with it Control Panel > Network.
- A real Pi 2, 3 and 4.
- Pi 4: memory above the first GB, the USB-C port, USB Ethernet adapters.

## Legal

- This repository contains no Microsoft or Windows copyrighted material. 
- It is just a build toolchain plus own ARM ARC firmware code. 
- Written from scratch by Dave Clunker based on U-Boot.
- Public domain.
