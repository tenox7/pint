#!/usr/bin/env bash
# Build the INF files of the ARM workstation as the NT build makes those of a platform
# (setup\inf\win4: its tools PRODFILT and XDOSNET, then inf\daytona\wks): each INF is its
# logic (.inx) and its English text (usa\*.txt), with the lines of the product (@w:) and of
# the platform (@r:, ARM) kept by PRODFILT. The INFs layout.inf puts on the system go into
# sdcard/root/WINNT/INF and System32, where text setup copies them; the setup media files
# (txtsetup.sif, dosnet.inf) stay in obj/gen/tree.
#
# Usage: tools/inf/make-infs.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../../.. && pwd)"

docker run --rm -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 \
    /work/RASPI/tools/inf/infs.sh
