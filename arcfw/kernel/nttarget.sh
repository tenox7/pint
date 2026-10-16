#!/bin/bash
# Make the NTTARGETFILE0/1 and NTTARGETFILES of a component (its SOURCES) by the
# recipes of its MAKEFILE.INC (nmk.awk), as makefile.def does: NTTARGETFILE0 and
# the sources among NTTARGETFILES before the component compiles (pre), the rest,
# such as a library made of its library, after it (post); the dependents of a
# target that have recipes first. Files land in /tmp/f/gen/<directory>, where the
# builds find generated files; a library obj\<machine>\<name>.lib is <out>/<name>.lib.
# Usage: nttarget.sh componentdir pre|post [out]
K=$(cd "$(dirname "$0")" && pwd)
D=$1 PHASE=$2 OUT=${3:-/work/RASPI/obj/um}
resolve() {
    local p=$D c n
    for c in $(echo "$1" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
gen() { echo /tmp/f/gen/${1#/work/PRIVATE/}; }
# A file of a recipe: in the tree, or made by the build (generated files and the
# outputs of the mc pass).
locate() {
    local f r n g
    f=$(echo "$1" | tr '\\' /)
    r=$(resolve "$(dirname "$f")") || return 1
    for g in $r $(gen $r) /tmp/f/mc/$(echo ${r#/work/PRIVATE/} | tr A-Z a-z); do
        n=$(ls "$g" 2>/dev/null | grep -ix "$(basename "$f")" | head -1)
        [ -z "$n" ] || { echo "$g/$n"; return; }
    done
    return 1
}
# copy a dest: dest a directory (as . or ..) or a file
nmk_copy() {
    local s d f
    s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    if d=$(resolve "${2:-.}") && [ -d "$d" ]; then f=$(basename "$s")
    else d=$(resolve "$(dirname "$2")") || return 1; f=$(basename "$2"); fi
    mkdir -p "$(gen $d)" && cp "$s" "$(gen $d)/$(echo $f | tr A-Z a-z)"
}
outlib() { echo $OUT/$(basename "$1" .lib | tr A-Z a-z).lib; }
# a library of libraries, given to lib in reverse order as ntlib (msvc.sh) does
nmk_lib() {
    local a o l=
    for a in "$@"; do
        case $a in
        -out:*) o=$(outlib "${a#-out:}") ;;
        *.lib) l="$(outlib "$a") $l" ;;
        esac
    done
    rm -f $o
    MSVC_ENV=0 WINEDEBUG=-all lib -nologo -out:$o $l > /tmp/nttarget.err 2>&1 || { cat /tmp/nttarget.err >&2; return 1; }
}
nmk_mc() { :; }    # mcinc.sh runs the mc recipes
# a file a recipe makes, among the generated files of the component
out() { echo "$(gen $D)/$(basename "$1" | tr A-Z a-z)"; }
nmk_cat() { local s; s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; cat "$s" >> "$(out $2)"; }
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
mk=$(resolve makefile.inc) || exit 0
mkdir -p $(gen $D)
set -f
targets() { bash $K/srcmacro.sh $D/SOURCES $1 | tr " " "\n" | sed 's/\*/$(TARGET_DIRECTORY)/g'; }
macros="TARGET_DIRECTORY=arm"
for m in NTTARGETFILE0 NTTARGETFILE1 NTTARGETFILES; do
    macros="$macros
$m=$(targets $m | tr "\n" " " | sed "s/ *$//")"
done
made=" "
target() {
    local t=$1 d
    case "$made" in *" $t "*) return 0 ;; esac
    made="$made$t "
    locate "$t" >/dev/null && return 0    # up to date: made by the build already
    for d in $(NMK_DEPS=1 NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $K/nmk.awk $mk); do
        target "$d" || return 1
    done
    NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $K/nmk.awk $mk > /tmp/nttarget.sh
    ( set -e; . /tmp/nttarget.sh ) || { echo "nttarget: $t failed" >&2; return 1; }
}
case $PHASE in
pre) T="$(targets NTTARGETFILE0) $(targets NTTARGETFILES | grep -iE "\.(c|cxx|h|hxx)$")" ;;
*) T="$(targets NTTARGETFILE1) $(targets NTTARGETFILES | grep -viE "\.(c|cxx|h|hxx)$")" ;;
esac
for t in $T; do
    target "$t" || exit 1
done
