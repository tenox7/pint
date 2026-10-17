#!/bin/bash
# The header pass of the NT build (the MAKEFIL0 and mc recipes): the headers and
# sources NT makes with its build tools (mc, hextract, hsplit, wcshdr, zwapi,
# listmung of obj/host, tools/make-tools.sh) from the farm of mkfarm.sh, saved
# to obj/gen/farm, which mkfarm.sh adds to every farm (and runs this again when
# an input of the ARM port changed). The tools write DOS line ends; the farm has
# none.
set -u
F=/tmp/f
W=${NTROOT:-/work}
K=$W/RASPI/arcfw/kernel
G=$W/RASPI/obj/gen/farm
HOST="bash $W/RASPI/tools/host.sh"
for t in mc hextract hsplit wcshdr zwapi listmung; do
    [ -f $W/RASPI/obj/host/$(echo $t | tr a-z A-Z).EXE ] || { echo "mkgen: no obj/host/$t.exe (tools/make-tools.sh)" >&2; exit 1; }
done
rm -rf $G $F
MKGEN=1 bash $K/mkfarm.sh $F
U=$W/PRIVATE/WINDOWS/USER
UI=$(bash $K/incdirs.sh $U/INC "" | sed 's/^ -I//')
touch /tmp/mkgen.stamp

# Message files: the mc recipes of the makefil0 pass. Headers go where the build
# finds them (NTOS\INC -> priv, an NTOS component -> its farm directory; the shipped
# SDK headers are kept), the resource scripts and message tables to $F/mc/<directory>,
# where rc.sh has RC find them.
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
for mk in . NTOS NTOS/DD WINDOWS; do
    b=$(mcpath $W/PRIVATE $mk)
    tr -d '\r' < $b/MAKEFIL0 | sed -n 's/^[[:space:]]*mc \(.*\)/\1/p' | while read -r a; do
        set -- $a; o=; r=; h=
        while [ $# -gt 1 ]; do
            case $1 in -r) r=$2; shift 2 ;; -h) h=$2; shift 2 ;; -x) shift 2 ;; *) o="$o $1"; shift ;; esac
        done
        rd=$(mcpath $b "$r"); rd=$F/mc/$(echo ${rd#$W/PRIVATE/} | tr A-Z a-z)
        case $h in
        *SDKINC*) hd=$rd ;;
        *) hd=$(mcpath $b "$h")
           case $hd in
           $W/PRIVATE/NTOS/INC) hd=$F/priv ;;
           $W/PRIVATE/NTOS/*) hd=$F/$(echo ${hd#$W/PRIVATE/NTOS/} | tr A-Z a-z) ;;
           *) hd=$rd ;;
           esac ;;
        esac
        mkdir -p $rd $hd
        $HOST mc $o -h $hd -r $rd $(mcpath $b "$1") > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; echo "mkgen: mc $a failed" >&2; }
        mclower $hd $rd
    done
done

# The headers NT generates from tagged sections of its headers with hextract, made as
# WINDOWS\MAKEFIL0 (winnt.h) and NTOS\MAKEFIL0 (the driver and HAL headers) make them;
# the ported makefiles in arcfw/ported add ARM. Inputs resolve as the kernel's include
# path does. hextract takes / as a switch character, so paths are farm-relative.
# The Windows headers are made from their .w sources by hsplit (the public and
# private parts) and wcshdr (the A/W name forms).
mkdir -p $F/nmk
nmk_out() {
    [ -z "${NMK_MIRROR:-}" ] || { case $1 in nul) echo /dev/null ;; SDKINC) echo sdkinc ;; SDKINC/*) echo "sdkinc/${1#*/}" ;; *) echo "$1" ;; esac; return; }
    case $1 in
    nul) echo /dev/null ;;
    inc/*) echo "$NMK_INC/${1#*/}" ;;
    NTOSINC/*) echo "priv/${1#*/}" ;;
    SDKINC/*) echo "pub/${1#*/}" ;;
    OAKINC/*) echo "oak/${1#*/}" ;;
    media/*) echo "gen/WINDOWS/$(dirname $1 | tr a-z A-Z)/${1##*/}" ;;
    *) echo "$1" ;;
    esac
}
nmk_path() {
    local b d
    [ -z "${NMK_MIRROR:-}" ] || { nmk_out "$1"; return; }
    b=$(basename "$1" | tr A-Z a-z)
    [ -f $K/inc/$b ] && { tr -d '\032\r' < $K/inc/$b > nmk/$b; echo nmk/$b; return; }
    for d in ovr priv oak pub crt; do [ -f $d/$b ] && { echo $d/$b; return; }; done
    echo "$1"
}
nmk_copy() { local d; d=$(nmk_out "$2"); [ -d "$d" ] && d=$d/$(basename "$1"); cp "$(nmk_path "$1")" "$d"; }
nmk_type() { cat "$(nmk_path $1)" > "$(nmk_out $2)"; }
nmk_new() { cat > "$(nmk_out $1)"; }
nmk_add() { cat >> "$(nmk_out $1)"; }
nmk_cat() { cat "$(nmk_path $1)" >> "$(nmk_out $2)"; }
nmk_hsplit() {
    local a=() x
    for x in "$@"; do case $x in -*) a+=("$x") ;; *) a+=("$(nmk_out $x)") ;; esac; done
    $HOST hsplit "${a[@]}"
}
nmk_wcshdr() { $HOST wcshdr < "$(nmk_out $1)" > "$(nmk_out $2)"; }
nmk_hextract() {
    local a=()
    while [ $# -gt 0 ]; do
        case $1 in
        -o) a+=(-o "$(nmk_out $2)"); shift 2 ;;
        -lt) a+=(-lt "$2"); shift 2 ;;
        -bt) a+=(-bt "$2" "$3"); shift 3 ;;
        -f) a+=(-f); shift ;;
        *) a+=("$(nmk_path $1)"); shift ;;
        esac
    done
    $HOST hextract "${a[@]}"
}
nmk() {
    NMK_TARGET="$2" awk -f $K/nmk.awk $W/RASPI/arcfw/ported/$1/makefil0 > /tmp/nmk.sh
    (cd $F && NMK_INC=$3 && . /tmp/nmk.sh) || echo "mkgen: $2 failed" >&2
}
nmk windows '$(SDKINC)\winnt.h' winc
for t in ntddk nthal miniport ntifs updriver; do nmk ntos "inc\\$t.h" priv; done
while read -r t; do nmk windows "$t" winc; done <<'EOF'
inc\winbase.x inc\winbasep.x
$(SDKINC)\winbase.h
inc\winbasep.h
inc\winuser.x inc\winuserp.x
$(SDKINC)\winuser.h
inc\winuserp.h
$(SDKINC)\windef.h inc\windefp.h
inc\wingdi.x inc\wingdip.x
$(SDKINC)\wingdi.h
inc\wingdip.h
inc\winnls.x inc\winnlsp.h
$(SDKINC)\winnls.h
inc\winspool.x inc\winsprlp.x
$(SDKINC)\winspool.h
inc\ddeml.x inc\ddemlp.h
$(SDKINC)\ddeml.h
$(SDKINC)\winreg.h
$(SDKINC)\wincon.h
$(SDKINC)\winnetwk.h
$(SDKINC)\winsvc.h
$(SDKINC)\winver.h
$(SDKINC)\lzexpand.h
$(SDKINC)\nspapi.h
$(SDKINC)\digitalv.h
inc\conapi.h
$(OAKINC)\winsecp.h
inc\mmsystem.x
$(SDKINC)\mmsystem.h
EOF
# the internal mmsystem.h of winmm (all of mmsystem.w), where its build finds generated files
mkdir -p $F/gen/WINDOWS/MEDIA/WINMM
nmk windows 'media\winmm\mmsystem.h' winc

# MEDIA\AVI\MAKEFIL0, run as in NT's tree, on a lower case mirror of the AVI directory: the
# headers, resource scripts and source it copies within the tree go to $F/gen, where the AVI
# builds find them, and the SDK headers it makes of them (vfw.h, mciavi.h) to the farm.
A=$W/PRIVATE/WINDOWS/MEDIA/AVI M=/tmp/nmkavi
rm -rf $M && mkdir -p $M/sdkinc
(cd $A && find . -type f) | while read -r f; do
    grep -Iq . "$A/$f" || continue
    l=$(echo "$f" | tr A-Z a-z); mkdir -p $M/$(dirname $l); tr -d '\032\r' < "$A/$f" > $M/$l
done
for t in 'inc\msvideo.h' 'inc\msviddrv.h' 'inc\mciavi.h' 'inc\drawdib.h' 'inc\compman.h' 'inc\compddk.h' \
         'mciavi32\mciavi.rc' 'video\msvideo.rc' 'mciavi32\mciavi.rcv' 'video\msvideo.rcv' 'mciavi32\cnfgdlg.dlg' \
         'drawdib\lockbm.c' 'inc\icm.rc' 'inc\dith775.h' 'inc\aviiface.h' 'inc\avifile.h' 'inc\avicap.h' \
         'inc\profile.h' 'inc\mciwnd.h' 'inc\preview.h' 'inc\ivideo32.h' '$(SDKINC)\vfw.h' '$(SDKINC)\mciavi.h'; do
    NMK_TARGET="$t" awk -f $K/nmk.awk $M/makefil0 > /tmp/nmk.sh
    (cd $M && NMK_MIRROR=1 && . /tmp/nmk.sh) || { echo "mkgen: avi $t failed" >&2; continue; }
    f=$(echo "$t" | tr '\\' /)
    case $f in
    '$(SDKINC)'/*) cp $M/sdkinc/${f##*/} $F/pub/ ;;
    *) d=$F/gen/WINDOWS/MEDIA/AVI/$(dirname $f | tr a-z A-Z); mkdir -p $d && cp $M/$f $d/ ;;
    esac
done

# zwapi.h (public\oak\inc), made as MKZW.CMD makes it: the ZWAPI tool turns the Nt
# system service prototypes of nt*api.h into their Zw forms.
rm -f $F/oak/zwapi.h
(cd $F && $HOST zwapi -o oak/zwapi.h $(ls pub/nt*api.h | sort))

# USER\MAKEFIL0: the client/server API indices and dispatch tables, made by the
# listmung tool from the .lst lists and .tpl templates (listmung takes / as a
# switch character, so it runs where its inputs are). The headers go into the
# farm of USER\INC, the sources to $F/gen, where the builds find generated sources.
mkdir -p /tmp/listmung
listmung() {
    local l t
    l=/tmp/listmung/$(basename $1); t=/tmp/listmung/$(basename $2)
    tr -d '\032\r' < $U/$1 > $l; tr -d '\032\r' < $U/$2 > $t
    (cd /tmp/listmung && $HOST listmung $(basename $l) $(basename $t)) | tr -d '\r' | sed -E -f $K/riscgate.sed -f $K/incfix.sed > $3
}
mkdir -p $F/gen/WINDOWS/USER/SERVER $F/gen/WINDOWS/USER/CLIENT
listmung INC/CF.LST INC/CSUSER.TPL $UI/csuser.h
listmung INC/SCF.LST INC/CSCALL.TPL $UI/cscall.h
listmung INC/CB.LST INC/CALLBACK.TPL $UI/callback.h
listmung INC/CF.LST SERVER/DISPCF.TPL $F/gen/WINDOWS/USER/SERVER/dispcf.c
listmung INC/SCF.LST SERVER/CALLCF.TPL $F/gen/WINDOWS/USER/SERVER/callcf.c
listmung INC/CB.LST CLIENT/DISPCB.TPL $F/gen/WINDOWS/USER/CLIENT/dispcb.c

# What the pass made, as the farm has it (the message tables are binary).
cd $F && find . -type f -newer /tmp/mkgen.stamp ! -name .farmed ! -path "./nmk/*" | while read -r f; do
    mkdir -p "$G/$(dirname "$f")"
    case $f in *.[Bb][Ii][Nn]) cp "$f" "$G/$f" ;; *) tr -d '\r' < "$f" > "$G/$f" ;; esac
done
: > $G/.stamp
