#!/bin/bash
# Print the -I flags for an NT component as build.exe forms them: the component
# directory, then its SOURCES INCLUDES (resolved case-insensitively, as NT's file
# system does). NT's common include directories map to the header farm
# (mkfarm.sh), as do the components it farmed; any other directory is farmed
# under /tmp/f/dirs (its text files: headers and the other files sources include),
# with the files the build generated for it (/tmp/f/gen) and the message headers of
# its makefile.inc (mcinc.sh).
# Usage: incdirs.sh componentdir "INCLUDES value"
K=$(cd "$(dirname "$0")" && pwd)
# Microsoft C takes the sources as they are but for the ARM gates.
filt() { tr -d '\032\r' < "$1" | sed -E -f $K/riscgate.sed -f $K/msvcgate.sed -f $K/incfix.sed | sed -f $K/armport.sed; }
farm() {
    local s
    [ -e "$2/.farmed" ] && return
    mkdir -p "$2" && : > "$2/.farmed"
    # the message headers its makefile.inc makes, as the NT build made them first
    bash $K/mcinc.sh "$1" >&2
    for f in "$1"/* "/tmp/f/gen/${1#/work/PRIVATE/}"/*; do
        [ -f "$f" ] && grep -Iq . "$f" || continue
        filt "$f" > "$2/$(basename "$f" | tr A-Z a-z)"
    done
    # the sys\ headers, in their directory (incfix.sed)
    s=$(ls -d "$1"/[Ss][Yy][Ss] 2>/dev/null) && farm "$s" "$2/sys"
}
resolve() {
    local p=$1 c n
    for c in $(echo "$2" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
# A directory of the NT tree: $(BASEDIR), $(_NTROOT), $(_NTBINDIR) or $(_NTDRIVE)\nt.
include() {
    case $2 in
    '$(BASEDIR)'*|'$(_NTROOT)'*|'$(_NTBINDIR)'*) resolve /work "${2#*)}" ;;
    '$(_NTDRIVE)\nt\'*|'\nt\'*) resolve /work "${2#*\\nt\\}" ;;
    *) resolve "$1" "$2" ;;
    esac
}
for p in "$1" $(for i in $(echo "$2" | tr ';' ' '); do include "$1" "$i"; done); do
    case $p in
    /work/PRIVATE/NTOS/INC) echo -n " -I/tmp/f/priv" ;;
    /work/PRIVATE/INC) echo -n " -I/tmp/f/pinc" ;;
    /work/PUBLIC/SDK/INC) echo -n " -I/tmp/f/pub" ;;
    /work/PUBLIC/OAK/INC) echo -n " -I/tmp/f/oak" ;;
    /work/PRIVATE/WINDOWS/INC) echo -n " -I/tmp/f/winc" ;;
    /work/PRIVATE/NTOS/*) t=/tmp/f/$(echo ${p#/work/PRIVATE/NTOS/} | tr A-Z a-z)
        [ -d $t ] || { t=/tmp/f/dirs$p; farm "$p" $t; }
        echo -n " -I$t" ;;
    *) farm "$p" "/tmp/f/dirs$p"; echo -n " -I/tmp/f/dirs$p" ;;
    esac
done
# the headers of the ported sources of the component (arcfw/ported/<component>)
P=/work/RASPI/arcfw/ported/$(echo ${1#/work/PRIVATE/} | tr A-Z a-z)
[ ! -d $P ] || echo -n " -I$P"
