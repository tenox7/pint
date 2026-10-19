#!/bin/bash
# Build the NT header farm under $1 (default /tmp/f). NT 3.5 headers (and the
# .ver and .dlg files resource scripts include, the .dbg status names of the debug
# tools: ntstatus.dbg) are
# upper/mixed-case with DOS line ends and RISC-only arch gates; copy them flat
# and lower-case, strip ^Z/CR, add _ARM_ to the RISC gates and normalize the
# #include names. Then adds what the build generates: the headers and sources of
# its header pass (mkgen.sh, obj/gen/farm, made again when an input of the ARM
# port is newer), version.h, the MIDL pass (obj/gen/midl) and GENERR's error.h.
set -u
F=${1:-/tmp/f}
W=${NTROOT:-/work}
K=$W/RASPI/arcfw/kernel
G=$W/RASPI/obj/gen/farm
SUBSYS=${SUBSYS:-KE RTL EX OB PS MM IO SE CONFIG LPC CACHE FSRTL RAW DBGK KD VDM INIT}

# the header pass, when its inputs of the ARM port (the arch headers, the ported
# makefiles, the filters, the tools) are newer than its output
if [ -z "${MKGEN:-}" ] && { [ ! -f $G/.stamp ] || [ -n "$(find $K/inc $W/RASPI/arcfw/inc $W/RASPI/arcfw/ported/ntos/makefil0 $W/RASPI/arcfw/ported/windows/makefil0 $K/*.sed $K/nmk.awk $K/mkgen.sh $(for t in mc hextract hsplit wcshdr zwapi listmung; do echo $W/RASPI/obj/host/$(echo $t | tr a-z A-Z).EXE; done) -newer $G/.stamp -print -quit 2>/dev/null)" ]; }; then
    bash $K/mkgen.sh >&2 || exit 1
fi

filt() { tr -d '\032\r' < "$1" | sed -E -f "$K/riscgate.sed" -f "$K/msvcgate.sed" -f "$K/incfix.sed" | sed -f "$K/armport.sed" -f "$K/msc2.sed"; }
farm() {
    local s
    mkdir -p "$2"
    for f in "$1"/*.[Hh] "$1"/*.[Vv][Ee][Rr] "$1"/*.[Dd][Ll][Gg] "$1"/*.[Dd][Bb][Gg]; do
        [ -e "$f" ] || continue
        filt "$f" > "$2/$(basename "$f" | tr A-Z a-z)"
    done
    # the sys\ headers, in their directory (incfix.sed)
    s=$(ls -d "$1"/[Ss][Yy][Ss] 2>/dev/null) && farm "$s" "$2/sys"
}

farm $W/PRIVATE/NTOS/INC $F/priv
farm $W/PRIVATE/INC $F/pinc
farm $W/PUBLIC/SDK/INC $F/pub
farm $W/PUBLIC/OAK/INC $F/oak
farm $W/PUBLIC/SDK/INC/CRT $F/crt
farm $W/PUBLIC/SDK/INC/GL $F/pub
farm $W/PRIVATE/CRT32NT/H $F/crth
farm $W/PRIVATE/CSR/INC $F/csr
farm $W/PRIVATE/LSA/CLIENT $F/lsa
farm $W/PRIVATE/LSA/INC $F/lsa
# PRIVATE\WINDOWS\INC also holds the sources of the headers its makefil0 makes.
mkdir -p $F/winc
for f in $W/PRIVATE/WINDOWS/INC/*; do
    filt "$f" > "$F/winc/$(basename "$f" | tr A-Z a-z)"
done
for S in $SUBSYS; do farm $W/PRIVATE/NTOS/$S $F/$(echo $S | tr A-Z a-z); done

# The ARM port versions of genuine headers (arcfw/inc), ahead of the farm on every NT
# build: arc.h (the ARM loader block).
mkdir -p $F/ovr
for h in $W/RASPI/arcfw/inc/*.h; do filt "$h" > $F/ovr/$(basename $h); done
# GDI's EFLOAT (engine.hxx includes math\<machine>\efloat.hxx): that of ARM is the
# IEEE single precision class of the RISC machines, MATH\MIPS\EFLOAT.HXX.
tr -d '\032\r' < $W/PRIVATE/WINDOWS/GDI/MATH/MIPS/EFLOAT.HXX | sed -E -f "$K/riscgate.sed" -f "$K/incfix.sed" > $F/ovr/efloat.hxx

# mm/mi.h pulls the per-arch MM header; add the ARM one ahead of the i386 gate.
[ -f $F/mm/mi.h ] && sed -i '/^#ifdef i386/i #ifdef _ARM_\n#include <miarm.h>\n#endif' $F/mm/mi.h
printf '#ifndef _V_\n#define _V_\n#define VER_PRODUCTBUILD 782\n#endif\n' > $F/priv/version.h

# The header pass (mkgen.sh).
[ -n "${MKGEN:-}" ] || cp -r $G/. $F/

# The MIDL pass (tools/midl/gen-midl.sh): stubs and headers to $F/gen, those of
# PRIVATE\INC also to its farm.
if [ -d $W/RASPI/obj/gen/midl ]; then
    mkdir -p $F/gen && cp -r $W/RASPI/obj/gen/midl/. $F/gen/
    farm $F/gen/INC $F/pinc
fi

# GENERR (tools/make-tools.sh): rtl/error.h.
if [ -f $W/RASPI/obj/gen/error.h ]; then
    [ -d $F/rtl ] && cp $W/RASPI/obj/gen/error.h $F/rtl/error.h
    mkdir -p $F/gen/NTOS/RTL && cp $W/RASPI/obj/gen/error.h $F/gen/NTOS/RTL/error.h
fi

# What the builds of other components generated for the ones that include it (gensave.sh).
[ ! -d $W/RASPI/obj/gen/tree ] || { mkdir -p $F/gen && cp -r $W/RASPI/obj/gen/tree/. $F/gen/; }
touch $F/.genbase
exit 0
