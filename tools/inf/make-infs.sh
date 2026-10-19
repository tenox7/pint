#!/usr/bin/env bash
# Build the INF files of the ARM product as the NT build makes those of \ntcd\mips
# (WINDOWS\SETUP\INF\MAKEFILE.INC, LOCATION \ntcd\arm): the BOM tools lay out the bill of
# materials (BOM\BOM.TXT) and write the file lists of the INFs, LOCAL appends their
# English text, and each INF is its logic (INF\BETA) followed by the media, product and
# file lists, stripped by STRIPINF. The INFs the bill of materials puts in system32 go
# into sdcard/root/WINNT/System32, as a retail setup leaves them. The tools are NT
# programs built into obj/inf/host (not obj/host, the input of every step).
#
# Usage: tools/inf/make-infs.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"

docker run --rm -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel --entrypoint bash tenox7/msvc-arm32 \
    /work/RASPI/tools/inf/infs.sh
