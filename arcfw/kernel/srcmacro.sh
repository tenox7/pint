#!/bin/bash
# Print the value of a macro in an NT SOURCES file as nmake reads it: !include
# files read (names resolved case-insensitively), !IF/!IFDEF/!IFNDEF/!ELSE/
# !ELSEIF/!ENDIF conditionals taken, continuation lines joined and $(NAME)
# references to defined macros expanded; others, such as $(BASEDIR), are kept.
# No architecture macro (MIPS, ALPHA, 386, PPC) is defined; SRCMACROS="NAME=value ..."
# defines the macros nmake would take from the environment (CRTLIBTYPE).
# Usage: srcmacro.sh SOURCES NAME
resolve() {
    local p=$1 c n
    for c in $(echo "$2" | tr '\\/' '  '); do
        case $c in
        ..) p=$(dirname "$p") ;;
        .) ;;
        *) n=$(ls "$p" 2>/dev/null | grep -ix "$c" | head -1); [ -n "$n" ] || return 1; p=$p/$n ;;
        esac
    done
    echo "$p"
}
flatten() {
    local d line f
    d=$(dirname "$1")
    while IFS= read -r line; do
        if [[ $line =~ ^[[:space:]]*![[:space:]]*[Ii][Nn][Cc][Ll][Uu][Dd][Ee][[:space:]]+([^[:space:]]+) ]]; then
            f=$(resolve "$d" "$(echo "${BASH_REMATCH[1]}" | tr -d '"<>')") && flatten "$f"
        else
            printf '%s\n' "$line"
        fi
    done < <(tr -d '\032\r' < "$1")
}
flatten "$1" | awk -v k="$2" '
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
BEGIN {
    depth = 0; on[0] = 1
    n = split(ENVIRON["SRCMACROS"], pre, " ")
    for (i = 1; i <= n; i++) if ((j = index(pre[i], "=")) > 1) mac[substr(pre[i], 1, j - 1)] = substr(pre[i], j + 1)
}
# Directives are taken where they stand, also inside a definition continued over
# lines; the lines of a branch not taken are skipped.
/^[ \t]*!/ {
    d = $0; sub(/^[ \t]*![ \t]*/, "", d)
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
    }
    next
}
!on[depth] { next }
cont == "" && /^[ \t]*#/ { next }
{
    line = $0; more = sub(/\\[ \t]*$/, "", line)
    if (cont != "") line = cont " " line
    if (more) { cont = line; next }
    cont = ""
    if (line !~ /^[A-Za-z_][A-Za-z0-9_]*[ \t]*=/) next
    n = line; sub(/[ \t]*=.*/, "", n)
    v = line; sub(/^[^=]*=[ \t]*/, "", v)
    v = trim(expand(v)); mac[n] = v
}
END { print (k in mac) ? mac[k] : "" }' | tr -s ' \t' ' '
