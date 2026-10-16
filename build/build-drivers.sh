#!/usr/bin/env bash
# Build the kernel-mode drivers into sdcard/root/WINNT/System32/Drivers: storage,
# file systems, display, keyboard and mouse, and the pipe, mailslot, null, beep and
# file system recognizer drivers. Needs the kernel (make-execlink.sh) and the HAL.
#
# Usage:  build/build-drivers.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../arcfw/kernel"
drv() { INSTALL=1 ./make-driver.sh $1; }
bcm() { INSTALL=1 ROOT=/work/RASPI/arcfw/drivers ./make-driver.sh $1; }

drv DD/SCSIPORT; ./make-lib.sh DD/CLASS; drv DD/SCSIDISK; bcm bcmemmc; drv DD/NTFT
drv FASTFAT; drv FSREC; drv NPFS; drv MAILSLOT
drv VIDEO/PORT; bcm bcmvideo
drv DD/KBDCLASS; drv DD/MOUCLASS; bcm bcmusb
drv DD/NULL; drv DD/BEEP
