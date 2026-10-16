#!/bin/bash
# Print the library or object that a SOURCES names by its directory
# (..\server\obj\*\winreg.lib, $(BASEDIR)\private\...\obj\*\name.lib): the one
# make-lib.sh made for that directory (obj[/um]/dir/<directory>/<name>), if it did. Two
# components may share a TARGETNAME (WINREG\CLIENT and WINREG\SERVER).
# Usage: libpath.sh componentdir lib
l=$(echo "$2" | tr '\\' /)
case $l in */[Oo][Bb][Jj]/\*/*) ;; *) exit 0 ;; esac
d=${l%/*/\*/*}
case $d in
'$(BASEDIR)'*|'$(_NTROOT)'*|'$(_NTBINDIR)'*) p=/work; d=${d#*)} ;;
*) p=$1 ;;
esac
for c in $(echo "$d" | tr / ' '); do
    case $c in
    ..) p=$(dirname "$p") ;;
    .) ;;
    *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || exit 0; p=$p/$n ;;
    esac
done
b=$(basename "$l" | tr A-Z a-z)
for o in /work/RASPI/obj/um /work/RASPI/obj; do
    [ -f $o/dir/${p#/work/}/$b ] && { echo $o/dir/${p#/work/}/$b; exit 0; }
done
