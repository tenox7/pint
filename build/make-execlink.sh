#!/usr/bin/env bash
# Build the NT kernel with Microsoft C for ARM: the KE/ARM layer, the genuine
# portable KE and executive (each component's UP/SOURCES list), INIT and the LSA
# kernel library, linked from KiSystemStartup with the exports of NTOSKRNL.SRC and
# the ARM additions (armdef.src). The kernel imports the HAL from hal.dll
# (hal/make-hal.sh). INSTALL=1 writes NTOSKRNL.EXE.
#
# Usage:  cd RASPI/build && ./make-execlink.sh                  # build only
#         INSTALL=1 ./make-execlink.sh                          # also install
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
[ -f "$NT40/RASPI/obj/gen/error.h" ] || { echo "make-execlink: build the tools first (tools/make-tools.sh)"; exit 1; }
[ -f "$NT40/RASPI/obj/gen/ksarm.h" ] && [ -f "$NT40/RASPI/obj/libcntpr.lib" ] || { echo "make-execlink: build ksarm.h and the C run-time first (make-ksarm.sh, ../crt/make-crt.sh)"; exit 1; }

docker run --rm -e CJ="${CJ:-4}" -e INSTALL="${INSTALL:-0}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/kernel \
    --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. /work/RASPI/build/msvc.sh
O=/work/RASPI/obj
SUBSYS="KE RTL EX OB PS MM IO SE CONFIG LPC CACHE FSRTL FSTUB RAW DBGK KD VDM PO PNP INIT"
bash $B/mkfarm.sh /tmp/f
ALLSUB=""; for S in $SUBSYS; do ALLSUB="$ALLSUB -I/tmp/f/$(echo $S | tr A-Z a-z)"; done
COMMON="-Iinc -Imm -I/tmp/f/ovr $ALLSUB -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
KINCS="-Iinc -I/tmp/f/ovr -I/tmp/f/ke -I/tmp/f/priv -I/tmp/f/pinc -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
DEFS="$NTDEFINES -D_NTSYSTEM_"

# The C_DEFINES of the SOURCES of a component (RTL\UP adds NTOS_KERNEL_RUNTIME).
cdefs() {
  local c=$(echo $1 | tr A-Z a-z)
  local s=/work/private/ntos/$c/up/sources; [ -f $s ] || s=/work/private/ntos/$c/sources
  bash $B/srcmacro.sh $s C_DEFINES | sed -E "s/[\$][(][A-Za-z0-9_]+[)]//g"
}
MODE="-kernel"
DIR=/tmp/elink; rm -rf $DIR; mkdir -p $DIR/arch $DIR/exec $DIR/src $DIR/gen
fail=0

# -------- 1. the ARM kernel layer: assembly, C, the system service table and stubs --------
echo ">> compiling the ARM kernel layer"
bash $B/gensrv.sh $DIR/gen > /dev/null || { echo "  GENSRV FAIL"; exit 1; }
for s in ke/*.asm ex/*.asm $DIR/gen/sysstubs.asm $DIR/gen/systable.asm; do
  par ntas $DIR/arch/$(basename ${s%.asm})_asm.obj $s $DEFS -I$O/gen $KINCS
done
for s in rtl/debugstb.asm rtl/xcptmisc.asm rtl/xxcaptur.asm rtl/xxmvmem.asm; do
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
         ke/alignem.c ke/buserror.c ke/dmpstate.c ke/mpipi.c ke/apcuser.c ke/allproc.c ke/callback.c; do
  par kec $c
done
# The ARM sources of the components (their ARM_SOURCES): ports of the MIPS ones, and
# the machine-neutral C NT keeps in its MIPS and Alpha directories.
# A uniprocessor kernel flushes the TB through KE\MIPS\XXFLSHTB.C, a multiprocessor one
# through ke/tbarm.c (the ARMv7 TB maintenance reaches every processor).
FLUSHTB=; [ "$NT_UP" = 0 ] || FLUSHTB=KE:/work/private/ntos/ke/mips/xxflshtb.c
for e in EX:/work/private/ntos/ex/mips/splocks.c EX:/work/private/ntos/ex/mips/probe.c \
         PS:/work/private/ntos/ps/mips/psldt.c PS:/work/private/ntos/ps/mips/psvdm.c CONFIG:config/initarm.c \
         EX:ex/fmutex.c EX:ex/intrlock.c EX:ex/gettick.c EX:ex/evpair.c EX:ex/raisests.c \
         MM:mm/initarm.c MM:mm/dataarm.c MM:/work/RASPI/ported/ntos/mm/alpha/hypermap.c \
         MM:/work/private/ntos/mm/mips/setdirty.c MM:/work/private/ntos/mm/mips/debugsup.c LPC:lpc/lpcmove.c \
         KD:/work/private/ntos/kd/mips/kdcmsup.c KD:/work/private/ntos/kd/mips/kdreboot.c \
         KD:kd/kdtrap.c KD:kd/kdcpuapi.c KE:/work/private/ntos/ke/mips/flush.c \
         KE:ke/intobj.c PS:ps/psctxarm.c \
         RTL:rtl/context.c RTL:rtl/largeint.c RTL:rtl/exdsptch.c RTL:rtl/vunwind.c RTL:rtl/chandler.c RTL:rtl/getcalr.c $FLUSHTB; do
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
  for f in $(bash $B/srclist.sh $S); do par exec_c $f; done
done
for f in $(bash $B/srclist.sh CLIENT/KERNEL /work/private/lsa); do par lsa_c $f; done
pwait; n=$PAROK; fail=$((fail+PARFAIL))
echo "   $n executive objects, $fail failed"
[ $fail -eq 0 ] || exit 1
ntlib $DIR/exec.lib $DIR/exec/*.obj || exit 1

# -------- 3. link --------
# The exports: NTOSKRNL.SRC and the ARM additions, preprocessed as the NT build does;
# the HAL import library from hal.src, as LIB -def makes it.
cat /work/private/ntos/init/ntoskrnl.src armdef.src | tr -d "\032\r" > $DIR/ntoskrnl.src
cl -nologo -EP -D_ARM_ $DIR/ntoskrnl.src > $DIR/ntoskrnl.def 2>/dev/null
tr -d "\032\r" < /work/RASPI/hal/hal.src > $DIR/hal.src
cl -nologo -EP -D_ARM_ $DIR/hal.src > $DIR/hal.def 2>/dev/null
lib -nologo -machine:arm -def:$DIR/hal.def -name:hal.dll -out:$DIR/hal.lib > $DIR/hal.err 2>&1 || { cat $DIR/hal.err; exit 1; }
RES=$(bash $B/rc.sh /work/private/ntos/init /work/private/ntos/init/ntoskrnl.rc $DEFS $KINCS) || exit 1
echo ">> linking"
link -nologo -machine:arm -release -opt:ref,noicf -driver -subsystem:native -entry:KiSystemStartup -base:0x81000000 -nodefaultlib $LINKVER \
     -debug -pdb:$O/ntoskrnl.pdb -map:$O/ntoskrnl.map -def:$DIR/ntoskrnl.def -out:$DIR/ntoskrnl.exe \
     $DIR/arch/*.obj $DIR/exec.lib $O/libcntpr.lib $DIR/hal.lib $RES > $DIR/link.err 2>&1 \
  || { echo ">> LINK FAILED (all messages in obj/ntoskrnl.link.err)"; cp $DIR/link.err $O/ntoskrnl.link.err
       grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -80; exit 1; }
ntver $DIR/ntoskrnl.exe || exit 1
cp $DIR/ntoskrnl.exe $O/ntoskrnl.exe
ntimplib $DIR/ntoskrnl.def $DIR/ntoskrnl.lib $O/ntoskrnl.lib
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/WINNT/System32 && mkdir -p $OUT
cp $DIR/ntoskrnl.exe $OUT/NTOSKRNL.EXE
echo "   wrote $OUT/NTOSKRNL.EXE"
'
