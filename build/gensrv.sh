#!/bin/bash
# Make the system service dispatch table and stubs as KE\UP\MAKEFILE.INC and
# ntdll's MAKEFILE.INC make them: GENSRV (obj/host, build/build-tools.sh) reads
# KE\SERVICES.TAB and the ARM templates ke/table.stb and ke/services.stb, and writes
# the kernel table and stubs (-g), ntdll's user stubs, and ntdll's export list (-f
# ntdll.xtr). The lines GENSRV writes are indented, as the assembler takes column 1
# for labels.
# Usage (build container): gensrv.sh outdir
set -e
O=$1
W=${NTROOT:-/work}
K=$W/RASPI/kernel
G=/tmp/gensrv
rm -rf $G && mkdir -p $G/arm
tr -d '\032\r' < $W/PRIVATE/NTOS/KE/SERVICES.TAB > $G/services.tab
cp $K/ke/table.stb $K/ke/services.stb $G/arm/
(cd $G && for a in "-d arm -e asm -g . -B" "-d arm -e asm -B ." "-f ntdll.xtr ."; do bash $W/RASPI/tools/host.sh gensrv $a; done)
mkdir -p $O
for f in sysstubs systable usrstubs; do
    tr -d '\r' < $G/arm/$f.asm | sed -E 's/^(STUBS_|SYSSTUBS_|USRSTUBS_|TABLE_|ARGTBL_)/        &/' > $O/$f.asm
done
tr -d '\r' < $G/ntdll.xtr > $O/ntdll.xtr
