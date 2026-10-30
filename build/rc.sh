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
# farmed directories, f: the farm, k: kernel): RC keeps its include path in
# 520 bytes, as NT's \nt\ paths fit. Prints the name of the .res.
[ -f /work/RASPI/obj/host/RC.EXE ] || { echo "rc.sh: no obj/host/RC.EXE (tools/make-tools.sh)" >&2; exit 1; }
for m in p:/work/private d:/tmp/f/dirs/work/private f:/tmp/f k:/work/RASPI/kernel; do
    [ -e /opt/wine32/dosdevices/${m%%:*}: ] || ln -s ${m#*:} /opt/wine32/dosdevices/${m%%:*}:
done
dos() {
    case $1 in
    /tmp/f/dirs/work/private/*) echo "d:${1#/tmp/f/dirs/work/private}" ;;
    /tmp/f/*) echo "f:${1#/tmp/f}" ;;
    /work/private/*) echo "p:${1#/work/private}" ;;
    /work/RASPI/kernel/*) echo "k:${1#/work/RASPI/kernel}" ;;
    *) echo "Z:$1" ;;
    esac | tr / '\\'
}
rel() { echo ${1#/work/private/}; }
d=$(realpath -m "$1"); rc=$(realpath -m "$2"); shift 2
s=$(dirname $rc)
w=/tmp/f/gen/$(rel $d); mkdir -p $w /tmp/f/gen/$(rel $s) /tmp/rc
copy=/tmp/f/gen/$(rel $s)/$(basename $rc | tr A-Z a-z)
# (its RISC gates given ARM, as the farm's headers: the countdown checkbox of the startup
# dialog of sysdm.rc, which the RISC code of sysdm reads)
LC_ALL=C sed -E -f "$(dirname "${BASH_SOURCE[0]}")/riscgate.sed" $rc > $copy
res=/tmp/rc/$(basename "${rc%.*}" | tr A-Z a-z).res
# each farmed directory followed by its source directory and its message table scripts
# (mcinc.sh: rlmsgtbl.rc of RLTOOLS\COMMON), and the component directory,
# where NT ran RC (msvideo.rc of the generated files names ..\mciwnd\strings.rc); when
# that is longer than RC keeps, the farmed directories alone (they link the other files
# of their source directories, incdirs.sh). The directory of the message table scripts of
# a source directory is there also when it has none: an include relative to it names
# those of another (W32\KMODE: ..\ntuser\kernel\DAYTONA\strid.rc, from W32\W32INC). When
# that is still too long, those directories only when they have scripts (devapps.rc of
# DEVAPPS\CPL, whose C sources include the headers of six other directories).
incs() {
    for p in $s /tmp/f/gen/$(rel $s) /tmp/f/mc/$(rel $s | tr A-Z a-z) /tmp/f/mc/$(rel $d | tr A-Z a-z); do echo "$p"; done
    for a in "$@"; do
        case $a in
        -I/tmp/f/dirs/*) echo "${a#-I}"; m=/tmp/f/mc/$(rel ${a#-I/tmp/f/dirs} | tr A-Z a-z)
                         if [ -n "$TWINS" ]; then echo "${a#-I/tmp/f/dirs}"; mkdir -p $m; fi
                         [ ! -d $m ] || { [ -n "${BARE:-}" ] && [ -z "$(ls -A $m)" ]; } || echo $m ;;
        -I*) realpath -m "${a#-I}" ;;
        esac
    done
    echo $d
}
inc=$(TWINS=1 incs "$@" | awk '!seen[$0]++' | while read -r p; do echo "-I$(dos $p)"; done)
n=$(echo "$inc" | sed 's/^-I//' | tr '\n' ';' | wc -c)
[ $n -lt 520 ] || inc=$(TWINS= incs "$@" | awk '!seen[$0]++' | while read -r p; do echo "-I$(dos $p)"; done)
n=$(echo "$inc" | sed 's/^-I//' | tr '\n' ';' | wc -c)
[ $n -lt 520 ] || inc=$(TWINS= BARE=1 incs "$@" | awk '!seen[$0]++' | while read -r p; do echo "-I$(dos $p)"; done)
n=$(echo "$inc" | sed 's/^-I//' | tr '\n' ';' | wc -c)
# And still too long, without the directories of the relative includes of the C sources (RELINC
# of the make scripts, relinc.sh), which NT's RC had not: the script names those by their path
# (HyperTerminal's term.rc: <cncttapi\cncttapi.rc>, the C sources <cncttapi\cncttapi.hh>).
[ $n -lt 520 ] || inc=$(TWINS= BARE=1 incs $(for a; do case " ${RELINC:-} " in *" $a "*) ;; *) echo "$a" ;; esac; done) | awk '!seen[$0]++' | while read -r p; do echo "-I$(dos $p)"; done)
n=$(echo "$inc" | sed 's/^-I//' | tr '\n' ';' | wc -c)
[ $n -lt 520 ] || { echo "rc.sh: the include path of $(basename $rc) is $n bytes, RC keeps 520:" $inc >&2; exit 1; }
flags=$(for a in "$@"; do case $a in -D*) echo "$a" ;; esac; done)
# and the defines of the RCOPTIONS of its SOURCES (the _AFX_MONOLITHIC and RELEASE of the MFC 4.2
# DLLs, which their C++ has not)
flags="$flags $(for a in $(bash "$(dirname "${BASH_SOURCE[0]}")/srcmacro.sh" $d/sources RCOPTIONS); do case $a in [-/][Dd]?*) echo "-D${a#??}" ;; esac; done)"
(cd $w && bash /work/RASPI/tools/host.sh rc -l 409 -r -fo $(dos $res) -x $inc $flags $(dos $copy)) > /tmp/rc/out 2>&1 ||
    { grep -v "^Microsoft\|^Copyright\|^$" /tmp/rc/out >&2; exit 1; }
echo $res
