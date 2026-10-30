#!/usr/bin/env bash
# Build everything from scratch: remove what the build made (obj, the images installed
# in sdcard/root), run every step of build-update.sh and make a new system disk.
#
# Usage:  build/build-all.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
rm -rf obj/*
rm -rf sdcard/root/SDKTOOLS sdcard/root/GAMES sdcard/root/APPS
rm -f sdcard/root/os/winnt40/*.{EXE,DLL}sdcard/root/WINNT/System32/*.{EXE,DLL,DRV,CPL,COM,SCR,INF} sdcard/root/WINNT/System32/WINPERMS.TXT sdcard/root/WINNT/System32/Drivers/*.SYS
FORCE=1 exec build/build-update.sh
