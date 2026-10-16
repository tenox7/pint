#!/usr/bin/env bash
# The pass 0 of the NT build: run the midl (and wcshdr) recipes of the components' MAKEFIL0
# (with arcfw/kernel/nmk.awk) on a lower case mirror of their directories, with
# MIDL (obj/host, tools/make-tools.sh, on the 32-bit Wine: tools/host.sh) and the ARM
# target: MIDL_CPP is the cl -E of the target (obj/host/MIDLCPP.EXE, midlcpp),
# MIDL_FLAGS -D_ARM_ -D_WCHAR_T_DEFINED. The stubs and headers it makes land in
# obj/gen/midl/<directory>, where mkfarm.sh gives them to the builds.
#
# Usage:  cd RASPI/tools/midl && ./gen-midl.sh          # all
#         ./gen-midl.sh NET/SVCDLLS LSA            # the recipes of those makefiles only
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"
[ -f "$NT35/RASPI/obj/host/MIDL.EXE" ] || { echo "gen-midl: build the tools first (tools/make-tools.sh)"; exit 1; }

docker run --rm -e ONLY="$*" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel --entrypoint bash nt-msvc:latest -c '
set -eu
K=/work/RASPI/arcfw/kernel
P=/work/PRIVATE
W=/tmp/mw
G=/work/RASPI/obj/gen/midl
bash mkfarm.sh /tmp/f > /dev/null
host() { bash /work/RASPI/tools/host.sh "$@"; }
dos() { printf "Z:%s" "$1" | tr / "\\\\"; }

lower() { printf "%s" "$1" | tr A-Z a-z; }
# The real directory of a path relative to the current one, case insensitively.
real() {
    local p=$CUR c n
    for c in $(echo "$1" | tr "/" " "); do
        case $c in
        ..) p=$(dirname $p) ;;
        .) ;;
        *) n=$(ls $p 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo $p
}
# Mirror a directory of the tree, lower case, with all its subdirectories.
mirror() {
    local m=$W/$(lower "${1#$P/}") f
    [ -e $m/.mirrored ] && return
    mkdir -p $m
    (cd "$1" && find . -type d) | while read -r f; do mkdir -p "$m/$(lower "$f")"; done
    (cd "$1" && find . -type f) | while read -r f; do
        tr -d "\032\r" < "$1/$f" | sed -E -f /work/RASPI/tools/midl/incpath.sed > "$m/$(lower "$f")"
    done
    find $m -type f -exec touch -d @0 {} +
    find $m -type d | while read -r f; do : > "$f/.mirrored"; done
}
# A path of a recipe: the SDK and OAK include directories are the farm of the ARM
# build (the ARM headers ahead of the SDK ones); a directory of the tree is mirrored.
path() {
    case $1 in
    BASEDIR/public/sdk/inc) echo "/work/RASPI/arcfw/kernel/inc -I/tmp/f/pub" ;;
    BASEDIR/public/sdk/inc/crt) echo /tmp/f/crt ;;
    BASEDIR/public/oak/inc) echo /tmp/f/oak ;;
    BASEDIR/private/*) local r; r=$(CUR=/work real "${1#BASEDIR/}") && mirror $r && echo $W/$(lower "${r#$P/}") ;;
    nul|NUL) echo nul ;;
    *) local r d=$1
       [ -d "$(real "$d")" 2>/dev/null ] || d=$(dirname "$1")
       r=$(real "$d") && mirror $r
       lower "$1" ;;
    esac
}
arg() {
    case $1 in
    -I*) echo "-I$(path "${1#-I}")" ;;
    -*) echo "$1" ;;
    *) path "$1" ;;
    esac
}
nmk_midl() {
    local a=() x o
    while [ $# -gt 0 ]; do
        case $1 in
        -cpp_cmd) a+=("$1" "$2"); shift 2 ;;
        -cpp_opt) o=""; for x in $2; do for x in $(arg $x); do case $x in -I/*) x=-I$(dos ${x#-I}) ;; esac; o="$o $x"; done; done; a+=(-cpp_opt "$o"); shift 2 ;;
        -prefix) a+=("$1" "$2" "$3"); shift 3 ;;
        -acf|-header|-cstub|-sstub|-saux|-caux) a+=("$1" "$(path "$2")"); shift 2 ;;
        -I*) for x in $(arg "$1"); do a+=("$x"); done; shift ;;
        -*) a+=("$1"); shift ;;
        *) a+=("$(path "$1")"); shift ;;
        esac
    done
    local s=0
    host midl "${a[@]}" > /tmp/midl.out 2>&1 || s=$?
    grep -v "^Processing\\|^Microsoft\\|^Copyright\\|MIDL2169" /tmp/midl.out || true
    return $s
}
nmk_exists() { [ -e "$(lower "$1")" ]; }
nmk_copy() { cp "$(path "$1")" "$(path "$2")"; }
nmk_del() { local f; for f in "$@"; do rm -f "$(lower "$f")"; done; }
nmk_wcshdr() { host wcshdr < "$(path "$1")" > "$(path "$2")"; }

recipe() {
    local mk=$1; shift
    want $mk || return 0
    CUR=$(dirname $P/$mk)
    # the component: the includes reach its sibling directories (NOMIRROR: only the
    # directories the recipe names)
    if [ -n "${NOMIRROR:-}" ]; then mkdir -p $W/$(lower "${CUR#$P/}"); elif [ "$(dirname $CUR)" = $P ]; then mirror $CUR; else mirror $(dirname $CUR); fi
    : > /tmp/mw.stamp
    sleep 1
    for t in "$@"; do
        NMK_MACROS="MIDL_CPP=/work/RASPI/obj/host/MIDLCPP.EXE
MIDL_FLAGS=-D_ARM_ -D_WCHAR_T_DEFINED" NMK_KEEP=BASEDIR NMK_TARGET="$t" awk -f $K/nmk.awk $P/$mk > /tmp/nmk.sh
        (cd $W/$(lower "${CUR#$P/}") && . /tmp/nmk.sh)
    done
    # what the recipes made, in the directories of the tree
    find $W -type f -newer /tmp/mw.stamp ! -name .mirrored | while read -r f; do
        d=$(dirname "$f"); r=$P
        for c in $(echo "${d#$W/}" | tr "/" " "); do r=$r/$(ls $r | grep -ix "$c" | head -1); done
        mkdir -p $G/${r#$P/}
        tr -d "\r" < "$f" > $G/${r#$P/}/$(basename "$f")
        echo "   ${r#$P/}/$(basename "$f")"
    done
}

# the recipes named (by the start of their makefile path), else all of them afresh
want() { local o; [ -z "$ONLY" ] && return 0; for o in $ONLY; do case $1 in $o*) return 0 ;; esac; done; return 1; }
[ -n "$ONLY" ] || rm -rf $G
mkdir -p $G
recipe WINDOWS/SCREG/WINREG/MAKEFIL0 "\$(TARGETS)"
recipe WINDOWS/SCREG/SC/MAKEFIL0 "\$(TARGETS)"
recipe LSA/MAKEFIL0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
recipe NEWSAM/MAKEFIL0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
recipe EVENTLOG/MAKEFIL0 "elfclnt\\\$(ELFIDL)_c.c elfclnt\\\$(ELFIDL)_x.c server\\\$(ELFIDL)_y.c server\\\$(ELFIDL)_s.c \$(ELFIDL).h"
recipe RPC/RUNTIME/RTIFS/MAKEFIL0 "\$(MGMT_HDR) \$(MGMT_CSTUB) \$(MGMT_SSTUB)" "\$(CONV_HDR) \$(CONV_CSTUB) \$(CONV_SSTUB)" "\$(EPMP_HDR) \$(EPMP_CSTUB) \$(EPMP_SSTUB)"
recipe WINDOWS/SPOOLER/SPOOLSS/MAKEFIL0 "\$(TARGETS)"
recipe WINDOWS/NETDDE/SRC/NDEAPI/MAKEFIL0 "\$(TARGETS)"
NOMIRROR=1 recipe NET/MAKEFIL0 "inc\\dosprint.h" "inc\\rxprint.h"
for s in SRVSVC WKSSVC BROWSER REPL MSGSVC AT; do recipe NET/SVCDLLS/$s/MAKEFIL0 "\$(TARGETS)"; done
for s in LOGONSRV RPL; do recipe NET/SVCDLLS/$s/MAKEFIL0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"; done
'
