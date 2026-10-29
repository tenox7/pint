#!/bin/bash
# mcinc.sh COMPONENTDIR - run the mc recipes of a component's MAKEFILE.INC (its
# NTTARGETFILE0 message files) and of the MAKEFIL0 of its directory and of those above
# it (the pass 0 of build.exe runs the makefil0 of each directory it walks: win31evt.h of
# ADVAPI for ADVAPI\DAYTONA; mkgen.sh runs those of private, windows and ntos\dd), once
# each, with mc (tools/host.sh), as the NT build does before it compiles: headers to the
# directory -h names (that of the makefile) - its farm directory for an NTOS one, else
# the generated headers of the directory (/tmp/f/gen) - and the resource script and
# message tables to /tmp/f/mc/<the directory -r names> for rc.sh. Paths are those of the
# recipe, relative to the directory of the makefile or from $(BASEDIR), also in a
# makefile its makefile.inc includes (NTUSER\SERVER\DAYTONA: !include ..\makefile.inc),
# which nmake runs in the component.
d=$1
# resolve PATH [DIR]: PATH from DIR (the component)
resolve() {
    local p=${2:-$d} c n
    case $1 in '$(BASEDIR)'*) p=/work ;; esac
    for c in $(echo "${1#\$(BASEDIR)}" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
# outdir PATH [DIR]: a directory of the outputs, else one the build makes there, its name in lower
# case (PASS0_HEADERDIR=obj of windisk)
outdir() {
    local i=$(echo "$1" | tr '\\' /) p
    resolve "$1" $2 || { p=$(resolve "$(dirname "$i")" $2) && echo "$p/$(basename "$i" | tr A-Z a-z)"; }
}
mk() {
    local i
    tr -d '\r' < $1
    tr -d '\r' < $1 | sed -n 's/^![[:space:]]*[Ii][Nn][Cc][Ll][Uu][Dd][Ee][[:space:]]*\([^<>[:space:]]*\)[[:space:]]*$/\1/p' | while read -r i; do
        i=$(resolve "$i") && [ -f "$i" ] && mk "$i"
    done
}
# (mc, also as $(MC), makefile.def's mc: GUIDLIB's messages.mc)
recipes() { sed -n -E 's/^[[:space:]]*(mc|\$\(MC\))[[:space:]]+(.*)/\2/p' | sed 's/\$(LANGUAGE)/usa/g'; }
# recipe DIR ARGUMENTS: an mc recipe of a makefile of DIR
recipe() {
    local b=$1 a o= h r f hd rd
    shift; a="$*"; h=$b r=$b f=
    while [ $# -gt 0 ]; do
        case $1 in
        -h) h=$(outdir "$2" $b); shift 2 ;;
        -r) r=$(outdir "$2" $b); shift 2 ;;
        -x) shift 2 ;;
        # (-m maxmsglen, -e extension: CMD's mc -v -m 4000)
        -m|-e) o="$o $1 $2"; shift 2 ;;
        -*) o="$o $1"; shift ;;
        *) f=$1; shift ;;
        esac
    done
    # (an NTOS directory the farm has not, W32, as any other)
    case $h in
    /work/private/ntos/*) hd=/tmp/f/$(echo ${h#/work/private/ntos/} | tr A-Z a-z); [ -d $hd ] || hd=/tmp/f/gen/${h#/work/private/} ;;
    *) hd=/tmp/f/gen/${h#/work/private/} ;;
    esac
    rd=/tmp/f/mc/$(echo ${r#/work/private/} | tr A-Z a-z)
    mkdir -p $hd $rd
    # an input a recipe of the component makes first: nttarget.sh runs that mc
    f=$(resolve "$f" $b) || return 0
    bash /work/RASPI/tools/host.sh mc $o -h $hd -r $rd $f > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; echo "mcinc: mc $a failed" >&2; return 1; }
    # mc names its outputs as its input (BUGCODES.h): the farm has them lower case
    for f in $(find $hd $rd -maxdepth 1 -type f -name "*[A-Z]*" \( -name "*.h" -o -name "*.rc" \) | sort -u); do mv $f $(dirname $f)/$(basename $f | tr A-Z a-z); done
}
ran=
if m=$(ls $d 2>/dev/null | grep -ix makefile.inc); then
    ran=1
    mk $d/$m | recipes | while read -r a; do recipe $d $a || exit 1; done
fi
# The message files of the SOURCES, which build.exe gives the pass 0 of makefile.def (lsass:
# ..\lsapmsgs.mc): $(MC) -h $(PASS0_HEADERDIR) -r $(MC_SOURCEDIR) $(MC_FLAGS), MC_SOURCEDIR the
# PASS0_SOURCEDIR (IDL_TYPE OLE, the default) or PASS0_CLIENTDIR (IDL_TYPE RPC, else the
# TARGETPATH); "." by default.
mm() { bash "$(dirname "$0")/srcmacro.sh" $d/sources $1; }
if [ -f $d/sources ] && mcs=$(mm SOURCES | tr " " "\n" | grep -iE "\.mc$"); then
    ran=1
    h=$(mm PASS0_HEADERDIR | tr -d " "); h=${h:-.}
    case $(mm IDL_TYPE | tr -d " " | tr a-z A-Z) in
    RPC) r=$(mm PASS0_CLIENTDIR | tr -d " "); r=${r:-$(mm TARGETPATH | tr -d " ")} ;;
    *) r=$(mm PASS0_SOURCEDIR | tr -d " "); r=${r:-.} ;;
    esac
    o=; [ -z "$(mm USE_OLE_MC | tr -d " ")" ] || o=-o
    for s in $mcs; do recipe $d $o -h $h -r $r $(mm MC_FLAGS) $s || exit 1; done
fi
p=$d
while [ "$p" != /work/private ] && [ "$p" != / ]; do
    case $p in
    /work/private/windows|/work/private/ntos/dd) ;;
    *) if m=$(ls $p 2>/dev/null | grep -ix makefil0) && [ ! -e /tmp/f/.makefil0$p ]; then
           mkdir -p /tmp/f/.makefil0$p
           r=$(tr -d '\r' < $p/$m | recipes)
           [ -z "$r" ] || { ran=1; echo "$r" | while read -r a; do recipe $p $a || exit 1; done; }
       fi ;;
    esac
    p=$(dirname $p)
done
[ -z "$ran" ] || bash "$(dirname "$0")/gensave.sh"
