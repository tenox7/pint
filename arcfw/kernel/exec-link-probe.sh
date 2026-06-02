#!/usr/bin/env bash
# Measure the executive LINK CLOSURE: compile the KE/ARM kernel objects AND the
# portable executive objects (the make-exec.sh set), then use nm to compute the
# symbols the executive references that NOTHING in the combined object set yet
# defines. That undefined-and-undefined-everywhere set is the concrete, prioritized
# "next layer to port/stub" to link ExpInitializeExecutive - the project's
# "unresolved = the next layer" method, scaled from one subsystem to the executive.
#
# This does NOT attempt a real ld (the executive objects carry duplicate/alternative
# definitions + need a HAL); the symbol-set closure is the right first-order map.
# Caveat: a symbol counts as "provided" if ANY object defines it, so this slightly
# UNDER-counts the TODO (alternative impls), which is fine for prioritization.
#
# Usage:  cd ARM32/arcfw/kernel && ./exec-link-probe.sh
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
IMAGE="${IMAGE:-arc-rpi-build:latest}"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$NT35/ARM32/build"

docker run --rm -v "$NT35":/work -w /work/ARM32/arcfw/kernel "$IMAGE" bash -c '
set -u
CROSS=arm-linux-gnueabihf-
CLFILTER=/work/ARM32/arcfw/kernel/castlvalue.pl
SUBSYS="RTL EX OB PS MM IO SE CONFIG LPC"
RISC_SED="/^[[:space:]]*#(if|elif)/{/_MIPS_/{/_ALPHA_/{/_PPC_/s/\$/ || defined(_ARM_)/}}}"
mkfarm(){ s=$1; d=$2; mkdir -p "$d"; for f in "$s"/*.[Hh]; do [ -e "$f" ]||continue; \
          b=$(basename "$f"|tr A-Z a-z); tr -d "\032\r" < "$f" | sed -E "$RISC_SED" > "$d/$b"; done; }

mkfarm /work/PRIVATE/NTOS/INC /tmp/f/priv
mkfarm /work/PRIVATE/INC      /tmp/f/pinc
mkfarm /work/PUBLIC/SDK/INC   /tmp/f/pub
mkfarm /work/PUBLIC/SDK/INC/CRT /tmp/f/crt
mkfarm /work/PRIVATE/NTOS/KE  /tmp/f/ke
for S in $SUBSYS; do mkfarm /work/PRIVATE/NTOS/$S /tmp/f/$(echo $S|tr A-Z a-z); done

[ -f /tmp/f/mm/mi.h ] && sed -i "/^#ifdef i386/i #ifdef _ARM_\n#include <miarm.h>\n#endif" /tmp/f/mm/mi.h
[ -f /tmp/f/mm/mi.h ] && perl -p "$CLFILTER" /tmp/f/mm/mi.h > /tmp/mi.cl && mv /tmp/mi.cl /tmp/f/mm/mi.h

{ echo "#ifndef _BUGCODES_"; echo "#define _BUGCODES_";
  tr -d "\r" < /work/PRIVATE/NTOS/NLS/BUGCODES.MC | awk "/^MessageId=/{ sev=\"Fatal\"; nm=\"\"; id=\"\";
       for(i=1;i<=NF;i++){ split(\$i,kv,\"=\");
         if(kv[1]==\"MessageId\"){ v=kv[2]; sub(/^0[xX]/,\"\",v); id=v }
         if(kv[1]==\"Severity\"){ sev=kv[2] }
         if(kv[1]==\"SymbolicName\"){ nm=kv[2] } }
       if(nm!=\"\"){ while(length(id)<4) id=\"0\" id; hi=(sev==\"None\")?\"4000\":\"0000\";
         printf \"#define %s ((ULONG)0x%s%sL)\n\", nm, hi, id } }";
  echo "#endif"; } > /tmp/f/priv/bugcodes.h
printf "#ifndef _V_\n#define _V_\n#define VER_PRODUCTBUILD 782\n#endif\n" > /tmp/f/priv/version.h

CFLAGS="-mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fshort-wchar \
        -D_ARM_ -DNT_UP -DDBG=0 -DFPO=0 -DDEVL=1 -D_EXCEPTION_DISPOSITION_DEFINED \
        -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -ffunction-sections -fdata-sections -O1 -w -include /work/ARM32/arcfw/inc/ntshim.h"
ALLSUB=""; for S in $SUBSYS; do ALLSUB="$ALLSUB -I/tmp/f/$(echo $S|tr A-Z a-z)"; done
COMMON="-Iinc -I/work/ARM32/arcfw/inc $ALLSUB -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/pub -I/tmp/f/crt"
OUT=/tmp/eobj; mkdir -p "$OUT"; rm -f "$OUT"/*.o

# ---- 1. the KE/ARM kernel objects (what the kernel already provides) ----
KINCS="-Iinc -I/work/ARM32/arcfw/inc -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pub -I/tmp/f/crt"
for s in ke/armstart.S ke/interlock.S ke/ctxsw.S ke/trap.S ke/seh.S ke/zwstubs.S; do
  ${CROSS}gcc $CFLAGS $KINCS -c $s -o "$OUT/k_$(basename ${s%.S})_asm.o" 2>/dev/null  # _asm: avoid .c/.S basename clash (seh, ctxsw)
done
for c in ke/kearm.c ke/initkr.c ke/ctxsw.c ke/timindex.c ke/clock.c ke/seh.c ke/mmuarm.c ke/exarm.c ke/exglobals.c ke/rtlarm.c ke/portstubs.c ../ported/wait.c ../ported/queueobj.c; do
  tr -d "\032\r" < "$c" > /tmp/c.c
  ${CROSS}gcc $CFLAGS $KINCS -c /tmp/c.c -o "$OUT/k_$(basename ${c%.c}).o" 2>/dev/null
done
# the HAL display half (HalDisplayString / HalpInitializeDisplay0), built like make-kernel.sh
${CROSS}gcc -mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fno-builtin \
   -fno-stack-protector -ffunction-sections -fdata-sections -O2 -w -c jxdisp.c -o "$OUT/k_jxdisp.o" 2>/dev/null
for c in KERNLDAT KIINIT PROCOBJ THREDOBJ THREDSUP DPCOBJ DPCSUP TIMEROBJ TIMERSUP SEMPHOBJ APCOBJ EVENTOBJ WAITSUP MUTNTOBJ PROFOBJ; do
  tr -d "\032\r" < /work/PRIVATE/NTOS/KE/$c.C > /tmp/c.c
  ${CROSS}gcc $CFLAGS $KINCS -c /tmp/c.c -o "$OUT/k_$(echo $c|tr A-Z a-z).o" 2>/dev/null
done

# ---- 2. the portable executive objects (the make-exec.sh set) ----
for S in $SUBSYS; do
  s=$(echo $S|tr A-Z a-z); INCS="-I/tmp/f/$s $COMMON"
  for f in /work/PRIVATE/NTOS/$S/*.[Cc]; do
    [ -e "$f" ] || continue
    base=$(basename "${f%.[Cc]}")
    grep -q "#include" "$f" || continue                 # skip #include-fragment files (not TUs)
    grep -qE "[^A-Za-z_]main[ \t]*\(" "$f" && continue  # skip user-mode test programs
    case "$base" in CTXMIP|CTXALPHA|CTXPPC|CTXI386) continue;; esac   # other-arch context
    tr -d "\032\r" < "$f" | perl -p "$CLFILTER" > /tmp/tu.c
    ${CROSS}gcc $CFLAGS $INCS -c /tmp/tu.c -o "$OUT/e_${s}_${base}.o" 2>/dev/null
  done
done

echo "objects: kernel $(ls $OUT/k_*.o 2>/dev/null|wc -l), executive $(ls $OUT/e_*.o 2>/dev/null|wc -l)"

# ---- 3. nm closure: undefined (U) minus everything-defined ----
${CROSS}nm "$OUT"/*.o > /tmp/nm.txt 2>/dev/null
awk "\$2 ~ /^[A-TV-Za-tv-z]\$/ {print \$NF}" /tmp/nm.txt | sort -u > /tmp/defined.txt   # any defined symbol
awk "\$1==\"U\"{print \$2} \$2==\"U\"{print \$3}" /tmp/nm.txt | sort -u > /tmp/undef.txt
comm -23 /tmp/undef.txt /tmp/defined.txt > /tmp/closure.txt
echo "defined symbols: $(wc -l < /tmp/defined.txt) | distinct undefined: $(wc -l < /tmp/undef.txt) | UNRESOLVED CLOSURE: $(wc -l < /tmp/closure.txt)"
echo
echo "=== unresolved closure by subsystem prefix (the next layer to port/stub) ==="
for p in Hal Ke Ki Mm Mi Ex Ob Ps Io Iop Se Cm Rtl Kd Dbg Po Lpc Nt Zw Csr Inbv; do
  c=$(grep -cE "^_?${p}[A-Z0-9]" /tmp/closure.txt)
  [ "$c" -gt 0 ] && printf "  %-6s %3d\n" "$p" "$c"
done | sort -t" " -k2 -rn
echo "  ----- (other / non-prefixed below)"
grep -vE "^_?(Hal|Ke|Ki|Mm|Mi|Ex|Ob|Ps|Io|Iop|Se|Cm|Rtl|Kd|Dbg|Po|Lpc|Nt|Zw|Csr|Inbv)[A-Z0-9]" /tmp/closure.txt \
  | grep -vE "^(__|memset|memcpy|memmove|memcmp|strlen)" | head -40 | sed "s/^/   ? /"
echo
echo "=== full unresolved closure written to EXEC-LINK-TODO.txt ($(wc -l < /tmp/closure.txt) symbols) ==="
TODO=/work/ARM32/arcfw/kernel/EXEC-LINK-TODO.txt
{
  echo "# Executive link closure - symbols ExpInitializeExecutive needs that neither"
  echo "# the portable executive objects nor the current KE/ARM kernel define yet."
  echo "# Generated by exec-link-probe.sh. The prioritized next layer to port/stub."
  echo "# kernel objects: $(ls $OUT/k_*.o 2>/dev/null|wc -l), executive objects: $(ls $OUT/e_*.o 2>/dev/null|wc -l)"
  echo "# defined: $(wc -l < /tmp/defined.txt)  undefined(distinct): $(wc -l < /tmp/undef.txt)  UNRESOLVED: $(wc -l < /tmp/closure.txt)"
  echo
  echo "## by subsystem prefix"
  for p in Hal Ke Ki Mm Mi Ex Ob Ps Io Iop Se Cm Rtl Kd Dbg Dbgk Po Lpc Lpcp Hv Nls Cc FsRtl Nt Zw Csr Inbv; do
    c=$(grep -cE "^_?${p}[A-Z0-9]" /tmp/closure.txt); [ "$c" -gt 0 ] && printf "  %-7s %3d\n" "$p" "$c"
  done | sort -t" " -k2 -rn
  echo
  echo "## full list (sorted)"
  cat /tmp/closure.txt
} > "$TODO"
echo "   -> $TODO"
'
