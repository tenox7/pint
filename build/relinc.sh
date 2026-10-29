#!/bin/bash
# Print -I flags for the directories that sources and the headers beside them
# name in relative includes ("..\server\ssiapi.h", "..\string\strcat.c"), which
# the compiler resolves from the directory of the file (the include names are
# flattened, incfix.sed): each farmed as incdirs.sh farms a directory. A relative
# include in a header of an include directory of the component (an -I flag of a
# farmed directory) is resolved, as by the compiler, from the directory of the
# source that includes it ("..\string\mappers.hxx" of NET UI's pchstr.hxx). A directory
# the -I flags given hold already is not printed again.
#   With -a, the subdirectories of include directories that the sources and the headers
# beside them name in angle brackets instead (HyperTerminal's <tdll\assert.h>; not sys\, which
# incfix.sed keeps), which the compiler found through those include directories, ahead of
# the directories of the SDK and the C run-time.
# Usage: relinc.sh [-a] [-I/tmp/f/dirs/<directory>...] source...
B=$(cd "$(dirname "$0")" && pwd)
angle= inc= srcs=()
[ "$1" != -a ] || { angle=1; shift; }
for a; do
    case $a in
    -I/tmp/f/dirs/*) inc="$inc ${a#-I/tmp/f/dirs}" ;;
    -I*) ;;
    *) srcs+=("$a") ;;
    esac
done
# (also a parent directory alone, "..\global.h", the directories of the tree that includes
# name from its root: "/nt/private/inc/srvfsctl.h", and a subdirectory a quoted include names:
# "ppc\vunwind.c" of imagehlp's walkppc.c)
rel() {
    if [ -n "$angle" ]; then
        tr -d '\r' < "$1" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*<([A-Za-z0-9_][^>:]*)[\\/][^\\/>]+>.*/\1/p' | grep -vix sys
    else
        tr -d '\r' < "$1" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<](\.\.?|\.\.?[\\/][^">]*|[\\/][Nn][Tt][\\/][^">]*)[\\/][^\\/">]+[">].*/\1/p; s/^[[:space:]]*#[[:space:]]*include[[:space:]]*"([A-Za-z0-9_][^":]*)[\\/][^\\/"]+".*/\1/p'
    fi | tr / '\\' | sort -u
}
dirs=$(for s in "${srcs[@]}"; do dirname "$s"; done | sort -u)
{
for f in "${srcs[@]}" $(for d in $dirs; do ls "$d"/*.[Hh] 2>/dev/null; done); do
    [ -f "$f" ] || continue
    # (one the directory of the file has not, from an include directory, as the compiler looks:
    # "..\..\..\stg\h\dfentry.hxx" of OLE's com\moniker2, from ..\..\dcomidl\daytona)
    rel "$f" | while read -r r; do
        o=$(bash $B/incdirs.sh "$(dirname "$f")" "$r" | cut -d" " -f3-)
        [ -n "$o" ] || for i in $inc; do o=$(bash $B/incdirs.sh "$i" "$r" | cut -d" " -f3-); [ -n "$o" ] && break; done
        echo "$o"
    done
done
[ -n "$angle" ] || for f in $(for i in $inc; do grep -liE '^[[:space:]]*#[[:space:]]*include[[:space:]]*"\.\.?[\\/]' "$i"/*.[Hh] "$i"/*.[Hh][Xx][Xx] 2>/dev/null; done); do
    rel "$f" | while read -r r; do for d in $dirs; do bash $B/incdirs.sh "$d" "$r" | cut -d" " -f3-; done; done
done
} | tr ' ' '\n' | grep -v '^$' | awk -v given=" $* " '!seen[$0]++ && !index(given, " " $0 " ")' | tr '\n' ' '
