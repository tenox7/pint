#!/usr/bin/env bash
# Build the NT kernel with Microsoft C for ARM: the KE/ARM layer, the genuine
# portable KE and executive (each component's UP/SOURCES list), INIT and the LSA
# kernel library, linked from KiSystemStartup with the exports of NTOSKRNL.SRC and
# the ARM additions (armdef.src). The kernel imports the HAL from hal.dll
# (arcfw/hal, make-hal.sh). INSTALL=1 writes NTOSKRNL.EXE.
#
# Usage:  cd RASPI/arcfw/kernel && ./make-execlink.sh           # build only
#         INSTALL=1 ./make-execlink.sh                          # also install
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
[ -f "$NT35/RASPI/obj/gen/error.h" ] || { echo "make-execlink: build the tools first (tools/make-tools.sh)"; exit 1; }
[ -f "$NT35/RASPI/obj/gen/ksarm.h" ] && [ -f "$NT35/RASPI/obj/libcntpr.lib" ] || { echo "make-execlink: build ksarm.h and the C run-time first (make-ksarm.sh, ../crt/make-crt.sh)"; exit 1; }

docker run --rm -e CJ="${CJ:-4}" -e INSTALL="${INSTALL:-0}" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
SUBSYS="KE RTL EX OB PS MM IO SE CONFIG LPC CACHE FSRTL RAW DBGK KD VDM INIT"
bash mkfarm.sh /tmp/f
ALLSUB=""; for S in $SUBSYS; do ALLSUB="$ALLSUB -I/tmp/f/$(echo $S | tr A-Z a-z)"; done
COMMON="-Iinc -Imm -I/tmp/f/ovr $ALLSUB -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
KINCS="-Iinc -I/tmp/f/ovr -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
DEFS="$NTDEFINES -D_NTSYSTEM_"

# The C_DEFINES of the SOURCES of a component (RTL\UP adds NTOS_KERNEL_RUNTIME).
cdefs() {
  local s=/work/PRIVATE/NTOS/$1/UP/SOURCES; [ -f $s ] || s=/work/PRIVATE/NTOS/$1/SOURCES
  bash srcmacro.sh $s C_DEFINES | sed -E "s/[\$][(][A-Za-z0-9_]+[)]//g"
}
MODE="-kernel"
DIR=/tmp/elink; rm -rf $DIR; mkdir -p $DIR/arch $DIR/exec $DIR/src $DIR/gen
fail=0

# -------- 1. the ARM kernel layer: assembly, C, the system service table and stubs --------
echo ">> compiling the ARM kernel layer"
bash gensrv.sh $DIR/gen > /dev/null || { echo "  GENSRV FAIL"; exit 1; }
for s in ke/*.asm ex/*.asm $DIR/gen/sysstubs.asm $DIR/gen/systable.asm; do
  par ntas $DIR/arch/$(basename ${s%.asm})_asm.obj $s $DEFS -I$O/gen $KINCS
done
for s in rtl/debugstb.asm rtl/xcptmisc.asm rtl/xxcaptur.asm; do
  par ntas $DIR/arch/$(basename ${s%.asm})_asm.obj $s $DEFS $(cdefs RTL) -I$O/gen $KINCS
done
# one ARM C source of KE, and one ARM source of a component (SUBSYS:file), compiled by the
# pool of msvc.sh (par, pwait)
kec() { local c=$1 b=$(basename ${1%.c}); ntsrc $c $DIR/src/$b.c && ntcc $DIR/arch/$b.obj $DIR/src/$b.c $MODE $DEFS $(cdefs KE) $COMMON; }
archc() {
  local S=${1%%:*} f=${1#*:}; local s=$(echo $S | tr A-Z a-z) b=$(basename "${f%.*}" | tr A-Z a-z)
  [ -f $f ] || { echo "   no source $f"; return 1; }
  ntsrc $f $DIR/src/arch_${s}_$b.c && ntcc $DIR/arch/arch_${s}_$b.obj $DIR/src/arch_${s}_$b.c $MODE $DEFS $(cdefs $S) -I/tmp/f/$s $COMMON
}
for c in ke/kearm.c ke/initkr.c ke/thredini.c ke/timindex.c ke/clock.c ke/tbarm.c ke/trapc.c ke/exceptn.c \
         ke/buserror.c ke/dmpstate.c ke/mpipi.c ke/apcuser.c; do
  par kec $c
done
# The ARM sources of the components (their ARM_SOURCES): ports of the MIPS ones, and
# the machine-neutral C NT keeps in its MIPS and Alpha directories.
for e in EX:/work/PRIVATE/NTOS/EX/MIPS/SPLOCKS.C EX:/work/PRIVATE/NTOS/EX/MIPS/PROBE.C \
         PS:/work/PRIVATE/NTOS/PS/MIPS/KULOOKUP.C PS:/work/PRIVATE/NTOS/PS/MIPS/PSLDT.C \
         PS:/work/PRIVATE/NTOS/PS/MIPS/PSVDM.C CONFIG:config/initarm.c \
         EX:ex/fmutex.c EX:ex/intrlock.c EX:ex/gettick.c EX:ex/evpair.c EX:ex/raisests.c \
         MM:mm/initarm.c MM:mm/dataarm.c MM:/work/PRIVATE/NTOS/MM/ALPHA/HYPERMAP.C \
         MM:/work/PRIVATE/NTOS/MM/MIPS/SETDIRTY.C MM:/work/PRIVATE/NTOS/MM/MIPS/DEBUGSUP.C LPC:lpc/lpcmove.c \
         KD:/work/PRIVATE/NTOS/KD/MIPS/KDCMSUP.C KD:/work/PRIVATE/NTOS/KD/MIPS/KDREBOOT.C \
         KD:kd/kdtrap.c KD:kd/kdcpuapi.c RTL:/work/PRIVATE/NTOS/RTL/MIPS/DEBUG.C KE:/work/PRIVATE/NTOS/KE/MIPS/FLUSH.C \
         KE:/work/PRIVATE/NTOS/KE/MIPS/XXFLSHTB.C KE:ke/intobj.c PS:ps/psctxarm.c \
         RTL:rtl/context.c RTL:rtl/largeint.c RTL:rtl/movemem.c RTL:rtl/exdsptch.c RTL:rtl/vunwind.c RTL:rtl/chandler.c RTL:rtl/getcalr.c; do
  par archc $e
done
pwait; fail=$((fail+PARFAIL))

# -------- 2. the portable KE, the executive, INIT and lsakrnlp.lib (LSA\CLIENT\KERNEL) --------
echo ">> compiling the portable kernel and executive"
# one source of a component of the executive (or of the LSA, s lsa), compiled by the pool
exec_c() { local f=$1 b=$(basename "${1%.[Cc]}" | tr A-Z a-z); ntsrc $f $DIR/src/e_${s}_$b.c && ntcc $DIR/exec/e_${s}_$b.obj $DIR/src/e_${s}_$b.c $MODE $DEFS $CD -I/tmp/f/$s $COMMON; }
lsa_c() { local f=$1 b=$(basename "${1%.[Cc]}" | tr A-Z a-z); ntsrc $f $DIR/src/e_lsa_$b.c && ntcc $DIR/exec/e_lsa_$b.obj $DIR/src/e_lsa_$b.c $MODE $DEFS -I/tmp/f/lsa $COMMON; }
for S in $SUBSYS; do
  s=$(echo $S | tr A-Z a-z)
  CD=$(cdefs $S)
  for f in $(bash srclist.sh $S); do par exec_c $f; done
done
for f in $(bash srclist.sh CLIENT/KERNEL /work/PRIVATE/LSA); do par lsa_c $f; done
pwait; n=$PAROK; fail=$((fail+PARFAIL))
echo "   $n executive objects, $fail failed"
[ $fail -eq 0 ] || exit 1
ntlib $DIR/exec.lib $DIR/exec/*.obj || exit 1

# -------- 3. link --------
# The exports: NTOSKRNL.SRC and the ARM additions, preprocessed as the NT build does;
# the HAL import library from hal.src, as LIB -def makes it.
cat /work/PRIVATE/NTOS/INIT/NTOSKRNL.SRC armdef.src | tr -d "\032\r" > $DIR/ntoskrnl.src
cl -nologo -EP -D_ARM_ $DIR/ntoskrnl.src > $DIR/ntoskrnl.def 2>/dev/null
tr -d "\032\r" < /work/RASPI/arcfw/hal/hal.src > $DIR/hal.src
cl -nologo -EP -D_ARM_ $DIR/hal.src > $DIR/hal.def 2>/dev/null
lib -nologo -machine:arm -def:$DIR/hal.def -name:hal.dll -out:$DIR/hal.lib > $DIR/hal.err 2>&1 || { cat $DIR/hal.err; exit 1; }
RES=$(bash rc.sh /work/PRIVATE/NTOS/INIT /work/PRIVATE/NTOS/INIT/NTOSKRNL.RC $DEFS $KINCS) || exit 1
echo ">> linking"
link -nologo -machine:arm -release -driver -subsystem:native -entry:KiSystemStartup -base:0x81000000 -nodefaultlib \
     -debug -pdb:$O/ntoskrnl.pdb -map:$O/ntoskrnl.map -def:$DIR/ntoskrnl.def -out:$DIR/ntoskrnl.exe \
     $DIR/arch/*.obj $DIR/exec.lib $O/libcntpr.lib $DIR/hal.lib $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -80; exit 1; }
cp $DIR/ntoskrnl.exe $O/ntoskrnl.exe
ntimplib $DIR/ntoskrnl.def $DIR/ntoskrnl.lib $O/ntoskrnl.lib
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/WINNT/System32 && mkdir -p $OUT
cp $DIR/ntoskrnl.exe $OUT/NTOSKRNL.EXE
echo "   wrote $OUT/NTOSKRNL.EXE"
'
