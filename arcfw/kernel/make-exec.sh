#!/usr/bin/env bash
# Compile the NT portable executive (Ex/Mm/Ob/Ps/Io/Se/Config/Lpc/Rtl/...) for
# _ARM_ and report coverage. This is the bring-up build target that follows the
# KE/ARM kernel + the SEH layer: the executive is genuinely portable C, so the
# goal is to compile it as-is against the real NT headers, with the SEH neuter
# (arcfw/inc/excpt.h) standing in for MSVC structured exception handling.
#
# Method mirrors make-kernel.sh exactly (in-container lowercase header farm, the
# RISC arch-gate sed, the same CFLAGS), extended with every executive subsystem's
# private headers and two generated headers (bugcodes.h, version.h). Each file is
# compiled with its own subsystem dir first on the include path.
#
# Output: objects in /tmp/eobj (staged for an eventual link with the KE kernel)
# and a per-subsystem + total coverage report. Not yet linked - linking needs
# ExpInitializeExecutive and the full cross-subsystem closure (a later step).
#
# Usage:  cd ARM32/arcfw/kernel && ./make-exec.sh [SUBSYS ...]   (default: all)
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
IMAGE="${IMAGE:-arc-rpi-build:latest}"
docker image inspect "$IMAGE" >/dev/null 2>&1 \
  && docker run --rm "$IMAGE" sh -c 'command -v python3 >/dev/null && command -v perl >/dev/null' 2>/dev/null \
  || docker build -t "$IMAGE" "$NT35/ARM32/build"
SUBSYS="${*:-RTL EX OB PS MM IO SE CONFIG LPC}"

docker run --rm -v "$NT35":/work -w /work/ARM32/arcfw/kernel "$IMAGE" bash -c '
set -u
CROSS=arm-linux-gnueabihf-
CLFILTER=/work/ARM32/arcfw/kernel/castlvalue.pl   # MSVC cast-as-lvalue -> GCC
FIXUPS=/work/ARM32/arcfw/kernel/execfixups.sed
SUBSYS="'"$SUBSYS"'"
RISC_SED="/^[[:space:]]*#(if|elif)/{/_MIPS_/{/_ALPHA_/{/_PPC_/s/\$/ || defined(_ARM_)/}}}"
mkfarm(){ s=$1; d=$2; mkdir -p "$d"; for f in "$s"/*.[Hh]; do [ -e "$f" ]||continue; \
          b=$(basename "$f"|tr A-Z a-z); tr -d "\032\r" < "$f" | sed -E "$RISC_SED" > "$d/$b"; done; }

# commons + every executive subsystem private-header dir
mkfarm /work/PRIVATE/NTOS/INC /tmp/f/priv
mkfarm /work/PRIVATE/INC      /tmp/f/pinc   # shared private hdrs (seopaque.h, ntrmlsa.h, ...)
mkfarm /work/PUBLIC/SDK/INC   /tmp/f/pub
sed -i -E "s/BOOLEAN[[:space:]]+\*(NlsMb(Oem)?CodePageTag)/BOOLEAN \1/" /tmp/f/pub/ntrtl.h  # ntrtl.h decl disagrees w/ its .c users
mkfarm /work/PUBLIC/SDK/INC/CRT /tmp/f/crt
mkfarm /work/PRIVATE/NTOS/KE  /tmp/f/ke
for S in $SUBSYS; do mkfarm /work/PRIVATE/NTOS/$S /tmp/f/$(echo $S|tr A-Z a-z); done

# MM arch gate: mi.h includes a per-arch header (i386/R4000/ALPHA, all dormant
# for _ARM_) right after it pulls ntos.h. Inject an _ARM_ include of our ARMv7 MM
# arch header (kernel/inc/miarm.h, -Iinc) just before the i386 gate, so the base
# NT types (ULONG/PVOID/...) from ntos.h are already in scope.
[ -f /tmp/f/mm/mi.h ] && sed -i "/^#ifdef i386/i #ifdef _ARM_\n#include <miarm.h>\n#endif" /tmp/f/mm/mi.h
# mm/mi.h carries the MI_SET_BIT/MI_CLEAR_BIT (ULONG)ARRAY[..] cast-as-lvalue macros
[ -f /tmp/f/mm/mi.h ] && perl -p "$CLFILTER" /tmp/f/mm/mi.h > /tmp/mi.cl && mv /tmp/mi.cl /tmp/f/mm/mi.h

# generated headers: bugcodes.h (real, from BUGCODES.MC) + a minimal version.h.
{ echo "#ifndef _BUGCODES_"; echo "#define _BUGCODES_";
  tr -d "\r" < /work/PRIVATE/NTOS/NLS/BUGCODES.MC | awk "/^MessageId=/{ sev=\"Fatal\"; nm=\"\"; id=\"\";
       for(i=1;i<=NF;i++){ split(\$i,kv,\"=\");
         if(kv[1]==\"MessageId\"){ v=kv[2]; sub(/^0[xX]/,\"\",v); id=v }
         if(kv[1]==\"Severity\"){ sev=kv[2] }
         if(kv[1]==\"SymbolicName\"){ nm=kv[2] } }
       if(nm!=\"\"){ while(length(id)<4) id=\"0\" id; hi=(sev==\"None\")?\"4000\":\"0000\";
         printf \"#define %s ((ULONG)0x%s%sL)\n\", nm, hi, id } }";
  echo "#endif"; } > /tmp/f/priv/bugcodes.h
cat > /tmp/f/priv/version.h <<EOF
#ifndef _ARM_VERSION_H_
#define _ARM_VERSION_H_
#define VER_PRODUCTBUILD 782
#endif
EOF

CFLAGS="-mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fshort-wchar \
        -D_ARM_ -DNT_UP -DDBG=0 -DFPO=0 -DDEVL=1 -D_EXCEPTION_DISPOSITION_DEFINED \
        -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -ffunction-sections -fdata-sections -O1 -w -include /work/ARM32/arcfw/inc/ntshim.h"

# all subsystem dirs (for cross-subsystem private includes); the compiled
# subsystems own dir is prepended per-subsystem so its headers win.
ALLSUB=""; for S in $SUBSYS; do ALLSUB="$ALLSUB -I/tmp/f/$(echo $S|tr A-Z a-z)"; done
COMMON="-Iinc -I/work/ARM32/arcfw/inc $ALLSUB -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/pub -I/tmp/f/crt"
OUT=/tmp/eobj; mkdir -p "$OUT"; rm -f "$OUT"/*.o; rm -f /tmp/exerr.txt
mkdir -p /tmp/cl   # per-file staged TUs (real basename in the error path -> attribution)

TOK=0; TOT=0
for S in $SUBSYS; do
  s=$(echo $S|tr A-Z a-z); INCS="-I/tmp/f/$s $COMMON"
  ok=0; n=0
  for f in /work/PRIVATE/NTOS/$S/*.[Cc]; do
    [ -e "$f" ] || continue
    base=$(basename "${f%.[Cc]}")
    grep -q "#include" "$f" || continue                 # skip #include-fragment files (not TUs)
    grep -qE "[^A-Za-z_]main[ \t]*\(" "$f" && continue  # skip user-mode test programs (T*/U*/REGTEST)
    case "$base" in CTXMIP|CTXALPHA|CTXPPC|CTXI386) continue;; esac   # other-arch context (not ARM)
    n=$((n+1))
    tr -d "\032\r" < "$f" | perl -p "$CLFILTER" | sed -f "$FIXUPS" > "/tmp/cl/${s}_${base}.c"
    if ${CROSS}gcc $CFLAGS $INCS -c "/tmp/cl/${s}_${base}.c" -o "$OUT/${s}_${base}.o" 2>>/tmp/exerr.txt; then
      ok=$((ok+1))
    fi
  done
  printf "  %-8s %3d / %-3d\n" "$S" "$ok" "$n"
  TOK=$((TOK+ok)); TOT=$((TOT+n))
done
echo "  -------- ------------"
printf "  %-8s %3d / %-3d  (%d%%) compiled to objects\n" "TOTAL" "$TOK" "$TOT" "$((TOK*100/TOT))"
echo
echo "=== top remaining failure reasons (the executive-port TODO) ==="
grep -hoE "error: .*" /tmp/exerr.txt | sed -E "s/(0x[0-9a-f]+|'\''[^'\'']*'\'')/X/g" \
  | sort | uniq -c | sort -rn | head -12 | sed "s/^/   /"
echo
echo "=== top UNKNOWN TYPE NAMES (leverage map for the unknown-type bucket) ==="
grep -hoE "error: unknown type name '\''[A-Za-z_][A-Za-z0-9_]*'\''" /tmp/exerr.txt \
  | grep -oE "'\''[A-Za-z_][A-Za-z0-9_]*'\''" | sort | uniq -c | sort -rn | head -25 | sed "s/^/   /"
echo
echo "=== worst failing files by error count (the real compile-tail TODO) ==="
grep -oE "/tmp/cl/[A-Za-z0-9_]+\.c" /tmp/exerr.txt | sort | uniq -c | sort -rn | head -18 | sed "s/^/   /"
echo
echo "=== errors of specific real files (set DUMPFILE to a basename, default NLSXLAT) ==="
for df in ${DUMPFILE:-NLSXLAT STRING MESSAGE GENERR}; do
  echo "  -- $df --"
  grep -E "/tmp/cl/[a-z]+_${df}\.c" /tmp/exerr.txt | grep "error:" | head -8 | sed "s#/tmp/cl/[a-z]*_##;s/^/   /"
done
echo
sed -E "s#/tmp/cl/##" /tmp/exerr.txt > /work/ARM32/arcfw/kernel/EXEC-COMPILE-ERRORS.txt
echo "=== full per-file compile errors -> EXEC-COMPILE-ERRORS.txt ($(grep -c "error:" /tmp/exerr.txt) errors) ==="
echo "=== objects staged: $(ls /tmp/eobj/*.o 2>/dev/null | wc -l) in /tmp/eobj ==="
'
