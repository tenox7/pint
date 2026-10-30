#!/bin/bash
# The container half of make-infs.sh: the tools of setup\inf\win4 (PRODFILT and XDOSNET) as
# NT builds them, then the INFs of the workstation by the recipes of its
# setup\inf\win4\inf\daytona\wks (NTTARGETFILE0 make_infs of makefile.inc, PRODSWITCH w)
# for ARM; the INFs of the setup of NT 3.x that NT 4.0 still ships (the network components
# and MMDRIVER.INF) by setup\inf\daytona (makefile.inc, LOCATION \ntcd\arm); the INFs of the
# modems (unimodem\inf); and the INFs layout.inf puts on the system where text setup copies them.
set -eu
. /work/RASPI/build/msvc.sh
W=/work R=$W/RASPI S=$W/private/windows/setup/inf/win4
H=$R/obj/host U=$R/obj/um T=/tmp/inftools
D=$S/inf/daytona/wks G=/tmp/f/gen/windows/setup/inf/win4/inf/daytona/wks
SS=$W/private/windows/setup IH=$R/obj/inf/host L=/tmp/linf NL=/tmp/linf/inf/newinf/daytona/ntcd/arm
rm -rf $T; mkdir -p $T $IH
SUBSYS=setup bash $B/mkfarm.sh /tmp/f > /dev/null

echo ">> tools"
macro() { bash $B/srcmacro.sh $1/sources $2 | sed "s/\$([A-Za-z_0-9]*)//g"; }
BASE=$(tr -d "\r" < $W/public/sdk/lib/coffbase.txt | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
# tool NAME [LIB...]: the console program of tools\NAME (its UMAPPL; the port's version of
# its source, if any) compiled as NT compiles it, linked with libc and its UMLIBS:
# obj/host/NAME.EXE, made when it is missing or older than its source
tool() {
    local n=$1 d=$S/tools/$1 s p f; shift
    s=$d/$n.c; p=$(bash $B/portdir.sh $d)/$n.c; [ -f $p ] && s=$p
    f=$H/$(echo $n | tr a-z A-Z).EXE
    [ -f $f ] && [ $f -nt $s ] && return 0
    ntsrc $s $T/$n.c && ntcc $T/$n.obj $T/$n.c $(ntdefines $d) $(macro $d C_DEFINES) -I$K/inc -I/tmp/f/ovr \
        $(bash $B/incdirs.sh $d "$(macro $d INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt -I$d || exit 1
    link -nologo -machine:arm -release -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$T/$n.exe $T/$n.obj "$@" $U/libc.lib $U/advapi32.lib $U/kernel32.lib > $T/link.err 2>&1 \
      || { grep -E "error|unresolved" $T/link.err | head -20; exit 1; }
    cp $T/$n.exe $f
    echo "   obj/host/$(basename $f)"
}
tool prodfilt
tool xdosnet $U/setupapi.lib

echo ">> infs"
bash $B/nttarget.sh $D pre

echo ">> bom tools"
# The tools of the INFs of NT 3.x (bom: SIZE, LAYOUT, INFS with GENERAL; LOCAL; STRIPINF) and
# INFSIZE (the release's file sizes of layout.inf: make-setup-image.sh), compiled as NT compiles them (the port's version of a source, if any) and linked with libc
# as the build tools of make-tools.sh (their USE_CRTDLL is for the run-time DLL of NT), into
# obj/inf/host: obj/host is an input of every step of the build (stamp.sh)
# bomcc DIR SOURCE: SOURCE of setup\DIR as NT compiles the programs of DIR
bomcc() {
    local d=$SS/$1 s=$2 p
    p=$(bash $B/portdir.sh $d)/$s; [ -f $p ] || p=$d/$s
    ntsrc $p $T/$s && ntcc $T/${s%.c}.obj $T/$s $NTDEFINES $(macro $d C_DEFINES) -I$K/inc -I/tmp/f/ovr \
        $(bash $B/incdirs.sh $d "$(macro $d INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt -I$d || exit 1
}
# bomprog NAME DIR SOURCE... -- OBJECT...: obj/inf/host/NAME.EXE of the SOURCEs of setup\DIR and
# OBJECTs, made when it is missing or older than one of their sources
bomprog() {
    local n=$(echo $1 | tr a-z A-Z) d=$2 s f new= objs=(); shift 2
    f=$IH/$n.EXE; [ -f $f ] || new=1
    while [ "$1" != -- ]; do
        for s in $SS/$d/$1 $(bash $B/portdir.sh $SS/$d)/$1; do [ ! -f $s ] || [ $s -ot $f ] || new=1; done
        objs+=($T/${1%.c}.obj); shift
    done; shift
    for s in $SS/bom/general/general.c $R/ported/windows/setup/bom/general/general.c; do
        [ $# -eq 0 ] || [ ! -f $s ] || [ $s -ot $f ] || new=1
    done
    [ -n "$new" ] || return 0
    for s in ${objs[@]}; do s=$(basename ${s%.obj}).c; bomcc $d $s; done
    [ $# -eq 0 ] || [ -f $T/general.obj ] || bomcc bom/general general.c
    link -nologo -machine:arm -release -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$T/$n.EXE ${objs[@]} "$@" $U/libc.lib $U/advapi32.lib $U/kernel32.lib > $T/link.err 2>&1 \
      || { grep -E "error|unresolved" $T/link.err | head -20; exit 1; }
    cp $T/$n.EXE $f
    echo "   obj/inf/host/$n.EXE"
}
for t in size layout infs; do bomprog $t bom/$t $t.c -- $T/general.obj; done
bomprog local localinf local.c --
bomprog stripinf stripinf stripinf.c --
bomprog infsize bom/infsize infsize.c --
mkdir -p /tmp/ir/RASPI/obj && ln -sfn $IH /tmp/ir/RASPI/obj/host
run() { NTROOT=/tmp/ir bash $R/tools/host.sh "$@"; }

echo ">> legacy infs"
# The INF tree: the logic INFs (beta, beta\<cpu>), the file list parts that are not made
# (filelist\ntcd\<cpu>; ARM's are MIPS's), the makefiles; the port's versions over them
rm -rf $L; mkdir -p $L/bom $L/inf
cp -r $SS/inf/beta $SS/inf/filelist $SS/inf/*.mk $L/inf/
mkdir -p $L/inf/filelist/ntcd/arm $L/inf/beta/arm $NL
cp $L/inf/filelist/ntcd/mips/* $L/inf/filelist/ntcd/arm/
(cd $R/ported/windows/setup/inf && find . -path ./win4 -prune -o -type f ! -name "*.ntdiff" -print) | while read -r f; do
    cp $R/ported/windows/setup/inf/$f $L/inf/$f
done
# make_log: the sizes and the filters of the bill of materials (LANGUAGE usa), its layout for
# the CD of the workstation and the file lists of its INFs
cd $L/bom
f=$R/ported/windows/setup/bom/bom.txt; [ -f $f ] || f=$SS/bom/bom.txt
cp $f cdbom.txt
for s in . "#S+usa" "#F-*" "#P-FIN" "#L+usa"; do run size cdlog.log cdbom.txt "$s" . z > /dev/null; done
run layout cdlog.log cdbom.txt lay.txt ntcd 500000000 > /dev/null
run infs cdlog.log lay.txt $L/inf/filelist/ntcd > /dev/null
grep -a -i "^ERROR" cdlog.log | sort -u | head -20 || true
# localize: the English text of the INFs
cd $L/inf/beta
for t in ansi net dev con; do run local usa_$t.txt $L/inf/filelist/ntcd > /dev/null; done
# the server and workstation service INFs, through the C preprocessor
for f in oemnsvrp oemnsvwk; do
    bash $R/tools/host.sh rcpp -P -f $f.inp -g $f.inf 2>&1 | grep -v "RC4093\|RC2142" || true
    [ -s $f.inf ] || { echo "   rcpp $f.inf failed"; exit 1; }
done
# make_fil: the rules of a makefile of the INFs: each target, its copy of the parts (copy
# without /A or /B joins them as text: each up to its Ctrl-Z; a missing part is left out) and
# the strip of the result
make_fil() {
    local mk=$1 dir=$2 t c p parts o k v
    tr -d "\r" < $mk | awk '/^(\.\.\\)?\$\(NEWINF\)\$\(LOCATION\)\\[^ \t:]+[ \t]*:/ { t = $0; sub(/[ \t]*:.*/, "", t); print "T\t" t; next }
         /^[ \t]+[^ \t]/ && t != "" { c = $0; sub(/^[ \t]+/, "", c); print "C\t" c; next }
         { t = "" }' |
    sed -e 's/\$(NEWINF)/..\\newinf\\daytona/g; s/\$(FILELIST)/..\\filelist/g; s/\$(LOCATION)/\\ntcd\\arm/g; s/\$(MEDIAINP)/usamedia.inp/g' |
    while IFS=$'\t' read -r k v; do
        v=$(echo "$v" | tr "\\\\A-Z" "/a-z")
        case $k in T) t=$v; continue ;; esac
        c=${v//\$@/$t}; c=${c//\$(@f)/$(basename $t)}
        case $c in
        copy*)
            set -- $c; parts=$2 o=$3
            : > $dir/$o
            for p in ${parts//+/ }; do
                if [ -f $dir/$p ]; then perl -0777 -pe 's/\x1a.*//s' $dir/$p >> $dir/$o; else echo "   $(basename $o): no $p"; fi
            done ;;
        cp\ *)
            set -- $c; cp $dir/$2 $dir/$3/ ;;
        *strip.cmd*)
            set -- $c; (cd $dir && run stripinf $2 tempinf > /dev/null && mv tempinf $2) ;;
        esac
    done
}
make_fil $L/inf/inf.mk $L/inf/beta > $T/make_fil.log
make_fil $L/inf/arminf.mk $L/inf/beta/arm >> $T/make_fil.log
F=$L/inf/filelist/ntcd/arm
cat $F/usamedia.inp $F/mmdriver.inf > $NL/mmdriver.inf
cp $L/inf/beta/ipinfo.inf $L/inf/beta/ipinfor.inf $NL/
echo "   $(ls $NL | wc -l) INFs in newinf\\daytona\\ntcd\\arm"

echo ">> modem infs"
# the INFs of the modems (unimodem\inf: its NTTARGETFILE0 make_infs copies each .inx)
MI=/tmp/f/gen/unimodem/inf
bash $B/nttarget.sh $W/private/unimodem/inf pre
echo "   $(ls $MI | grep -c "\.inf$") modem INFs"

echo ">> install"
# The files of layout.inf ([SourceDisksFiles] and [SourceDisksFiles.arm]: name = disk,subdir,
# size,checksum,,,bootmedia,targetdirectory,upgrade,textmode,targetname) that are made here,
# in their target directory: 20 the INF directory, 2 system32
N=$R/sdcard/root/WINNT n=0
mkdir -p $N/INF
while IFS=, read -r f t m; do
    i=$G/$f; [ -f $i ] || i=$NL/$f; [ -f $i ] || i=$MI/$f; [ -f $i ] || continue
    case $t in 20) o=$N/INF ;; 2) o=$N/System32 ;; *) continue ;; esac
    m=$(echo "${m:-$f}" | tr a-z A-Z)
    cmp -s $i $o/$m || cp $i $o/$m
    n=$((n+1))
done < <(tr -d "\r\032" < $G/layout.inf | awk '
    /^\[/ { s = tolower($0) == "[sourcedisksfiles]" || tolower($0) == "[sourcedisksfiles.arm]"; next }
    s && /^[^;].*=/ { k = $0; sub(/[ \t]*=.*/, "", k); v = $0; sub(/^[^=]*=[ \t]*/, "", v); split(v, a, ",")
                      if (!seen[tolower(k)]++) print tolower(k) "," a[8] "," tolower(a[11]) }')
echo "   $n files in sdcard/root/WINNT/INF and System32"
