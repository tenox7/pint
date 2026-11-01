#!/usr/bin/env bash
# Build everything from scratch: remove what the build made (obj, and the files installed in
# sdcard/root but the port's own, README.TXT and the samples of System32\Drivers\etc), run
# every step of build-update.sh and make a new system disk and setup card. After a failure,
# build-update.sh goes on from the step that failed.
#
# Usage:  build/build-all.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
rm -rf obj/*
find sdcard/root -type f ! -path sdcard/root/README.TXT ! -path "sdcard/root/WINNT/System32/Drivers/etc/*" -delete
find sdcard/root -mindepth 1 -type d -empty -delete
FORCE=1 exec build/build-update.sh
