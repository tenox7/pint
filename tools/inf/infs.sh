#!/bin/bash
# The container half of make-infs.sh: the BOM tools, LOCAL and STRIPINF as NT compiles
# them, then the INFs of \ntcd\arm as INF\MAKEFILE.INC makes them (make_log, localize,
# make_fil) and their install into System32.
set -eu
. /work/RASPI/arcfw/kernel/msvc.sh
W=/work R=$W/RASPI S=$W/PRIVATE/WINDOWS/SETUP P=$R/arcfw/ported/windows/setup
O=$R/obj/inf U=$R/obj/um B=/tmp/ib I=/tmp/ib/inf
rm -rf $B; mkdir -p $B/obj $B/bom $O/host
bash $K/mkfarm.sh /tmp/f > /dev/null

# setup PATH: a file of WINDOWS\SETUP (PATH in lower case): the port's version, else the tree's
setup() {
    local p=$S c n
    [ -f $P/$1 ] && { echo $P/$1; return; }
    for c in $(echo $1 | tr / " "); do n=$(ls $p | grep -ix "$c") || return 1; p=$p/$n; done
    echo $p
}

echo ">> tools"
macro() { bash $K/srcmacro.sh $1/SOURCES $2 | sed "s/\$([A-Za-z_0-9]*)//g"; }
BASE=$(tr -d "\r" < $W/PUBLIC/SDK/LIB/COFFBASE.TXT | awk "tolower(\$1) == \"usermode\" {print \$2; exit}")
# cc DIR SOURCE: compile as NT compiles the console programs of component DIR
cc() {
    local b=$(basename "${2%.*}" | tr A-Z a-z)
    ntsrc $2 $B/obj/$b.c && ntcc $B/obj/$b.obj $B/obj/$b.c $NTDEFINES $(macro $1 C_DEFINES) -I$K/inc -I/tmp/f/ovr \
        $(bash $K/incdirs.sh $1 "$(macro $1 INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt
}
# prog NAME OBJECT...: obj/inf/host/NAME.EXE with NT C run-time, replaced only when it changed
prog() {
    local n=$(echo $1 | tr a-z A-Z); shift
    link -nologo -machine:arm -release -brepro -force:multiple -subsystem:console -entry:mainCRTStartup -base:$BASE -nodefaultlib \
         -out:$B/$n.EXE "$@" $U/libc.lib $U/advapi32.lib $U/kernel32.lib > $B/link.err 2>&1 \
      || { grep -E "error|unresolved" $B/link.err | head -20; exit 1; }
    cmp -s $B/$n.EXE $O/host/$n.EXE || cp $B/$n.EXE $O/host/$n.EXE
}
cc $S/BOM/GENERAL $(setup bom/general/general.c)
for t in size layout infs dosnet acllist; do
    cc $S/BOM/$(echo $t | tr a-z A-Z) $(setup bom/$t/$t.c)
    prog $t $B/obj/$t.obj $B/obj/general.obj
done
cc $S/LOCALINF $(setup localinf/local.c); prog local $B/obj/local.obj
cc $S/STRIPINF $(setup stripinf/stripinf.c); prog stripinf $B/obj/stripinf.obj
mkdir -p /tmp/ir/RASPI/obj && ln -sfn $O/host /tmp/ir/RASPI/obj/host
run() { NTROOT=/tmp/ir bash $R/tools/host.sh "$@"; }
host() { bash $R/tools/host.sh "$@"; }

# The INF tree in lower case, CR and ^Z stripped: the logic INFs (beta, beta\<cpu>), the
# file list parts that are not made (filelist\ntcd\<cpu>), the makefiles; the port's
# versions over them.
lower() {
    local s=$1 d=$2 f n
    mkdir -p $d
    for f in $s/*; do
        [ -e $f ] || continue
        n=$d/$(basename $f | tr A-Z a-z)
        if [ -d $f ]; then lower $f $n; else tr -d "\r\032" < $f > $n; fi
    done
}
lower $S/INF $I
(cd $P/inf && find . -type f ! -name "*.ntdiff") | while read -r f; do mkdir -p $(dirname $I/$f); cp $P/inf/$f $I/$f; done
mkdir -p $I/filelist/ntcd/{i386,mips,alpha,arm} $I/newinf/ntcd/arm

echo ">> make_log"
cd $B/bom
tr -d "\r" < $(setup bom/bom.txt) > cdbom.txt
for s in . "#S+ENG" "#F-*" "#P-FIN" "#L+ENG"; do run size cdlog.log cdbom.txt "$s" . z > /dev/null; done
run layout cdlog.log cdbom.txt lay.txt ntcd 500000000 > /dev/null
run infs cdlog.log lay.txt $I/filelist/ntcd > /dev/null
run acllist cdlog.log lay.txt $I/filelist/ntcd/arm/winperms.txt ntcd arm > /dev/null
grep -i "^ERROR" cdlog.log | sort -u | head -20 || true

echo ">> localize"
# (LOCAL returns the TRUE of process(), 1, when it succeeds)
for t in ansi net dev oem con; do run local $I/beta/eng_$t.txt $I/filelist/ntcd > /dev/null || true; done

echo ">> make_fil"
cd $I/beta
for f in ntidwnet:-DIDW_INF=1 ntlminst:-DNWK_INF=1 ntlanman:; do
    # (RCPP counts the ".." of a comment as an error, and writes the INF)
    host rcpp -P ${f#*:} -f ntlanman.inp -g ${f%%:*}.inf 2>&1 | grep -v "RC4093\|RC2142" || true
    [ -s ${f%%:*}.inf ] || { echo "   rcpp ${f%%:*}.inf failed"; exit 1; }
done
# The rules of a makefile of the INFs: each target, its copy of the parts (a missing part
# is left out, as copy does) and the strip of the result.
make_fil() {
    local mk=$1 dir=$2 t c p parts o
    awk '/^(\.\.\\)?\$\(NEWINF\)\$\(LOCATION\)\\[^ \t:]+[ \t]*:/ { t = $0; sub(/[ \t]*:.*/, "", t); print "T\t" t; next }
         /^[ \t]+[^ \t]/ && t != "" { c = $0; sub(/^[ \t]+/, "", c); print "C\t" c; next }
         { t = "" }' $mk |
    sed -e 's/\$(NEWINF)/..\\newinf/g; s/\$(FILELIST)/..\\filelist/g; s/\$(LOCATION)/\\ntcd\\arm/g' \
        -e 's/\$(MEDIAINP)/ENGmedia.inp/g; s/\$(TXTDATINP)/ENGtxdat.inp/g; s/\$(TXTMEDINP)/ENGtxmed.inp/g' |
    while IFS=$'\t' read -r k v; do
        v=$(echo "$v" | tr "\\\\A-Z" "/a-z")
        case $k in
        T) t=$v; continue ;;
        esac
        c=${v//\$@/$t}; c=${c//\$(@f)/$(basename $t)}
        case $c in
        copy*)
            set -- $c; parts=$2 o=$3
            : > $dir/$o
            for p in ${parts//+/ }; do
                if [ -f $dir/$p ]; then cat $dir/$p >> $dir/$o; else echo "   $(basename $o): no $p"; fi
            done ;;
        cp\ *)
            set -- $c; cp $dir/$2 $dir/$3/ ;;
        *strip.cmd*)
            (cd $dir && run stripinf $o tempinf > /dev/null && mv tempinf $o) ;;
        esac
    done
}
make_fil $I/inf.mk $I/beta
make_fil $I/arminf.mk $I/beta/arm
F=$I/filelist/ntcd/arm N=$I/newinf/ntcd/arm
cat $I/beta/arm/ntperms.txt $F/winperms.txt > $N/winperms.txt
cat $F/engmedia.inp $F/mmdriver.inf > $N/mmdriver.inf
cp $I/beta/ipinfo.inf $I/beta/ipinfor.inf $N/

echo ">> install"
# The INFs binplace puts in system32 in a developer build (PLACEFIL.TXT class system), and those
# the bill of materials puts there in a retail setup (the ARM adapter INF is one of these)
D=$R/sdcard/root/WINNT/System32 n=0
for f in $( { tr -d "\r" < $W/PUBLIC/SDK/LIB/PLACEFIL.TXT | awk 'tolower($2) == "system" && tolower($1) ~ /\.inf$/ {print tolower($1)}'
              awk -F"\t" 'tolower($2) == "infs" && tolower($8) == "arm" && tolower($19) == ".\\system32" {print tolower($1)}' $B/bom/lay.txt; } | sort -u); do
    [ -f $N/$f ] || continue
    sed "s/\r*\$/\r/" $N/$f > $B/out && { cmp -s $B/out $D/${f^^} || cp $B/out $D/${f^^}; } && n=$((n+1))
done
# the INF of the developer first run that stores the network bindings (registry.ini)
sed "s/\r*\$/\r/" $R/apps/net/ncpabind/ncpabind.inf > $B/out && { cmp -s $B/out $D/NCPABIND.INF || cp $B/out $D/NCPABIND.INF; } && n=$((n+1))
echo "   $n INFs in sdcard/root/WINNT/System32"
