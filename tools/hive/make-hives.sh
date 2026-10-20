#!/usr/bin/env bash
# Build the registry hives the way PUBLIC\OAK\BIN\MKHIVES.CMD does: RCPP preprocesses
# the .INI with the hive options and REGINI -h loads it into a new hive file. REGINI
# (SDKTOOLS\REGINI) and HIVEDMP (CONFIG\UTILS) are NT console programs over the
# genuine Configuration Manager (CONFIG, compiled for user mode as uconfig with
# CONFIG\UTILS\{EDITHIVE,CMWRAPER}.C) and the genuine RTL; ntreg.c is the registry
# services REGINI calls on a live system, over the hive file, and hostnt.c the build
# token. They run on the build machine (tools/host.sh); RCPP comes from tools/make-tools.sh.
#
# Usage: make-hives.sh [ARM|GENERAL|RETAIL|NETIDW|VALIDATE ...]   (default ARM)
#   ARM -> obj/gen/hives/{SYSTEM,SOFTWARE,DEFAULT}; every variant -> obj/gen/hives/<variant>/
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
[ -f "$NT35/RASPI/obj/host/RCPP.EXE" ] || { echo "make-hives: build the tools first (tools/make-tools.sh)"; exit 1; }

docker run --rm -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 \
    /work/RASPI/tools/hive/hives.sh ${*:-ARM}
