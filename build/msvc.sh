# Microsoft C for ARM (MSVC 14.44, tenox7/msvc-arm32 under Wine): the compiler,
# assembler, librarian and linker of the NT build, with the options and defines
# makefile.def gives a RISC machine. Sourced by the build scripts in the
# container; the ARM gates of the sources come from ntsrc().
export MSVC_ENV=0 WINEDEBUG=-all
wineserver -p 2>/dev/null

K=/work/RASPI/kernel B=/work/RASPI/build

# The sources of the port that are NT's with ARM changes, from their edit scripts
# over the NT tree (tools/ntdiff.pl).
perl /work/RASPI/tools/ntdiff.pl apply /work /work/RASPI

# NT_UP: as in makefile.def, 1 builds for a uniprocessor and 0 for a multiprocessor;
# every Raspberry Pi has four cores, so the default here is 0.
NT_UP=${NT_UP:-0}
NTUPDEFINE=; [ "$NT_UP" = 0 ] || NTUPDEFINE=-DNT_UP=1

# The machine and build defines of makefile.def (_ARM_ for the RISC machine), and
# the options: no default libraries, wchar_t as NT's unsigned short, no security
# checks, function-level linking, string literals writable and unpooled as the MIPS
# compiler placed them (-GF- after -O2, which pools them read-only), debug
# information in the objects, the C++ of MSVC 2.0: implicit int, no RTTI, no sized
# delete, no thread safe statics, and the warnings of level 1. A free build defines
# DEVL alone, not DBG (sources test #ifdef DBG).
NTDEFINES="-D_ARM_=1 $NTUPDEFINE -DNT_INST=0 -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DWIN32=100 -D_NT1X_=100 -DWINNT=1 -D_WIN32_WINNT=0x0400 -DDEVL=1 -DFPO=1"
# ntdefines D: those of the SOURCES of component D: with CAIRO_PRODUCT (USER's run-time
# library), the WIN32_DEFINE of makefile.def for Cairo, Unicode unless NOT_UNICODE; no
# WIN32_LEAN_AND_MEAN with NOT_LEAN_AND_MEAN (the shell); and
# the LIBC_DEFINES of makefile.def: NDEBUG (a free build; _DEBUG with DEBUG_CRTS, none with
# USE_MFC30), then _DLL and _MT for the run-time DLL (USE_CRTDLL, USE_MSVCRT20 and
# USE_MSVCRT40 are USE_MSVCRT on a RISC machine), _MT for USE_LIBCMT, nothing more for
# USE_LIBCNTPR, _DLL for USE_NTDLL, USE_SYSDLL and a DRIVER or EXPORT_DRIVER.
ntdefines() {
    local m=$(bash $B/srcmacro.sh $1/sources "*") d=$NTDEFINES t
    if echo "$m" | grep -q "^CAIRO_PRODUCT="; then
        d=$(echo "$d" | sed "s/-DWIN32=100 -D_NT1X_=100/-DWIN32=300 -D_CAIRO_=300/")
        echo "$m" | grep -q "^NOT_UNICODE=" || d="$d -DUNICODE -D_UNICODE"
    fi
    echo "$m" | grep -q "^NOT_LEAN_AND_MEAN=" && d=$(echo "$d" | sed "s/ -DWIN32_LEAN_AND_MEAN=1//")
    if echo "$m" | grep -q "^USE_MFC30="; then :
    elif echo "$m" | grep -q "^DEBUG_CRTS="; then d="$d -D_DEBUG"
    else d="$d -DNDEBUG"
    fi
    # the C++ exceptions and run-time type information of USE_NATIVE_EH (EH_FLAGS /GX /GR of
    # mipsmk.inc), which makefile.def sets for MFC 4 (srcmacro.sh), the MFC_FLAGS of MFC, the
    # USER_C_FLAGS and the flags of the machine, ARM_FLAGS (MIPS_FLAGS of mipsmk.inc): flags of
    # the compiler, which makefile.def does not give RC (ntdefines D rc)
    if [ "${2:-}" != rc ]; then
        echo "$m" | grep -q "^USE_NATIVE_EH=" && d="$d -GX -GR"
        d="$d $(echo "$m" | sed -n "s/^MFC_FLAGS=//p") $(echo "$m" | sed -n "s/^USER_C_FLAGS=//p") $(echo "$m" | sed -n "s/^ARM_FLAGS=//p")"
    fi
    t=$(echo "$m" | sed -n "s/^TARGETTYPE=//p" | tr -d " ")
    if echo "$m" | grep -qE "^USE_(CRTDLL|MSVCRT|MSVCRT20|MSVCRT40)="; then d="$d -D_DLL=1 -D_MT=1"
    elif echo "$m" | grep -q "^USE_LIBCMT="; then d="$d -D_MT=1"
    elif echo "$m" | grep -q "^USE_LIBCNTPR="; then :
    elif echo "$m" | grep -qE "^USE_(NTDLL|SYSDLL)=" || [ "$t" = DRIVER ] || [ "$t" = EXPORT_DRIVER ]; then d="$d -D_DLL=1"
    fi
    echo "$d"
}
CLOPTS="-nologo -c -Zl -Zc:wchar_t- -Zc:forScope- -GS- -Gy -O2 -GF- -Oy- -Z7 -W1 -wd4430 -GR- -Zc:sizedDealloc- -Zc:threadSafeInit-"
# The forced include of every C and C++ compile, as in makefile.def (warning.h: the try,
# except, finally and leave of C); a SOURCES may set its own, also none (make-lib.sh).
COMPILER_WARNINGS=${COMPILER_WARNINGS--FI/tmp/f/pub/warning.h}

# ntsrc SOURCE OUT: the source as NT's build compiles it for ARM: the RISC and
# compiler gates take ARM (riscgate.sed, msvcgate.sed), the includes name the
# farmed headers (incfix.sed), with the ARM port's additions (armport.sed) and the
# C and C++ of Microsoft C 2.0 the compiler of today rejects (msc2.sed); #line keeps the
# original name for the debugger. A quoted include by its path of a header the build
# made in another directory names it there (ntgen, incdirs.sh --gen).
ntgen() {
    grep -q '^[[:space:]]*#[[:space:]]*include[[:space:]]*"\.\.' "$1" || { cat "$1"; return; }
    local d=$(dirname "$1")
    case $d in
    /work/RASPI/ported/*) d=$(bash $B/srcmacro.sh --resolve /work/private "${d#/work/RASPI/ported/}") || { cat "$1"; return; } ;;
    /work/RASPI/apps/*) d=$(bash $B/srcmacro.sh --resolve /work/private "${d#/work/RASPI/apps/}") || { cat "$1"; return; } ;;
    esac
    bash $B/incdirs.sh --gen "$1" "$d"
}
# The data of #pragma data_seg(".CRT$X?") (the run-time: its initializer tables and their bounds,
# cinitexe.c __xc_a ... __xc_z) was in writable sections, as Microsoft C 2.0 put the C++
# initializers (.CRT$XCU); the compiler of today makes those read-only, and the link keeps
# sections of other attributes apart: the bounds then held none and no static constructor ran
# (ole32, rpcss). crtseg (of the source SOURCE) declares each read-only before its pragma, with
# read no macro: io.h of NT defines read as _read, an attribute the compiler ignores (C4109),
# so crt32 fflush.c's _endstdio stayed writable, outside the terminators, and nothing was
# flushed at exit. #line goes on with the line of the pragma.
crtseg() {
    awk -v f="$1" 'match($0, /^[ \t]*#[ \t]*pragma[ \t]+data_seg[ \t]*\([ \t]*"\.CRT\$X[A-Z]*"/) {
        s = substr($0, RSTART, RLENGTH); sub(/^[^"]*"/, "", s); sub(/"$/, "", s)
        printf "#pragma push_macro(\"read\")\n#undef read\n#pragma section(\"%s\", long, read)\n#pragma pop_macro(\"read\")\n#line %d \"%s\"\n", s, NR, f
    } { print }'
}
ntsrc() {
    { echo "#line 1 \"$1\""; ntgen "$1" | tr -d '\032\r' | sed -E -f $B/riscgate.sed -f $B/msvcgate.sed -f $B/incfix.sed | sed -f $B/armport.sed -f $B/msc2.sed | crtseg "$1"; } > "$2" || return 1
    # A source that includes the source of its own name in another directory (crt32\small\
    # timeset.c: "..\time\timeset.c") would include itself by the name incfix.sed leaves: it
    # includes that source (the port's version, if any) as ntsrc makes it, from a directory
    # of its own (OUT.d) that holds the headers of the directory of that source, which its
    # quoted includes find first, as they found them beside it.
    local b=$(basename "$1") l d p r f
    grep -qiF "$b\"" "$1" || return 0
    l=$(echo "$b" | tr A-Z a-z)
    d=$(dirname "$1")
    case $d in
    /work/RASPI/ported/*) d=$(bash $B/srcmacro.sh --resolve /work/private "${d#/work/RASPI/ported/}") || return 0 ;;
    /work/RASPI/apps/*) d=$(bash $B/srcmacro.sh --resolve /work/private "${d#/work/RASPI/apps/}") || return 0 ;;
    esac
    p=$(tr -d '\032\r' < "$1" | sed -nE "s/^[[:space:]]*#[[:space:]]*include[[:space:]]*\"([^\"]*[\\\\/]$(echo "$b" | sed 's/[.[\*^$]/\\&/g'))\".*/\1/Ip" | head -1)
    [ -n "$p" ] && r=$(bash $B/srcmacro.sh --resolve "$d" "$p") && [ "$r" != "$d/$b" ] || return 0
    rm -rf "$2.d"; mkdir -p "$2.d"
    for f in $(dirname "$r")/*.[Hh] $(dirname "$r")/*.[Hh][Xx][Xx] $(bash $B/portdir.sh $(dirname "$r"))/*.[Hh]; do
        [ -f "$f" ] && ntsrc "$f" "$2.d/$(basename "$f" | tr A-Z a-z)"
    done
    f=$(bash $B/portdir.sh $(dirname "$r"))/$(basename "$r" | tr A-Z a-z)
    [ -f "$f" ] || f=$r
    ntsrc "$f" "$2.d/$l" && sed -i -E "s|^([[:space:]]*#[[:space:]]*include[[:space:]]*)\"$l\"|\1\"$(basename "$2").d/$l\"|" "$2"
}

# ntcc OBJ SOURCE OPTION...: compile a C or C++ source; its messages to OBJ.err, and
# those of a function that falls off its end (C4715: what the return register held was
# its value under Microsoft C 2.0, not under the compiler of today) to obj/c4715.log.
# Without -D_MT, for the single-threaded run-time (inc/stcrt.h). -GX is the C++ exceptions
# of Visual C++ 4.2, which catch (...) catches system exceptions in: -EHa today (-GX is
# -EHsc).
ntcc() {
    local o=$1 s=$2 st=-FI$K/inc/stcrt.h a=() x; shift 2
    case " $* " in *" -D_MT "*|*" -D_MT="*) st= ;; esac
    for x in "$@"; do case $x in -GX|/GX) a+=(-EHa) ;; *) a+=("$x") ;; esac; done
    cl $CLOPTS $st $COMPILER_WARNINGS "${a[@]}" -Fo$o "$s" > $o.err 2>&1 || { echo "   CC FAIL $(basename $s): $(grep -m1 -E "error|fatal" $o.err)"; return 1; }
    grep -h C4715 $o.err >> /work/RASPI/obj/c4715.log || true
}
# ntcxx DIR: -TP for a component whose SOURCES compile its C sources as C++ (USECXX_FLAG=/Tp,
# which makefile.def's rules put before each .c source)
ntcxx() { [ -z "$(bash $B/srcmacro.sh $1/sources USECXX_FLAG | tr -d ' ')" ] || echo -TP; }

# ntas OBJ SOURCE OPTION...: assemble an armasm source (Thumb-2), after the C
# preprocessor for its includes and defines (as NT's .s sources were), with the
# ARM assembler macros of the Windows SDK (kxarm.h: entries, prologs and their
# unwind data) after the include directories given.
KXARM=$(ls -d /opt/msvc/kits/Include/*/shared | tail -1)
ntas() {
    local o=$1 s=$2; shift 2
    cl -nologo -EP "$@" -I$KXARM "$s" > $o.asm 2> $o.err || { echo "   CPP FAIL $(basename $s): $(grep -m1 -E "error|fatal" $o.err)"; return 1; }
    armasm -nologo -g $o.asm $o > $o.err 2>&1 || { echo "   AS FAIL $(basename $s): $(grep -m1 -E "error" $o.err)"; return 1; }
}

# par COMMAND...: the compile of one source in the background, at most CJ (default 4) at
# once; pwait waits for them all and sets PAROK and PARFAIL to the numbers that succeeded
# and failed.
CJ=${CJ:-4}
PARD=$(mktemp -d)
par() {
    while [ $(jobs -rp | wc -l) -ge $CJ ]; do wait -n || true; done
    { "$@" && touch $PARD/ok.$BASHPID || touch $PARD/fail.$BASHPID; } &
}
pwait() {
    wait
    PAROK=$(ls $PARD | grep -c "^ok" || true)
    PARFAIL=$(ls $PARD | grep -c "^fail" || true)
    rm -f $PARD/*
}

# ntlib LIB OBJ...: a library. Of the definitions of a symbol in more than one
# object the 1994 LIB indexed the first, that of today indexes the last: it gets
# the objects in reverse order.
ntlib() {
    local l=$1 o= f; shift
    for f in "$@"; do o="$f $o"; done
    rm -f $l; lib -nologo -out:$l $o > $l.err 2>&1 || { echo "   LIB FAIL $(basename $l): $(grep -m1 -E "error|fatal" $l.err)"; return 1; }
}

# ntimplib DEF LIB OUT: install the import library LIB of DEF as OUT (DEF beside it)
# when DEF changed: makefile.def made an import library again only from a changed
# .def, so relinking an image does not relink the images that import from it. OUT
# is installed again when something else wrote it after DEF.
ntimplib() {
    [ -f $3 ] && [ ! $3 -nt ${3%.lib}.def ] && cmp -s $1 ${3%.lib}.def || { cp $2 $3 && cp $1 ${3%.lib}.def; }
}

# The versions makefile.def gives every image: LINKVER (LINK_OS_VERSIONS) to the link,
# and NT 4.00 as the subsystem version (SUBSYSTEM_NATVER, _WINVER, _CONVER), or the
# SUBSYSTEM_VERSION of the SOURCES. The linker for ARM writes subsystem version 6.02
# whatever it is given (LNK4010): ntver IMAGE [VERSION] sets it in the linked image
# (tools/pe2img). The kernel reports its own as the version of NT (init.c), and
# CreateProcess runs no image of a later one. It also gives the image the misc debug
# record binplace left in every image of NT, the name IMAGEHLP gives its module.
LINKVER="-version:4.00 -osversion:4.00"
ntver() {
    bash /work/RASPI/tools/host.sh pe2img -version ${2:-4.00} -misc $1
}

# ntlinkflags D: the options of the LINKER_FLAGS of the SOURCES of component D that make its
# image: the sections it merges or sets (a keyboard layout all in its .data, which win32k.sys
# reads from the file; a shared section) and the symbols it includes. Those of the map, the
# listings and the warnings of the 1996 linker are left out (the map is always made).
ntlinkflags() {
    bash $B/srcmacro.sh $1/sources LINKER_FLAGS | tr " " "\n" | grep -iE "^[-/](merge|section|include):" | tr "\n" " "
}

# ntpch D OUT: the -I of directory OUT, holding the PRECOMPILED_INCLUDE of the SOURCES of
# component D as ntsrc makes it. Its sources include it by name (<pch.cxx> of ntdll), which
# the NT build answered from the precompiled header (/Yu) without looking for the file.
# A header another component made (PRECOMPILED_TARGET in its obj: com2int.pch of OLE's
# com\inc, which the com libraries share) holds what that one found: its include path too.
ntpch() {
    local p=$(bash $B/srcmacro.sh $1/sources PRECOMPILED_INCLUDE | tr -d " " | tr "\\\\" /) f t m
    [ -n "$p" ] && f=$(bash $B/srcmacro.sh --resolve $1 "$p") || return 0
    mkdir -p $2 && ntsrc $f $2/$(basename "$f" | tr A-Z a-z) && echo "-I$2"
    t=$(bash $B/srcmacro.sh $1/sources PRECOMPILED_TARGET | tr -d " " | tr "\\\\" /)
    case $t in */obj/*) t=$(bash $B/srcmacro.sh --resolve $1 "${t%%/obj/*}") || return 0 ;; *) return 0 ;; esac
    [ "$(realpath $t)" != "$(realpath $1)" ] || return 0
    m="bash $B/srcmacro.sh $t/sources"
    bash $B/incdirs.sh $t "$($m MFC_INCLUDES);$($m USER_INCLUDES);$($m INCLUDES)"
}

# ntpchobj D OUT: in directory OUT, the source of the object of the PRECOMPILED_INCLUDE of
# component D (HEADEROBJNAME: PRECOMPILED_OBJ, else named as the include; with a
# PRECOMPILED_OPTION, a PRECOMPILED_OBJ alone), which makefile.def compiles from its
# pch_hdr.src (#include "<include>"), C or C++ (an include .hxx, .cxx or .cpp,
# PRECOMPILED_CXX), and links with the objects or puts in the library. A source of the
# SOURCES of that name (pch.cxx of ntdll) is that object: the NT build made it once.
ntpchobj() {
    local m="bash $B/srcmacro.sh $1/sources" p o x=c
    p=$($m PRECOMPILED_INCLUDE | tr -d " " | tr "\\\\" /); [ -n "$p" ] || return 0
    o=$($m PRECOMPILED_OBJ | tr -d " " | tr "\\\\" /)
    [ -n "$o" ] || [ -z "$($m PRECOMPILED_OPTION)" ] || return 0
    o=$(basename "${o:-$p}" | tr A-Z a-z); o=${o%.*}
    $m SOURCES | tr " " "\n" | sed "s#.*[\\\\/]##" | tr A-Z a-z | grep -qE "^$o\.(c|cxx|cpp)$" && return 0
    case $p in *.[Hh][Xx][Xx]|*.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) x=cxx ;; esac
    [ -z "$($m PRECOMPILED_CXX | tr -d " ")" ] || x=cxx
    # (an include that is a source of that name, ..\astghead.cxx of OLE's async docfile: that
    # source, which the source made here would include in its place)
    [ "$(basename "$p" | tr A-Z a-z)" != "$o.$x" ] || { bash $B/srcmacro.sh --resolve $1 "$p"; return; }
    mkdir -p $2 && printf '#include "%s"\n' "$(basename "$p" | tr A-Z a-z)" > $2/$o.$x && echo $2/$o.$x
}

# ntyu D EXT SRC: a source of component D (c, cxx or cpp) as ntsrc made it, when makefile.def
# compiled it with the precompiled header (/Yu: the C sources, the C++ ones with PRECOMPILED_CXX
# or an include .hxx, .cxx or .cpp; the /Yu of a PRECOMPILED_OPTION): the compiler took all it
# had before the include of that header from the header, and read none of the text there
# (shell32's grepstrs.c declares functions there by types the header defines). Those lines
# are left blank. With a /Yu of no header and a PRECOMPILED_SOURCEFILE instead of a header,
# the precompiled state is that of the source file (/Yc), which a source takes in place of
# all its text up to its #pragma hdrstop: the type library of OLE Automation (tlibpch.cxx,
# #pragma hdrstop(RTPCHNAME)); the source file itself is compiled as it is.
ntyu() {
    local m="bash $B/srcmacro.sh $1/sources" p x=c o s
    o=$($m PRECOMPILED_OPTION)
    if [ -n "$(echo $o)" ]; then
        p=$(echo "$o" | sed -nE 's#.*[-/][Yy]u([^ ]+).*#\1#p')
        # (a /Yu of no header: to the #pragma hdrstop, which follows the include of the
        # PRECOMPILED_INCLUDE: wstart.cpp of netsetup includes registry.hxx before it)
        [ -n "$p" ] || p=$($m PRECOMPILED_INCLUDE | tr -d " ")
        s=$($m PRECOMPILED_SOURCEFILE | tr -d " ")
        if [ -z "$p" ] && [ -n "$s" ]; then
            s=$(basename "$(echo "$s" | tr "\\\\" /)" | tr A-Z a-z)
            case $s in *.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) x=cxx ;; esac
            case $2 in c) [ $x = c ] ;; *) [ $x = cxx ] ;; esac || return 0
            [ "$(basename "$3")" != "${s%.*}.$2" ] || return 0
            awk -v h="$s" '
            NR > 1 && !f && /^[ \t]*#[ \t]*pragma[ \t]+hdrstop/ { f = NR }
            { l[NR] = $0 }
            END { for (i = 1; i <= NR; i++) print (i == f) ? "#include \"" h "\"" : (f && i > 1 && i < f) ? "" : l[i] }' "$3" > "$3.yu" && mv "$3.yu" "$3"
            return 0
        fi
    else
        p=$($m PRECOMPILED_INCLUDE | tr -d " ")
        case $p in *.[Hh][Xx][Xx]|*.[Cc][Xx][Xx]|*.[Cc][Pp][Pp]) x=cxx ;; esac
    fi
    [ -n "$p" ] || return 0
    [ -z "$($m PRECOMPILED_CXX | tr -d " ")" ] || x=cxx
    case $2 in c) [ $x = c ] ;; *) [ $x = cxx ] ;; esac || return 0
    p=$(basename "$(echo "$p" | tr "\\\\" /)" | tr A-Z a-z)
    awk -v h="$p" '
    NR > 1 && !f && match($0, /^[ \t]*#[ \t]*include[ \t]*["<][^">]*[">]/) {
        s = substr($0, RSTART, RLENGTH); sub(/^[^"<]*["<]/, "", s); sub(/[">]$/, "", s); sub(/^.*[\\\/]/, "", s)
        if (tolower(s) == h) f = NR
    }
    { l[NR] = $0 }
    END { for (i = 1; i <= NR; i++) print (f && i > 1 && i < f) ? "" : l[i] }' "$3" > "$3.yu" && mv "$3.yu" "$3"
}
