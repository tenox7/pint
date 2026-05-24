Windows NT 3.5 ARC loader - SD disk image

These files are packaged into ramdisk.img by make-ramdisk.sh and read by the
loader's FAT engine. On a real Pi 2 the GPU firmware stages ramdisk.img in RAM
(config.txt initramfs); the loader serves it as its Arc disk. Edit anything in
this directory, re-run make-ramdisk.sh, rebuild, reflash. 8.3 file names only.
