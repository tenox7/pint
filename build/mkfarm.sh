#!/bin/bash
# Build the NT header farm under $1 (default /tmp/f). NT's headers, C and C++ (and the
# .ver and .dlg files resource scripts include, the .dbg status names of the debug
# tools: ntstatus.dbg) have RISC-only arch gates and Windows directory names in
# their includes; copy them flat and lower-case, strip any ^Z/CR, add _ARM_ to the
# RISC gates and normalize the #include names. Then adds what the build generates: the headers and sources of
# its header pass (mkgen.sh, obj/gen/farm, made again when an input of the ARM
# port is newer), version.h, the MIDL pass (obj/gen/midl) and GENERR's error.h.
# The farm of a SUBSYS is kept as obj/farm/<key>.tar, which the next builds unpack: the
# NT tree does not change, and the farm is made again when an input of the port is newer.
set -u
F=${1:-/tmp/f}
W=${NTROOT:-/work}
K=$W/RASPI/kernel B=$W/RASPI/build
G=$W/RASPI/obj/gen/farm
SUBSYS=${SUBSYS:-ke rtl ex ob ps mm io se config lpc cache fsrtl fstub raw dbgk kd vdm po pnp init}

# the header pass, when its inputs of the ARM port (the arch headers, the port's versions
# of NT headers, the ported makefiles, the filters, the tools) are newer than its output
if [ -z "${MKGEN:-}" ] && { [ ! -f $G/.stamp ] || [ -n "$(find $K/inc $W/RASPI/arcfw/inc $W/RASPI/ported/inc $W/RASPI/ported/ntos/inc $W/RASPI/ported/ntos/makefil0 $W/RASPI/ported/windows/makefil0 $B/*.sed $B/nmk.awk $B/mkgen.sh $(for t in mc hextract hsplit wcshdr ws2hdr zwapi; do echo $W/RASPI/obj/host/$(echo $t | tr a-z A-Z).EXE; done) -newer $G/.stamp -print -quit 2>/dev/null)" ]; }; then
    bash $B/mkgen.sh >&2 || exit 1
fi

# What the builds of other components generated for the ones that include it (gensave.sh).
tree() {
    [ ! -d $W/RASPI/obj/gen/tree ] || { mkdir -p $F/gen && cp -r $W/RASPI/obj/gen/tree/. $F/gen/; }
    [ ! -d $W/RASPI/obj/gen/mc ] || { mkdir -p $F/mc && cp -r $W/RASPI/obj/gen/mc/. $F/mc/; }
    touch $F/.genbase
}

T=$W/RASPI/obj/farm/$(echo "$SUBSYS" | md5sum | cut -c1-12).tar
# (the port's versions of the files of the farmed directories, portdir.sh, are inputs too)
P=$W/RASPI/ported
PD="$P/ntos/inc $P/inc $P/csr/inc $P/lsa/client $P/lsa/inc $P/crt32/h $P/windows/inc $P/windows/inc16 $(for S in $SUBSYS; do echo $P/ntos/$S; done)"
if [ -z "${MKGEN:-}" ] && [ -f $T ] && [ -z "$(find $B/*.sed $B/mkfarm.sh $W/RASPI/arcfw/inc $PD $G/.stamp $W/RASPI/obj/gen/midl $W/RASPI/obj/gen/error.h -newer $T -print -quit 2>/dev/null)" ]; then
    mkdir -p $F && tar -xf $T -C $F && tree
    exit 0
fi

filt() { tr -d '\032\r' < "$1" | sed -E -f "$B/riscgate.sed" -f "$B/msvcgate.sed" -f "$B/incfix.sed" | sed -f "$B/armport.sed" -f "$B/msc2.sed"; }
farm() {
    local s b p=$(bash $B/portdir.sh "$1")
    mkdir -p "$2"
    for f in "$1"/*.[Hh] "$1"/*.[Hh][Xx][Xx] "$1"/*.[Hh][Pp][Pp] "$1"/*.[Vv][Ee][Rr] "$1"/*.[Dd][Ll][Gg] "$1"/*.[Dd][Bb][Gg]; do
        [ -e "$f" ] || continue
        # or the port's version of it (portdir.sh)
        b=$(basename "$f" | tr A-Z a-z); [ -f "$p/$b" ] && f=$p/$b
        filt "$f" > "$2/$b"
    done
    # the sys\ headers, in their directory (incfix.sed)
    s=$(ls -d "$1"/[Ss][Yy][Ss] 2>/dev/null) && farm "$s" "$2/sys"
}

farm $W/private/ntos/inc $F/priv
# The port's ntos.h (kernel/inc) beside the headers it includes, as in ntos\inc: its quoted
# includes ("io.h") find them there before the include path (the C run-time's io.h).
cp $K/inc/ntos.h $F/priv/ntos.h
farm $W/private/inc $F/pinc
# with the sockets headers, which includes name by their directories (<sockets\arpa\nameser.h>,
# <sys\snet\ip_proto.h>), flat as incfix.sed names them; but sys\snet\snmp.h: <snmp.h> is
# the SDK's (no include names that one by its directory)
for d in sockets sockets/arpa sockets/netinet sys/snet; do
    p=$(bash $B/portdir.sh $W/private/inc/$d)
    for f in $W/private/inc/$d/*.h; do
        b=$(basename $f); [ -e $W/public/sdk/inc/$b ] && continue
        [ -f $p/$b ] && f=$p/$b
        filt $f > $F/pinc/$b
    done
done
farm $W/public/sdk/inc $F/pub
# and the interfaces of the SDK, which MIDL imports from it (types\filter\idl: objidl.idl)
for f in $W/public/sdk/inc/*.idl; do filt "$f" > $F/pub/$(basename "$f" | tr A-Z a-z); done
farm $W/public/oak/inc $F/oak
farm $W/public/sdk/inc/crt $F/crt
farm $W/public/sdk/inc/gl $F/pub
# and in their directory, where resource scripts find them (RC takes include names as they
# are: <GL\gl.h> of the OpenGL screen savers)
farm $W/public/sdk/inc/gl $F/pub/gl
# the POSIX headers in their directory, which sources name by it (incfix.sed)
farm $W/public/sdk/inc/posix $F/pub/posix
farm $W/private/crt32/h $F/crth
farm $W/private/csr/inc $F/csr
farm $W/private/lsa/client $F/lsa
farm $W/private/lsa/inc $F/lsa
# private\windows\inc also holds the sources of the headers its makefil0 makes.
mkdir -p $F/winc
for f in $W/private/windows/inc/*; do
    filt "$f" > "$F/winc/$(basename "$f" | tr A-Z a-z)"
done
# private\windows\inc16, which its headers and sources name by its path (incfix.sed)
farm $W/private/windows/inc16 $F/inc16
for S in $SUBSYS; do farm $W/private/ntos/$S $F/$S; done

# The ARM port versions of genuine headers (arcfw/inc), ahead of the farm on every NT
# build: arc.h (the ARM loader block).
mkdir -p $F/ovr
for h in $W/RASPI/arcfw/inc/*.h; do [ -f $h ] && filt "$h" > $F/ovr/$(basename $h); done
# GDI's EFLOAT (engine.hxx includes math\<machine>\efloat.hxx): that of ARM is the
# IEEE single precision class of the RISC machines, math\mips\efloat.hxx.
tr -d '\032\r' < $W/private/ntos/w32/ntgdi/math/mips/efloat.hxx | sed -E -f "$B/riscgate.sed" -f "$B/incfix.sed" > $F/ovr/efloat.hxx

printf '#ifndef _V_\n#define _V_\n#define VER_PRODUCTBUILD 1381\n#endif\n' > $F/priv/version.h

# The header pass (mkgen.sh).
[ -n "${MKGEN:-}" ] || cp -r $G/. $F/

# The MIDL pass (tools/midl/gen-midl.sh): stubs and headers to $F/gen, those of
# private\inc also to its farm.
if [ -d $W/RASPI/obj/gen/midl ]; then
    mkdir -p $F/gen && cp -r $W/RASPI/obj/gen/midl/. $F/gen/
    farm $F/gen/inc $F/pinc
fi

# GENERR (tools/make-tools.sh): rtl/error.h.
if [ -f $W/RASPI/obj/gen/error.h ]; then
    [ -d $F/rtl ] && cp $W/RASPI/obj/gen/error.h $F/rtl/error.h
    mkdir -p $F/gen/ntos/rtl && cp $W/RASPI/obj/gen/error.h $F/gen/ntos/rtl/error.h
fi

[ -n "${MKGEN:-}" ] || { mkdir -p $(dirname $T) && t=$(mktemp $T.XXXXXX) && tar -cf $t -C $F . && mv $t $T; }
tree
exit 0
