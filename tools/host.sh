#!/bin/bash
# host.sh TOOL ARG...: run an NT build tool (obj/host/TOOL.EXE, tools/make-tools.sh) on
# the 32-bit ARM Wine of the build image. The tools take / for a switch: an absolute
# path, alone or after an option (-I/abs, -o/abs), goes to them in DOS form (Z:\abs).
H=${NTROOT:-/work}/RASPI/obj/host
export WINEPREFIX=/opt/wine32 WINEDEBUG=-all
/usr/lib/wine/wineserver32 -p 2>/dev/null
t=$1; shift
dos() { printf 'Z:%s' "$1" | tr / '\\'; }
abs() { [ "$(dirname "$1")" != / ] && [ -d "$(dirname "$1")" ]; }
for a; do
    shift
    case $a in
    /dev/null) a=NUL ;;
    /*) abs "$a" && a=$(dos "$a") ;;
    -*) p=$(printf '%s' "$a" | sed -nE 's#^-[A-Za-z_]+:?(/.*)$#\1#p'); [ -n "$p" ] && abs "$p" && a="${a%"$p"}$(dos "$p")" ;;
    esac
    set -- "$@" "$a"
done
exec /usr/lib/wine/wine $H/$(echo $t | tr a-z A-Z).EXE "$@"
