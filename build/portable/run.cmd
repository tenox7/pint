@echo off
cd /d "%~dp0"
if "%RES%"=="" set RES=1920x1080
for /f "tokens=1,2 delims=x" %%a in ("%RES%") do set XRES=%%a& set YRES=%%b
bin\windows-x86_64\qemu-system-arm.exe ^
 -M raspi2b -m 1G ^
 -kernel arcfw.elf ^
 -drive file=nt-arcfw-sd.img,if=sd,format=raw ^
 -serial mon:stdio ^
 -device ds1338,bus=i2c-bus.1,address=0x68 -rtc base=localtime ^
 -device usb-mouse -device usb-kbd ^
 -netdev user,id=n0 -device usb-net,netdev=n0 ^
 -global bcm2835-fb.xres=%XRES% -global bcm2835-fb.yres=%YRES% ^
 %*
