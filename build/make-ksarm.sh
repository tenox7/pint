#!/usr/bin/env bash
# Generate obj/gen/ksarm.h, the structure offsets and constants of the ARM
# assembly (ke/genarm.c, the GENMIPS.C analog), with Microsoft C: each constant
# genarm.c defines is read back from the compiler's listing. Written only when it
# changes, as what includes it is built again then.
#
# Usage:  cd RASPI/build && ./make-ksarm.sh
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"

docker run --rm -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/kernel --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/build/msvc.sh
SUBSYS="KE" bash $B/mkfarm.sh /tmp/f
INCS="-Iinc -I/tmp/f/ovr -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
ntsrc ke/genarm.c /tmp/genarm.c
ntcc /tmp/genarm.obj /tmp/genarm.c -kernel -D_NTSYSTEM_ $NTDEFINES $INCS -FA -Fa/tmp/genarm.lst || exit 1
mkdir -p /work/RASPI/obj/gen
sed -nE "s/^\|KsArm_([A-Za-z0-9_]+)\|[[:space:]]+DCD[[:space:]]+(0x[0-9a-fA-F]+|[0-9]+)[[:space:]]*$/#define \1 \2/p" /tmp/genarm.lst > /tmp/ksarm.h
cmp -s /tmp/ksarm.h /work/RASPI/obj/gen/ksarm.h || cp /tmp/ksarm.h /work/RASPI/obj/gen/ksarm.h
echo "   obj/gen/ksarm.h ($(grep -c . /tmp/ksarm.h) definitions)"
'
