#!/usr/bin/env bash
# Build everything from scratch: remove what the build made (obj, the images installed
# in sdcard/root), run every step of build-update.sh and make a new system disk.
#
# Usage:  build/build-all.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
rm -rf obj/*
rm -f sdcard/root/os/nt/*.{EXE,DLL} sdcard/root/WINNT/System32/*.{EXE,DLL,DRV,CPL} sdcard/root/WINNT/System32/Drivers/*.SYS
FORCE=1 exec build/build-update.sh
