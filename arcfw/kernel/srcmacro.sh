#!/bin/bash
# Print the value of a macro in an NT SOURCES file as nmake reads it under build.exe:
# the MAKEFILE of the component first, up to its include of makefile.def, where the
# SOURCES is read (a NET UI makefile includes its rules.mk there); !include files read
# (names resolved case-insensitively, from the directory of the including file, then
# from those of the files including it), !IF/!IFDEF/!IFNDEF/!ELSE/!ELSEIF/!ENDIF
# conditionals taken, !UNDEF, continuation lines joined and $(NAME) references to
# defined macros expanded; others, such as $(BASEDIR), are kept. NTMAKEENV is defined,
# NTDEBUG is not (a free build) and no architecture macro (MIPS, ALPHA, 386, PPC)
# is. SRCMACROS="NAME=value ..." defines the macros nmake would take from the
# environment (CRTLIBTYPE).
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
awk -v k="$2" -v self="$0" -v src="$src" -v mk="$mk" '
function expand(s,    out, name) {
    out = ""
    while (match(s, /\$\([A-Za-z0-9_]+\)/)) {
        name = substr(s, RSTART + 2, RLENGTH - 3)
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
# files including it.
function locate(p,    i, cmd, r) {
    gsub(/\\/, "/", p); gsub(/["<>]/, "", p)
    for (i = nest; i >= 1; i--) {
        cmd = "bash \"" self "\" --resolve \"" dirs[i] "\" \"" p "\""
        r = ""; cmd | getline r; close(cmd)
        if (r != "") return r
    }
    return ""
}
function include(f,    line) {
    dirs[++nest] = dirof(f)
    while ((getline line < f) > 0) take(line)
    close(f)
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
            if (tolower(f) ~ /makefile\.def$/) { if (!sources++) include(src) }
            else if ((f = locate(f)) != "") include(f)
        }
        return
    }
    if (!on[depth]) return
    if (cont == "" && line ~ /^[ \t]*#/) return
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
    mac["NTMAKEENV"] = "\\nt\\public\\oak\\bin"
    n = split(ENVIRON["SRCMACROS"], pre, " ")
    for (i = 1; i <= n; i++) if ((j = index(pre[i], "=")) > 1) mac[substr(pre[i], 1, j - 1)] = substr(pre[i], j + 1)
    if (mk != "") include(mk)
    if (!sources) { depth = 0; include(src) }
    if (k == "*") for (n in mac) print n "=" mac[n]
    else print (k in mac) ? mac[k] : ""
}' | tr -s ' \t' ' '
