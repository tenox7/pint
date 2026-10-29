# Translate one target's recipe in an NT makefil0 into shell for mkfarm.sh and
# the MIDL pass (tools/midl/gen-midl.sh):
#   NMK_TARGET='inc\ntddk.h' awk -f nmk.awk makefil0
# Supports what the recipes use: macros (also NAME=value lines in NMK_MACROS;
# undefined ones are empty, as in nmake; $(NAME:a=b) substitutes; $O, a name of one letter), conditionals (!IF
# with || && ! and parentheses, !IFDEF, !IFNDEF, !ELSEIF, !ELSE, !ENDIF), continued command lines,
# "type << > f" and "type << >> f" inline files,
# "type a > b", "type a >> b", hextract, hsplit, "wcshdr < a > b" and "ws2hdr < a" (also in a
# pipe, without the redirection), midl, mc, copy and cp (also "copy a + b c"),
# del/erase, md/mkdir, delnode, attrib (nothing), lib (also link -lib), rcpp, solidpp, binplace, link32 -dump, uixport, guidlib, kbdtool, prodfilt, xdosnet and mibcc, piped with "|", "mapmsg ... > f", "listmung list template > f",
# the generators (set, "nmake obj\x.obj", link with an inline response file, "obj\machine\x.exe args")
# ($@ and $** or $(**) as the target and its dependents, $? too, $(@D) $(@B) $(@F) $(@R) the parts of
# the target's name, also in inline files and as $$(@D) in a dependency line), echo ignored but into a file, inference rules
# (.FROM.TO, {FROMDIR}.FROM{TODIR}.TO: $<, $** and $? the source, $* its base), comment lines
# in recipes and "IF EXIST f cmd" with commands joined by "&". Paths come out with "/" and the
# macros NMK_KEEP names (default SDKINC NTOSINC OAKINC BASEDIR) left as prefixes
# for the caller to resolve. With NMK_DEPS set it prints the target's dependents
# instead; with NMK_RULE set, the targets of its rule (exit status 1: it has none).

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

# s with the file name macros of target t ($@, $(@D) its directory, $(@B) its base name, $(@F)
# its file name, $(@R) it without its extension), with pre before each: $ in a dependency line,
# where nmake reads $$@ and $$(@D) as the target's (its dynamic dependents).
function tnames(s, t, pre,    d, f, b) {
    if (match(t, /.*[\/\\]/)) { d = substr(t, 1, RLENGTH - 1); f = substr(t, RLENGTH + 1) } else { d = "."; f = t }
    b = f; sub(/\.[^.]*$/, "", b)
    s = subst(s, pre "$(@D)", d); s = subst(s, pre "$(@B)", b); s = subst(s, pre "$(@F)", f)
    s = subst(s, pre "$(@R)", substr(t, 1, length(t) - length(f)) b)
    return subst(s, pre "$@", t)
}

# The value of macro name.
function expandm(name,    v) {
    busy[name] = 1; v = expand(mac[name]); busy[name] = 0
    return v
}

# A definition v of macro name as nmake reads it: where it names itself ($(NAME) or
# $(NAME:a=b)), the earlier value e (INCLUDES=$(SDKINC);$(INCLUDES) of OLE's async docfile).
function selfref(v, name, e,    out, ref, val, i) {
    out = ""
    while (match(v, "\\$\\(" name "(:[^)=]*=[^)]*)?\\)")) {
        ref = substr(v, RSTART + 2, RLENGTH - 3); val = e
        if ((i = index(ref, ":")) > 0) { ref = substr(ref, i + 1); i = index(ref, "="); val = subst(val, substr(ref, 1, i - 1), substr(ref, i + 1)) }
        out = out substr(v, 1, RSTART - 1) val
        v = substr(v, RSTART + RLENGTH)
    }
    return out v
}

function expand(s,    name, val, out, st, ln, sb, i) {
    out = ""
    while (match(s, /\$(\$|\([A-Za-z0-9_]+(:[^)=]*=[^)]*)?\)|[A-Za-z0-9_])/)) {
        st = RSTART; ln = RLENGTH
        # ($$, a dollar, is kept)
        if (substr(s, st + 1, 1) == "$") { out = out substr(s, 1, st + 1); s = substr(s, st + 2); continue }
        # (a name of one letter: $O)
        name = ln == 2 ? substr(s, st + 1, 1) : substr(s, st + 2, ln - 3); sb = ""
        if ((i = index(name, ":")) > 0) { sb = substr(name, i + 1); name = substr(name, 1, i - 1) }
        if (!rawtext && index(" " keep " ", " " name " "))
            val = name
        else
            val = (name in mac) && !busy[name] ? expandm(name) : ""
        if (sb != "") { i = index(sb, "="); val = subst(val, substr(sb, 1, i - 1), substr(sb, i + 1)) }
        out = out substr(s, 1, st - 1) val
        s = substr(s, st + ln)
    }
    out = out s
    if (!rawtext)
        gsub(/\\/, "/", out)
    return out
}

# A line of an inline file, as nmake writes it: its macros expanded, its backslashes kept.
function text(s,    v) {
    rawtext = 1; v = expand(s); rawtext = 0
    return v
}

# The position of the colon that ends the targets of a rule line s (0: none), outside the macro
# references: a substitution has its own ($(TARGETSOURCEFILES:kbd=..\tmp\kbd): of ntuser\kbd).
function colon(s,    i, c, depth) {
    depth = 0
    for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c == "(") depth++
        else if (c == ")") depth--
        else if (c == ":" && depth == 0) return i
    }
    return 0
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
    # the filters wcshdr and ws2hdr: their input and output ("-" in a pipe)
    if (cmd ~ /^(wcshdr|ws2hdr)([ \t<>]|$)/) {
        a[1] = substr(cmd, 1, 6); a[2] = a[3] = "-"
        if (match(cmd, /<[ \t]*[^ \t<>|]+/)) { a[2] = substr(cmd, RSTART + 1, RLENGTH - 1); sub(/^[ \t]+/, "", a[2]) }
        if (match(cmd, />[ \t]*[^ \t<>|]+/)) { a[3] = substr(cmd, RSTART + 1, RLENGTH - 1); sub(/^[ \t]+/, "", a[3]) }
        return "nmk_" a[1] " " a[2] " " a[3]
    }
    # copy: its /a and /b switches dropped, and "a+b" spaced as "a + b"
    if (tolower(cmd) ~ /^copy[ \t]/) {
        gsub(/[ \t]+\/[AaBb]([ \t]|$)/, " ", cmd)
        gsub(/[ \t]*\+[ \t]*/, " + ", cmd)
        sub(/[ \t]+$/, "", cmd)
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
    # attrib: the file attributes (the archive bit) do not matter here
    if (tolower(a[1]) == "attrib")
        return ":"
    if (tolower(a[1]) == "del" || tolower(a[1]) == "erase")
        return "nmk_del " substr(cmd, length(a[1]) + 2)
    if (tolower(a[1]) == "md" || tolower(a[1]) == "mkdir")
        return "nmk_md " substr(cmd, length(a[1]) + 2)
    if (tolower(a[1]) == "delnode") {
        sub(/^[^ \t]+[ \t]+([-\/][Qq][ \t]+)?/, "", cmd)
        return "nmk_delnode " cmd
    }
    if (tolower(a[1]) == "lib")
        return "nmk_lib " substr(cmd, 5)
    if (tolower(a[1]) == "link" && tolower(a[2]) ~ /^[-\/]lib$/) {
        sub(/^[^ \t]+[ \t]+[^ \t]+[ \t]*/, "", cmd)
        return "nmk_lib " cmd
    }
    if (tolower(a[1]) == "mc")
        return "nmk_mc " substr(cmd, 4)
    # (an include list -I.;..\inc is one argument: ; is no command separator for nmake)
    if (a[1] == "midl" || tolower(a[1]) == "midl.exe") {
        cmd = substr(cmd, length(a[1]) + 2); gsub(/;/, "\\;", cmd)
        return "nmk_midl " cmd
    }
    if (a[1] == "hextract")
        return "nmk_hextract " substr(cmd, 10)
    if (a[1] == "hsplit")
        return "nmk_hsplit " substr(cmd, 8)
    if (a[1] == "type" && a[3] == ">>")
        return "nmk_cat " a[2] " " a[4]
    if (a[1] == "type" && a[3] == ">")
        return "nmk_type " a[2] " " a[4]
    if (tolower(a[1]) == "mapmsg") {
        sub(/[ \t]*>[ \t]*/, " \\> ", cmd)
        return "nmk_mapmsg " substr(cmd, 8)
    }
    if (tolower(a[1]) == "listmung") {
        sub(/[ \t]*>[ \t]*/, " \\> ", cmd)
        return "nmk_listmung " substr(cmd, 10)
    }
    if (tolower(a[1]) == "cl") {
        sub(/[ \t]*>[ \t]*/, " \\> ", cmd)
        return "nmk_cpp " substr(cmd, 4)
    }
    if (a[1] == "rcpp" || a[1] == "solidpp" || a[1] == "binplace" || a[1] == "link32" || a[1] == "uixport" || a[1] == "gensrv" || a[1] == "guidlib" || a[1] == "kbdtool" || a[1] == "prodfilt" || a[1] == "xdosnet" || a[1] == "mibcc")
        return "nmk_" cmd
    # a generator: the environment of nmake (set NAME=), the object nmake makes, its link and
    # the program the recipe made, run
    if (tolower(a[1]) == "set")
        return ":"
    if (tolower(a[1]) == "nmake")
        return "nmk_nmake " substr(cmd, 7)
    if (tolower(a[1]) == "link")
        return "nmk_link " substr(cmd, 6)
    if (tolower(a[1]) ~ /^obj\/[^\/]+\/[^\/]+\.exe$/)
        return "nmk_run " cmd
    return "echo \x27nmk: unsupported command: " cmd "\x27 >&2; exit 1"
}

# A directory of an inference rule ({..\}, {$O}; {} the current one), as the paths of targets are.
function ruledir(d) {
    d = expand(d); sub(/\/+$/, "", d)
    return d == "" ? "." : d
}

# The commands of an inference rule .FROM.TO or {FROMDIR}.FROM{TODIR}.TO for the target, $< (and
# $** and $?) its source, $* its base: the source of the base of a target in TODIR in FROMDIR (either
# directory the current one if not named), of a rule of no directories that of the target.
function infer(    r, s, fe, te, fd, td, paths, ext, dir, base, src, i, l) {
    ext = tgt; sub(/^.*\./, "", ext)
    dir = tgt; if (!sub(/\/[^\/]*$/, "", dir)) dir = "."
    for (r in infn) {
        s = r; fd = td = ""; paths = r ~ /[{]/
        if (substr(s, 1, 1) == "{") { fd = substr(s, 2, index(s, "}") - 2); s = substr(s, index(s, "}") + 1) }
        fe = te = substr(s, 2)
        if ((i = index(fe, "{")) > 0) {
            td = substr(fe, i + 1); fe = substr(fe, 1, i - 1)
            te = substr(td, index(td, "}") + 2); td = substr(td, 1, index(td, "}") - 1)
        } else { sub(/\..*/, "", fe); sub(/^[^.]*\./, "", te) }
        if (tolower(te) != tolower(ext) || paths && tolower(ruledir(td)) != tolower(dir))
            continue
        base = substr(tgt, 1, length(tgt) - length(ext) - 1)
        b = base; sub(/^.*\//, "", b)
        src = paths ? ruledir(fd) "/" b "." fe : base "." fe
        sub(/^\.\//, "", src)
        if (ENVIRON["NMK_DEPS"]) { print src; return }
        s = substr(src, 1, length(src) - length(fe) - 1)
        for (i = 1; i <= infn[r]; i++) {
            l = infc[r, i]
            if (substr(l, 1, 1) == "\001") {
                l = substr(l, 2)
                print (l == "<<" ? "NMK_EOF" : text(l))
                continue
            }
            gsub(/\$\(<B\)/, b, l); gsub(/\$\(<F\)/, src, l); gsub(/\$\(@B\)/, b, l); gsub(/\$\(@F\)/, tgt, l)
            gsub(/\$\(<R\)/, s, l); gsub(/\$\(@R\)/, base, l)
            gsub(/\$\(\*\*\)/, src, l); gsub(/\$\*\*/, src, l)
            gsub(/\$</, src, l); gsub(/\$\?/, src, l); gsub(/\$\*/, base, l); gsub(/\$@/, tgt, l)
            l = expand(l); sub(/^[ \t]+/, "", l)
            split(l, a, /[ \t]+/)
            if (a[1] == "type" && a[2] == "<<") {
                print (a[3] == ">" ? "nmk_new " : "nmk_add ") a[4] " <<'NMK_EOF'"
                continue
            }
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
    print text(tnames($0, target, ""))
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

# The lines of an inline file (<<) of an inference rule, as they are.
infinline { infc[inf, ++infn[inf]] = "\001" $0; if ($0 == "<<") infinline = 0; next }

# Comment lines, in and out of recipes.
/^#/ { next }

# Inference rules (.FROM.TO:) and their commands, for a target without a rule of its own.
ininf && /^[ \t]+[^ \t]/ { infc[inf, ++infn[inf]] = $0; if ($0 ~ /<<[ \t]*(>|$)/) infinline = 1; next }
ininf { ininf = 0 }
!inrecipe && /^([{][^}]*[}])?\.[A-Za-z0-9]+([{][^}]*[}])?\.[A-Za-z0-9]+[ \t]*:/ {
    inf = $0; sub(/[ \t]*:.*/, "", inf); infn[inf] = 0; ininf = 1
    next
}

# Macro definitions, with backslash continuation.
!inrecipe && cont != "" {
    line = $0; more = sub(/\\$/, "", line)
    mac[cont] = mac[cont] " " selfref(line, cont, earlier)
    if (!more) cont = ""
    next
}

!inrecipe && /^[A-Za-z_][A-Za-z0-9_]*[ \t]*=/ {
    name = $0; sub(/[ \t]*=.*/, "", name)
    val = $0; sub(/^[^=]*=[ \t]*/, "", val)
    more = sub(/\\$/, "", val)
    earlier = (name in mac) ? mac[name] : ""
    mac[name] = selfref(val, name, earlier)
    cont = more ? name : ""
    next
}

# The rule of the target (its dependents continued over lines ending in a backslash).
!inrecipe {
    c = colon($0)
    head = substr($0, 1, c - 1); sub(/[ \t]+$/, "", head)
    if (c && names(expand(head), expand(target))) {
        tgt = target; gsub(/\\/, "/", tgt)
        deps = substr($0, c + 1); sub(/^:?[ \t]*/, "", deps)
        headcont = sub(/\\[ \t]*$/, "", deps)
        inrecipe = 1
        matched = 1
        if (ENVIRON["NMK_RULE"]) { print expand(head); exit }
        if (!headcont && ENVIRON["NMK_DEPS"]) { print expand(tnames(deps, tgt, "$")); exit }
        next
    }
}

inrecipe && headcont {
    l = $0; headcont = sub(/\\[ \t]*$/, "", l); deps = deps " " l
    if (!headcont && ENVIRON["NMK_DEPS"]) { print expand(tnames(deps, tgt, "$")); exit }
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
    gsub(/\$\?/, deps, l)
    line = expand(tnames(l, tgt, ""))
    gsub(/[ \t]*[12]?>[ \t]*[Nn][Uu][Ll]([ \t]*2>&1)?/, "", line)
    sub(/^[ \t]+/, "", line)
    # (a command of macros that are not defined is none: $(BINPLACE_CMD))
    if (line == "")
        next
    n = split(line, a, /[ \t]+/)
    if (a[1] == "type" && a[2] == "<<") {
        print (a[3] == ">" ? "nmk_new " : "nmk_add ") a[4] " <<'NMK_EOF'"
        ncmds++
        inline = 1
        next
    }
    # echo: a message, but "echo text > file" and "echo text >> file" write the text, with the
    # blanks before the >, as a line of the file
    if (a[1] ~ /^@?ech(o)?$/) {
        if (match(line, />>?[ \t]*[^ \t>]+[ \t]*$/) && substr(line, RSTART - 1, 1) != "^") {
            efile = substr(line, RSTART); etext = substr(line, 1, RSTART - 1)
            eop = efile ~ /^>>/ ? "nmk_echo -a " : "nmk_echo -n "
            sub(/^>>?[ \t]*/, "", efile); sub(/[ \t]+$/, "", efile)
            sub(/^@?ech(o)?[ \t]?/, "", etext); gsub(/\x27/, "\x27\\\x27\x27", etext)
            print eop efile " \x27" etext "\x27"
            ncmds++
        }
        next
    }
    # a command with an inline file (link @<<, its response file): the file is its input
    if (line ~ /@<<[ \t]*$/) {
        sub(/[ \t]*@<<[ \t]*$/, "", line)
        print command(line) " <<'NMK_EOF'"
        ncmds++
        inline = 1
        next
    }
    n = split(line, part, /[ \t]*&[ \t]*/)
    out = command(part[1])
    if (out ~ /^if nmk_exists/ && n > 1) {
        sub(/; fi$/, "", out)
        for (i = 2; i <= n; i++) out = out "; " command(part[i])
        out = out "; fi"
    } else
        for (i = 2; i <= n; i++) out = out "; " command(part[i])
    print out
    ncmds++
}

# A target without a rule, or whose rule has no commands, takes an inference rule.
END {
    if (ENVIRON["NMK_RULE"]) exit !matched
    if (!matched || !ncmds) infer()
}
