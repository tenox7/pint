#!/usr/bin/env bash
# Build what changed since the last build: each step (build/stamp.sh) runs only when
# an input of the port is newer, and the system disk is updated in place, keeping what
# NT wrote on it (make-sd-image.sh -u). build-all.sh builds everything from scratch.
#
# Usage:  build/build-update.sh    # J jobs at once (default 8, J=1 in order), CJ compiles each (default 4)
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
# the port's versions of NT files from their edit scripts first, so that their times count
docker run --rm -v "$R/..":/work -v "$R/../../nt35":/nt35:ro -v "$R/../private":/work/private:ro -v "$R/../public":/work/public:ro --entrypoint perl tenox7/msvc-arm32 /work/RASPI/tools/ntdiff.pl apply /work /work/RASPI

GEN= step tools $R/tools/pe2img.c $R/tools/midl/midlcpp.c $R/ported/ntos/makefil0 $R/ported/windows/makefil0 $CRT \
    -- $R/tools/make-tools.sh
GEN=$HOSTGEN step hives $R/tools/hive $R/ported/oak -- $R/tools/hive/make-hives.sh
GEN=$HOSTGEN step ksarm $K/ke/genarm.c -- $B/make-ksarm.sh
GEN=$HOSTGEN step midl $R/tools/midl -- $R/tools/midl/gen-midl.sh
step crt $CRT -- $R/crt/make-crt.sh
INSTALL=1 step kernel $K/ke $K/kd $K/mm $K/ex $K/lpc $K/ps $K/rtl $K/config $K/armdef.src $R/ported/ntos/rtl $R/ported/ntos/ke $R/ported/ntos/init \
    $R/ported/ntos/makefil0 $R/ported/ntos/diskreg $R/ported/ntos/mm $R/ported/ntos/ps \
    $R/obj/libcntpr.lib -- $B/make-execlink.sh
INSTALL=1 job hal step hal $R/hal $R/obj/ntoskrnl.lib $R/obj/libcntpr.lib -- $R/hal/make-hal.sh
for c in RTL/BOOT BOOT/LIB CONFIG/BOOT BOOT/BLDR; do job lib/$c cstep lib /work/private/ntos $c -- $B/make-lib.sh $c; done
barrier
INSTALL=1 step osloader $R/obj/bldr.lib $R/obj/boot.lib $R/obj/bconfig.lib $R/obj/bldrrtl.lib $R/obj/libcntpr.lib \
    -- $B/make-osloader.sh
$R/build/build-drivers.sh
$R/build/build-user.sh
$R/build/build-sdktools.sh
GEN=$HOSTGEN job infs step infs $R/tools/inf $R/ported/windows/setup/bom $R/ported/windows/setup/inf $R/ported/windows/setup/localinf \
    $R/apps/net/ncpabind -- $R/tools/inf/make-infs.sh
job firmware step firmware $R/arcfw/arm $R/drivers/bcmusb/usbhost.c $R/drivers/bcmusb/usbhost.h $R/drivers/bcmusb/xhci.c $R/drivers/bcmusb/xhci.h $R/drivers/bcmeth/bcmethhw.h $R/arcfw/arcdos $R/tools/fon2fw.pl $R/obj/boot.lib \
    $R/obj/bldrrtl.lib $R/obj/libcntpr.lib -- $R/build/build.sh
barrier
$R/sdcard/make-sd-image.sh $([ -n "${FORCE:-}" ] || echo -u)
