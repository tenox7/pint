#!/usr/bin/env bash
# The pass 0 of the NT build: run the midl (and wcshdr) recipes of the components' MAKEFIL0
# (with build/nmk.awk) and the .idl rule of makefile.def for the interfaces a SOURCES lists
# (types\idl) on a lower case mirror of their directories, with
# MIDL (obj/host, tools/make-tools.sh, on the 32-bit Wine: tools/host.sh) and the ARM
# target: MIDL_CPP is the cl -E of the target (obj/host/MIDLCPP.EXE, midlcpp),
# MIDL_FLAGS -D_ARM_ -D_WCHAR_T_DEFINED. The stubs and headers it makes land in
# obj/gen/midl/<directory>, where mkfarm.sh gives them to the builds.
#
# Usage:  cd RASPI/tools/midl && ./gen-midl.sh          # all
#         ./gen-midl.sh net/svcdlls lsa            # the recipes of those makefiles only
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../../.. && pwd)"
[ -f "$NT40/RASPI/obj/host/MIDL.EXE" ] || { echo "gen-midl: build the tools first (tools/make-tools.sh)"; exit 1; }

docker run --rm -e ONLY="$*" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -eu
B=/work/RASPI/build
P=/work/private
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
# Mirror a directory of the tree, lower case, with all its subdirectories. None outside the
# tree: no recipe reaches one through $W, and the parent of an include directory the tree has
# not (..\..\..\..\inc of nw\svcdlls\nwwks) is all of /work.
mirror() {
    case $1 in $P/*) ;; *) return ;; esac
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
# build (the ARM headers ahead of the SDK ones), as is private\windows\inc, which
# holds the headers of the header pass (winregp.h); a directory of the tree is mirrored.
# (\nt\x, the tree on the drive of the build, /nt/x from nmk.awk, is BASEDIR\x: nw\svcdlls\nwwks SDKINC.)
path() {
    case $1 in /nt/*) set -- "BASEDIR/${1#/nt/}" ;; esac
    case $1 in
    BASEDIR/public/sdk/inc) echo "/work/RASPI/kernel/inc -I/tmp/f/pub" ;;
    BASEDIR/public/sdk/inc/crt) echo /tmp/f/crt ;;
    BASEDIR/public/oak/inc) echo /tmp/f/oak ;;
    BASEDIR/private/windows/inc) echo /tmp/f/winc ;;
    # (a file there, also one a recipe makes: in its directory, mirrored)
    BASEDIR/private/*) local r f=
       r=$(CUR=/work real "${1#BASEDIR/}") && [ -d "$r" ] || { f=/$(lower "${1##*/}"); r=$(CUR=/work real "$(dirname "${1#BASEDIR/}")") || return 1; }
       mirror $r && echo $W/$(lower "${r#$P/}")$f ;;
    nul|NUL) echo nul ;;
    *) local r d=$1
       [ -d "$(real "$d")" 2>/dev/null ] || d=$(dirname "$1")
       r=$(real "$d") && mirror $r
       lower "$1" ;;
    esac
}
arg() {
    case $1 in
    -I*) local i; for i in $(printf "%s" "${1#-I}" | tr ";" " "); do echo "-I$(path "$i")"; done ;;
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
        -D|-U|-client|-server) a+=("$1" "$2"); shift 2 ;;
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

MIDL_CPP=/work/RASPI/obj/host/MIDLCPP.EXE
MIDL_FLAGS="-D_ARM_ -D_WCHAR_T_DEFINED"
# (MACROS: more macros of a recipe, those of its SOURCES that a makefile.inc takes.)
# The directory of a component, mirrored: the includes reach its sibling directories
# (NOMIRROR: only the directories the recipe names), with its obj directory (that of a
# makefile.inc recipe, which the tree has not); keep takes what is made from now.
component() {
    CUR=$P/$1
    if [ -n "${NOMIRROR:-}" ]; then mkdir -p $W/$(lower "$1"); elif [ "$(dirname $CUR)" = $P ]; then mirror $CUR; else mirror $(dirname $CUR); fi
    mkdir -p $W/$(lower "$1")/obj
    : > /tmp/mw.stamp
    sleep 1
}
# what the recipes made, in the directories of the tree
keep() {
    find $W -type f -newer /tmp/mw.stamp ! -name .mirrored | while read -r f; do
        d=$(dirname "$f"); r=$P
        for c in $(echo "${d#$W/}" | tr "/" " "); do n=$(ls $r 2>/dev/null | grep -ix "$c" | head -1); r=$r/${n:-$c}; done
        mkdir -p $G/${r#$P/}
        tr -d "\r" < "$f" > $G/${r#$P/}/$(basename "$f")
        echo "   ${r#$P/}/$(basename "$f")"
    done
}
# A makefile with the files it includes in their place, as nmake reads them (named from its
# directory; $(NTMAKEENV)\makefile.plt is none here).
incl() {
    local l f
    while IFS= read -r l || [ -n "$l" ]; do
        case $(printf "%s" "$l" | tr A-Z a-z) in
        "!include "*) f=$(CUR=$(dirname "$1") real "$(printf "%s" "${l#* }" | tr -d "\r " | tr "\\\\" /)") && [ -f "$f" ] && { incl "$f"; continue; } ;;
        esac
        printf "%s\n" "$l"
    done < "$1"
}
recipe() {
    local mk=$1; shift
    want $mk || return 0
    component $(dirname $mk)
    incl $P/$mk > /tmp/nmk.mk
    for t in "$@"; do
        NMK_MACROS="MIDL_CPP=$MIDL_CPP
MIDL_FLAGS=$MIDL_FLAGS
${MACROS:-}" NMK_KEEP=BASEDIR NMK_TARGET="$t" awk -f $B/nmk.awk /tmp/nmk.mk > /tmp/nmk.sh
        (cd $W/$(lower "${CUR#$P/}") && . /tmp/nmk.sh)
    done
    keep
}
# A makefile.inc recipe with the macros of the SOURCES beside it, as makefile.def has them
# when it reads the makefile.inc (DOCFILEINC, INCLUDES, C_DEFINES: the ARM MIDL_FLAGS last),
# and TARGET_CPP.
srcrecipe() {
    want $1 || return 0
    local m; m=$(bash $B/srcmacro.sh $P/$(dirname $1)/sources "*" | sed -e "s/\$(BASEDIR)/BASEDIR/g" -e "s/\$([A-Za-z_0-9]*)//g")
    MACROS="$m
C_DEFINES=$(printf "%s\n" "$m" | sed -n "s/^C_DEFINES=//p") $MIDL_FLAGS
TARGET_CPP=$MIDL_CPP" recipe "$@"
}
# The interfaces a SOURCES lists, by the .idl rule of makefile.def for IDL_TYPE OLE (that of
# each such SOURCES of the tree): the proxy and dlldata.c to PASS0_SOURCEDIR, the GUIDs (-iid)
# to MIDL_UUIDDIR, with the include path of the build (INCPATH0: USER_INCLUDES, INCLUDES and
# the OAK, SDK and C run-time headers), C_DEFINES and MIDL_FLAGS (oleprx32: -hookole). A
# header of the SDK (PASS0_HEADERDIR public\sdk\inc)
# is the one of the tree, which this MIDL (3.00.44) made: the one made here is not kept.
pass0() {
    local d=$1 f h s u o i inc=
    want $d || return 0
    smac() { bash $B/srcmacro.sh $P/$d/sources $1 | sed -e "s/\$(BASEDIR)/BASEDIR/g" -e "s/\$([A-Za-z_0-9]*)//g" | tr "\\\\" /; }
    case $(smac IDL_TYPE | tr a-z A-Z) in ""|OLE) ;; *) echo "gen-midl: $d: IDL_TYPE $(smac IDL_TYPE)"; return 1 ;; esac
    h=$(smac PASS0_HEADERDIR); s=$(smac PASS0_SOURCEDIR); u=$(smac MIDL_UUIDDIR); o=$(smac MIDL_OPTIMIZATION)
    s=${s:-.}; u=${u:-$s}
    case $h in BASEDIR/public/*) h=/tmp/mwh; mkdir -p $h ;; "") h=. ;; esac
    for i in $(smac USER_INCLUDES | tr ";" " ") $(smac INCLUDES | tr ";" " ") BASEDIR/public/oak/inc BASEDIR/public/sdk/inc BASEDIR/public/sdk/inc/crt; do inc="$inc -I$i"; done
    component $d
    for f in $(smac SOURCES | tr " " "\n" | grep -i "\\.idl$"); do
        f=$(lower "$f")
        (cd $W/$(lower "$d") && nmk_midl -Zp8 $inc -char unsigned -ms_ext -c_ext -proxy $s/${f%.*}_p.c -dlldata $s/dlldata.c -iid $u/${f%.*}_i.c \
            -header $h/${f%.*}.h -cpp_cmd $MIDL_CPP $(smac C_DEFINES) $MIDL_FLAGS $(smac MIDL_FLAGS) ${o:--Oicf} $f)
    done
    keep
}

# the recipes named (by the start of their makefile path), else all of them afresh
want() { local o; [ -z "$ONLY" ] && return 0; for o in $ONLY; do case $1 in $o*) return 0 ;; esac; done; return 1; }
[ -n "$ONLY" ] || rm -rf $G
mkdir -p $G
recipe windows/screg/winreg/makefil0 "\$(TARGETS)"
recipe windows/screg/sc/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
recipe lsa/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
recipe newsam/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
recipe eventlog/makefil0 "\$(CLIENT_FILES)" "\$(SERVER_FILES)"
# (the interfaces of the runtime: the NTTARGETFILE0 allidl of its makefile.inc)
recipe rpc/runtime/rtifs/makefile.inc "\$(NBASE_HDR)" "\$(MGMT_HDR)" "\$(MGMT_SSTUB)" "\$(CONV_HDR)" "\$(CONV_SSTUB)" "\$(EPMP_HDR)" "\$(EPMP_SSTUB)"
# (the name service interfaces: the client stubs of rpcns4, the server stubs of the locator)
recipe rpc/locator/makefil0 nsicom.h nsisvr.h nsiclt.h nsimgm.h
# (the headers of the interfaces of the locator, and the stubs of the one between locators, loctoloc)
recipe rpc/locator/newservr/makefil0 common.h nsisvr.h nsiclt.h nsimgm.h loctoloc.h
recipe windows/spooler/spoolss/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
recipe windows/netdde/src/ndeapi/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
# (the user mode Plug and Play manager of services.exe and its client, cfgmgr32)
recipe windows/pnp/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
NOMIRROR=1 recipe net/makefil0 "inc\\dosprint.h" "inc\\rxprint.h"
for s in srvsvc wkssvc browser repl msgsvc at logonsrv rpl dfs; do recipe net/svcdlls/$s/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"; done
# (the license API of the server service, ntlsapi)
recipe net/svcdlls/lls/makefil0 "\$(LSAPI_CLIENT_TARGETS)"
# (the DHCP server API, dhcpsapi: the header of its client stub, which dhcp_clt.c holds edited)
recipe net/sockets/tcpcmd/dhcp/server/makefil0 "\$(CLIENT_TARGETS)"
# (the NetWare client: the interface between nwprovau and the NWCWorkstation service, nwwks)
recipe nw/svcdlls/nwwks/makefil0 "\$(TARGETS)"
# (Services for Macintosh: the interface between sfmapi and the AFP service, sfmsvc)
recipe net/sfm/afp/service/makefil0 "\$(TARGETS)"
recipe net/sockets/tcpcmd/ftpsvc/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)"
# (TAPI: the interface between tapi32 and tapisrv, and the one of the remote service
# provider; the makefile of the NT build: OS Windows_NT, as on NT)
MACROS="OS=Windows_NT" recipe tapi/dev/makefil0 "\$(CLIENT_TARGETS)" "\$(SERVER_TARGETS)" "\$(CLIENT_TARGETS2)" "\$(SERVER_TARGETS2)"
# (DCOM: the object exporter and activation interfaces of ole32 and rpcss)
MACROS="TARGET_CPP=$MIDL_CPP
INCLUDES=.;..\\cinc;BASEDIR\\public\\sdk\\inc;obj
C_DEFINES=-DFLAT -DWIN32=100 -D_NT1X_=100 -DUNICODE -D_UNICODE -DINC_OLE2 -DNOEXCEPTIONS -DCAIROLE_DOWNLEVEL $MIDL_FLAGS" recipe dcomidl/makefile.inc \
    "obj\\dce.h" "obj\\obase.h" "obj\\objex.h" "obj\\remact.h" "obj\\lclor.h" "obj\\odeth.h" "obj\\rawodeth.h" "obj\\iface.h" "obj\\orcb.h" "obj\\remunk.h"
# (OLE: the activation and running object table interfaces of ole32 and rpcss, with their
# stubs for com\class and dcomss\olescm; the access control pickling of com\accctrl; the
# connection interface of the asynchronous docfile, also to stg\h; the proxies of oleprx32)
srcrecipe ole32/com/dcomidl/daytona/makefile.inc irot.h getif.h host.h objsrv.h scm.h dscm.h multqi.h
srcrecipe ole32/com/accctrl/daytona/makefile.inc "obj\\acpickl.h"
srcrecipe ole32/stg/async/docfile/daytona/makefile.inc "obj\\iconn.h" "\$(DOCFILEINC)\\iconn.h"
pass0 ole32/oleprx32/idl/daytona
# (the interfaces of OLE, their GUIDs for types\uuid: uuid.lib)
pass0 types/idl
pass0 types/filter/idl
'
