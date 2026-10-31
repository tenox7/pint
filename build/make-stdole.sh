#!/usr/bin/env bash
# Build stdole32.tlb, the type library of the standard types of OLE Automation (GUID,
# DISPPARAMS, EXCEPINFO, IUnknown, IDispatch, IEnumVARIANT), as oleauto\build\mktyplib.mak
# makes it for WIN32: the C preprocessor over src\mktyplib\stdole.odl, mktyplib over that
# (obj/MKTYPLIB.EXE: make-exe.sh oleauto/src/mktyplib mktyplib) on the machine of the build,
# as NT's build ran it on each CPU: here the ARM Wine of the build image, with our ole32.dll
# (it writes the CompObj stream) and oleaut32.dll (the library); then stdole32.rc (the
# library as resource 1 TYPELIB, and the version) linked into a DLL of resources alone:
# no entry, no default libraries, heap and stack 0.
#
# Usage:  cd RASPI/build && ./make-stdole.sh       # obj/STDOLE32.TLB
#         INSTALL=1 ./make-stdole.sh              # also into System32
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"

docker run --rm -e INSTALL="${INSTALL:-0}" -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
O=/work/RASPI/obj
S=/work/RASPI/sdcard/root/WINNT/System32
M=/work/private/oleauto/src/mktyplib
SUBSYS=oleauto bash mkfarm.sh /tmp/f
W=/tmp/stdole; rm -rf $W; mkdir -p $W
[ -f $O/MKTYPLIB.EXE ] || { echo "   no obj/MKTYPLIB.EXE (make-exe.sh oleauto/src/mktyplib mktyplib)"; exit 1; }
for f in OLE32.DLL OLEAUT32.DLL; do [ -f $S/$f ] || { echo "   no $f in System32 (build it first)"; exit 1; }; cp $S/$f $W/; done
cp $O/MKTYPLIB.EXE $W/ && cp $M/stdole.odl $W/stdole32.odl
cd $W
cl -nologo -EP stdole32.odl > stdole32.pre 2> cpp.err || { cat cpp.err; exit 1; }
(export WINEPREFIX=/opt/wine32; /usr/lib/wine/wineserver32 -p 2>/dev/null
 WINEDLLOVERRIDES="ole32,oleaut32=n,b" /usr/lib/wine/wine MKTYPLIB.EXE /h /o stdole32.log /tlb stdole32.tmp /nocpp stdole32.pre) > mktyplib.out 2>&1
[ -s stdole32.tmp ] || { echo ">> MKTYPLIB FAILED"; tr -d "\r" < mktyplib.out; exit 1; }
tr -d "\r" < stdole32.log
res=$(bash $B/rc.sh $M $M/stdole32.rc -DWIN32 -I$W -I/work/private/oleauto/src/inc -I/tmp/f/pub -I/tmp/f/crt) || exit 1
# (no image version, as that link gave none; the operating system version of the linker of NT 4.0)
link -nologo -machine:arm -dll -noentry -nodefaultlib -debug:none -heap:0,0 -stack:0,0 -osversion:4.00 -map:$W/stdole32.map \
     -out:$W/STDOLE32.TLB $res > link.err 2>&1 || { echo ">> LINK FAILED"; tr -d "\r" < link.err; exit 1; }
ntver $W/STDOLE32.TLB || exit 1
cp $W/STDOLE32.TLB $W/stdole32.map $O/
OUT=$O; [ "$INSTALL" = 1 ] && OUT=$S && cp $W/STDOLE32.TLB $S/
echo "   wrote $OUT/STDOLE32.TLB"
'
