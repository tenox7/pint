#!/usr/bin/env bash
# STUB-AND-LINK the genuine executive entry ExpInitializeExecutive into the
# KE/ARM kernel. Unlike exec-link-probe.sh (which only nm-computes the symbol
# closure), this performs a REAL ld and converges it:
#
#   1. Build the in-container lowercase header farm + fixups (same recipe as
#      exec-link-probe.sh, so the objects are byte-identical to the probe's).
#   2. Compile the portable executive (RTL/EX/OB/PS/MM/IO/SE/CONFIG/LPC), the
#      portable KE set, and INIT/INIT.C (which DEFINES ExpInitializeExecutive +
#      Phase1Initialization - the probe omits INIT, so this is the new piece) and
#      archive them into libexec.a. ld pulls only the members reachable from the
#      roots, which sidesteps most real-vs-real multiple-definition errors.
#   3. Compile the ARM kernel-architecture objects + our support/stub layer
#      (clib.c, exarm/exglobals/rtlarm/portstubs, and the hand-written
#      linkstubs/linkdata/dataarm if present) as DIRECT objects. Every symbol we
#      provide is weak, so a genuine definition in the archive always wins.
#   4. Auto-stub fixed point: ld --gc-sections rooted at KiSystemStartup +
#      `-u ExpInitializeExecutive`; on `undefined reference`, emit a weak
#      `unsigned long NAME(void){return 0;}` for each into autostubs.c and relink.
#      Repeat until it links or stops making progress. The residual autostubs are
#      printed - those are the symbols still needing a real definition for Phase 0
#      to run (the hand stubs already satisfy the ones that matter).
#   5. On success: size, fold-bss assert, objcopy, mkpe -> NTOSKRNL.EXE.execlink
#      (NOT the known-good NTOSKRNL.EXE - install only after a boot test).
#
# Usage:  cd ARM32/arcfw/kernel && ./make-execlink.sh           # link only
#         INSTALL=1 ./make-execlink.sh                          # also write NTOSKRNL.EXE
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
IMAGE="${IMAGE:-arc-rpi-build:latest}"
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$NT35/ARM32/build"

docker run --rm -e INSTALL="${INSTALL:-0}" -e DIAG="${DIAG:-0}" -e FAULTPC="${FAULTPC:-}" -v "$NT35":/work -w /work/ARM32/arcfw/kernel "$IMAGE" bash -c '
set -u
CROSS=arm-linux-gnueabihf-
CLFILTER=/work/ARM32/arcfw/kernel/castlvalue.pl
FIXUPS=/work/ARM32/arcfw/kernel/execfixups.sed
SUBSYS="RTL EX OB PS MM IO SE CONFIG LPC"
RISC_SED="/^[[:space:]]*#(if|elif)/{/_MIPS_/{/_ALPHA_/{/_PPC_/s/\$/ || defined(_ARM_)/}}}"
mkfarm(){ s=$1; d=$2; mkdir -p "$d"; for f in "$s"/*.[Hh]; do [ -e "$f" ]||continue; \
          b=$(basename "$f"|tr A-Z a-z); tr -d "\032\r" < "$f" | sed -E "$RISC_SED" > "$d/$b"; done; }

mkfarm /work/PRIVATE/NTOS/INC /tmp/f/priv
mkfarm /work/PRIVATE/INC      /tmp/f/pinc
mkfarm /work/PUBLIC/SDK/INC   /tmp/f/pub
sed -i -E "s/BOOLEAN[[:space:]]+\*(NlsMb(Oem)?CodePageTag)/BOOLEAN \1/" /tmp/f/pub/ntrtl.h
mkfarm /work/PUBLIC/SDK/INC/CRT /tmp/f/crt
mkfarm /work/PRIVATE/NTOS/KE  /tmp/f/ke
for S in $SUBSYS; do mkfarm /work/PRIVATE/NTOS/$S /tmp/f/$(echo $S|tr A-Z a-z); done
mkfarm /work/PRIVATE/NTOS/INIT /tmp/f/init

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
{ echo "#ifndef _NTIOLOGC_"; echo "#define _NTIOLOGC_";
  tr -d "\r" < /work/PRIVATE/NTOS/DD/NLSMSG/NTIOLOGC.MC | awk "/^MessageId=/{ id=\"\"; sev=\"Error\"; nm=\"\";
       for(i=1;i<=NF;i++){ split(\$i,kv,\"=\");
         if(kv[1]==\"MessageId\"){ v=kv[2]; sub(/^0[xX]/,\"\",v); id=v }
         if(kv[1]==\"Severity\"){ sev=kv[2] }
         if(kv[1]==\"SymbolicName\"){ nm=kv[2] } }
       if(nm!=\"\"){ hi=(sev==\"Error\")?\"C\":(sev==\"Warning\")?\"8\":(sev==\"Informational\")?\"4\":\"0\";
         while(length(id)<4) id=\"0\" id; printf \"#define %s ((ULONG)0x%s004%sL)\n\", nm, hi, id } }";
  echo "#endif"; } > /tmp/f/priv/ntiologc.h
printf "#ifndef _V_\n#define _V_\n#define VER_PRODUCTBUILD 782\n#endif\n" > /tmp/f/priv/version.h

# -fcommon: the 1994 NT sources put tentative definitions (ULONG LpcpNextMessageId;)
# in shared headers (LPCP.H) included by many .c - legal under the pre-GCC-10
# common-symbol model. GCC 12 defaults to -fno-common, turning each into a strong
# definition -> "multiple definition" at link. -fcommon restores the merge semantics.
CFLAGS="-mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fshort-wchar -fcommon \
        -D_ARM_ -DIMAGE_FILE_MACHINE_ARM=0x1c0 -DNT_UP -DDBG=0 -DFPO=0 -DDEVL=1 -D_EXCEPTION_DISPOSITION_DEFINED -DKI_RUN_EXECUTIVE=1 \
        -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -ffunction-sections -fdata-sections -O1 -w -include /work/ARM32/arcfw/inc/ntshim.h"
ALLSUB=""; for S in $SUBSYS; do ALLSUB="$ALLSUB -I/tmp/f/$(echo $S|tr A-Z a-z)"; done
COMMON="-Iinc -I/work/ARM32/arcfw/inc $ALLSUB -I/tmp/f/init -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/pub -I/tmp/f/crt"
KINCS="-Iinc -I/work/ARM32/arcfw/inc -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/pub -I/tmp/f/crt"

DIR=/tmp/elink; rm -rf "$DIR"; mkdir -p "$DIR"
DOBJ="$DIR/direct"; AOBJ="$DIR/arch"; mkdir -p "$DOBJ" "$AOBJ"

# -------- 1. DIRECT objects: ARM arch + entry + our support/stub layer --------
echo ">> compiling ARM kernel-architecture + support objects (direct)"
for s in ke/armstart.S ke/interlock.S ke/ctxsw.S ke/trap.S ke/seh.S ke/zwstubs.S; do
  ${CROSS}gcc $CFLAGS $KINCS -c $s -o "$DOBJ/$(basename ${s%.S})_asm.o" || echo "  ASM FAIL $s"
done
DCFILES="ke/kearm.c ke/initkr.c ke/ctxsw.c ke/timindex.c ke/clock.c ke/seh.c ke/mmuarm.c \
         ke/exarm.c ke/exglobals.c ke/rtlarm.c ke/portstubs.c ke/clib.c \
         ../ported/wait.c ../ported/queueobj.c"
# the hand-written stub/data files (added as they are authored)
for opt in ke/linkstubs.c ke/linkdata.c ke/dataarm.c; do [ -f "$opt" ] && DCFILES="$DCFILES $opt"; done
for c in $DCFILES; do
  tr -d "\032\r" < "$c" > /tmp/c.c
  if [ "$c" = ke/dataarm.c ]; then INC="$COMMON"; else INC="$KINCS"; fi   # dataarm needs mi.h/miarm.h
  ${CROSS}gcc $CFLAGS $INC -c /tmp/c.c -o "$DOBJ/$(basename ${c%.c}).o" 2>"$DIR/cc_$(basename ${c%.c}).err" \
    || echo "  CC FAIL $c (see $DIR/cc_$(basename ${c%.c}).err)"
done
${CROSS}gcc -mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic -fno-builtin \
   -fno-stack-protector -ffunction-sections -fdata-sections -O2 -w -c jxdisp.c -o "$DOBJ/jxdisp.o"

# Weaken the support/stub layer: these provided executive symbols BEFORE their
# genuine defining files compiled. Now that INIT.C / PS / RTL / the KE set are in
# the archive, every genuine (strong) definition must win - so demote our copies
# to weak. Leaves the ARM arch + entry objects (kearm/initkr/mmuarm/...) strong.
for w in exarm exglobals rtlarm portstubs dataarm linkdata linkstubs clib; do
  [ -f "$DOBJ/$w.o" ] && ${CROSS}objcopy --weaken "$DOBJ/$w.o"
done

# -------- 2. ARCHIVE: portable KE + executive subsystems + INIT --------
echo ">> compiling portable KE + executive subsystems + INIT (-> libexec.a)"
KE=/work/PRIVATE/NTOS/KE
for c in KERNLDAT KIINIT PROCOBJ THREDOBJ THREDSUP DPCOBJ DPCSUP TIMEROBJ TIMERSUP SEMPHOBJ APCOBJ EVENTOBJ WAITSUP MUTNTOBJ PROFOBJ DEVQUOBJ APCSUP MISCC; do
  tr -d "\032\r" < "$KE/$c.C" > /tmp/c.c
  ${CROSS}gcc $CFLAGS $KINCS -c /tmp/c.c -o "$AOBJ/ke_$(echo $c|tr A-Z a-z).o" 2>/dev/null \
    || echo "  KE CC FAIL $c"
done
nexec=0; nfail=0
for S in $SUBSYS; do
  s=$(echo $S|tr A-Z a-z); INCS="-I/tmp/f/$s $COMMON"
  for f in /work/PRIVATE/NTOS/$S/*.[Cc]; do
    [ -e "$f" ] || continue
    base=$(basename "${f%.[Cc]}")
    grep -q "#include" "$f" || continue
    grep -qE "[^A-Za-z_]main[ \t]*\(" "$f" && continue
    case "$base" in CTXMIP|CTXALPHA|CTXPPC|CTXI386) continue;; esac
    tr -d "\032\r" < "$f" | perl -p "$CLFILTER" | sed -f "$FIXUPS" > /tmp/tu.c
    if ${CROSS}gcc $CFLAGS $INCS -c /tmp/tu.c -o "$AOBJ/e_${s}_${base}.o" 2>/dev/null; then
      nexec=$((nexec+1)); else nfail=$((nfail+1)); fi
  done
done
# INIT/INIT.C - defines ExpInitializeExecutive + Phase1Initialization (the new piece).
# Prefer the ported copy (arcfw/ported/init.c) if present: it carries Phase-0
# breadcrumb HalDisplayString calls so a boot pinpoints which subsystem init hangs.
INITINCS="-I/tmp/f/init $COMMON"
INITSRC=/work/PRIVATE/NTOS/INIT/INIT.C
[ -f /work/ARM32/arcfw/ported/init.c ] && INITSRC=/work/ARM32/arcfw/ported/init.c && echo "   (using ported init.c with breadcrumbs)"
tr -d "\032\r" < "$INITSRC" | perl -p "$CLFILTER" | sed -f "$FIXUPS" > /tmp/tu.c
if ${CROSS}gcc $CFLAGS $INITINCS -c /tmp/tu.c -o "$AOBJ/e_init_init.o" 2>"$DIR/cc_init.err"; then
  echo "   INIT.C compiled OK (ExpInitializeExecutive available)"
else
  echo "   INIT.C FAILED to compile - ExpInitializeExecutive will be unresolved. Errors:"
  grep -E "error:" "$DIR/cc_init.err" | head -25
fi
echo "   executive objects: $nexec compiled, $nfail failed to compile"
${CROSS}ar rcs "$DIR/libexec.a" "$AOBJ"/*.o 2>/dev/null
echo "   libexec.a: $(${CROSS}ar t "$DIR/libexec.a" 2>/dev/null | wc -l) members"

# -------- 3. auto-stub fixed-point link --------
echo ">> linking (gc-sections, root KiSystemStartup + -u ExpInitializeExecutive); auto-stubbing residual undefined"
: > "$DIR/autostubs.c"
echo "// auto-generated weak stubs for residual undefined symbols (make-execlink.sh)" > "$DIR/autostubs.c"
${CROSS}gcc $CFLAGS $KINCS -c "$DIR/autostubs.c" -o "$DOBJ/autostubs.o" 2>/dev/null
prevcount=-1
for iter in $(seq 1 16); do
  if ${CROSS}ld -T kernel.ld --gc-sections -e KiSystemStartup -u ExpInitializeExecutive \
       --no-warn-rwx-segments "$DOBJ"/*.o --start-group "$DIR/libexec.a" --end-group \
       -o "$DIR/kernel.elf" 2>"$DIR/lderr"; then
    echo "   LINK OK on iteration $iter"; LINKED=1; break
  fi
  # collect non-multiple-definition undefined symbols
  grep -oE "undefined reference to .[A-Za-z_][A-Za-z0-9_]*" "$DIR/lderr" | sed "s/.* .//" | sort -u > "$DIR/undef.txt"
  grep -E "multiple definition|relocation|truncated|recompile" "$DIR/lderr" | sort -u > "$DIR/hard.txt"
  ucount=$(wc -l < "$DIR/undef.txt")
  hcount=$(wc -l < "$DIR/hard.txt")
  echo "   iter $iter: $ucount undefined, $hcount hard (muldef/reloc) errors"
  if [ "$hcount" -gt 0 ]; then echo "   --- HARD errors (need manual resolution): ---"; cat "$DIR/hard.txt" | head -30; break; fi
  if [ "$ucount" -eq 0 ] || [ "$ucount" -eq "$prevcount" ]; then
    echo "   no further progress; remaining undefined:"; cat "$DIR/undef.txt"; break
  fi
  prevcount=$ucount
  while read -r sym; do
    [ -n "$sym" ] && echo "__attribute__((weak)) unsigned long $sym(void){return 0;}" >> "$DIR/autostubs.c"
  done < "$DIR/undef.txt"
  ${CROSS}gcc $CFLAGS $KINCS -c "$DIR/autostubs.c" -o "$DOBJ/autostubs.o" 2>/dev/null
done

if [ "${LINKED:-0}" != 1 ]; then echo ">> LINK NOT YET CLOSED. lderr tail:"; tail -25 "$DIR/lderr"; exit 1; fi

if [ "${DIAG:-0}" = 1 ]; then
  echo ">> DIAG: autostubs.c CmpFind/CmGet entries:"; grep -E "CmpFind|CmGetSystem" "$DIR/autostubs.c" || echo "   (none - not auto-stubbed)"
  echo ">> DIAG: CmpFindControlSet disassembly head (real=has bl KiEmit; stub=mov r0,#0;bx lr):"
  ${CROSS}objdump -d "$DIR/kernel.elf" 2>/dev/null | sed -n "/<CmpFindControlSet>:/,/^$/p" | head -25
  echo ">> DIAG: nm CmpFindControlSet:"; ${CROSS}nm "$DIR/kernel.elf" | grep -iE "CmpFindControlSet|CmGetSystemControlValues"
  if [ -n "${FAULTPC:-}" ]; then
    hx=$(printf %x "$FAULTPC")
    echo ">> DIAG: function + context at FAULTPC=$FAULTPC ($hx):"
    ${CROSS}objdump -d "$DIR/kernel.elf" 2>/dev/null | awk -v t="$hx" "
        /^[0-9a-f]+ </ {fn=\$0}
        index(\$0, t\":\")==1 {print \"  in: \" fn; c=1}
        c && c<=5 {print \"  \" \$0; c++}"
  fi
fi

echo ">> auto-stubbed symbols (still need a real definition for Phase 0 to run):"
grep -oE "weak\)\) unsigned long [A-Za-z_][A-Za-z0-9_]*" "$DIR/autostubs.c" | sed "s/.* //" | sort -u | sed "s/^/   /"
echo "   ($(grep -c "weak)) unsigned long" "$DIR/autostubs.c") auto-stubs)"

${CROSS}size "$DIR/kernel.elf"
bss=$(${CROSS}size "$DIR/kernel.elf" | awk "NR==2 {print \$3}")
[ "${bss:-0}" -eq 0 ] || echo "   NOTE: .bss=$bss (kernel.ld should fold bss->data; check if mkpe is affected)"
${CROSS}objcopy -O binary "$DIR/kernel.elf" "$DIR/kernel.bin"
entry=$(${CROSS}readelf -h "$DIR/kernel.elf" | awk "/Entry point/ {print \$NF}")
echo "   kernel.elf entry=$entry, kernel.bin=$(stat -c%s "$DIR/kernel.bin") bytes"

if [ "${INSTALL:-0}" = 1 ]; then
  mkdir -p /work/ARM32/arcfw/ramdisk/root/WINNT/System32
  python3 mkpe.py "$DIR/kernel.bin" /work/ARM32/arcfw/ramdisk/root/WINNT/System32/NTOSKRNL.EXE \
      --image-base 0x81000000 --section-rva 0x1000 --entry "$entry" --machine 0x1c0
  echo "   INSTALLED -> NTOSKRNL.EXE"
else
  python3 mkpe.py "$DIR/kernel.bin" "$DIR/NTOSKRNL.EXE.execlink" \
      --image-base 0x81000000 --section-rva 0x1000 --entry "$entry" --machine 0x1c0
  echo "   wrote $DIR/NTOSKRNL.EXE.execlink (not installed; set INSTALL=1 to deploy)"
fi
'
