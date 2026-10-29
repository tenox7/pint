#!/bin/bash
# Print the value of a macro in an NT SOURCES file as nmake reads it under build.exe:
# the MAKEFILE of the component first, up to its include of makefile.def, where the
# SOURCES is read (a NET UI makefile includes its rules.mk there); !include files read
# (names resolved case-insensitively, from the directory of the including file, then
# from those of the files including it), !IF/!IFDEF/!IFNDEF/!ELSE/!ELSEIF/!ENDIF
# conditionals taken, !UNDEF, continuation lines joined and $(NAME) and $N (a name of
# one letter) references to defined macros expanded; others, such as $(BASEDIR), are
# kept. NTMAKEENV is defined, NTDEBUG is not (a free build: FREEBUILD is 1, as
# makefile.plt sets it) and no architecture macro (MIPS, ALPHA, 386, PPC)
# is. SRCMACROS="NAME=value ..." defines the macros nmake would take from the
# environment (CRTLIBTYPE). The port's version of a SOURCES (portdir.sh: a component
# built without a part the tree has no source of) is read in its place, its includes
# resolved from the directory of the tree's.
# Usage: srcmacro.sh SOURCES NAME       (NAME * prints every macro as NAME=value)
resolve() {
    local p=$1 c n
    case $2 in /*) p= ;; esac
    for c in $(echo "$2" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "${p:-/}" 2>/dev/null | grep -ix -- "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
[ "$1" = --resolve ] && { resolve "$2" "$3"; exit; }
src=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mk=$(ls "$(dirname "$src")" | grep -ix makefile | head -1) && mk=$(dirname "$src")/$mk || mk=
port=$(bash "$(dirname "$0")/portdir.sh" "$(dirname "$src")")/$(basename "$src" | tr A-Z a-z)
[ -f "$port" ] || port=$src
# (or the one the build made, in a directory the tree leaves empty: ntuser\kbd\tmp\kbdus)
[ -f "$port" ] || [ ! -f "/tmp/f/gen/${src#/work/private/}" ] || port=/tmp/f/gen/${src#/work/private/}
awk -v k="$2" -v self="$0" -v src="$src" -v port="$port" -v mk="$mk" '
function expand(s,    out, name) {
    out = ""
    while (match(s, /\$(\$|\([A-Za-z0-9_]+\)|[A-Za-z0-9_])/)) {
        name = substr(s, RSTART + 1, RLENGTH - 1)
        if (RLENGTH > 2) name = substr(name, 2, RLENGTH - 3)
        # ($$, a dollar, is kept)
        out = out substr(s, 1, RSTART - 1) ((name in mac) ? mac[name] : substr(s, RSTART, RLENGTH))
        s = substr(s, RSTART + RLENGTH)
    }
    return out s
}
function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
function term(s,    a, op) {
    s = trim(s)
    if (s ~ /^!/) return !term(substr(s, 2))
    if (s ~ /^EXIST\(/ || s ~ /^exist\(/) return 0
    if (tolower(s) ~ /^defined[ \t]*\(/) {
        a = s; sub(/^[^(]*\([ \t]*/, "", a); sub(/[ \t]*\).*$/, "", a)
        return a in mac
    }
    if (match(s, /(==|!=)/)) {
        op = substr(s, RSTART, 2)
        a = trim(substr(s, 1, RSTART - 1)); s = trim(substr(s, RSTART + 2))
        gsub(/"/, "", a); gsub(/"/, "", s)
        return op == "==" ? a == s : a != s
    }
    gsub(/"/, "", s)
    return s != "" && s != "0" && s !~ /^\$\(/
}
function cond(s,    n, i, j, m, a, b, v) {
    s = expand(s)
    gsub(/\$\([A-Za-z0-9_]+\)/, "", s)
    n = split(s, a, /\|\|/)
    for (i = 1; i <= n; i++) {
        v = 1
        m = split(a[i], b, /&&/)
        for (j = 1; j <= m; j++) if (!term(b[j])) v = 0
        if (v) return 1
    }
    return 0
}
function dirof(f) { sub(/\/[^\/]*$/, "", f); return f }
# An included file: from the directory of the including file, then from those of the
# files including it; one of the NT tree ($(BASEDIR), $(_NTROOT) or $(_NTBINDIR), as
# incdirs.sh: the SFM makefiles UI=$(_NTBINDIR)\private\net\ui) from /work; another
# undefined macro leaves it unresolved.
function locate(p,    i, cmd, r) {
    gsub(/\\/, "/", p); gsub(/["<>]/, "", p)
    sub(/^\$\((BASEDIR|_NTROOT|_NTBINDIR)\)/, "/work", p)
    if (p ~ /\$/) return ""
    for (i = nest; i >= 1; i--) {
        cmd = "bash \"" self "\" --resolve \"" dirs[i] "\" \"" p "\""
        r = ""; cmd | getline r; close(cmd)
        if (r != "") return r
    }
    return ""
}
function include(f, d,    line) {
    dirs[++nest] = d != "" ? d : dirof(f)
    while ((getline line < f) > 0) take(line)
    close(f)
    # (a definition continued to the end of the file ends there: RSHELL\SERVER UMLIBS)
    if (cont != "") { line = cont; cont = ""; take(line) }
    nest--
}
# Directives are taken where they stand, also inside a definition continued over
# lines; the lines of a branch not taken are skipped.
function take(line,    d, w, rest, c, more, n, v, f) {
    gsub(/[\r\032]/, "", line)
    if (line ~ /^[ \t]*!/) {
        d = line; sub(/^[ \t]*![ \t]*/, "", d)
        w = toupper(d); sub(/[ \t(].*/, "", w)
        rest = d; sub(/^[A-Za-z]+[ \t]*/, "", rest)
        if (w == "IF" || w == "IFDEF" || w == "IFNDEF") {
            c = w == "IF" ? cond(rest) : (w == "IFDEF" ? (trim(rest) in mac) : !(trim(rest) in mac))
            depth++; taken[depth] = c; on[depth] = on[depth - 1] && c
        } else if (w == "ELSEIF") {
            c = !taken[depth] && cond(rest)
            if (c) taken[depth] = 1
            on[depth] = on[depth - 1] && c
        } else if (w == "ELSE") {
            on[depth] = on[depth - 1] && !taken[depth]; taken[depth] = 1
        } else if (w == "ENDIF") {
            depth--
        } else if (on[depth] && w == "UNDEF") {
            delete mac[trim(rest)]
        } else if (on[depth] && w == "INCLUDE") {
            f = trim(expand(rest))
            if (tolower(f) ~ /makefile\.def$/) { if (!sources++) include(port, dirof(src)) }
            else if ((f = locate(f)) != "") include(f)
        }
        return
    }
    if (!on[depth]) return
    # a comment line, also one inside a definition continued over lines (nmake drops it
    # and the definition goes on)
    if (line ~ /^[ \t]*#/) return
    more = sub(/\\[ \t]*$/, "", line)
    if (cont != "") line = cont " " line
    if (more) { cont = line; return }
    cont = ""
    if (line !~ /^[A-Za-z_][A-Za-z0-9_]*[ \t]*=/) return
    n = line; sub(/[ \t]*=.*/, "", n)
    v = line; sub(/^[^=]*=[ \t]*/, "", v)
    v = trim(expand(v)); mac[n] = v
}
BEGIN {
    depth = 0; on[0] = 1; nest = 0; sources = 0
    mac["NTMAKEENV"] = "\\nt\\public\\oak\\bin"; mac["FREEBUILD"] = "1"
    n = split(ENVIRON["SRCMACROS"], pre, " ")
    for (i = 1; i <= n; i++) if ((j = index(pre[i], "=")) > 1) mac[substr(pre[i], 1, j - 1)] = substr(pre[i], j + 1)
    if (mk != "") include(mk)
    if (!sources) { depth = 0; include(port, dirof(src)) }
    if (k == "*") for (n in mac) print n "=" mac[n]
    else print (k in mac) ? mac[k] : ""
}' | tr -s ' \t' ' '
