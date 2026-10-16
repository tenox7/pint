#!/bin/bash
# Print the sources NT's <component>/UP/SOURCES (or <component>/SOURCES) builds,
# resolved to the on-disk file names; like the NT build, a source not in the
# component directory is taken from its parent, else from the ported sources of
# the component (arcfw/ported/<component>, those missing from the tree) or from
# the sources the farm generated ($F/gen). Usage: srclist.sh RTL [root [ext]]
d=${2:-/work/PRIVATE/NTOS}/$1
ext=${3:-c}
s=$d/UP/SOURCES; [ -f "$s" ] || s=$d/SOURCES
r=$(echo ${d#/work/PRIVATE/} | tr A-Z a-z)
bash "$(dirname "$0")/srcmacro.sh" "$s" SOURCES | grep -oiE "[a-z0-9_]+\\.$ext\\b" | while read -r n; do
    for p in "$d" "$d/.."; do
        f=$(ls "$p" | grep -ix "$n") && { echo "$p/$f"; continue 2; }
    done
    for g in /work/RASPI/arcfw/ported/$r/$(echo $n | tr A-Z a-z) /tmp/f/gen/${d#/work/PRIVATE/}/$(echo $n | tr A-Z a-z); do
        [ -f $g ] && { echo $g; continue 2; }
    done
    echo "srclist: $1 missing $n" >&2
done
