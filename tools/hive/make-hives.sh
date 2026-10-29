#!/usr/bin/env bash
# Build the registry hives the way public\oak\bin\mkhives.cmd does: RCPP preprocesses
# the .INI with the hive options and REGINI -h loads it into a new hive file. REGINI
# (SDKTOOLS\REGINI) and HIVEDMP (CONFIG\UTILS) are NT console programs over the
# genuine Configuration Manager (CONFIG, compiled for user mode as uconfig with
# CONFIG\UTILS\{EDITHIVE,CMWRAPER}.C) and the genuine RTL; ntreg.c is the registry
# services REGINI calls on a live system, over the hive file, and hostnt.c the build
# token. They run on the build machine (tools/host.sh); RCPP comes from tools/make-tools.sh.
#
# Usage: make-hives.sh [ARM|RETAIL|VALIDATE ...]   (default ARM)
#   ARM -> obj/gen/hives/{SYSTEM,SOFTWARE,DEFAULT,USERDIFF}; every variant -> obj/gen/hives/<variant>/
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../../.. && pwd)"
[ -f "$NT40/RASPI/obj/host/RCPP.EXE" ] || { echo "make-hives: build the tools first (tools/make-tools.sh)"; exit 1; }

docker run --rm -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 \
    /work/RASPI/tools/hive/hives.sh ${*:-ARM}
