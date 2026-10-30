#!/bin/bash
# Print the -I flags for an NT component as build.exe forms them: the component
# directory, then its SOURCES INCLUDES (resolved case-insensitively, as NT's file
# system does). NT's common include directories map to the header farm
# (mkfarm.sh), as do the components it farmed; any other directory is farmed
# under /tmp/f/dirs (its text files: headers and the other files sources include),
# with the files the build generated for it (/tmp/f/gen), the port's versions of its
# files (ported/<directory> or apps/<directory>, portdir.sh) and the message headers
# of its makefile.inc (mcinc.sh).
# Usage: incdirs.sh componentdir "INCLUDES value"
B=$(cd "$(dirname "$0")" && pwd)
# Microsoft C takes the sources as they are but for the ARM gates and the C and C++ the
# compiler of today rejects (msc2.sed, as ntsrc of msvc.sh). RC (on Wine, which finds
# names as NT does) takes the include names of resource scripts as they are (NET UI's
# <string\lmobj.str>).
filt() {
    local f=-f$B/incfix.sed m=-f$B/msc2.sed
    case $1 in *.[Rr][Cc]|*.[Rr][Cc]2|*.[Dd][Ll][Gg]|*.[Ss][Tt][Rr]) f= m= ;; esac
    if [ -n "$f" ]; then own "$1" "$2"; else cat "$1"; fi | tr -d '\032\r' | sed -E -f $B/riscgate.sed -f $B/msvcgate.sed $f | sed -f $B/armport.sed $m
}
# own FILE DIR: FILE of directory DIR, where a quoted include of a header of another
# directory by its path names the farm of that directory (that of NT's common include
# directories, else /tmp/f/dirs<directory>): the compiler found it from the directory of
# the file, the flattened name (incfix.sed, which keeps such a path) would find a header
# of that name beside the file (userrtl.h of NTUSER\RTL: "..\kernel\precomp.h", beside its
# own precomp.h) or beside a file that includes it (shell\inc\shlobj.h: "..\..\inc\shlobjp.h",
# the header pass's, beside shelldll\shellprv.h), and so would the name of a header of a
# directory below it, which a file includes on every machine (setup\syssetup\setupp.h:
# "i386\spx86.h"). A header of the port's include directories (kernel/inc, the farm's ovr)
# keeps the flattened name, which finds the port's.
#   So does an include of another directory by its path in angle brackets (OLE's
# com\inc\comsrgt.hxx: <..\com\dcomrem\locks.hxx>), which the compiler found from an
# include directory of the same parent as the directory of the file or one above it.
own() {
    local i b t f p m=
    for i in $(tr -d '\r' < "$1" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*<(\.\.[\\/][^>]*)>.*/\1/p' | sort -u); do
        b=$(echo "${i##*[\\/]}" | tr A-Z a-z); p=$2
        while [ "$p" != /work ] && [ "$p" != / ]; do
            if t=$(resolve "$p" "${i%[\\/]*}") && ls "$t" | grep -qix -- "$b"; then
                f=$(common "$t"); [ -n "$f" ] || { farm "$t" "/tmp/f/dirs$t"; f=/tmp/f/dirs$t; }
                m="$m$i=$f/$b"$'\n'; break
            fi
            p=$(dirname "$p")
        done
    done
    for i in $(tr -d '\r' < "$1" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*"((\.\.?|[A-Za-z0-9_]+)[\\/][^"]*)".*/\1/p' | sort -u); do
        b=$(echo "${i##*[\\/]}" | tr A-Z a-z)
        { [ -e $B/../kernel/inc/$b ] || [ -e /tmp/f/ovr/$b ]; } && continue
        t=$(resolve "$2" "${i%[\\/]*}") && [ "$t" != "$2" ] || continue
        f=$(common "$t")
        if [ -n "$f" ]; then
            [ -e "$f/$b" ] || continue
        else
            # (a header of the tree there, or one the build made there: net\messages\netmsg.h
            # of netcmd\common\pword.c, where the flattened name finds the old netcmd\inc\netmsg.h)
            ls "$t" "/tmp/f/gen/${t#/work/private/}" 2>/dev/null | grep -qix -- "$b" || continue
            farm "$t" "/tmp/f/dirs$t"; f=/tmp/f/dirs$t
        fi
        m="$m$i=$f/$b"$'\n'
    done
    [ -n "$m" ] || { cat "$1"; return; }
    M="$m" perl -pe 'BEGIN { for (split /\n/, $ENV{M}) { my ($k, $v) = split /=/, $_, 2; $m{$k} = $v; } } s/^(\s*#\s*include\s*)["<]([^">]*)[">]/exists $m{$2} ? "$1\"$m{$2}\"" : $&/e' "$1"
}
farm() {
    local s
    # A directory the farm of its parent linked (below) gets a farm of its own in place of
    # the link: a farm is never written through a link into the NT tree, which it would
    # empty (its names are those of the farm). So does a parent the farm of its own parent
    # linked (W32 then W32\NTUSER\INC of winsrv).
    case $(realpath -m "$(dirname "$2")") in /tmp/*) ;; *) farm "$(dirname "$1")" "$(dirname "$2")" ;; esac
    [ -L "$2" ] && rm "$2"
    [ -e "$2/.farmed" ] && return
    mkdir -p "$2" || exit 1
    case $(realpath "$2") in /tmp/*) ;; *) echo "incdirs: the farm $2 is not under /tmp" >&2; exit 1 ;; esac
    : > "$2/.farmed"
    # the message headers its makefile.inc makes, as the NT build made them first
    bash $B/mcinc.sh "$1" >&2
    # and the port's versions of its files (portdir.sh), which a quoted
    # include of a header beside them finds there too; its other files (the bitmaps and
    # icons of resource scripts) and directories (NET UI's xlate\string) are links, so
    # that RC needs the farmed directory alone (RC keeps its include path in 520 bytes)
    for f in "$1"/* "/tmp/f/gen/${1#/work/private/}"/* $(bash $B/portdir.sh "$1")/*; do
        n="$2/$(basename "$f" | tr A-Z a-z)"
        case $f in *.ntdiff|*/[Ss][Yy][Ss]) continue ;; esac
        if [ -d "$f" ]; then
            [ -e "$n" ] || ln -s "$f" "$n"
        elif grep -Iq . "$f" 2>/dev/null; then
            rm -f "$n"; filt "$f" "$1" > "$n"
        elif [ -f "$f" ]; then
            rm -f "$n"; ln -s "$f" "$n"
        fi
    done
    # the sys\ headers, in their directory (incfix.sed)
    s=$(ls -d "$1"/[Ss][Yy][Ss] 2>/dev/null) && farm "$s" "$2/sys"
}
resolve() {
    local p=$1 c n
    # (from a directory named through its parent, the directory of a source ..\x.c: spoolss\client\km\..)
    case $p in */..|*/../*) p=$(realpath -m "$p") ;; esac
    for c in $(echo "$2" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        # (a name with trailing dots, which NT drops: snmp\agent\common\trapapi.c, "..\authapi.\berapi.h")
        *) c=$(echo "$c" | sed 's/\.*$//')
           n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
# A directory of the NT tree: $(BASEDIR), $(_NTROOT), $(_NTBINDIR) or $(_NTDRIVE)\nt.
include() {
    case $2 in
    '$(BASEDIR)'*|'$(_NTROOT)'*|'$(_NTBINDIR)'*) resolve /work "${2#*)}" || generated /work "${2#*)}" ;;
    '$(_NTDRIVE)\nt\'*|'\nt\'*) resolve /work "${2#*\\nt\\}" ;;
    *) resolve "$1" "$2" || generated "$1" "$2" ;;
    esac
}
# A directory that holds files the build generated alone (the obj of the headers the
# MIDL pass makes, RPC\RUNTIME\RTIFS\obj), farmed from them.
generated() {
    local i=$(echo "$2" | tr '\\' /) p
    p=$(resolve "$1" "$(dirname "$i")") || return 1
    p=$p/$(basename "$i" | tr A-Z a-z)
    [ -d "/tmp/f/gen/${p#/work/private/}" ] && echo "$p"
}
# The farm of one of NT's common include directories (mkfarm.sh).
common() {
    case $1 in
    /work/private/ntos/inc) echo /tmp/f/priv ;;
    /work/private/inc) echo /tmp/f/pinc ;;
    /work/public/sdk/inc) echo /tmp/f/pub ;;
    /work/public/sdk/inc/crt) echo /tmp/f/crt ;;
    /work/public/oak/inc) echo /tmp/f/oak ;;
    /work/private/windows/inc) echo /tmp/f/winc ;;
    esac
}
# incdirs.sh --gen SOURCE DIR: SOURCE of directory DIR, where a quoted include by its path of
# a header the build made in another directory names the farm of that directory: the NT
# build made it there, in the tree, and the compiler found it from the directory of the
# source; the flattened name (incfix.sed) would find first a header of that name the tree
# has elsewhere (netcmd\common\pword.c: "..\..\messages\netmsg.h", not netcmd\inc\netmsg.h).
# So does one of a file of the tree there of the name of one beside the source, which the
# flattened name would find (psprint\lib\prnlib.c: "..\..\lib\devmode.c", beside its own
# devmode.c), or of one of NT's common include directories (sndrec32's srecnew.c:
# "..\..\msacm\msacm\msacm.h", newer than the SDK's msacm.h); not a source of its own name,
# which ntsrc includes from a directory of its own.
shadowed() {
    local c
    for c in priv pinc pub crt oak winc; do [ -e /tmp/f/$c/$1 ] && return 0; done
    return 1
}
if [ "$1" = --gen ]; then
    m=
    for i in $(tr -d '\r' < "$2" | sed -nE 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*"(\.\.[\\/][^"]*)".*/\1/p' | sort -u); do
        b=$(echo "${i##*[\\/]}" | tr A-Z a-z)
        t=$(resolve "$3" "${i%[\\/]*}") && [ -z "$(common "$t")" ] || continue
        if ls "$t" | grep -qix -- "$b"; then
            [ "$(realpath "$t")" != "$(realpath "$3")" ] && { ls "$3" | grep -qix -- "$b" || shadowed "$b"; } &&
                [ "$b" != "$(basename "$2" | tr A-Z a-z)" ] || continue
        else
            ls "/tmp/f/gen/${t#/work/private/}" 2>/dev/null | grep -qix -- "$b" || continue
        fi
        farm "$t" "/tmp/f/dirs$t"
        m="$m$i=/tmp/f/dirs$t/$b"$'\n'
    done
    [ -n "$m" ] || exec cat "$2"
    M="$m" exec perl -pe 'BEGIN { for (split /\n/, $ENV{M}) { my ($k, $v) = split /=/, $_, 2; $m{$k} = $v; } } s/^(\s*#\s*include\s*)"([^"]*)"/exists $m{$2} ? "$1\"$m{$2}\"" : $&/e' "$2"
fi
for p in "$1" $(for i in $(echo "$2" | tr ';' ' '); do include "$1" "$i"; done); do
    c=$(common "$p")
    case $p in
    /work/private/ntos/inc|/work/private/inc|/work/public/sdk/inc|/work/public/oak/inc|/work/private/windows/inc) echo -n " -I$c" ;;
    /work/private/ntos/*) t=/tmp/f/$(echo ${p#/work/private/ntos/} | tr A-Z a-z)
        [ -d $t ] || { t=/tmp/f/dirs$p; farm "$p" $t; }
        echo -n " -I$t" ;;
    *) farm "$p" "/tmp/f/dirs$p"; echo -n " -I/tmp/f/dirs$p" ;;
    esac
done
