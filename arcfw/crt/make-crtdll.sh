#!/usr/bin/env bash
# Link crtdll.dll as CRTLIB\MAKEFILE does, with Microsoft C for ARM: the DLL C
# run-time (obj/um/libcdll.lib, make-crt32.sh DLL) importing kernel32, at base
# 0x10010000 with entry _CRTDLL_INIT, exporting CRTDLL.DEF and its ARM part
# (lib/arm/crtdll.arm), with the resources of crtdll.rc. Its import library, with
# the startup objects of its client images (obj/um/crtdlls.lib), is obj/um/crtdll.lib.
#
# Usage:  cd RASPI/arcfw/crt && ./make-crtdll.sh       INSTALL=1 to install
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../../.. && pwd)"

docker run --rm -e INSTALL="${INSTALL:-0}" -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/arcfw/kernel \
    --entrypoint bash nt-msvc:latest -c '
set -u
. ./msvc.sh
A=/work/RASPI/arcfw/crt
O=/work/RASPI/obj
U=$O/um
L=/work/PRIVATE/CRTLIB
SUBSYS=" " bash mkfarm.sh /tmp/f
DIR=/tmp/crtdll; rm -rf $DIR; mkdir -p $DIR
[ -f $U/kernel32.lib ] || { echo "   build the kernel32 import library first"; exit 1; }

cat $L/LIB/CRTDLL.DEF $A/lib/arm/crtdll.arm | tr -d "\032\r" | sed -f $K/deffix.sed > $DIR/crtdll.def
lib -nologo -machine:arm -def:$DIR/crtdll.def -out:$DIR/crtdll.lib > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
RES=$(bash rc.sh $L $L/CRTDLL.RC -D_ARM_ -DWIN32=100 -D_WIN32_=1 -DNT_UP=1 -DDBG=0 -DDEVL=1 -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt) || exit 1
echo ">> linking crtdll.dll"
link -nologo -machine:arm -release -dll -entry:_CRTDLL_INIT -base:0x10010000 -subsystem:console -nodefaultlib \
     -debug -pdb:$O/crtdll.pdb -map:$O/crtdll.map -out:$DIR/CRTDLL.DLL $DIR/crtdll.exp $RES $U/libcdll.lib $U/kernel32.lib \
     > $DIR/link.err 2>&1 || { echo ">> LINK FAILED"; grep -E "error|unresolved" $DIR/link.err | sort | uniq -c | sort -rn | head -40; exit 1; }
bash /work/RASPI/tools/host.sh pe2img -version 3.50 $DIR/CRTDLL.DLL || exit 1
lib -nologo -out:$U/crtdll.lib $DIR/crtdll.lib $U/crtdlls.lib > $DIR/lib.err 2>&1 || { cat $DIR/lib.err; exit 1; }
OUT=$O; [ "$INSTALL" = 1 ] && OUT=/work/RASPI/sdcard/root/WINNT/System32
cp $DIR/CRTDLL.DLL $OUT/CRTDLL.DLL
echo "   wrote obj/um/crtdll.lib, $OUT/CRTDLL.DLL"
'
