/*
   Unicode text in the Windows ANSI character set, see htansi.h.

   Characters the NT 3.1 fonts have come out as code page 1252 bytes
   (Latin-1 plus the quotes, dashes, bullet and so on at 0x80-0x9F that
   Windows 3.1 added).  Others get an ASCII stand-in, or "?".
 */

#include "all.h"

/* U+0100 - U+017F (Latin Extended-A): the letter without its accent */
static CONST char szLatinA[] =
    "AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi"
    "IiIiJjKkkLlLlLlL" "lLlNnNnNnnNnOoOo" "\326\366\214\234RrRrRrSsSsSs"
    "\212\232TtTtTtUuUuUuUu" "\334\374UuWwYy\237ZzZzZzs";

/* Other characters, by code point */
static CONST struct
{
    unsigned short cp;
    char s[4];
}
aMap[] =
{
    { 0x0192, "\203" },     /* f with hook */
    { 0x02BC, "'" },
    { 0x02C6, "\210" },     /* circumflex */
    { 0x02DC, "\230" },     /* small tilde */
    { 0x03BC, "\265" },     /* mu: micro sign */
    { 0x2010, "-" },        /* hyphens and dashes */
    { 0x2011, "-" },
    { 0x2012, "-" },
    { 0x2013, "\226" },
    { 0x2014, "\227" },
    { 0x2015, "\227" },
    { 0x2018, "\221" },     /* quotes */
    { 0x2019, "\222" },
    { 0x201A, "\202" },
    { 0x201B, "'" },
    { 0x201C, "\223" },
    { 0x201D, "\224" },
    { 0x201E, "\204" },
    { 0x201F, "\"" },
    { 0x2020, "\206" },     /* daggers, bullets, ellipsis */
    { 0x2021, "\207" },
    { 0x2022, "\225" },
    { 0x2023, "\225" },
    { 0x2024, "." },
    { 0x2025, ".." },
    { 0x2026, "\205" },
    { 0x2027, "\267" },
    { 0x2030, "\211" },     /* per mille */
    { 0x2032, "'" },        /* primes */
    { 0x2033, "\"" },
    { 0x2039, "\213" },     /* single angle quotes */
    { 0x203A, "\233" },
    { 0x203E, "-" },        /* overline */
    { 0x2044, "/" },        /* fraction slash */
    { 0x20AC, "EUR" },      /* euro: newer than these fonts */
    { 0x2116, "No" },
    { 0x2122, "\231" },     /* trade mark */
    { 0x2190, "<-" },       /* arrows */
    { 0x2191, "^" },
    { 0x2192, "->" },
    { 0x2193, "v" },
    { 0x2194, "<->" },
    { 0x21D0, "<=" },
    { 0x21D2, "=>" },
    { 0x21D4, "<=>" },
    { 0x2212, "-" },        /* maths */
    { 0x2215, "/" },
    { 0x2217, "*" },
    { 0x2219, "\267" },
    { 0x2248, "~" },
    { 0x2260, "!=" },
    { 0x2264, "<=" },
    { 0x2265, ">=" },
    { 0x22C5, "\267" },
    { 0x25B2, "^" },        /* triangles and circles */
    { 0x25B6, ">" },
    { 0x25B8, ">" },
    { 0x25BA, ">" },
    { 0x25BC, "v" },
    { 0x25C0, "<" },
    { 0x25C2, "<" },
    { 0x25C4, "<" },
    { 0x25CB, "o" },
    { 0x25CF, "\225" },
    { 0x25E6, "o" },
    { 0x2605, "*" },        /* stars, boxes, ticks and crosses */
    { 0x2606, "*" },
    { 0x2610, "[ ]" },
    { 0x2611, "[x]" },
    { 0x2612, "[x]" },
    { 0x2713, "v" },
    { 0x2714, "v" },
    { 0x2715, "x" },
    { 0x2716, "x" },
    { 0x2717, "x" },
    { 0x2718, "x" },
};

/* Named character references the HTML DTD (htmlpdtd.c) does not have;
   sorted for strcmp. */
static CONST struct
{
    char name[8];
    unsigned short cp;
}
aEntities[] =
{
    { "Dagger", 8225 }, { "OElig", 338 }, { "Prime", 8243 }, { "Scaron", 352 },
    { "Yuml", 376 }, { "acute", 180 }, { "apos", 39 }, { "asymp", 8776 },
    { "bdquo", 8222 }, { "brvbar", 166 }, { "bull", 8226 }, { "cedil", 184 },
    { "cent", 162 }, { "check", 10003 }, { "circ", 710 }, { "cross", 10007 },
    { "curren", 164 }, { "dagger", 8224 }, { "darr", 8595 }, { "deg", 176 },
    { "divide", 247 }, { "emsp", 8195 }, { "ensp", 8194 }, { "euro", 8364 },
    { "fnof", 402 }, { "frac12", 189 }, { "frac14", 188 }, { "frac34", 190 },
    { "frasl", 8260 }, { "ge", 8805 }, { "hArr", 8660 }, { "harr", 8596 },
    { "hellip", 8230 }, { "iexcl", 161 }, { "iquest", 191 }, { "lArr", 8656 },
    { "laquo", 171 }, { "larr", 8592 }, { "ldquo", 8220 }, { "le", 8804 },
    { "lowast", 8727 }, { "lrm", 8206 }, { "lsaquo", 8249 }, { "lsquo", 8216 },
    { "macr", 175 }, { "mdash", 8212 }, { "micro", 181 }, { "middot", 183 },
    { "minus", 8722 }, { "mu", 956 }, { "ndash", 8211 }, { "ne", 8800 },
    { "not", 172 }, { "oelig", 339 }, { "oline", 8254 }, { "ordf", 170 },
    { "ordm", 186 }, { "para", 182 }, { "permil", 8240 }, { "plusmn", 177 },
    { "pound", 163 }, { "prime", 8242 }, { "rArr", 8658 }, { "raquo", 187 },
    { "rarr", 8594 }, { "rdquo", 8221 }, { "rlm", 8207 }, { "rsaquo", 8250 },
    { "rsquo", 8217 }, { "sbquo", 8218 }, { "scaron", 353 }, { "sdot", 8901 },
    { "sect", 167 }, { "star", 9734 }, { "starf", 9733 }, { "sup1", 185 },
    { "sup2", 178 }, { "sup3", 179 }, { "thinsp", 8201 }, { "tilde", 732 },
    { "times", 215 }, { "uarr", 8593 }, { "uml", 168 }, { "yen", 165 },
    { "zwj", 8205 }, { "zwnj", 8204 },
};

/*
    Writes the ANSI form of a Unicode code point to out (up to 3 chars,
    not terminated) and returns how many chars it wrote.  Invisible
    characters (joiners, marks, byte order mark) write nothing.
*/
int ANSI_FromUnicode(unsigned long cp, char *out)
{
    int low, high, i;
    CONST char *s;

    if (cp < 0x20)                      /* controls but tab and line ends */
    {
        if (cp != 9 && cp != 10 && cp != 13)
            return 0;
    }
    if (cp < 0x100 && cp != 0x7F)       /* ASCII, Latin-1, and 0x80-0x9F */
    {                                   /* as code page 1252 (as HTML does) */
        out[0] = (char) cp;
        return 1;
    }
    if (cp >= 0x100 && cp <= 0x17F)
    {
        out[0] = szLatinA[cp - 0x100];
        return 1;
    }
    if (   (cp >= 0x2000 && cp <= 0x200A)       /* spaces */
        || cp == 0x2028 || cp == 0x2029 || cp == 0x205F)
    {
        out[0] = ' ';
        return 1;
    }
    if (cp == 0x202F)                   /* narrow no-break space */
    {
        out[0] = (char) 0xA0;
        return 1;
    }
    if (   cp == 0x7F
        || (cp >= 0x0300 && cp <= 0x036F)       /* combining accents */
        || (cp >= 0x200B && cp <= 0x200F)       /* zero width, direction */
        || (cp >= 0x202A && cp <= 0x202E)
        || (cp >= 0x2060 && cp <= 0x2064)
        || (cp >= 0xFE00 && cp <= 0xFE0F)       /* variation selectors */
        || cp == 0xFEFF                         /* byte order mark */
        || (cp >= 0x1F3FB && cp <= 0x1F3FF)     /* skin tones */
        || (cp >= 0xE0000 && cp <= 0xE007F))    /* tags */
    {
        return 0;
    }

    s = "?";
    for (low = 0, high = sizeof(aMap) / sizeof(aMap[0]); high > low; )
    {
        i = (low + high) / 2;
        if (aMap[i].cp == cp)
        {
            s = aMap[i].s;
            break;
        }
        if (aMap[i].cp < cp)
            low = i + 1;
        else
            high = i;
    }
    for (i = 0; s[i]; i++)
        out[i] = s[i];
    return i;
}

/*
    Hands back the bytes of an unfinished UTF-8 sequence as they are
    (Latin-1), and forgets it.
*/
int ANSI_FlushUTF8(struct _ANSI_UTF8 *u, char *out)
{
    int i, n;

    n = u->have;
    for (i = 0; i < n; i++)
        out[i] = (char) u->buf[i];
    u->have = 0;
    u->need = 0;
    return n;
}

/*
    Feeds one byte of UTF-8 text.  Writes the ANSI chars that are now
    complete to out and returns how many (up to ANSI_MAX_CHARS).  Bytes
    that are not part of a valid UTF-8 sequence come back unchanged, so
    Latin-1 and code page 1252 text still reads right.
*/
int ANSI_PutUTF8(struct _ANSI_UTF8 *u, int c, char *out)
{
    int n = 0;

    c &= 0xFF;
    if (u->need)
    {
        if ((c & 0xC0) == 0x80)
        {
            u->buf[u->have++] = (unsigned char) c;
            u->cp = (u->cp << 6) | (c & 0x3F);
            if (--u->need)
            {
                return 0;
            }
            /* no overlong forms, surrogates, or values past U+10FFFF */
            if (   (u->have == 2)
                || (u->have == 3 && u->cp >= 0x800 && (u->cp < 0xD800 || u->cp > 0xDFFF))
                || (u->have == 4 && u->cp >= 0x10000 && u->cp <= 0x10FFFF))
            {
                u->have = 0;
                return ANSI_FromUnicode(u->cp, out);
            }
            return ANSI_FlushUTF8(u, out);
        }
        n = ANSI_FlushUTF8(u, out);     /* sequence broken off: c starts afresh */
    }

    if (c >= 0xC2 && c <= 0xF4)         /* lead byte */
    {
        u->buf[0] = (unsigned char) c;
        u->have = 1;
        if (c >= 0xF0)
        {
            u->need = 3;
            u->cp = c & 0x07;
        }
        else if (c >= 0xE0)
        {
            u->need = 2;
            u->cp = c & 0x0F;
        }
        else
        {
            u->need = 1;
            u->cp = c & 0x1F;
        }
        return n;
    }

    out[n++] = (char) c;
    return n;
}

/*
    Looks up a named character reference that is not in the DTD.  Returns
    its code point, or -1.
*/
long ANSI_FindEntity(CONST char *name)
{
    int low, high, i, diff;

    for (low = 0, high = sizeof(aEntities) / sizeof(aEntities[0]); high > low; )
    {
        i = (low + high) / 2;
        diff = strcmp(aEntities[i].name, name);
        if (diff == 0)
            return aEntities[i].cp;
        if (diff < 0)
            low = i + 1;
        else
            high = i;
    }
    return -1;
}

/*
    Pages are read as UTF-8 unless the server names another character
    set.  Text that is not valid UTF-8 passes through unchanged, so this
    is also safe for Latin-1 pages that do not say what they are.
*/
BOOL ANSI_ReadAsUTF8(HTRequest *request)
{
    CONST char *cs;

    if (!request || !request->atomCharset)
        return TRUE;
    cs = HTAtom_name(request->atomCharset);
    if (!cs)
        return TRUE;
    if (*cs == '"' || *cs == '\'')
        cs++;
    return (0 == GTR_strncmpi(cs, "utf-8", 5) || 0 == GTR_strncmpi(cs, "utf8", 4));
}
