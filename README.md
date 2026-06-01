# pint

Windows NT 3.5 ported to 32-bit ARM (ARMv7-A / Cortex-A7) on the Raspberry Pi 2,
running on top of the [arcpi](https://github.com/tenox7/arcpi) ARC firmware emulator.

## Build from scratch

Everything builds in Docker — no host cross-toolchain needed:

    cd arcfw/kernel  && ./make-kernel.sh     # KE/ARM kernel -> WINNT/System32/{NTOSKRNL.EXE,HAL.DLL}
    cd ../ramdisk    && ./make-ramdisk.sh    # FAT image -> obj/ramdisk.img
    cd ../../build   && ./build.sh           # -> obj/arcfw.{elf,bin}

The build scripts auto-build the `arc-rpi-build` toolchain image on first run
(`build/Dockerfile`: Debian bookworm + `arm-linux-gnueabihf-gcc` 12.2.0 + python3 + perl).
To build it explicitly: `docker build -t arc-rpi-build:latest build`.

## Run

    build/run.sh        # QEMU raspi2b: HDMI window + PL011 serial in the terminal. Quit with Ctrl-A then X

For a real Pi 2, build the loader with `./build.sh RAMDISK_INITRAMFS=1` and write the SD
card image with `sdcard/make-sd-image.sh`.
