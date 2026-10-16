#!/bin/bash
# Print -I flags for the directories that sources and the headers beside them
# name in relative includes ("..\server\ssiapi.h", "..\string\strcat.c"), which
# the compiler resolves from the directory of the file (the include names are
# flattened, incfix.sed): each farmed as incdirs.sh farms a directory.
# Usage: relinc.sh source...
K=$(cd "$(dirname "$0")" && pwd)
for f in "$@" $(for d in $(for s in "$@"; do dirname "$s"; done | sort -u); do ls "$d"/*.[Hh] 2>/dev/null; done); do
    [ -f "$f" ] || continue
    tr -d '\r' < "$f" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<](\.\.?[\\/][^">]*)[\\/][^\\/">]+[">].*/\1/p' |
        sort -u | while read -r r; do bash $K/incdirs.sh "$(dirname "$f")" "$r" | cut -d" " -f3-; done
done | tr ' ' '\n' | grep -v '^$' | awk '!seen[$0]++' | tr '\n' ' '
