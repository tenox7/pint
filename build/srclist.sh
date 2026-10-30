#!/bin/bash
# Print the sources NT's <component>/up/sources (or <component>/sources) builds,
# resolved to the on-disk file names: like the NT build, the source in the component
# directory or its parent (..\x.c), the port's version of it (portdir.sh: the files the
# port changed or the tree lacks) first, else the one the farm generated ($F/gen) for
# either. A source the tree has under another extension is that one: nmake made the
# object of a source by its inference rules, in the order of .SUFFIXES (dplay's dllmain.cpp
# is the tree's dllmain.c). Usage: srclist.sh RTL [root [ext]]
# (NT 4.0 names are lower case, as are the port's.)
d=${2:-/work/private/ntos}/$(echo "$1" | tr A-Z a-z)
ext=${3:-c}
s=$d/up/sources; [ -f "$s" ] || s=$d/sources
look() {
    local n=$1 p g f u=$d/..
    # (a component of the port alone has no directory in the tree to go up from)
    [ -d "$d" ] || u=$(realpath -m "$d/..")
    for p in "$d" "$u"; do
        g=$(bash "$(dirname "$0")/portdir.sh" $(realpath -m "$p"))/$(echo $n | tr A-Z a-z)
        [ -f $g ] && { echo $g; return 0; }
        # (a component of the port alone has no directory in the tree: srcmacro.sh)
        f=$(ls "$p" 2>/dev/null | grep -ix "$n") && { echo "$p/$f"; return 0; }
    done
    for g in /tmp/f/gen/${d#/work/private/} /tmp/f/gen/$(dirname ${d#/work/private/}); do
        [ -f $g/$(echo $n | tr A-Z a-z) ] && { echo $g/$(echo $n | tr A-Z a-z); return 0; }
    done
    return 1
}
# (a source named twice is built once, as build.exe keeps one record of a file: psxss.rc;
# a name can hold a dash: time-api.cpp of OLE Automation)
bash "$(dirname "$0")/srcmacro.sh" "$s" SOURCES | grep -oiE "[a-z0-9_-]+\\.$ext\\b" | awk '!s[tolower($0)]++' | while read -r n; do
    look $n && continue
    for x in cxx cpp c; do
        [ "$x" = "$(echo ${n##*.} | tr A-Z a-z)" ] || ! look ${n%.*}.$x || continue 2
    done
    echo "srclist: $1 missing $n" >&2
done
