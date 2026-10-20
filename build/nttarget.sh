#!/bin/bash
# Make the NTTARGETFILE0/1 and NTTARGETFILES of a component (its SOURCES) by the
# recipes of its MAKEFILE.INC (nmk.awk), as makefile.def does: NTTARGETFILE0 and
# the sources among NTTARGETFILES before the component compiles (pre), the rest,
# such as a library made of its library, after it (post); the dependents of a
# target that have recipes first. Files land in /tmp/f/gen/<directory>, where the
# builds find generated files; a library obj\<machine>\<name>.lib is <out>/<name>.lib.
# Usage: nttarget.sh componentdir pre|post [out]
B=$(cd "$(dirname "$0")" && pwd)
D=$1 PHASE=$2 OUT=${3:-/work/RASPI/obj/um}
resolve() {
    local p=${2:-$D} c n f=$1
    case $f in BASEDIR/*) p=/work; f=${f#BASEDIR/} ;; esac
    for c in $(echo "$f" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
gen() { echo /tmp/f/gen/${1#/work/PRIVATE/}; }
# A file of a recipe: the port's version, in the tree, or made by the build (generated files and the
# outputs of the mc pass).
locate() {
    local f r n g
    f=$(echo "$1" | tr '\\' /)
    r=$(resolve "$(dirname "$f")") || return 1
    for g in $(bash $B/portdir.sh $r) $r $(gen $r) /tmp/f/mc/$(echo ${r#/work/PRIVATE/} | tr A-Z a-z); do
        n=$(ls "$g" 2>/dev/null | grep -ix "$(basename "$f")" | head -1)
        [ -z "$n" ] || { echo "$g/$n"; return; }
    done
    return 1
}
# copy a dest: dest a directory (as . or ..) or a file; one under obj\ is a file of the component
nmk_copy() {
    local s d f
    s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    if d=$(resolve "${2:-.}") && [ -d "$d" ]; then f=$(basename "$s")
    else case $2 in [Oo][Bb][Jj]/*) d=$D ;; *) d=$(resolve "$(dirname "$2")") || return 1 ;; esac; f=$(basename "$2"); fi
    mkdir -p "$(gen $d)" && cp "$s" "$(gen $d)/$(echo $f | tr A-Z a-z)"
}
outlib() { echo $OUT/$(basename "$1" .lib | tr A-Z a-z).lib; }
# a library a recipe names (..\buffer\obj\arm\buffer.lib): the one made for that directory
# (libpath.sh), else that of its name
inlib() { local l; l=$(bash $B/libpath.sh $D "$(echo "$1" | sed -E 's#(^|/)obj/arm/#\1obj/*/#')"); echo ${l:-$(outlib "$1")}; }
# a library of libraries, given to lib in reverse order as ntlib (msvc.sh) does: the library of
# the directory it names (obj\arm\uimisc.lib: the component's, ..\lib\arm\x.lib; libpath.sh -d),
# one of the public libraries <out>/<name>.lib, one of kernel mode both
nmk_lib() {
    local a o p= d l=
    for a in "$@"; do
        case $a in
        -out:*) p=${a#-out:} ;;
        *.lib) l="$(inlib "$a") $l" ;;
        esac
    done
    d=$(dirname "$p"); case $d in [Aa][Rr][Mm]|*/[Aa][Rr][Mm]) d=$(dirname "$d") ;; esac
    d=$(bash $B/libpath.sh -d $D "$(echo "$d" | sed 's/^BASEDIR/$(BASEDIR)/')")
    case $d in /work/PUBLIC/*) o=$(outlib "$p") ;; *) o=$OUT/dir/${d#/work/}/$(basename "$p" | tr A-Z a-z) ;; esac
    mkdir -p $(dirname $o) && rm -f $o
    MSVC_ENV=0 WINEDEBUG=-all lib -nologo -out:$o $l > /tmp/nttarget.err 2>&1 || { cat /tmp/nttarget.err >&2; return 1; }
    [ $OUT != /work/RASPI/obj ] || [ $o = $(outlib "$p") ] || cp $o $OUT/
}
# link32 -dump -symbols: the symbol tables of libraries, by the linker of Microsoft C
nmk_link32() {
    local a l=
    for a; do case $a in *.lib) l="$l $(inlib "$a")" ;; esac; done
    MSVC_ENV=0 WINEDEBUG=-all link -dump -symbols $l
}
# mapmsg, the message file of the message definitions of a header (netmsg.mc of
# NET\MESSAGES): mapmsg [-p] [-a file] system base header [> file]
nmk_mapmsg() {
    local o= args=() n
    while [ $# -gt 0 ]; do
        case $1 in
        -a) args+=(-a "$(out "$2")"); shift 2 ;;
        ">") o=$(out "$2"); shift 2 ;;
        *) args+=("$1"); shift ;;
        esac
    done
    n=${#args[@]}
    args[$((n-1))]=$(locate "${args[$((n-1))]}") || { echo "nttarget: no ${args[$((n-1))]}" >&2; return 1; }
    if [ -n "$o" ]; then bash /work/RASPI/tools/host.sh mapmsg "${args[@]}" > "$o" 2> /tmp/mapmsg.log
    else bash /work/RASPI/tools/host.sh mapmsg "${args[@]}" 2> /tmp/mapmsg.log; fi || { cat /tmp/mapmsg.log >&2; return 1; }
}
# the C preprocessor ($(TARGET_CPP) /EP): the export list of a library from its .src
nmk_cpp() {
    local o= args=() s=
    while [ $# -gt 0 ]; do
        case $1 in
        ">") o=$(out "$2"); shift 2 ;;
        [-/]*) args+=("$1"); shift ;;
        *) s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; shift ;;
        esac
    done
    tr -d '\032\r' < "$s" > /tmp/nmkcpp.src
    cl -nologo "${args[@]}" -D_ARM_ /tmp/nmkcpp.src > "$o" 2> /tmp/nmkcpp.log || { cat /tmp/nmkcpp.log >&2; return 1; }
}
# uixport, the export list of a NET UI DLL from the symbols of its libraries
nmk_uixport() {
    local a h o x= args=()
    for a; do
        case $a in
        -h:*) h=$(locate "${a#-h:}") || { echo "nttarget: no ${a#-h:}" >&2; return 1; }; args+=("-h:$h") ;;
        -o:*) args+=("-o:$(out "${a#-o:}")") ;;
        -x:*) x=$(locate "${a#-x:}") || { echo "nttarget: no ${a#-x:}" >&2; return 1; }; args+=("-x:$x") ;;
        *) args+=("$a") ;;
        esac
    done
    bash /work/RASPI/tools/host.sh uixport "${args[@]}" > /tmp/uixport.log 2>&1 || { cat /tmp/uixport.log >&2; return 1; }
}
# the files NT placed in its release (binplace), the disk image places (make-sd-image.sh)
nmk_binplace() { :; }
nmk_del() { local f; for f; do rm -f "$(out $f)"; done; }
# copy a + b c: a and b, one after the other, in c (which may be a)
nmk_copycat() {
    local d=$1 s f t=/tmp/nttarget.cat; shift
    : > $t
    for f; do s=$(locate "$f") || { echo "nttarget: no $f" >&2; return 1; }; cat "$s" >> $t; done
    mv $t "$(out $d)"
}
nmk_midl() { :; }    # gen-midl.sh runs the midl recipes
# mcinc.sh runs the mc recipes of an input of the tree; this one, of an input a recipe
# made (a message file copied from another component), puts the header, resource script
# and message tables among the generated files, where NT's build left them in its tree
nmk_mc() {
    local o= h=. r=. f= s hd rd x
    while [ $# -gt 0 ]; do
        case $1 in
        -h) h=$2; shift 2 ;;
        -r) r=$2; shift 2 ;;
        -*) o="$o $1"; shift ;;
        *) f=$1; shift ;;
        esac
    done
    s=$(locate "$f") || { echo "nttarget: no $f" >&2; return 1; }
    hd=$(gen $(resolve "$h")) && rd=$(gen $(resolve "$r")) || return 1
    mkdir -p $hd $rd
    bash /work/RASPI/tools/host.sh mc $o -h $hd -r $rd $s > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; return 1; }
    for x in $(find $hd $rd -maxdepth 1 -type f -name "*[A-Z]*" | sort -u); do mv $x $(dirname $x)/$(basename $x | tr A-Z a-z); done
}
# a file a recipe makes, among the generated files of the component
out() { echo "$(gen $D)/$(basename "$1" | tr A-Z a-z)"; }
# IF EXIST f: a file of the tree or one the build made (an image of the component is
# not made yet: winchat copies its winchat.exe to bin\arm, as binplace)
nmk_exists() { locate "$1" > /dev/null || [ -e "$(out "$1")" ]; }
# an inline file of a recipe (type << > f, type << >> f)
nmk_new() { cat > "$(out $1)"; }
nmk_add() { cat >> "$(out $1)"; }
nmk_cat() { local s; s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; cat "$s" >> "$(out $2)"; }
nmk_wcshdr() { local s; s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; bash /work/RASPI/tools/host.sh wcshdr < "$s" > "$(out $2)"; }
# the build tools of the recipes (tools/host.sh), on files of the tree or generated ones
nmk_solidpp() {
    local i f a=()
    i=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; shift
    for f; do a+=("$(out $f)"); done
    bash /work/RASPI/tools/host.sh solidpp "$i" "${a[@]}" > /dev/null
}
nmk_rcpp() {
    local f a=(-I$D -I$(gen $D))
    while [ $# -gt 0 ]; do
        case $1 in
        -f) f=$(locate "$2") || { echo "nttarget: no $2" >&2; return 1; }; a+=(-f "$f"); shift 2 ;;
        -g) a+=(-g "$(out $2)"); shift 2 ;;
        *) a+=("$1"); shift ;;
        esac
    done
    bash /work/RASPI/tools/host.sh rcpp "${a[@]}" > /dev/null
}
m=$(resolve makefile.inc) || exit 0
# the makefile.inc with the files it includes (the port's versions of them first)
ported() { local p=$(bash $B/portdir.sh $1); [ -f $p ] && echo $p || echo $1; }
flatten() {
    local line f
    while IFS= read -r line || [ -n "$line" ]; do
        if [[ $line =~ ^[[:space:]]*![[:space:]]*[Ii][Nn][Cc][Ll][Uu][Dd][Ee][[:space:]]+([^[:space:]]+) ]]; then
            f=$(resolve "$(echo "${BASH_REMATCH[1]}" | tr -d '"<>')" "$(dirname $1)") && flatten $f
        else
            printf '%s\n' "$line"
        fi
    done < <(tr -d '\032\r' < "$(ported $1)")
}
mk=/tmp/nttarget.mk
flatten $m > $mk
mkdir -p $(gen $D)
set -f
targets() { bash $B/srcmacro.sh $D/SOURCES $1 | tr " " "\n" | sed 's/\*/$(TARGET_DIRECTORY)/g'; }
# the macros of the component (its makefile and SOURCES) and of the build of ARM
macros="$(bash $B/srcmacro.sh $D/SOURCES '*')
TARGET_DIRECTORY=arm
TARGET_CPP=cl
386=0
MIPS=0
ALPHA=0
PPC=0
ARM=1"
for m in NTTARGETFILE0 NTTARGETFILE1 NTTARGETFILES; do
    macros="$macros
$m=$(targets $m | tr "\n" " " | sed "s/ *$//")"
done
made=" "
target() {
    local t=$1 d
    case "$made" in *" $t "*) return 0 ;; esac
    made="$made$t "
    # up to date: in the tree, or made by the build already unless its recipe does more
    # than the mc and midl passes do (ulib: rtmsg.rc, mc's and ulib.rc in one)
    if locate "$t" >/dev/null; then
        NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk > /tmp/nttarget.sh
        grep -qv "^nmk_mc \|^nmk_midl \|^ *$" /tmp/nttarget.sh || return 0
    fi
    for d in $(NMK_DEPS=1 NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk); do
        target "$d" || return 1
    done
    NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk > /tmp/nttarget.sh
    ( set -e; . /tmp/nttarget.sh ) || { echo "nttarget: $t failed" >&2; return 1; }
}
case $PHASE in
pre) T="$(targets NTTARGETFILE0) $(targets NTTARGETFILES | grep -iE "\.(c|cxx|h|hxx)$")" ;;
*) T="$(targets NTTARGETFILE1) $(targets NTTARGETFILES | grep -viE "\.(c|cxx|h|hxx)$")" ;;
esac
for t in $T; do
    target "$t" || exit 1
done
bash $B/gensave.sh
