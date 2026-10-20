# Microsoft C for ARM (MSVC 14.44, tenox7/msvc-arm32 under Wine): the compiler,
# assembler, librarian and linker of the NT build, with the options and defines
# makefile.def gives a RISC machine. Sourced by the build scripts in the
# container; the ARM gates of the sources come from ntsrc().
export MSVC_ENV=0 WINEDEBUG=-all
wineserver -p 2>/dev/null

K=/work/RASPI/arcfw/kernel

# The sources of the port that are NT's with ARM changes, from their edit scripts
# over the NT tree (tools/ntdiff.pl).
perl /work/RASPI/tools/ntdiff.pl apply /work /work/RASPI

# The machine and build defines of makefile.def (_ARM_ for the RISC machine), and
# the options: no default libraries, wchar_t as NT's unsigned short, no security
# checks, function-level linking, string literals writable and unpooled as the MIPS
# compiler placed them (-GF- after -O2, which pools them read-only), debug
# information in the objects, the C++ of MSVC 2.0: implicit int, no RTTI, no sized
# delete, no thread safe statics, and the warnings of level 1.
NTDEFINES="-D_ARM_=1 -DNT_UP=1 -DNT_INST=0 -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DWIN32=100 -D_NT1X_=100 -DDBG=0 -DDEVL=1 -DFPO=1"
CLOPTS="-nologo -c -Zl -Zc:wchar_t- -Zc:forScope- -GS- -Gy -O2 -GF- -Oy- -Z7 -W1 -wd4430 -GR- -Zc:sizedDealloc- -Zc:threadSafeInit-"

# ntsrc SOURCE OUT: the source as NT's build compiles it for ARM: the RISC and
# compiler gates take ARM (riscgate.sed, msvcgate.sed), the includes name the
# farmed headers (incfix.sed), with the ARM port's additions (armport.sed) and the
# C and C++ of Microsoft C 2.0 the compiler of today rejects (msc2.sed); #line keeps the
# original name for the debugger.
ntsrc() {
    { echo "#line 1 \"$1\""; tr -d '\032\r' < "$1" | sed -E -f $K/riscgate.sed -f $K/msvcgate.sed -f $K/incfix.sed | sed -f $K/armport.sed -f $K/msc2.sed; } > "$2"
}

# ntcc OBJ SOURCE OPTION...: compile a C or C++ source; its messages to OBJ.err, and
# those of a function that falls off its end (C4715: what the return register held was
# its value under Microsoft C 2.0, not under the compiler of today) to obj/c4715.log.
# Without -D_MT, for the single-threaded run-time (inc/stcrt.h).
ntcc() {
    local o=$1 s=$2 st=-FI$K/inc/stcrt.h; shift 2
    case " $* " in *" -D_MT "*|*" -D_MT="*) st= ;; esac
    cl $CLOPTS $st "$@" -Fo$o "$s" > $o.err 2>&1 || { echo "   CC FAIL $(basename $s): $(grep -m1 -E "error|fatal" $o.err)"; return 1; }
    grep -h C4715 $o.err >> /work/RASPI/obj/c4715.log || true
}

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
# is installed again when something wrote it after DEF: a library of the same name
# (ACLEDIT\ACLEDIT, of ACLEDIT.DLL).
ntimplib() {
    [ -f $3 ] && [ ! $3 -nt ${3%.lib}.def ] && cmp -s $1 ${3%.lib}.def || { cp $2 $3 && cp $1 ${3%.lib}.def; }
}
