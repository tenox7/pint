#!/bin/bash
# rc.sh COMPONENTDIR FILE.RC FLAGS... - compile the resource script of a
# component with RC (obj/host/RC.EXE, tools/make-tools.sh) as makefile.def does: rc -l 409 -r
# in the component's directory with the defines (-D) of its C compiles and its
# include path (-I, below), after the directories of the script, of
# the files the build made (/tmp/f/gen) and of the message table scripts of the
# mc pass (/tmp/f/mc). The tree is read-only, so RC runs in the generated files'
# directory of the component and compiles a copy of the script in that of the
# script (RC makes its temporary files beside it). RC takes / for a switch, so
# paths go to it in DOS form, on drives of the long prefixes (p: the tree, d: its
# farmed directories, f: the farm, k: arcfw/kernel): RC keeps its include path in
# 520 bytes, as NT's \nt\ paths fit. Prints the name of the .res.
[ -f /work/RASPI/obj/host/RC.EXE ] || { echo "rc.sh: no obj/host/RC.EXE (tools/make-tools.sh)" >&2; exit 1; }
for m in p:/work/PRIVATE d:/tmp/f/dirs/work/PRIVATE f:/tmp/f k:/work/RASPI/arcfw/kernel; do
    [ -e /opt/wine32/dosdevices/${m%%:*}: ] || ln -s ${m#*:} /opt/wine32/dosdevices/${m%%:*}:
done
dos() {
    case $1 in
    /tmp/f/dirs/work/PRIVATE/*) echo "d:${1#/tmp/f/dirs/work/PRIVATE}" ;;
    /tmp/f/*) echo "f:${1#/tmp/f}" ;;
    /work/PRIVATE/*) echo "p:${1#/work/PRIVATE}" ;;
    /work/RASPI/arcfw/kernel/*) echo "k:${1#/work/RASPI/arcfw/kernel}" ;;
    *) echo "Z:$1" ;;
    esac | tr / '\\'
}
rel() { echo ${1#/work/PRIVATE/}; }
d=$(realpath -m "$1"); rc=$(realpath -m "$2"); shift 2
s=$(dirname $rc)
w=/tmp/f/gen/$(rel $d); mkdir -p $w /tmp/f/gen/$(rel $s) /tmp/rc
copy=/tmp/f/gen/$(rel $s)/$(basename $rc | tr A-Z a-z)
cp $rc $copy
res=/tmp/rc/$(basename "${rc%.*}" | tr A-Z a-z).res
# each farmed directory followed by its source directory and its message table scripts
# (mcinc.sh: rlmsgtbl.rc of RLTOOLS\COMMON), and the component directory,
# where NT ran RC (msvideo.rc of the generated files names ..\mciwnd\strings.rc); when
# that is longer than RC keeps, the farmed directories alone (they link the other files
# of their source directories, incdirs.sh)
incs() {
    for p in $s /tmp/f/gen/$(rel $s) /tmp/f/mc/$(rel $s | tr A-Z a-z) /tmp/f/mc/$(rel $d | tr A-Z a-z); do echo "$p"; done
    for a in "$@"; do
        case $a in
        -I/tmp/f/dirs/*) echo "${a#-I}"; [ -z "$TWINS" ] || echo "${a#-I/tmp/f/dirs}"
                         m=/tmp/f/mc/$(rel ${a#-I/tmp/f/dirs} | tr A-Z a-z); [ ! -d $m ] || echo $m ;;
        -I*) realpath -m "${a#-I}" ;;
        esac
    done
    echo $d
}
inc=$(TWINS=1 incs "$@" | awk '!seen[$0]++' | while read -r p; do echo "-I$(dos $p)"; done)
n=$(echo "$inc" | sed 's/^-I//' | tr '\n' ';' | wc -c)
[ $n -lt 520 ] || inc=$(TWINS= incs "$@" | awk '!seen[$0]++' | while read -r p; do echo "-I$(dos $p)"; done)
n=$(echo "$inc" | sed 's/^-I//' | tr '\n' ';' | wc -c)
[ $n -lt 520 ] || { echo "rc.sh: the include path of $(basename $rc) is $n bytes, RC keeps 520" >&2; exit 1; }
flags=$(for a in "$@"; do case $a in -D*) echo "$a" ;; esac; done)
(cd $w && bash /work/RASPI/tools/host.sh rc -l 409 -r -fo $(dos $res) -x $inc $flags $(dos $copy)) > /tmp/rc/out 2>&1 ||
    { grep -v "^Microsoft\|^Copyright\|^$" /tmp/rc/out >&2; exit 1; }
echo $res
