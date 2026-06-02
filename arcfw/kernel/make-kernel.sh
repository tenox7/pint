#!/usr/bin/env bash
# Build the REAL KE/ARM NT kernel and wrap it as NTOSKRNL.EXE for the OS loader.
#
# Output: arcfw/ramdisk/root/WINNT/System32/{NTOSKRNL.EXE,HAL.DLL}. Run this
# BEFORE make-ramdisk.sh so the PE is packaged into the FAT image the loader reads.
#
# The kernel is the genuine NT KiInitializeKernel (ke/initkr.c, ported from
# KE/MIPS/INITKR.C) plus the real KE/ARM arch glue (ke/{kearm,ctxsw,timindex}.c,
# ke/{armstart,interlock,ctxsw}.S) and the real portable KE objects compiled
# as-is for _ARM_ (KERNLDAT/KIINIT/PROCOBJ/THREDOBJ/.../WAIT/QUEUEOBJ). It runs
# the real PCR/PRCB/idle-process/idle-thread initialization and halts at the
# ExpInitializeExecutive boundary (the executive is not yet ported). The HAL
# display (jxdisp.c) renders the init report on the framebuffer.
#
# Method (see memory nt35-ke-arm-headers): the build mounts the nt35 tree, builds
# an in-container lowercase header farm of the real NT headers (Docker's mount is
# case-sensitive), reuses the loader's _ARM_-adjusted inc/ (ntdef.h/ntshim.h/...),
# generates bugcodes.h from NLS/BUGCODES.MC, and adds _ARM_ to the all-RISC arch
# gates. Links with --gc-sections rooted at KiSystemStartup. binutils has no PE
# backend, so mkpe.py hand-builds the PE headers.
#
# Usage:  cd ARM32/arcfw/kernel && ./make-kernel.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
IMAGE="${IMAGE:-arc-rpi-build:latest}"
docker image inspect "$IMAGE" >/dev/null 2>&1 \
  && docker run --rm "$IMAGE" sh -c 'command -v python3 >/dev/null && command -v perl >/dev/null' 2>/dev/null \
  || docker build -t "$IMAGE" "$NT35/ARM32/build"

echo ">> building the real KE/ARM kernel -> NTOSKRNL.EXE (Docker: $IMAGE)"
docker run --rm -v "$NT35":/work -w /work/ARM32/arcfw/kernel "$IMAGE" bash -c '
set -e
CROSS=arm-linux-gnueabihf-
RISC_SED="/^[[:space:]]*#(if|elif)/{/_MIPS_/{/_ALPHA_/{/_PPC_/s/\$/ || defined(_ARM_)/}}}"
mkfarm(){ s=$1; d=$2; mkdir -p "$d"; for f in "$s"/*.[Hh]; do [ -e "$f" ]||continue; \
          b=$(basename "$f"|tr A-Z a-z); tr -d "\032\r" < "$f" | sed -E "$RISC_SED" > "$d/$b"; done; }
mkfarm /work/PRIVATE/NTOS/INC /tmp/farm/priv
mkfarm /work/PUBLIC/SDK/INC   /tmp/farm/pub
sed -i -E "s/BOOLEAN[[:space:]]+\*(NlsMb(Oem)?CodePageTag)/BOOLEAN \1/" /tmp/farm/pub/ntrtl.h  # ntrtl.h decl disagrees w/ its .c users (BOOLEAN flag)
mkfarm /work/PUBLIC/SDK/INC/CRT /tmp/farm/crt
mkfarm /work/PRIVATE/NTOS/KE  /tmp/farm/ke
{ echo "#ifndef _BUGCODES_"; echo "#define _BUGCODES_";
  tr -d "\r" < /work/PRIVATE/NTOS/NLS/BUGCODES.MC | awk "/^MessageId=/{ sev=\"Fatal\"; nm=\"\"; id=\"\";
       for(i=1;i<=NF;i++){ split(\$i,kv,\"=\");
         if(kv[1]==\"MessageId\"){ v=kv[2]; sub(/^0[xX]/,\"\",v); id=v }
         if(kv[1]==\"Severity\"){ sev=kv[2] }
         if(kv[1]==\"SymbolicName\"){ nm=kv[2] } }
       if(nm!=\"\"){ while(length(id)<4) id=\"0\" id; hi=(sev==\"None\")?\"4000\":\"0000\";
         printf \"#define %s ((ULONG)0x%s%sL)\n\", nm, hi, id } }";
  echo "#endif"; } > /tmp/farm/priv/bugcodes.h

INCS="-Iinc -I/work/ARM32/arcfw/inc -I/tmp/farm/ke -I/tmp/farm/priv -I/tmp/farm/pub -I/tmp/farm/crt"
CFLAGS="-mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fshort-wchar \
        -D_ARM_ -DNT_UP -DDBG=0 -DFPO=0 -DDEVL=1 -D_EXCEPTION_DISPOSITION_DEFINED \
        -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -ffunction-sections -fdata-sections -O1 -w -include /work/ARM32/arcfw/inc/ntshim.h"
OUT=/tmp/kobj; mkdir -p "$OUT"; rm -f "$OUT"/*.o

# ARM kernel-architecture code (compiled against the real NT kernel headers).
${CROSS}gcc $CFLAGS $INCS -c ke/armstart.S  -o "$OUT/armstart.o"
${CROSS}gcc $CFLAGS $INCS -c ke/interlock.S -o "$OUT/interlock.o"
${CROSS}gcc $CFLAGS $INCS -c ke/ctxsw.S     -o "$OUT/ctxsw_asm.o"
${CROSS}gcc $CFLAGS $INCS -c ke/trap.S      -o "$OUT/trap.o"
${CROSS}gcc $CFLAGS $INCS -c ke/seh.S       -o "$OUT/seh_asm.o"
for c in ke/kearm.c ke/initkr.c ke/ctxsw.c ke/timindex.c ke/clock.c ke/seh.c ke/mmuarm.c ke/exarm.c ke/exglobals.c ke/rtlarm.c ke/portstubs.c ../ported/wait.c ../ported/queueobj.c; do
  tr -d "\032\r" < "$c" > /tmp/c.c
  ${CROSS}gcc $CFLAGS $INCS -c /tmp/c.c -o "$OUT/$(basename ${c%.c}).o"
done

# Real portable KE objects, compiled as-is for _ARM_.
KE=/work/PRIVATE/NTOS/KE
for c in KERNLDAT KIINIT PROCOBJ THREDOBJ THREDSUP DPCOBJ DPCSUP TIMEROBJ TIMERSUP SEMPHOBJ APCOBJ EVENTOBJ WAITSUP; do
  tr -d "\032\r" < "$KE/$c.C" > /tmp/c.c
  ${CROSS}gcc $CFLAGS $INCS -c /tmp/c.c -o "$OUT/$(echo $c|tr A-Z a-z).o" \
    || { echo "CC FAIL $c"; exit 1; }
done

# HAL display: real NT HalDisplayString (jxdisp.c), built against kernel.h.
HCF="-mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fno-builtin \
     -fno-stack-protector -ffunction-sections -fdata-sections -O2 -w"
${CROSS}gcc $HCF -c jxdisp.c -o "$OUT/jxdisp.o"

echo ">> linking (--gc-sections, entry KiSystemStartup)"
if ! ${CROSS}ld -T kernel.ld --gc-sections -e KiSystemStartup --no-warn-rwx-segments \
   "$OUT"/*.o -o "$OUT/kernel.elf" 2>/tmp/lderr; then
   echo "LINK FAILED - unresolved (the next layer to port):"
   grep -oE "undefined reference to .[A-Za-z_][A-Za-z0-9_]*" /tmp/lderr | sed "s/.* //" | sort -u
   exit 1
fi
${CROSS}size "$OUT/kernel.elf"
bss=$(${CROSS}size "$OUT/kernel.elf" | awk "NR==2 {print \$3}")
[ "${bss:-0}" -eq 0 ] || { echo "ERROR: .bss=$bss (kernel.ld must fold bss->data)"; exit 1; }
${CROSS}objcopy -O binary "$OUT/kernel.elf" "$OUT/kernel.bin"
entry=$(${CROSS}readelf -h "$OUT/kernel.elf" | awk "/Entry point/ {print \$NF}")
echo "   kernel.elf entry=$entry, kernel.bin=$(stat -c%s "$OUT/kernel.bin") bytes"
mkdir -p /work/ARM32/arcfw/ramdisk/root/WINNT/System32
python3 mkpe.py "$OUT/kernel.bin" /work/ARM32/arcfw/ramdisk/root/WINNT/System32/NTOSKRNL.EXE \
    --image-base 0x81000000 --section-rva 0x1000 --entry "$entry" --machine 0x1c0
printf "\x1e\xff\x2f\xe1" > "$OUT/hal.bin"
python3 mkpe.py "$OUT/hal.bin" /work/ARM32/arcfw/ramdisk/root/WINNT/System32/HAL.DLL \
    --image-base 0x01100000 --section-rva 0x1000 --entry 0x01101000 --machine 0x1c0
'
echo ">> NTOSKRNL.EXE + HAL.DLL written. Next: make-ramdisk.sh, then build.sh, then run."
