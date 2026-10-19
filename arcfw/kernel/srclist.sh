#!/bin/bash
# Print the sources NT's <component>/UP/SOURCES (or <component>/SOURCES) builds,
# resolved to the on-disk file names: the port's version of a source
# (portdir.sh: the files the port changed or the tree lacks) first,
# else, like the NT build, the source in the component directory or its parent, else
# the one the farm generated ($F/gen) for either. Usage: srclist.sh RTL [root [ext]]
d=${2:-/work/PRIVATE/NTOS}/$1
ext=${3:-c}
s=$d/UP/SOURCES; [ -f "$s" ] || s=$d/SOURCES
P=$(bash "$(dirname "$0")/portdir.sh" $d)
bash "$(dirname "$0")/srcmacro.sh" "$s" SOURCES | grep -oiE "[a-z0-9_]+\\.$ext\\b" | while read -r n; do
    g=$P/$(echo $n | tr A-Z a-z)
    [ -f $g ] && { echo $g; continue; }
    for p in "$d" "$d/.."; do
        f=$(ls "$p" | grep -ix "$n") && { echo "$p/$f"; continue 2; }
    done
    for g in /tmp/f/gen/${d#/work/PRIVATE/} /tmp/f/gen/$(dirname ${d#/work/PRIVATE/}); do
        [ -f $g/$(echo $n | tr A-Z a-z) ] && { echo $g/$(echo $n | tr A-Z a-z); continue 2; }
    done
    echo "srclist: $1 missing $n" >&2
done
