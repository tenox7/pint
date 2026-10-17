#!/usr/bin/env bash
# Build what changed since the last build: each step (build/stamp.sh) runs only when
# an input of the port is newer, and the system disk is updated in place, keeping what
# NT wrote on it (make-sd-image.sh -u). build-all.sh builds everything from scratch.
#
# Usage:  build/build-update.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
# the port's versions of NT files from their edit scripts first, so that their times count
docker run --rm -v "$R/..":/work --entrypoint perl tenox7/msvc-arm32 /work/RASPI/tools/ntdiff.pl apply /work /work/RASPI

GEN= step tools $R/tools/pe2img.c $R/tools/midl/midlcpp.c $R/arcfw/ported/ntos/makefil0 $R/arcfw/ported/windows/makefil0 $CRT \
    -- $R/tools/make-tools.sh
GEN=$HOSTGEN step hives $R/tools/hive $R/arcfw/ported/oak -- $R/tools/hive/make-hives.sh
GEN=$HOSTGEN step ksarm $K/ke/genarm.c -- $K/make-ksarm.sh
GEN=$HOSTGEN step midl $R/tools/midl -- $R/tools/midl/gen-midl.sh
step crt $CRT -- $R/arcfw/crt/make-crt.sh
INSTALL=1 step kernel $K/ke $K/kd $K/mm $K/ex $K/lpc $K/ps $K/rtl $K/config $K/armdef.src $R/arcfw/ported/ntos \
    $R/obj/libcntpr.lib -- $K/make-execlink.sh
INSTALL=1 step hal $R/arcfw/hal $R/obj/ntoskrnl.lib $R/obj/libcntpr.lib -- $R/arcfw/hal/make-hal.sh
for c in RTL/BOOT BOOT/LIB CONFIG/BOOT BOOT/BLDR; do cstep lib /work/PRIVATE/NTOS $c -- $K/make-lib.sh $c; done
INSTALL=1 step osloader $R/obj/bldr.lib $R/obj/boot.lib $R/obj/bconfig.lib $R/obj/bldrrtl.lib $R/obj/libcntpr.lib \
    -- $K/make-osloader.sh
$R/build/build-drivers.sh
$R/build/build-user.sh
step firmware $R/arcfw/arm $R/arcfw/ported/arcdos.c $R/arcfw/arcdos-inc $R/tools/fon2fw.pl $R/obj/boot.lib \
    $R/obj/bldrrtl.lib $R/obj/libcntpr.lib -- $R/build/build.sh
$R/sdcard/make-sd-image.sh $([ -n "${FORCE:-}" ] || echo -u)
