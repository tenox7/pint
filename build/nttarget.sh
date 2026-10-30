#!/bin/bash
# Make the NTTARGETFILE0/1 and NTTARGETFILES of a component (its SOURCES) by the
# recipes of its MAKEFILE.INC (nmk.awk), as makefile.def does: NTTARGETFILE0 and
# the sources among NTTARGETFILES before the component compiles (pre), the rest,
# such as a library made of its library, after it (post); the dependents of a
# target that have recipes first. Files land in /tmp/f/gen/<directory>, where the
# builds find generated files; a library obj\<machine>\<name>.lib is <out>/<name>.lib.
# With src, the files named, as nmake made the sources of <machine>_SOURCES that have recipes
# (the ARM sources of the port's list that the component makes: arm\systable.asm of W32\KMODE).
# An image's build makes the post ones in two steps, as makefile.def: NTTARGETFILE1 (file1)
# before its link, the rest of NTTARGETFILES (files) after it.
# Usage: nttarget.sh componentdir pre|post|file1|files [out]
#        nttarget.sh componentdir src out file...
B=$(cd "$(dirname "$0")" && pwd)
D=$1 PHASE=$2 OUT=${3:-/work/RASPI/obj/um}
resolve() {
    local p=${2:-$D} c n f=$1
    case $f in BASEDIR/*) p=/work; f=${f#BASEDIR/} ;; esac
    for c in $(echo "$f" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
gen() { echo /tmp/f/gen/${1#/work/private/}; }
# A file of a recipe: the port's version, in the tree, or made by the build (generated files and the
# outputs of the mc pass); in a directory the port alone has (GRE\ARM), the port's.
locate() {
    local f r n g
    f=$(echo "$1" | tr '\\' /)
    # (a file of obj\<machine> of the component, which a recipe made)
    case $f in [Oo][Bb][Jj]/*/*) [ -f "$(out "$f")" ] && { out "$f"; return; } ;; esac
    r=$(resolve "$(dirname "$f")") || r=$(resolve "$(dirname "$(dirname "$f")")")/$(basename "$(dirname "$f")" | tr A-Z a-z) || return 1
    for g in $(bash $B/portdir.sh $r) $r $(gen $r) /tmp/f/mc/$(echo ${r#/work/private/} | tr A-Z a-z); do
        n=$(ls "$g" 2>/dev/null | grep -ix "$(basename "$f")" | head -1)
        [ -z "$n" ] || { echo "$g/$n"; return; }
    done
    return 1
}
# copy a dest: dest a directory (as . or ..) or a file; one under obj\ is a file of the component;
# a library of the public libraries to one of them is <out>/<name>.lib (snmp\agent\common: its
# import library snmpapi.lib as snmp.lib)
nmk_copy() {
    local s d f
    case $(echo "$1 $2" | tr A-Z a-z) in basedir/public/sdk/lib/*.lib\ basedir/public/sdk/lib/*.lib)
        cp "$(outlib "$1")" "$(outlib "$2")"; return ;;
    esac
    s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    if d=$(resolve "${2:-.}") && [ -d "$d" ]; then f=$(basename "$s")
    else case $2 in [Oo][Bb][Jj]/*) d=$D ;; *) d=$(resolve "$(dirname "$2")") || return 1 ;; esac; f=$(basename "$2"); fi
    mkdir -p "$(gen $d)" && cp "$s" "$(gen $d)/$(echo $f | tr A-Z a-z)"
}
outlib() { echo $OUT/$(basename "$1" .lib | tr A-Z a-z).lib; }
# a library a recipe names (..\buffer\obj\arm\buffer.lib, ..\lib\arm\msvfw32.lib of avi\mciavi32):
# the one made for that directory (libpath.sh), else that of its name
inlib() { local l; l=$(bash $B/libpath.sh $D "$(echo "$1" | sed -E 's#(^|/)arm/([^/]+)$#\1*/\2#')"); echo ${l:-$(outlib "$1")}; }
# a library of libraries and objects, given to lib in reverse order as ntlib (msvc.sh) does: the
# library of the directory it names (obj\arm\uimisc.lib: the component's, ..\lib\arm\x.lib;
# libpath.sh -d), one of the public libraries <out>/<name>.lib, one of kernel mode both; an object
# is the one the build of its directory made (obj\arm\shguid.obj of shell32); an import library of a
# .def the recipe made or the tree has (-def:user32p.def: user32p.lib, the private exports too). With
# @-, the arguments of an inline response file (lib @<<) are its input.
nmk_lib() {
    local a o p= d l= x f= r=
    for a; do [ "$a" != @- ] || r=$(cat); done
    for a in "$@" $r; do
        case $a in
        [-/][Oo][Uu][Tt]:*) p=${a#*:} ;;
        [-/][Dd][Ee][Ff]:*) f=$(out "${a#*:}"); [ -f "$f" ] || f=$(locate "${a#*:}") || { echo "nttarget: no ${a#*:}" >&2; return 1; } ;;
        *.lib) l="$(inlib "$a") $l" ;;
        *.obj) x=$(bash $B/libpath.sh $D "$(echo "$a" | sed -E 's#(^|/)obj/arm/#\1obj/*/#')")
               [ -n "$x" ] || { echo "nttarget: no $a" >&2; return 1; }; l="$x $l" ;;
        esac
    done
    d=$(dirname "$p"); case $d in [Aa][Rr][Mm]|*/[Aa][Rr][Mm]) d=$(dirname "$d") ;; esac
    d=$(bash $B/libpath.sh -d $D "$(echo "$d" | sed 's/^BASEDIR/$(BASEDIR)/')")
    case $d in /work/public/*) o=$(outlib "$p") ;; *) o=$OUT/dir/${d#/work/}/$(basename "$p" | tr A-Z a-z) ;; esac
    mkdir -p $(dirname $o) && rm -f $o
    MSVC_ENV=0 WINEDEBUG=-all lib -nologo ${f:+-machine:arm -def:$f} -out:$o $l > /tmp/nttarget.err 2>&1 || { cat /tmp/nttarget.err >&2; return 1; }
    [ $OUT != /work/RASPI/obj ] || [ $o = $(outlib "$p") ] || cp $o $OUT/
}
# link32 -dump -symbols: the symbol tables of libraries, by the linker of Microsoft C
nmk_link32() {
    local a l=
    for a; do case $a in *.lib) l="$l $(inlib "$a")" ;; esac; done
    MSVC_ENV=0 WINEDEBUG=-all link -dump -symbols $l
}
# mapmsg, the message file of the message definitions of a header (netmsg.mc of
# NET\MESSAGES): mapmsg [-p] [-a file] system base header [> file | >> file] (rasmsg.mc of
# RAS, of two headers)
nmk_mapmsg() {
    local o= a= args=() n
    while [ $# -gt 0 ]; do
        case $1 in
        -a) args+=(-a "$(out "$2")"); shift 2 ;;
        ">") o=$(out "$2"); shift 2 ;;
        ">>") o=$(out "$2"); a=1; shift 2 ;;
        *) args+=("$1"); shift ;;
        esac
    done
    n=${#args[@]}
    args[$((n-1))]=$(locate "${args[$((n-1))]}") || { echo "nttarget: no ${args[$((n-1))]}" >&2; return 1; }
    if [ -n "$a" ]; then bash /work/RASPI/tools/host.sh mapmsg "${args[@]}" >> "$o" 2> /tmp/mapmsg.log
    elif [ -n "$o" ]; then bash /work/RASPI/tools/host.sh mapmsg "${args[@]}" > "$o" 2> /tmp/mapmsg.log
    else bash /work/RASPI/tools/host.sh mapmsg "${args[@]}" 2> /tmp/mapmsg.log; fi < /dev/null || { cat /tmp/mapmsg.log >&2; return 1; }
}
# listmung list template > file: the template written out for the names of the list
# (the callback and server call tables of ntuser\inc, client and kernel)
nmk_listmung() {
    local l t o
    l=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    t=$(locate "$2") || { echo "nttarget: no $2" >&2; return 1; }
    [ "${3:-}" = ">" ] && o=$(out "$4") || { echo "nttarget: listmung $* (no output file)" >&2; return 1; }
    bash /work/RASPI/tools/host.sh listmung "$l" "$t" > $o.tmp 2> /tmp/listmung.log || { cat /tmp/listmung.log >&2; return 1; }
    tr -d '\r' < $o.tmp > $o && rm -f $o.tmp
}
# the C preprocessor ($(TARGET_CPP) /EP, $(C_PREPROCESSOR)): the export list of a library
# from its .src, a list preprocessed (ntuser's cb.lst), a file /Tc names as C (the version
# resources of HyperTerminal); the include directories of the SDK are those of the build, the
# directory of the file first, where cl looks before them
nmk_cpp() {
    local o= args=() s= t= i
    while [ $# -gt 0 ]; do
        case $1 in
        ">") o=$(out "$2"); shift 2 ;;
        [-/]Tc*) s=$(locate "${1:3}") || { echo "nttarget: no ${1:3}" >&2; return 1; }; t=-Tc; shift ;;
        [-/]I*)
            case ${1:2} in
            BASEDIR/public/sdk/inc) args+=(-I/work/RASPI/kernel/inc -I/tmp/f/pub) ;;
            BASEDIR/public/sdk/inc/crt) args+=(-I/tmp/f/crt) ;;
            *) i=$(resolve "${1:2}") || { echo "nttarget: no ${1:2}" >&2; return 1; }; args+=(-I$i) ;;
            esac; shift ;;
        [-/]*) args+=("$1"); shift ;;
        *) s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; shift ;;
        esac
    done
    tr -d '\032\r' < "$s" > /tmp/nmkcpp.src
    cl -nologo -I"$(dirname "$s")" "${args[@]}" -D_ARM_ $t/tmp/nmkcpp.src > "$o" 2> /tmp/nmkcpp.log || { cat /tmp/nmkcpp.log >&2; return 1; }
}
# riscdef [-x string]... [-r from to]... def > file: the module definition file of a RISC machine
# from that of the Intel machine (tools/riscdef.pl: the MFC 4.2 DLLs, whose RISC .def files the
# tree has not)
nmk_riscdef() {
    local o= s= x=()
    while [ $# -gt 0 ]; do
        case $1 in
        ">") o=$(out "$2"); shift 2 ;;
        -x) x+=(-x "$2"); shift 2 ;;
        -r) x+=(-r "$2" "$3"); shift 3 ;;
        *) s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; shift ;;
        esac
    done
    perl /work/RASPI/tools/riscdef.pl "$s" "${x[@]}" > "$o" 2> /tmp/riscdef.log || { cat /tmp/riscdef.log >&2; return 1; }
}
# uixport, the export list of a NET UI DLL from the symbols of its libraries
nmk_uixport() {
    local a h o x= args=()
    for a; do
        case $a in
        -h:*) h=$(locate "${a#-h:}") || { echo "nttarget: no ${a#-h:}" >&2; return 1; }; args+=("-h:$h") ;;
        -o:*) args+=("-o:$(out "${a#-o:}")") ;;
        # (an exclusion list that is not there is no error of uixport: netui1 of dll3\dll1\daytona,
        # whose ..\excludes.lst is not there, exports all)
        -x:*) x=$(locate "${a#-x:}") && args+=("-x:$x") || echo "nttarget: no ${a#-x:}, uixport excludes nothing" >&2 ;;
        *) args+=("$a") ;;
        esac
    done
    bash /work/RASPI/tools/host.sh uixport "${args[@]}" > /tmp/uixport.log 2>&1 || { cat /tmp/uixport.log >&2; return 1; }
}
# guidlib /Out:lib /CPP_OPT:"options" file: GUIDLIB, a library of an object for each GUID of a
# file of the component or one made for it, by the compiler and librarian it runs (obj/host/CL.EXE
# and LINK.EXE, tools/msvcexe), with the include directories of the build for those of the SDK;
# the library is that of the component's directory (libpath.sh)
nmk_guidlib() {
    local a o= c= f= i w=/tmp/nmkguid
    dos() { printf 'Z:%s' "$1" | tr / '\\'; }
    for a; do
        case $a in
        /[Oo][Uu][Tt]:*) o=$OUT/dir/${D#/work/}/$(basename "${a#*:}" | tr A-Z a-z) ;;
        /[Cc][Pp][Pp]_[Oo][Pp][Tt]:*)
            for i in ${a#*:}; do
                case $i in
                /IBASEDIR/public/sdk/inc) i="/I$(dos /work/RASPI/kernel/inc) /I$(dos /tmp/f/pub)" ;;
                /IBASEDIR/public/sdk/inc/crt) i=/I$(dos /tmp/f/crt) ;;
                esac
                c="$c $i"
            done ;;
        *) f=$(out "$a"); [ -f "$f" ] || f=$(locate "$a") || { echo "nttarget: no $a" >&2; return 1; } ;;
        esac
    done
    mkdir -p $(dirname $o) && rm -rf $w && mkdir -p $w || return 1
    (cd $w && bash /work/RASPI/tools/host.sh guidlib "/Out:$(dos $o)" "/CPP_OPT:$c" "$f") > /tmp/guidlib.log 2>&1 || { cat /tmp/guidlib.log >&2; return 1; }
}
# gensrv: the system service dispatch table and stubs of a services.tab (win32k.sys: its
# table, and the user mode stubs of w32umode.lib), by the ARM templates table.stb and
# services.stb of the port's version of the stub directory (-s), among the generated files
# of the component (arm\systable.asm, arm\usrstubs.asm); the services.tab is the one a
# recipe of the component made, else that of the directory named. GENSRV takes its options
# in one order: -d -e -g -B -a -s.
nmk_gensrv() {
    local d=arm e=asm g=. b= s=. t= w=/tmp/nmkgensrv f p
    while [ $# -gt 0 ]; do
        case $1 in
        -d) d=$2; shift 2 ;;
        -e) e=$2; shift 2 ;;
        -g) g=$2; shift 2 ;;
        -s) s=$2; shift 2 ;;
        -B) b=-B; shift ;;
        -*) echo "nttarget: gensrv $1 not supported" >&2; return 1 ;;
        *) t=$1; shift ;;
        esac
    done
    rm -rf $w && mkdir -p $w/tab $w/stb/$d $w/$d
    f=$(out services.tab); [ -f $f ] || f=$(locate "${t:-$g}/services.tab") || { echo "nttarget: no services.tab" >&2; return 1; }
    tr -d '\032\r' < $f > $w/tab/services.tab
    p=$(bash $B/portdir.sh $(resolve "$s"))/$d
    for f in table.stb services.stb; do
        [ -f $p/$f ] || { echo "nttarget: no $p/$f" >&2; return 1; }
        cp $p/$f $w/stb/$d/
    done
    if [ -n "$t" ]; then set -- -d $d -e $e -g tab $b -s stb tab; else set -- -d $d -e $e -g tab $b -a . -s stb; fi
    (cd $w && bash /work/RASPI/tools/host.sh gensrv "$@") > /tmp/gensrv.log 2>&1 || { cat /tmp/gensrv.log >&2; return 1; }
    mkdir -p $(gen $D)/$d
    for f in $(ls $w/$d); do
        tr -d '\r' < $w/$d/$f | sed -E 's/^(STUBS_|SYSSTUBS_|USRSTUBS_|TABLE_|ARGTBL_)/        &/' > $(gen $D)/$d/$f
    done
}
# A generator of the structure offsets of a component's assembler (gen<machine>.cxx of GRE):
# its recipe has nmake make its object, links it and runs it. The build host does not run
# ARM programs: the generator defines each offset as a constant (GenArm_<name>), nmk_nmake
# compiles it as the component's sources (make-lib.sh) with its listing, nmk_link links
# nothing and nmk_run writes the constants of the listing as #defines, as the program did.
# nmake obj\<machine>\x.obj [NAME=value...]: the source x of the component's directories of
# the inference rules of makefile.def (<machine>, ..\<machine>, ., ..), with the C run-time
# of USE_CRTDLL or USE_LIBCMT. The farm of the component (incdirs.sh), which this makes
# before the files of the recipes are, is made again when the component compiles.
nmk_nmake() {
    local o=$1 b d x s=; shift
    case $o in [Oo][Bb][Jj]/*/*.[Oo][Bb][Jj]) ;; *) echo "nttarget: nmake $o not supported" >&2; return 1 ;; esac
    b=$(basename "$o" | sed "s/\.[Oo][Bb][Jj]$//")
    for d in arm ../arm . ..; do
        for x in cxx c; do s=$(locate "$d/$b.$x") && break 2; done
    done
    [ -n "$s" ] || { echo "nttarget: no source of $o" >&2; return 1; }
    (
        set +f
        . $B/msvc.sh
        m() { bash $B/srcmacro.sh $D/sources $1; }
        defs="$(ntdefines $D) $(m C_DEFINES | sed "s/\$([A-Za-z_0-9]*)//g")"
        for x; do case $x in USE_CRTDLL=1) defs="$defs -D_DLL=1 -D_MT=1" ;; USE_LIBCMT=1) defs="$defs -D_MT=1" ;; esac; done
        incs="-I$K/inc -I/tmp/f/ovr $(bash $B/incdirs.sh $D "$(m MFC_INCLUDES);$(m USER_INCLUDES);$(m INCLUDES)") -I/tmp/f/oak -I/tmp/f/pub -I/tmp/f/crt"
        rm -rf /tmp/nmkgen && mkdir -p /tmp/nmkgen && ntsrc $s /tmp/nmkgen/$b.${s##*.} &&
        ntcc /tmp/nmkgen/$b.obj /tmp/nmkgen/$b.${s##*.} $defs $incs -FA -Fa/tmp/nmkgen/$b.lst
    ) || { cat /tmp/nmkgen/$b.obj.err >&2 2>/dev/null; return 1; }
    rm -f /tmp/f/dirs$D/.farmed
}
nmk_link() {
    case $1 in -out:[Oo][Bb][Jj]/*/*.[Ee][Xx][Ee]) cat > /dev/null ;; *) echo "nttarget: link $1 not supported" >&2; return 1 ;; esac
}
nmk_run() {
    local b=$(basename "$1" | sed "s/\.[Ee][Xx][Ee]$//") o
    [ -f /tmp/nmkgen/$b.lst ] || { echo "nttarget: $1 was not made" >&2; return 1; }
    o=$(out "$2")
    sed -nE "s/^\|GenArm_([A-Za-z0-9_]+)\|[[:space:]]+DCD[[:space:]]+(0x[0-9a-fA-F]+|[0-9]+)[[:space:]]*$/#define \1 \2/p" /tmp/nmkgen/$b.lst > $o
    [ -s $o ] || { echo "nttarget: $1 defined nothing" >&2; return 1; }
}
# the files NT placed in its release (binplace), the disk image places (make-sd-image.sh); with
# NTTARGET_PLACE (make-dll.sh INSTALL=1), those placefil.txt lists as retail go there with the image,
# as the disk has them (the .acm of a codec). (-B: a subdirectory of the release; B D I O P R S Y
# take a value.)
nmk_binplace() {
    local f n s
    [ -n "${NTTARGET_PLACE:-}" ] || return 0
    while [ $# -gt 0 ]; do
        case $1 in
        [-/][Bb]) return 0 ;;
        [-/][DdIiOoPpRrSsYy]) shift 2; continue ;;
        [-/]*) shift; continue ;;
        esac
        f=$1; shift
        n=$(basename "$f" | tr A-Z a-z)
        tr -d '\r' < /work/public/sdk/lib/placefil.txt | awk -v n="$n" 'tolower($1) == n && tolower($2) == "retail" {f = 1} END {exit !f}' || continue
        s=$(out "$f"); [ -f "$s" ] || s=$(locate "$f") || { echo "nttarget: no $f" >&2; return 1; }
        cp "$s" "$NTTARGET_PLACE/$(echo "$n" | tr a-z A-Z)" && echo "$n" >> /tmp/nttarget.placed || return 1
    done
}
nmk_del() { local f; for f; do rm -f "$(out $f)"; done; }
# copy a + b c: a and b, one after the other, in c (which may be a), as cmd copies the files
# it joins, as text: each up to its end of file mark (^Z), and the mark at the end
nmk_copycat() {
    local d=$1 s f t=/tmp/nttarget.cat; shift
    : > $t
    for f; do s=$(locate "$f") || { echo "nttarget: no $f" >&2; return 1; }; perl -0777 -pe 's/\x1a.*//s' "$s" >> $t; done
    printf '\032' >> $t
    mv $t "$(out $d)"
}
# echo text > file, echo text >> file (-n, -a): the text as a line of the file, as cmd writes it
nmk_echo() {
    local o=$(out "$2")
    mkdir -p "$(dirname "$o")"
    if [ "$1" = -a ]; then printf '%s\r\n' "$3" >> "$o"; else printf '%s\r\n' "$3" > "$o"; fi
}
# prodfilt in out +tag: the lines of a product or platform of an INF (setup\inf\win4\tools)
nmk_prodfilt() {
    local s
    s=$(out "$1"); [ -f "$s" ] || s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    bash /work/RASPI/tools/host.sh prodfilt "$s" "$(out $2)" "$3" > /tmp/prodfilt.log 2>&1 || { cat /tmp/prodfilt.log >&2; return 1; }
}
# xdosnet layout.inf out platform: the [Files] of dosnet.inf, the files of the layout
nmk_xdosnet() {
    local s
    s=$(out "$1"); [ -f "$s" ] || s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    bash /work/RASPI/tools/host.sh xdosnet "$s" "$(out $2)" "$3" > /tmp/xdosnet.log 2>&1 || { cat /tmp/xdosnet.log >&2; return 1; }
}
nmk_midl() { :; }    # gen-midl.sh runs the midl recipes
# mcinc.sh runs the mc recipes of an input of the tree; this one, of an input a recipe
# made (a message file copied from another component), puts the header, resource script
# and message tables among the generated files, where NT's build left them in its tree
nmk_mc() {
    local o= h=. r=. f= s hd rd x
    while [ $# -gt 0 ]; do
        case $1 in
        -h) h=$2; shift 2 ;;
        -r) r=$2; shift 2 ;;
        -m|-e) o="$o $1 $2"; shift 2 ;;
        -*) o="$o $1"; shift ;;
        *) f=$1; shift ;;
        esac
    done
    s=$(locate "$f") || { echo "nttarget: no $f" >&2; return 1; }
    hd=$(gen $(resolve "$h")) && rd=$(gen $(resolve "$r")) || return 1
    mkdir -p $hd $rd
    bash /work/RASPI/tools/host.sh mc $o -h $hd -r $rd $s > /tmp/mc.log 2>&1 || { cat /tmp/mc.log >&2; return 1; }
    for x in $(find $hd $rd -maxdepth 1 -type f -name "*[A-Z]*" | sort -u); do mv $x $(dirname $x)/$(basename $x | tr A-Z a-z); done
}
# a file a recipe makes, among the generated files of the component, or of the directory it names
# outside it (..\tmp\kbdus\sources of ntuser\kbd\us_kbd)
out() {
    local d
    case $1 in ../*) d=$(gendir "$(dirname "$1")") && { echo "$d/$(basename "$1" | tr A-Z a-z)"; return; } ;; esac
    echo "$(gen $D)/$(basename "$1" | tr A-Z a-z)"
}
# the generated files of a directory a recipe names; of one the tree has not, in those of its parent
gendir() {
    local d
    d=$(resolve "$1") || { d=$(resolve "$(dirname "$1")") || return 1; d=$d/$(basename "$1" | tr A-Z a-z); }
    gen "$d"
}
# md: a directory of generated files
nmk_md() { local f d; for f; do d=$(gendir "$f") && mkdir -p "$d" || return 1; done; }
# delnode: generated files and directories removed (a name may end in *)
nmk_delnode() {
    local f d
    for f; do
        d=$(gendir "$(dirname "$f")") || continue
        (set +f; rm -rf -- "${d:?}"/$(basename "$f"))
    done
}
# kbdtool layout.txt: the C source, header, resource script and export list of a keyboard
# layout (kbd<name>.c .h .rc .def), among the generated files of the component
nmk_kbdtool() {
    local f w=/tmp/nmkkbd x
    f=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }
    rm -rf $w && mkdir -p $w "$(gen $D)" || return 1
    (cd $w && bash /work/RASPI/tools/host.sh kbdtool "$f") > /tmp/kbdtool.log 2>&1 || { cat /tmp/kbdtool.log >&2; return 1; }
    [ -n "$(ls $w)" ] || { cat /tmp/kbdtool.log >&2; return 1; }
    for x in $(ls $w); do tr -d '\r' < $w/$x > "$(gen $D)/$(echo $x | tr A-Z a-z)"; done
}
# mibcc -o<file> MIB...: the compiled MIBs (snmp\mibs: mib.bin, which mgmtapi reads), by MIBCC of the
# tools pass (tools/make-tools.sh) in a directory of copies of the MIBs
nmk_mibcc() {
    local f s o=mib.bin a=() w=/tmp/nmkmib
    rm -rf $w && mkdir -p $w || return 1
    for f; do
        case $f in
        [-/][Oo]*) o=$(echo "${f#??}" | tr '\\' /) ;;
        [-/]*) a+=("$f") ;;
        *) s=$(locate "$f") || { echo "nttarget: no $f" >&2; return 1; }
           tr -d '\032' < "$s" > $w/$(basename "$s"); a+=("$(basename "$s")") ;;
        esac
    done
    (cd $w && bash /work/RASPI/tools/host.sh mibcc -omib.bin "${a[@]}") > /tmp/mibcc.log 2>&1 && [ -s $w/mib.bin ] ||
      { cat /tmp/mibcc.log >&2; return 1; }
    cp $w/mib.bin "$(out "$o")"
}
# IF EXIST f: a file of the tree or one the build made (an image of the component is
# not made yet: winchat copies its winchat.exe to bin\arm, as binplace)
nmk_exists() { locate "$1" > /dev/null || [ -e "$(out "$1")" ]; }
# an inline file of a recipe (type << > f, type << >> f)
nmk_new() { local o=$(out $1); mkdir -p "$(dirname "$o")" && cat > "$o"; }
nmk_add() { local o=$(out $1); mkdir -p "$(dirname "$o")" && cat >> "$o"; }
nmk_cat() { local s; s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; cat "$s" >> "$(out $2)"; }
nmk_wcshdr() { local s; s=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; bash /work/RASPI/tools/host.sh wcshdr < "$s" > "$(out $2)"; }
# the build tools of the recipes (tools/host.sh), on files of the tree or generated ones
nmk_solidpp() {
    local i f a=()
    i=$(locate "$1") || { echo "nttarget: no $1" >&2; return 1; }; shift
    for f; do a+=("$(out $f)"); done
    bash /work/RASPI/tools/host.sh solidpp "$i" "${a[@]}" > /dev/null
}
nmk_rcpp() {
    local f a=(-I$D -I$(gen $D))
    while [ $# -gt 0 ]; do
        case $1 in
        -f) f=$(locate "$2") || { echo "nttarget: no $2" >&2; return 1; }; a+=(-f "$f"); shift 2 ;;
        -g) a+=(-g "$(out $2)"); shift 2 ;;
        *) a+=("$1"); shift ;;
        esac
    done
    bash /work/RASPI/tools/host.sh rcpp "${a[@]}" > /dev/null
}
# (that of a component the build made: the keyboard layouts of ntuser\kbd\tmp; that of a
# component of the port alone: the MFC 4.2 DLLs)
m=$(resolve makefile.inc) || { m=$(bash $B/portdir.sh $D)/makefile.inc; [ -f $m ] || m=$(gen $D)/makefile.inc; [ -f $m ] || exit 0; }
# the makefile.inc with the files it includes (the port's versions of them first); an include
# from the component's directory, where nmake ran, else from that of the file that includes it
# (dll3\dll1\makefile.inc of NET UI, included by its daytona, includes ..\..\makefile.inc: the
# dll3\makefile.inc of the export list, not the src\makefile.inc of the combined libraries)
ported() { local p=$(bash $B/portdir.sh $1); [ -f $p ] && echo $p || echo $1; }
flatten() {
    local line f i
    while IFS= read -r line || [ -n "$line" ]; do
        if [[ $line =~ ^[[:space:]]*![[:space:]]*[Ii][Nn][Cc][Ll][Uu][Dd][Ee][[:space:]]+([^[:space:]]+) ]]; then
            i=$(echo "${BASH_REMATCH[1]}" | tr -d '"<>')
            f=$(resolve "$i") || f=$(resolve "$i" "$(dirname $1)") || { echo "nttarget: no include $i of $1" >&2; continue; }
            flatten $f
        else
            printf '%s\n' "$line"
        fi
    done < <(tr -d '\032\r' < "$(ported $1)")
}
mk=/tmp/nttarget.mk
flatten $m > $mk
mkdir -p $(gen $D)
set -f
targets() { bash $B/srcmacro.sh $D/sources $1 | tr " " "\n" | sed 's/\*/$(TARGET_DIRECTORY)/g'; }
# the macros of the component (its makefile and SOURCES) and of the build of ARM
macros="$(bash $B/srcmacro.sh $D/sources '*')
TARGET_DIRECTORY=arm
O=obj\\\$(TARGET_DIRECTORY)
_NTROOT=\$(BASEDIR)
_NTBINDIR=\$(BASEDIR)
SDK_INC_PATH=\$(BASEDIR)\public\sdk\inc
CRT_INC_PATH=\$(BASEDIR)\public\sdk\inc\crt
TARGET_CPP=cl
TARGET_BRACES=-B
ASM_SUFFIX=asm
ASM_INCLUDE_SUFFIX=h
C_PREPROCESSOR=cl -EP \$(C_DEFINES)
386=0
MIPS=0
ALPHA=0
PPC=0
ARM=1"
for m in NTTARGETFILE0 NTTARGETFILE1 NTTARGETFILES; do
    macros="$macros
$m=$(targets $m | tr "\n" " " | sed "s/ *$//")"
done
made=" "
target() {
    local t=$1 d f=$(echo "$1" | tr '\\' /)
    case "$made" in *" $t "*) return 0 ;; esac
    made="$made$t "
    # rtl\error.h, which GENERR made in the tools pass (tools/make-tools.sh), as this recipe would
    [ "$(resolve "$(dirname "$f")")/$(basename "$f" | tr A-Z a-z)" = /work/private/ntos/rtl/error.h ] && return 0
    # up to date: in the tree, or made by the build already unless its recipe does more
    # than the mc and midl passes do (ulib: rtmsg.rc, mc's and ulib.rc in one); its dependents
    # that are not there nmake makes all the same (setup\legacy\dll: the bitmaps of res.rc,
    # copies of those of its language)
    if locate "$t" >/dev/null; then
        NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk > /tmp/nttarget.sh
        if ! grep -qv "^nmk_mc \|^nmk_midl \|^ *$" /tmp/nttarget.sh; then
            for d in $(NMK_DEPS=1 NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk); do
                locate "$d" > /dev/null || target "$d" || return 1
            done
            return 0
        fi
    fi
    for d in $(NMK_DEPS=1 NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk); do
        target "$d" || return 1
    done
    NMK_MACROS="$macros" NMK_TARGET="$t" awk -f $B/nmk.awk $mk > /tmp/nttarget.sh
    ( set -e; . /tmp/nttarget.sh ) || { echo "nttarget: $t failed" >&2; return 1; }
}
case $PHASE in
pre) T="$(targets NTTARGETFILE0) $(targets NTTARGETFILES | grep -iE "\.(c|cxx|h|hxx)$")"
     # and the sources of the SOURCES the tree has not that a rule of the makefile.inc makes, which nmake
     # made before their objects (utils\ntlib\src: largemcb.c and sdchk.cxx, copies of those of untfs that
     # its NTTARGETFILE0 does not list)
     for s in $(targets SOURCES | grep -iE "\.(c|cxx|cpp)$"); do
         locate "$s" > /dev/null && continue
         NMK_RULE=1 NMK_MACROS="$macros" NMK_TARGET="$s" awk -f $B/nmk.awk $mk > /dev/null && T="$T $s"
     done
     # of a component of pass 0 alone (PASS0ONLY), what its makefile.inc adds to all too (ntuser\kbd\us_kbd:
     # the components of the keyboard layouts)
     [ -z "$(bash $B/srcmacro.sh $D/sources PASS0ONLY)" ] || T="$T $(NMK_DEPS=1 NMK_MACROS="$macros" NMK_TARGET=all awk -f $B/nmk.awk $mk)" ;;
src) T="${*:4}" ;;
file1) T="$(targets NTTARGETFILE1)" ;;
files) T="$(targets NTTARGETFILES | grep -viE "\.(c|cxx|h|hxx)$")" ;;
*) T="$(targets NTTARGETFILE1) $(targets NTTARGETFILES | grep -viE "\.(c|cxx|h|hxx)$")" ;;
esac
for t in $T; do
    # a target its caller makes (NTTARGET_SKIP, as obj/arm/ntdll.def)
    case " ${NTTARGET_SKIP:-} " in *" $(echo "$t" | tr '\\' / | sed 's/\$(TARGET_DIRECTORY)/arm/g' | tr A-Z a-z) "*) continue ;; esac
    target "$t" || exit 1
done
bash $B/gensave.sh
