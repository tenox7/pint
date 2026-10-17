#!/bin/bash
# Print -I flags for the directories that sources and the headers beside them
# name in relative includes ("..\server\ssiapi.h", "..\string\strcat.c"), which
# the compiler resolves from the directory of the file (the include names are
# flattened, incfix.sed): each farmed as incdirs.sh farms a directory. A relative
# include in a header of an include directory of the component (an -I flag of a
# farmed directory) is resolved, as by the compiler, from the directory of the
# source that includes it ("..\string\mappers.hxx" of NET UI's pchstr.hxx).
# Usage: relinc.sh [-I/tmp/f/dirs/<directory>...] source...
K=$(cd "$(dirname "$0")" && pwd)
inc= srcs=()
for a; do
    case $a in
    -I/tmp/f/dirs/*) inc="$inc ${a#-I/tmp/f/dirs}" ;;
    -I*) ;;
    *) srcs+=("$a") ;;
    esac
done
rel() { tr -d '\r' < "$1" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<](\.\.?[\\/][^">]*)[\\/][^\\/">]+[">].*/\1/p' | sort -u; }
dirs=$(for s in "${srcs[@]}"; do dirname "$s"; done | sort -u)
{
for f in "${srcs[@]}" $(for d in $dirs; do ls "$d"/*.[Hh] 2>/dev/null; done); do
    [ -f "$f" ] || continue
    rel "$f" | while read -r r; do bash $K/incdirs.sh "$(dirname "$f")" "$r" | cut -d" " -f3-; done
done
for f in $(for i in $inc; do grep -liE '^[[:space:]]*#[[:space:]]*include[[:space:]]*"\.\.?[\\/]' "$i"/*.[Hh] "$i"/*.[Hh][Xx][Xx] 2>/dev/null; done); do
    rel "$f" | while read -r r; do for d in $dirs; do bash $K/incdirs.sh "$d" "$r" | cut -d" " -f3-; done; done
done
} | tr ' ' '\n' | grep -v '^$' | awk '!seen[$0]++' | tr '\n' ' '
