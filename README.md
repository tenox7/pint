# PI NT

Windows NT 3.5 ported to 32-bit ARM (ARMv7-A / Cortex-A7, Thumb-2) on the Raspberry Pi 2,
booted by its own ARC firmware the way NT booted on MIPS.

## Build from scratch

The NT 3.5 source (`PRIVATE`, `PUBLIC`) goes beside this tree and is not part of it; the port's versions of NT
files are edit scripts (`*.ntdiff`) the build applies to it (`tools/ntdiff.pl`).

Everything builds in Docker with Microsoft C for ARM ([msvc-arm32](https://hub.docker.com/r/tenox7/msvc-arm32)),
installed once into `../msvc` beside the NT source; sources and outputs stay on the host, Docker only runs the tools:

    docker run --rm -e ACCEPT_MSVC_LICENSE=yes -v "$PWD/../msvc":/opt/msvc tenox7/msvc-arm32 msvc-setup
    docker build -t nt-msvc -f build/Dockerfile.msvc build
    tools/make-tools.sh                     # NT's build tools (RC, MIDL, MC, ...), run on 32-bit ARM Wine
    tools/midl/gen-midl.sh                  # RPC stubs
    (cd tools/hive && ./make-hives.sh)      # registry hives

Then the system, in order:

    cd arcfw/kernel && INSTALL=1 ./make-execlink.sh        # C run-time + kernel
    (cd ../hal && INSTALL=1 ./make-hal.sh)                 # HAL.DLL
    for c in RTL/BOOT BOOT/LIB CONFIG/BOOT BOOT/BLDR; do ./make-lib.sh $c; done
    INSTALL=1 ./make-osloader.sh                           # OSLOADER.EXE
    ../../build/build-drivers.sh                           # kernel-mode drivers
    ../../build/build-user.sh                              # user mode
    cd ../../build && ./build.sh                           # ARC firmware -> obj/arcfw.{elf,bin}
    cd ../sdcard && ./make-sd-image.sh                     # system disk -> obj/nt-arcfw-sd.img

## Run

    build/run.sh                  # console on serial in this terminal (Ctrl-A X quits)
    build/run.sh -device usb-kbd  # console on the HDMI window + USB keyboard

The firmware puts the console on HDMI when a USB keyboard is present, otherwise on serial. run.sh always attaches a USB
mouse (click the window to give it to NT, Ctrl+Alt+G releases it) and a DS1307-type RTC at 0x68 on I2C1 (GPIO 2/3),
where the HAL keeps the time.

The first run is NT's developer setup (three boots): Enter at the domain message, any key in the setup console,
then Ctrl+Alt+Del (QEMU monitor: Ctrl-A C, `sendkey ctrl-alt-delete`, Ctrl-A C) and Enter log on as Administrator.

For a real Pi 2, write `obj/nt-arcfw-sd.img` to an SD card; plug in the mouse before the first boot, as the setup
disables the drivers of missing devices.
