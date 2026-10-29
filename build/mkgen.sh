#!/bin/bash
# The header pass of the NT build (the makefil0 and mc recipes): the headers and
# sources NT makes with its build tools (mc, hextract, hsplit, wcshdr, ws2hdr, zwapi
# of obj/host, tools/make-tools.sh) from the farm of mkfarm.sh, saved
# to obj/gen/farm, which mkfarm.sh adds to every farm (and runs this again when
# an input of the ARM port changed). The tools write DOS line ends; the farm has
# none.
set -u
F=/tmp/f
W=${NTROOT:-/work}
K=$W/RASPI/kernel B=$W/RASPI/build
G=$W/RASPI/obj/gen/farm
HOST="bash $W/RASPI/tools/host.sh"
for t in mc hextract hsplit wcshdr ws2hdr zwapi; do
    [ -f $W/RASPI/obj/host/$(echo $t | tr a-z A-Z).EXE ] || { echo "mkgen: no obj/host/$t.exe (tools/make-tools.sh)" >&2; exit 1; }
done
rm -rf $G $F
MKGEN=1 bash $B/mkfarm.sh $F
touch /tmp/mkgen.stamp

# Message files: the mc recipes of the makefil0 pass (that of windows\makefil0 runs with
# its other recipes, below). Headers go where the build finds them (ntos\inc -> priv, an
# ntos component -> its farm directory; the shipped SDK headers are kept), the resource
# scripts and message tables to $F/mc/<directory>, where rc.sh has RC find them.
mcpath() {
    local p=$1 c n
    for c in $(echo "$2" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname $p) ;;
        .) ;;
        *) n=$(ls $p 2>/dev/null | grep -ix "$c" | head -1); p=$p/${n:-$c} ;;
        esac
    done
    echo $p
}
# mc names its outputs as its input (BUGCODES.h): the farm has them lower case
mclower() { local f; for f in $(find "$@" -maxdepth 1 -type f -name "*[A-Z]*" \( -name "*.h" -o -name "*.rc" \) | sort -u); do mv "$f" "$(dirname "$f")/$(basename "$f" | tr A-Z a-z)"; done; }
for mk in . ntos/dd; do
    b=$(mcpath $W/private $mk)
    tr -d '\r' < $b/makefil0 | sed -n 's/^[[:space:]]*mc \(.*\)/\1/p' | while read -r a; do
        set -- $a; o=; r=; h=
        while [ $# -gt 1 ]; do
            case $1 in -r) r=$2; shift 2 ;; -h) h=$2; shift 2 ;; -x) shift 2 ;; *) o="$o $1"; shift ;; esac
        done
        rd=$(mcpath $b "$r"); rd=$F/mc/$(echo ${rd#$W/private/} | tr A-Z a-z)
        case $h in
        *SDKINC*) hd=$rd ;;
        *) hd=$(mcpath $b "$h")
           case $hd in
           $W/private/ntos/inc) hd=$F/priv ;;
           $W/private/ntos/*) hd=$F/$(echo ${hd#$W/private/ntos/} | tr A-Z a-z) ;;
           *) hd=$rd ;;
           esac ;;
        esac
        mkdir -p $rd $hd
        $HOST mc $o -h $hd -r $rd $(mcpath $b "$1") < /dev/null > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; echo "mkgen: mc $a failed" >&2; }
        mclower $hd $rd
    done
done

# The headers NT generates from tagged sections of its headers with hextract, made as
# windows\makefil0 (winnt.h) and ntos\makefil0 (the driver and HAL headers) make them;
# the ported makefiles in ported/ add ARM. Inputs resolve as the kernel's include
# path does. hextract takes / as a switch character, so paths are farm-relative.
# The Windows headers are made from their .w sources by hsplit (the public and
# private parts), wcshdr (the A/W name forms) and ws2hdr (winsock2.h).
# The commands run in NMK_CWD ($F); NMK_DIR is the directory of the makefile, NMK_INC
# the farm directory of its inc\.
mkdir -p $F/nmk
# The file of a name in a recipe: the farm's (inc\ that of NMK_INC, the SDK, OAK and
# NTOS include directories theirs), else, in another directory of the makefile, one of
# the files generated for that directory ($F/gen), where the builds find them.
nmk_out() {
    [ -z "${NMK_MIRROR:-}" ] || { case $1 in nul) echo /dev/null ;; SDKINC) echo sdkinc ;; SDKINC/*) echo "sdkinc/${1#*/}" ;; *) echo "$1" ;; esac; return; }
    case $1 in
    nul) echo /dev/null ;;
    inc/*) echo "$NMK_INC/${1#*/}" ;;
    NTOSINC/*) echo "priv/${1#*/}" ;;
    SDKINC/*) echo "pub/${1#*/}" ;;
    OAKINC/*) echo "oak/${1#*/}" ;;
    *) echo "gen/$NMK_DIR/$1" ;;
    esac
}
# an output, its directory made
nmk_o() { local o; o=$(nmk_out "$1"); mkdir -p "$(dirname "$o")"; echo "$o"; }
# an input: a file of the farm or one the pass made, else that of the tree, filtered as
# mkfarm.sh filters the headers of the farm
nmk_in() {
    local o t
    o=$(nmk_out "$1")
    [ -z "${NMK_MIRROR:-}" ] && [ ! -e "$o" ] || { echo "$o"; return; }
    case $1 in
    NTOSINC/*) t=$W/private/ntos/inc/${1#*/} ;;
    SDKINC/*) t=$W/public/sdk/inc/${1#*/} ;;
    OAKINC/*) t=$W/public/oak/inc/${1#*/} ;;
    BASEDIR/*) t=$W/${1#*/} ;;
    *) t=$W/private/$NMK_DIR/$1 ;;
    esac
    [ -f "$t" ] || { echo "$o"; return; }
    o=nmk/tree/$(realpath -m --relative-to=$W "$t")
    mkdir -p "$(dirname "$o")"
    tr -d '\032\r' < "$t" | sed -E -f $B/riscgate.sed -f $B/msvcgate.sed -f $B/incfix.sed | sed -f $B/armport.sed -f $B/msc2.sed > "$o"
    echo "$o"
}
# a header to extract or copy: the port's version first (kernel/inc), else as nmk_in, else
# the farm's header of its name
nmk_path() {
    local b d p
    [ -z "${NMK_MIRROR:-}" ] || { nmk_out "$1"; return; }
    b=$(basename "$1" | tr A-Z a-z)
    [ -f $K/inc/$b ] && { tr -d '\032\r' < $K/inc/$b > nmk/$b; echo nmk/$b; return; }
    p=$(nmk_in "$1"); [ -f "$p" ] && { echo "$p"; return; }
    for d in ovr priv oak pub crt; do [ -f $d/$b ] && { echo $d/$b; return; }; done
    echo "$p"
}
nmk_copy() { local d; d=$(nmk_o "$2"); [ -d "$d" ] && d=$d/$(basename "$1"); cp "$(nmk_path "$1")" "$d"; }
nmk_type() { cat "$(nmk_path $1)" > "$(nmk_o $2)"; }
nmk_new() { cat > "$(nmk_o $1)"; }
nmk_add() { cat >> "$(nmk_o $1)"; }
nmk_cat() { cat "$(nmk_path $1)" >> "$(nmk_o $2)"; }
nmk_del() { local f; for f; do rm -f "$(nmk_out "$f")"; done; }
# hsplit: -o names its two outputs, -c, -lt2 and -ltb take a value, -bt2 and -btb two,
# the names that follow the options are its inputs
nmk_hsplit() {
    local a=()
    while [ $# -gt 0 ]; do
        case $1 in
        -[Oo]) a+=(-o "$(nmk_o $2)" "$(nmk_o $3)"); shift 3 ;;
        -[Cc]|-[Ll][Tt]?) a+=("$1" "$2"); shift 2 ;;
        -[Bb][Tt]?) a+=("$1" "$2" "$3"); shift 3 ;;
        -*) a+=("$1"); shift ;;
        *) a+=("$(nmk_in $1)"); shift ;;
        esac
    done
    $HOST hsplit "${a[@]}"
}
# the filters wcshdr and ws2hdr: NAME INPUT OUTPUT, - for a pipe
nmk_filter() {
    local t=$1 i=$2 o=$3
    if [ "$i" = - ]; then $HOST $t; else $HOST $t < "$(nmk_in "$i")"; fi | if [ "$o" = - ]; then cat; else cat > "$(nmk_o "$o")"; fi
}
nmk_wcshdr() { nmk_filter wcshdr "$@"; }
nmk_ws2hdr() { nmk_filter ws2hdr "$@"; }
nmk_hextract() {
    local a=()
    while [ $# -gt 0 ]; do
        case $1 in
        -o) a+=(-o "$(nmk_o $2)"); shift 2 ;;
        -lt) a+=(-lt "$2"); shift 2 ;;
        -bt) a+=(-bt "$2" "$3"); shift 3 ;;
        -f) a+=(-f); shift ;;
        *) a+=("$(nmk_path $1)"); shift ;;
        esac
    done
    $HOST hextract "${a[@]}"
}
# mc, as the mc recipes above: the header to the directory -h names (the shipped SDK
# headers kept), the resource script and message tables to $F/mc/<the directory -r names>
nmk_mc() {
    local o= r=. h= f= rd hd
    while [ $# -gt 0 ]; do
        case $1 in -r) r=$2; shift 2 ;; -h) h=$2; shift 2 ;; -x) shift 2 ;; -*) o="$o $1"; shift ;; *) f=$1; shift ;; esac
    done
    rd=$F/mc/$NMK_DIR/${r%/}
    case $h in
    SDKINC*|'') hd=$rd ;;
    inc|inc/) hd=$NMK_INC ;;
    NTOSINC*) hd=priv ;;
    *) hd=gen/$NMK_DIR/${h%/} ;;
    esac
    mkdir -p "$rd" "$hd"
    $HOST mc $o -h "$hd" -r "$rd" "$(nmk_in "$f")" > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; return 1; }
    mclower "$hd" "$rd"
}
# deps0 MAKEFILE TARGET: the dependents of the rule of TARGET, one a line
deps0() { NMK_DEPS=1 NMK_TARGET="$2" awk -f $B/nmk.awk $1 | tr -s " \t" "\n\n" | grep -v "^$"; }
# nmk MAKEFILE TARGET: run the recipe of TARGET
nmk() {
    NMK_TARGET="$2" awk -f $B/nmk.awk $1 > /tmp/nmk.sh
    (cd $NMK_CWD && . /tmp/nmk.sh) < /dev/null || echo "mkgen: $2 failed" >&2
}
# make0 MAKEFILE TARGET: make TARGET as nmake does, first the dependents the makefile has
# rules for; each rule once
made0=" "
make0() {
    local h d
    h=$(NMK_RULE=1 NMK_TARGET="$2" awk -f $B/nmk.awk $1) || return 0
    h="$1:$(printf '%s' "$h" | tr -s ' \t' '||')"
    case $made0 in *" $h "*) return 0 ;; esac
    made0="$made0$h "
    while read -r d; do make0 $1 "$d"; done < <(deps0 $1 "$2")
    nmk $1 "$2"
}

# windows\makefil0: all its targets
NMK_CWD=$F NMK_DIR=windows NMK_INC=winc
while read -r t; do make0 $W/RASPI/ported/windows/makefil0 "$t"; done < <(deps0 $W/RASPI/ported/windows/makefil0 all)
# ntos\makefil0: its headers (the rules of its other targets run the root makefil0 and
# that of ntos\dd, whose mc recipes made them above)
NMK_DIR=ntos NMK_INC=priv
for t in 'inc\ntddk.h' 'inc\nthal.h' 'inc\miniport.h' 'inc\minitape.h' 'inc\ntifs.h' 'inc\ntsrv.h' 'inc\ndis.h' \
         '$(SDKINC)\windbgkd.h'; do
    nmk $W/RASPI/ported/ntos/makefil0 "$t"
done

# media\avi\makefil0, run as in NT's tree, on a lower case mirror of the AVI directory: the
# headers and resource scripts it copies within the tree go to $F/gen, where the AVI
# builds find them, and the SDK headers it makes of them (vfw.h, mciavi.h) to the farm.
A=$W/private/windows/media/avi M=/tmp/nmkavi
rm -rf $M && mkdir -p $M/sdkinc
(cd $A && find . -type f) | while read -r f; do
    grep -Iq . "$A/$f" || continue
    l=$(echo "$f" | tr A-Z a-z); mkdir -p $M/$(dirname $l); tr -d '\032\r' < "$A/$f" > $M/$l
done
NMK_CWD=$M NMK_MIRROR=1
while read -r t; do
    make0 $M/makefil0 "$t"
    case $t in
    SDKINC/*) cp $M/sdkinc/${t##*/} $F/pub/ ;;
    *) d=$F/gen/windows/media/avi/$(dirname $t); mkdir -p $d && cp $M/$t $d/ ;;
    esac
done < <(deps0 $M/makefil0 all)
NMK_CWD=$F NMK_MIRROR=

# zwapi.h (public\oak\inc), made as mkzw.cmd makes it: the ZWAPI tool turns the Nt
# system service prototypes of nt*api.h into their Zw forms.
rm -f $F/oak/zwapi.h
(cd $F && $HOST zwapi -o oak/zwapi.h $(ls pub/nt*api.h | sort))

# What the pass made, as the farm has it (the message tables are binary).
cd $F && find . -type f -newer /tmp/mkgen.stamp ! -name .farmed ! -path "./nmk/*" | while read -r f; do
    mkdir -p "$G/$(dirname "$f")"
    case $f in *.[Bb][Ii][Nn]) cp "$f" "$G/$f" ;; *) tr -d '\r' < "$f" > "$G/$f" ;; esac
done
: > $G/.stamp
