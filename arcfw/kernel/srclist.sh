#!/bin/bash
# Print the sources NT's <component>/UP/SOURCES (or <component>/SOURCES) builds,
# resolved to the on-disk file names: the port's version of a source
# (arcfw/ported/<component>, the files the port changed or the tree lacks) first,
# else, like the NT build, the source in the component directory or its parent, else
# the one the farm generated ($F/gen). Usage: srclist.sh RTL [root [ext]]
d=${2:-/work/PRIVATE/NTOS}/$1
ext=${3:-c}
s=$d/UP/SOURCES; [ -f "$s" ] || s=$d/SOURCES
r=$(echo ${d#/work/PRIVATE/} | tr A-Z a-z)
bash "$(dirname "$0")/srcmacro.sh" "$s" SOURCES | grep -oiE "[a-z0-9_]+\\.$ext\\b" | while read -r n; do
    g=/work/RASPI/arcfw/ported/$r/$(echo $n | tr A-Z a-z)
    [ -f $g ] && { echo $g; continue; }
    for p in "$d" "$d/.."; do
        f=$(ls "$p" | grep -ix "$n") && { echo "$p/$f"; continue 2; }
    done
    g=/tmp/f/gen/${d#/work/PRIVATE/}/$(echo $n | tr A-Z a-z)
    [ -f $g ] && { echo $g; continue; }
    echo "srclist: $1 missing $n" >&2
done
