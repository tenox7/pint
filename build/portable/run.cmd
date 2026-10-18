@echo off
cd /d "%~dp0"
bin\windows-x86_64\qemu-system-arm.exe ^
 -M raspi2b -m 1G ^
 -kernel arcfw.elf ^
 -drive file=nt-arcfw-sd.img,if=sd,format=raw ^
 -serial mon:stdio ^
 -device ds1338,bus=i2c-bus.1,address=0x68 -rtc base=localtime ^
 -device usb-mouse -device usb-kbd ^
 %*
