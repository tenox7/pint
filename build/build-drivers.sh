#!/usr/bin/env bash
# Build the kernel-mode drivers into sdcard/root/WINNT/System32/Drivers: storage,
# file systems, display, keyboard and mouse, the pipe, mailslot, null, beep and file
# system recognizer drivers, and the network. Needs the kernel (make-execlink.sh) and
# the HAL.
# Each step runs when its inputs changed (build/stamp.sh).
#
# Usage:  build/build-drivers.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
N=/work/PRIVATE/NTOS B=/work/RASPI/arcfw/drivers
drv() { INSTALL=1 cstep drv $N $1 link -- $K/make-driver.sh $1; }
bcm() { INSTALL=1 ROOT=$B cstep drv $B $1 link -- $K/make-driver.sh $1; }

drv DD/SCSIPORT; cstep lib $N DD/CLASS -- $K/make-lib.sh DD/CLASS; drv DD/SCSIDISK; bcm bcmemmc; drv DD/NTFT
drv FASTFAT; drv FSREC; drv NPFS; drv MAILSLOT
drv VIDEO/PORT; bcm bcmvideo
drv DD/KBDCLASS; drv DD/MOUCLASS; bcm bcmusb
drv DD/NULL; drv DD/BEEP
# networking: the NDIS wrapper, the netcard detection driver of setup, the Ethernet miniport of
# the board (a function of bcmusb), the TDI wrapper and TCP/IP with NetBIOS over TCP/IP, the
# NetBIOS interface, Windows Sockets (afd), and the redirector, server, browser and multiple UNC
# provider of LAN Manager
lib() { cstep lib $N $1 -- $K/make-lib.sh $1; }
drv NDIS/WRAPPER; drv NDIS/DETECT/DRIVER; bcm bcmeth
drv TDI/WRAPPER; lib TDI/TCPIP/IP; drv TDI/TCPIP/TCP; lib NBT/NBT; drv NBT/NT; drv NETBIOS; drv AFD
lib SMBTRSUP; lib BOWSER; drv MUP/DAYTONA; drv RDR/DAYTONA; drv SRV/DAYTONA
