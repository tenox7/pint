#!/usr/bin/env bash
# Build what changed since the last build: each step (build/stamp.sh) runs only when
# an input of the port is newer. First the tools of the build, ksarm.h and the stubs and
# headers of MIDL, which every later step takes; the C run-time; the registry hives; the
# kernel, the HAL, the OS loader and the ARC firmware; the components of NT (build-nt.sh);
# then the INFs, the images rebased as the build lab rebased a build (sdcard/rebase.sh),
# the system disk, updated in place, keeping what NT wrote on it (make-sd-image.sh -u), and
# the setup card (make-setup-image.sh). build-all.sh builds everything from scratch.
#
# Usage:  build/build-update.sh    # J jobs at once (default 8, J=1 in order), CJ compiles each (default 12)
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
export CJ=${CJ:-12}
# the port's versions of NT files from their edit scripts first, so that their times count
docker run --rm -v "$R/..":/work -v "$R/../../nt35":/nt35:ro -v "$R/../../XPSP1":/XPSP1:ro -v "$R/../private":/work/private:ro -v "$R/../public":/work/public:ro --entrypoint perl tenox7/msvc-arm32 /work/RASPI/tools/ntdiff.pl apply /work /work/RASPI

# the tools (with the header pass, and the C run-time and import libraries they link when
# missing), ksarm.h, the MIDL pass
GEN= step tools $R/tools/pe2img.c $R/tools/msvcexe.c $R/tools/midl/midlcpp.c $R/apps/sdktools/guidlib $R/apps/sdktools/vctools/mc \
    $R/ported/rpc/midl20 $R/ported/ntos/makefil0 $R/ported/windows/makefil0 -- $R/tools/make-tools.sh
GEN=$HOSTGEN step ksarm $K/ke/genarm.c -- $B/make-ksarm.sh
GEN=$HOSTGEN step midl $R/tools/midl -- $R/tools/midl/gen-midl.sh

# The C run-time: NT's (crt32, of crtdll.dll), then that of Visual C++ 4.2, whose libc.lib
# and libcmt.lib NT 4.0 links its images with, in their place, and msvcrt.dll; the
# run-time of the kernel, of the native programs and of POSIX, and the old names.
# crtdll.lib of crt32 takes txtmode, ncommode and tlssup from libcmt.lib.
crt() (
    cd $R/crt
    ./make-crt32.sh ST && ./make-crt32.sh MT && ./make-crt.sh ST && ./make-crt.sh MT && ./make-crt32.sh DLL &&
    ./make-crt.sh && UM=1 ./make-crt.sh && INSTALL=1 ./make-crt.sh DLL && ./make-crt.sh DLLLIB &&
    INSTALL=1 ./make-crt.sh DLL40 && INSTALL=1 ./make-crtdll.sh && ./make-crt.sh POSIX && ./make-crt.sh OLDNAMES
)
step crt $CRT $R/obj/um/kernel32.lib $R/obj/um/user32.lib -- crt

# the hives of the system disk and of the setup card
GEN=$HOSTGEN step hives $R/tools/hive $R/ported/oak -- $R/tools/hive/make-hives.sh ARM SETUP

# the kernel, the HAL, the OS loader and the ARC firmware
INSTALL=1 step kernel $K/ke $K/kd $K/mm $K/ex $K/lpc $K/ps $K/rtl $K/config $K/armdef.src $R/ported/ntos/makefil0 \
    $(for c in ke rtl ex ob ps mm io se config lpc cache fsrtl fstub raw dbgk kd vdm po pnp init; do echo $R/ported/ntos/$c; done) \
    $R/ported/lsa/client/kernel $R/obj/libcntpr.lib -- $B/make-execlink.sh
INSTALL=1 CJ=4 job hal step hal $R/hal $R/obj/ntoskrnl.lib $R/obj/libcntpr.lib -- $R/hal/make-hal.sh
for c in rtl/boot boot/lib config/boot boot/bldr; do CJ=4 job lib/$c cstep lib /work/private/ntos $c -- $B/make-lib.sh $c; done
barrier
INSTALL=1 CJ=4 job osloader step osloader $R/obj/bldr.lib $R/obj/boot.lib $R/obj/bconfig.lib $R/obj/bldrrtl.lib $R/obj/libcntpr.lib \
    -- $B/make-osloader.sh
CJ=4 job firmware step firmware $R/arcfw/arm $R/drivers/bcmusb/usbhost.c $R/drivers/bcmusb/usbhost.h $R/drivers/bcmusb/xhci.c $R/drivers/bcmusb/xhci.h $R/drivers/bcmeth/bcmethhw.h $R/arcfw/arcdos $R/tools/fon2fw.pl $R/obj/boot.lib \
    $R/obj/bldrrtl.lib $R/obj/libcntpr.lib -- $R/build/build.sh
barrier

$R/build/build-nt.sh

# the INFs (XDOSNET, a tool of theirs, links setupapi.lib), the images of the system rebased,
# the system disk, the setup card
GEN=$HOSTGEN step infs $R/tools/inf $R/ported/windows/setup/bom $R/ported/windows/setup/inf $R/ported/windows/setup/localinf \
    $R/obj/um/setupapi.lib -- $R/tools/inf/make-infs.sh
step rebase $R/sdcard/root/WINNT $R/obj/REBASE.EXE $R/obj/IMAGECFG.EXE -- $R/sdcard/rebase.sh
$R/sdcard/make-sd-image.sh $([ -n "${FORCE:-}" ] || echo -u)
step setup-image $R/sdcard/root $R/sdcard/config.txt $R/sdcard/firmware $R/sdcard/setupfiles.pl $R/sdcard/arcenv.pl $R/obj/arcfw.bin \
    $R/obj/SETUPLDR $R/obj/USETUP.EXE $R/obj/SETUPDD.SYS $R/obj/SPDDLANG.SYS $R/obj/gen/hives $R/obj/gen/tree/windows/setup/inf \
    $R/obj/inf/host $R/obj/*.DLL $R/obj/*.EXE -- $R/sdcard/make-setup-image.sh
