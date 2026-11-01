Windows NT 4.0 for the Raspberry Pi (PINT 4.0) on QEMU's raspi2b

Run ./run.sh (macOS 15+ arm64, Linux x86_64/aarch64 with glibc 2.34+ and GTK 3) or run.cmd (Windows 10+ x64).
NT is in the window: click it to give NT the mouse, Ctrl+Alt+G releases it. The serial console and the QEMU monitor
are in the terminal: Ctrl-A C switches between them (monitor: sendkey ctrl-alt-delete), Ctrl-A X quits.
A new disk first runs NT's developer setup (three boots). In the second, OK the boxes (Detected Display, Display
Properties) and press a key at the console's pause; then Ctrl+Alt+Del and Enter to log on as Administrator.
Extra QEMU options are passed through, e.g. -display vnc=:1.
Networking: a USB Ethernet adapter on QEMU user mode networking (NAT, DHCP).
Resolution: RES=1280x1024 ./run.sh (run.cmd: set RES=1280x1024 first), default 1920x1080.

QEMU @V@ source: https://download.qemu.org/qemu-@V@.tar.xz (GPLv2, see bin/COPYING).
