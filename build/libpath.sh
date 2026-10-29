#!/bin/bash
# Print the library or object that a SOURCES names by its directory
# (obj\*\rev.obj, ..\server\obj\*\winreg.lib, ..\bin\*\acledit.lib,
# $(BASEDIR)\private\...\obj\*\name.lib, or *\rpcmtrt.lib: the component's own directory,
# the TARGETPATH of another): the one make-lib.sh or make-dll.sh made for that
# directory (obj[/um]/dir/<directory>/<name>), if one did. Two components may share a
# TARGETNAME (WINREG\CLIENT and WINREG\SERVER). The directory of obj\* is the component's,
# that of a TARGETPATH other than obj (..\bin) is that directory.
# With -d it prints the directory a TARGETPATH stands for, where the builds put what they
# make (/work/private/net/ui/acledit/bin for ..\bin of ACLEDIT\ACLEDIT).
# Usage: libpath.sh componentdir lib
#        libpath.sh -d componentdir targetpath
dir() {
    local p=$1 d=$(echo "$2" | tr '\\' /) c n
    case $d in [Oo][Bb][Jj]) d=. ;; */[Oo][Bb][Jj]) d=${d%/*} ;; esac
    case $d in
    '$(BASEDIR)'*|'$(_NTROOT)'*|'$(_NTBINDIR)'*) p=/work; d=${d#*)} ;;
    '$(_NTDRIVE)/nt/'*|/[Nn][Tt]/*) p=/work; d=${d#*/[Nn][Tt]/} ;;
    esac
    for c in $(echo "$d" | tr / ' '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); p=$p/${n:-$(echo $c | tr A-Z a-z)} ;;
        esac
    done
    echo "$p"
}
[ "$1" = -d ] && { dir "$2" "$3"; exit 0; }
l=$(echo "$2" | tr '\\' /)
case $l in
*/\*/*) p=$(dir "$1" "${l%/\*/*}") ;;
\*/*) p=$1 ;;
*) exit 0 ;;
esac
b=$(basename "$l" | tr A-Z a-z)
for o in /work/RASPI/obj/um /work/RASPI/obj; do
    [ -f $o/dir/${p#/work/}/$b ] && { echo $o/dir/${p#/work/}/$b; exit 0; }
done
