#!/bin/bash
# Print the sources of the CRT32NT objects in the NT library (the OBJECTS list
# CRT32NT\MAKEFILE gives for TARGETNAMESUFFIX nt), resolved to the on-disk names.
# Usage: crtlist.sh [crt32nt dir]
C=${1:-/work/PRIVATE/CRT32NT}
K=$(cd "$(dirname "$0")/../kernel" && pwd)
tr -d '\r' < $C/MAKEFILE | awk '/TARGETNAMESUFFIX\)" == "nt"/ {n++} n == 2 && /OBJECTS=/ {p = 1} p {print} p && !/\\$/ {exit}' |
grep -oiE '[a-z]+\\obj\\[a-z0-9_*]+\.obj' | tr '\\' ' ' | while read -r d o f; do
    D=$(echo $d | tr a-z A-Z)
    if [ "$f" = "*.obj" ]; then bash $K/srclist.sh $D $C; continue; fi
    n=$(ls $C/$D | grep -ix "${f%.obj}.c") || { echo "crtlist: missing $d/${f%.obj}.c" >&2; continue; }
    echo $C/$D/$n
done
