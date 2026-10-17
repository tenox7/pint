# Translate one target's recipe in an NT makefil0 into shell for mkfarm.sh and
# the MIDL pass (tools/midl/gen-midl.sh):
#   NMK_TARGET='inc\ntddk.h' awk -f nmk.awk makefil0
# Supports what the recipes use: macros (also NAME=value lines in NMK_MACROS;
# undefined ones are empty, as in nmake; $(NAME:a=b) substitutes), conditionals (!IF
# with || && ! and parentheses, !IFDEF, !IFNDEF, !ELSEIF, !ELSE, !ENDIF), continued command lines,
# "type << > f" and "type << >> f" inline files,
# "type a > b", "type a >> b", hextract, hsplit, "wcshdr < a > b", midl, mc, copy and cp (also "copy a + b c"),
# del/erase, lib, rcpp, solidpp, binplace, link32 -dump and uixport, piped with "|", "mapmsg ... > f"
# ($@ and $** or $(**) as the target and its dependents), echo ignored, inference rules ($< and $*), comment lines
# in recipes and "IF EXIST f cmd" with commands joined by "&". Paths come out with "/" and the
# macros NMK_KEEP names (default SDKINC NTOSINC OAKINC BASEDIR) left as prefixes
# for the caller to resolve. With NMK_DEPS set it prints the target's dependents
# instead.

# s with each f replaced by t ($(NAME:f=t)).
function subst(s, f, t,    out, i) {
    if (f == "")
        return s
    out = ""
    while ((i = index(s, f)) > 0) {
        out = out substr(s, 1, i - 1) t
        s = substr(s, i + length(f))
    }
    return out s
}

# The value of macro name (one that names itself, such as OS2FLAGS=$(OS2FLAGS) -DOS2,
# has the earlier value: none).
function expandm(name,    v) {
    busy[name] = 1; v = expand(mac[name]); busy[name] = 0
    return v
}

function expand(s,    name, val, out, st, ln, sb, i) {
    out = ""
    while (match(s, /\$\([A-Za-z0-9_]+(:[^)=]*=[^)]*)?\)/)) {
        st = RSTART; ln = RLENGTH
        name = substr(s, st + 2, ln - 3); sb = ""
        if ((i = index(name, ":")) > 0) { sb = substr(name, i + 1); name = substr(name, 1, i - 1) }
        if (index(" " keep " ", " " name " "))
            val = name
        else
            val = (name in mac) && !busy[name] ? expandm(name) : ""
        if (sb != "") { i = index(sb, "="); val = subst(val, substr(sb, 1, i - 1), substr(sb, i + 1)) }
        out = out substr(s, 1, st - 1) val
        s = substr(s, st + ln)
    }
    out = out s
    gsub(/\\/, "/", out)
    return out
}

# The head names the target: as its whole list of targets, or as one of them.
function names(h, t,    w, n, i) {
    if (h == t)
        return 1
    if (t ~ /[ \t]/)
        return 0
    n = split(h, w, /[ \t]+/)
    for (i = 1; i <= n; i++)
        if (w[i] == t)
            return 1
    return 0
}

# One command of a recipe, of those that "&" joins; a pipeline, each of its commands.
function command(cmd,    a, n, f, i) {
    sub(/^[ \t@-]+/, "", cmd)
    sub(/[ \t]+$/, "", cmd)
    gsub(/[ \t]*[12]?>[ \t]*[Nn][Uu][Ll]( 2>&1)?/, "", cmd)
    if (cmd ~ /[ \t]\|[ \t]/) {
        n = split(cmd, a, /[ \t]+\|[ \t]+/)
        f = command(a[1])
        for (i = 2; i <= n; i++) f = f " | " command(a[i])
        return f
    }
    if (cmd ~ /^wcshdr[ \t]*</) {
        sub(/[ \t]*<[ \t]*/, " < ", cmd)
        sub(/[ \t]*>[ \t]*/, " > ", cmd)
    }
    n = split(cmd, a, /[ \t]+/)
    if (tolower(a[1]) == "if" && tolower(a[2]) == "exist") {
        f = a[3]
        sub(/^[ \t]*[^ \t]+[ \t]+[^ \t]+[ \t]+[^ \t]+[ \t]*/, "", cmd)
        return "if nmk_exists " f "; then " command(cmd) "; fi"
    }
    if (tolower(a[1]) == "copy" && a[3] == "+") {
        f = ""
        for (i = 2; i < n; i++) if (a[i] != "+") f = f " " a[i]
        return "nmk_copycat " a[n] f
    }
    if (tolower(a[1]) == "copy" || a[1] == "cp")
        return "nmk_copy " a[2] " " (n > 2 ? a[3] : ".")
    if (tolower(a[1]) == "del" || tolower(a[1]) == "erase")
        return "nmk_del " substr(cmd, length(a[1]) + 2)
    if (tolower(a[1]) == "lib")
        return "nmk_lib " substr(cmd, 5)
    if (tolower(a[1]) == "mc")
        return "nmk_mc " substr(cmd, 4)
    if (a[1] == "midl")
        return "nmk_midl " substr(cmd, 6)
    if (a[1] == "hextract")
        return "nmk_hextract " substr(cmd, 10)
    if (a[1] == "hsplit")
        return "nmk_hsplit " substr(cmd, 8)
    if (a[1] == "wcshdr" && a[2] == "<" && a[4] == ">")
        return "nmk_wcshdr " a[3] " " a[5]
    if (a[1] == "type" && a[3] == ">>")
        return "nmk_cat " a[2] " " a[4]
    if (a[1] == "type" && a[3] == ">")
        return "nmk_type " a[2] " " a[4]
    if (tolower(a[1]) == "mapmsg") {
        sub(/[ \t]*>[ \t]*/, " \\> ", cmd)
        return "nmk_mapmsg " substr(cmd, 8)
    }
    if (a[1] == "rcpp" || a[1] == "solidpp" || a[1] == "binplace" || a[1] == "link32" || a[1] == "uixport")
        return "nmk_" cmd
    return "echo \x27nmk: unsupported command: " cmd "\x27 >&2; exit 1"
}

# The commands of an inference rule .FROM.TO for the target, $< its source and $* its base.
function infer(    r, e, ext, base, src, i, l) {
    ext = tgt; sub(/^.*\./, "", ext)
    for (r in infn) {
        split(substr(r, 2), e, ".")
        if (tolower(e[2]) != tolower(ext))
            continue
        base = substr(tgt, 1, length(tgt) - length(ext) - 1)
        src = base "." e[1]
        if (ENVIRON["NMK_DEPS"]) { print src; return }
        for (i = 1; i <= infn[r]; i++) {
            l = infc[r, i]
            gsub(/\$</, src, l); gsub(/\$\*/, base, l); gsub(/\$@/, tgt, l)
            l = expand(l); sub(/^[ \t]+/, "", l)
            print command(l)
        }
        return
    }
}

# The value of an !IF expression: terms joined by || and &&, each "a" == "b", "a" != "b",
# defined(NAME) or a number, in parentheses or negated by !.
function term(e,    l, r) {
    gsub(/^[ \t(]+|[ \t)]+$/, "", e)
    if (e ~ /^![^=]/)
        return !term(substr(e, 2))
    if (e ~ /"[^"]*"[ \t]*[!=]=[ \t]*"[^"]*"/) {
        l = e; sub(/^[^"]*"/, "", l); sub(/".*/, "", l)
        r = e; sub(/^.*[!=]=[ \t]*"/, "", r); sub(/".*/, "", r)
        return e ~ /!=/ ? l != r : l == r
    }
    if (match(e, /[Dd][Ee][Ff][Ii][Nn][Ee][Dd][ \t]*\([ \t]*[A-Za-z0-9_]+/)) {
        l = substr(e, RSTART, RLENGTH); sub(/^[^(]*\([ \t]*/, "", l)
        return l in mac
    }
    return e + 0 != 0
}
function cond(e,    n, i, j, m, a, b, v) {
    n = split(expand(e), a, /\|\|/)
    for (i = 1; i <= n; i++) {
        v = 1
        m = split(a[i], b, /&&/)
        for (j = 1; j <= m; j++) if (!term(b[j])) v = 0
        if (v) return 1
    }
    return 0
}

BEGIN {
    active = 1
    target = ENVIRON["NMK_TARGET"]
    tgt = target; gsub(/\\/, "/", tgt)
    keep = ENVIRON["NMK_KEEP"] ? ENVIRON["NMK_KEEP"] : "SDKINC NTOSINC OAKINC BASEDIR"
    n = split(ENVIRON["NMK_MACROS"], d, "\n")
    for (i = 1; i <= n; i++)
        if ((j = index(d[i], "=")) > 0)
            mac[substr(d[i], 1, j - 1)] = substr(d[i], j + 1)
}

{ sub(/\r$/, "") }

inline {
    if ($0 == "<<") { print "NMK_EOF"; inline = 0; next }
    print
    next
}

# Conditionals (!IF, !IFDEF, !IFNDEF, !ELSEIF, !ELSE, !ENDIF): the lines of the branch taken.
/^![ \t]*([Ii][Ff]|[Ee][Ll][Ss][Ee]|[Ee][Nn][Dd][Ii][Ff])/ {
    dv = $0; sub(/^![ \t]*/, "", dv)
    kw = tolower(dv); sub(/[ \t].*/, "", kw)
    arg = dv; sub(/^[^ \t]*[ \t]*/, "", arg); sub(/[ \t]+$/, "", arg)
    if (kw == "if" || kw == "ifdef" || kw == "ifndef") {
        up[++depth] = active
        c = kw == "ifdef" ? (arg in mac) : kw == "ifndef" ? !(arg in mac) : cond(arg)
        taken[depth] = c; active = up[depth] && c
    } else if (kw == "elseif") {
        c = !taken[depth] && cond(arg); taken[depth] = taken[depth] || c; active = up[depth] && c
    } else if (kw == "else") {
        active = up[depth] && !taken[depth]; taken[depth] = 1
    } else if (kw == "endif") {
        active = up[depth--]
    }
    next
}
!active { next }

# Comment lines, in and out of recipes.
/^#/ { next }

# Inference rules (.FROM.TO:) and their commands, for a target without a rule of its own.
ininf && /^[ \t]+[^ \t]/ { infc[inf, ++infn[inf]] = $0; next }
ininf { ininf = 0 }
!inrecipe && /^\.[A-Za-z0-9]+\.[A-Za-z0-9]+[ \t]*:/ {
    inf = $0; sub(/[ \t]*:.*/, "", inf); infn[inf] = 0; ininf = 1
    next
}

# Macro definitions, with backslash continuation.
!inrecipe && cont != "" {
    line = $0; more = sub(/\\$/, "", line)
    mac[cont] = mac[cont] " " line
    if (!more) cont = ""
    next
}

!inrecipe && /^[A-Za-z_][A-Za-z0-9_]*[ \t]*=/ {
    name = $0; sub(/[ \t]*=.*/, "", name)
    val = $0; sub(/^[^=]*=[ \t]*/, "", val)
    more = sub(/\\$/, "", val)
    mac[name] = val
    cont = more ? name : ""
    next
}

# The rule of the target (its dependents continued over lines ending in a backslash).
!inrecipe {
    head = $0
    if (sub(/[ \t]*:.*/, "", head) && names(expand(head), expand(target))) {
        tgt = target; gsub(/\\/, "/", tgt)
        deps = $0; sub(/^[^:]*:[ \t]*/, "", deps)
        headcont = sub(/\\[ \t]*$/, "", deps)
        inrecipe = 1
        matched = 1
        if (!headcont && ENVIRON["NMK_DEPS"]) { print expand(deps); exit }
        next
    }
}

inrecipe && headcont {
    l = $0; headcont = sub(/\\[ \t]*$/, "", l); deps = deps " " l
    if (!headcont && ENVIRON["NMK_DEPS"]) { print expand(deps); exit }
    next
}

inrecipe && /^[^ \t]/ { exit }

# A command line, continued over lines ending in a backslash.
inrecipe && /^[ \t]+[^ \t]/ {
    line = $0
    if (sub(/\\[ \t]*$/, "", line)) { cmdcont = cmdcont line " "; next }
    l = cmdcont line; cmdcont = ""
    gsub(/\\/, "/", deps)
    gsub(/\$\(\*\*\)/, deps, l)
    gsub(/\$\*\*/, deps, l)
    gsub(/\$@/, tgt, l)
    line = expand(l)
    gsub(/[ \t]*[12]?>[ \t]*[Nn][Uu][Ll]([ \t]*2>&1)?/, "", line)
    sub(/^[ \t]+/, "", line)
    n = split(line, a, /[ \t]+/)
    if (a[1] == "type" && a[2] == "<<") {
        print (a[3] == ">" ? "nmk_new " : "nmk_add ") a[4] " <<'NMK_EOF'"
        inline = 1
        next
    }
    if (a[1] ~ /^@?ech(o)?$/)
        next
    n = split(line, part, /[ \t]*&[ \t]*/)
    out = command(part[1])
    if (out ~ /^if nmk_exists/ && n > 1) {
        sub(/; fi$/, "", out)
        for (i = 2; i <= n; i++) out = out "; " command(part[i])
        out = out "; fi"
    } else
        for (i = 2; i <= n; i++) out = out "; " command(part[i])
    print out
}

END { if (!matched) infer() }
