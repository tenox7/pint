#!/bin/bash
# mcinc.sh COMPONENTDIR - run the mc recipes of a component's MAKEFILE.INC (its
# NTTARGETFILE0 message files) with mc (tools/host.sh), as the NT build does before it
# compiles: headers to the directory -h names (the component's) - its farm
# directory for an NTOS one, else the generated headers of the directory
# (/tmp/f/gen) - and the resource script and message tables to /tmp/f/mc/<the
# directory -r names> for rc.sh. Paths are those of the recipe, relative to the
# component or from $(BASEDIR).
d=$1; m=$(ls $d 2>/dev/null | grep -ix makefile.inc) || exit 0
resolve() {
    local p=$d c n
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
tr -d '\r' < $d/$m | sed -n 's/^[[:space:]]*mc \(.*\)/\1/p' | sed 's/\$(LANGUAGE)/usa/g' | while read -r a; do
    set -- $a; o= h=$d r=$d f=
    while [ $# -gt 0 ]; do
        case $1 in
        -h) h=$(resolve "$2"); shift 2 ;;
        -r) r=$(resolve "$2"); shift 2 ;;
        -x) shift 2 ;;
        -*) o="$o $1"; shift ;;
        *) f=$1; shift ;;
        esac
    done
    case $h in
    /work/PRIVATE/NTOS/*) hd=/tmp/f/$(echo ${h#/work/PRIVATE/NTOS/} | tr A-Z a-z) ;;
    *) hd=/tmp/f/gen/${h#/work/PRIVATE/} ;;
    esac
    rd=/tmp/f/mc/$(echo ${r#/work/PRIVATE/} | tr A-Z a-z)
    mkdir -p $hd $rd
    f=$(resolve "$f") && bash /work/RASPI/tools/host.sh mc $o -h $hd -r $rd $f > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; echo "mcinc: mc $a failed" >&2; exit 1; }
    # mc names its outputs as its input (BUGCODES.h): the farm has them lower case
    for f in $(find $hd $rd -maxdepth 1 -type f -name "*[A-Z]*" \( -name "*.h" -o -name "*.rc" \) | sort -u); do mv $f $(dirname $f)/$(basename $f | tr A-Z a-z); done
done
