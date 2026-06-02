/*++

Copyright (c) 2026

Module Name:

    clib.c

Abstract:

    Freestanding C runtime for the executive link. The portable executive (and
    GCC's implicit calls) reference a handful of CRT string/memory/printf
    routines that ntoskrnl normally gets from the kernel-mode CRT (PUBLIC/SDK/LIB)
    - none of which is in our object set. This supplies exactly the set the link
    closure reports unresolved (exec-link-probe.sh): memcpy, the str and wcs
    string family, the sprintf group (sprintf, snprintf, swprintf, printf), max,
    plus atol / strupr that the INIT/Phase1 paths pull.

    EVERY symbol here is WEAK: if a genuine executive object defines the same
    routine (e.g. an RTL CRT object), the strong definition wins and this one is
    silently dropped - so adding clib can never introduce a multiple-definition
    error, only resolve an otherwise-undefined reference.

    Varargs go through GCC's __builtin_va_* directly (VA_* below): the farm's
    1994 MSVC <stdarg.h> does not yield a working va_arg macro in this header
    environment, and the builtins are ABI-correct for AAPCS regardless.

    Only the closure set is defined. memset/memmove/memcmp/strchr/strnicmp are
    deliberately omitted - they are already provided by a compiled object (not in
    the closure); defining them here would duplicate.

Environment:

    Kernel mode (executive bring-up), pre-CRT.

--*/

#include "ki.h"

#define WEAK __attribute__((weak))

#define VA_LIST  __builtin_va_list
#define VA_START __builtin_va_start
#define VA_ARG   __builtin_va_arg
#define VA_END   __builtin_va_end

// ---------------------------------------------------------------- memory ----

WEAK void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

// ---------------------------------------------------------------- string ----

WEAK size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

WEAK char *strcpy(char *dst, const char *src) { char *d = dst; while ((*d++ = *src++)) ; return dst; }

WEAK char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && (*d = *src)) { d++; src++; n--; }
    while (n--) *d++ = '\0';
    return dst;
}

WEAK char *strcat(char *dst, const char *src) { char *d = dst; while (*d) d++; while ((*d++ = *src++)) ; return dst; }

WEAK int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

WEAK int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

WEAK char *strstr(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return (char *)haystack;
    for (; *haystack; haystack++)
        if (*haystack == *needle && strncmp(haystack, needle, nl) == 0)
            return (char *)haystack;
    return NULL;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }
static int upper(int c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }

WEAK int stricmp(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

WEAK char *strupr(char *s) { char *p = s; for (; *p; p++) *p = (char)upper((unsigned char)*p); return s; }

WEAK long atol(const char *s)
{
    long v = 0; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

// ------------------------------------------------------------ wide string ----

WEAK size_t wcslen(const WCHAR *s) { const WCHAR *p = s; while (*p) p++; return (size_t)(p - s); }

WEAK int wcscmp(const WCHAR *a, const WCHAR *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}

WEAK int wcsncmp(const WCHAR *a, const WCHAR *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)*a - (int)*b;
}

static int wlower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

WEAK int wcsnicmp(const WCHAR *a, const WCHAR *b, size_t n)
{
    while (n && *a && wlower(*a) == wlower(*b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return wlower(*a) - wlower(*b);
}

WEAK WCHAR *wcsstr(const WCHAR *haystack, const WCHAR *needle)
{
    size_t nl = wcslen(needle);
    if (nl == 0) return (WCHAR *)haystack;
    for (; *haystack; haystack++)
        if (*haystack == *needle && wcsncmp(haystack, needle, nl) == 0)
            return (WCHAR *)haystack;
    return NULL;
}

// ---------------------------------------------------------------- printf ----
//
// Minimal vsnprintf: %% %c %s/%ws %Z/%wZ, signed/unsigned/hex integers with an
// optional 'l'/'h'/'w' length and zero/space field width. Enough for the
// executive's init/version diagnostics; bounded by the count argument.
//

static char *emit_uint(char *out, char *end, unsigned long val, unsigned base, int up)
{
    char tmp[32];
    const char *digits = up ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    while (val) { tmp[i++] = digits[val % base]; val /= base; }
    while (i) { if (out < end) *out = tmp[i - 1]; out++; i--; }
    return out;
}

static int kvsnprintf(char *buf, size_t count, const char *fmt, VA_LIST ap)
{
    char *out = buf;
    char *end = (count > 0) ? buf + count - 1 : buf;       // reserve one for NUL

#define PUT(ch) do { if (out < end) *out = (char)(ch); out++; } while (0)

    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;

        int zero = 0, width = 0, longf = 0, wide = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'w') {
            if (*fmt == 'l') longf = 1;
            if (*fmt == 'w') wide = 1;
            fmt++;
        }

        char numbuf[32];
        char *np;
        int neg = 0;
        unsigned long uv;
        long sv;

        switch (*fmt) {
        case '%': PUT('%'); break;
        case 'c': PUT((char)VA_ARG(ap, int)); break;
        case 's': {
            if (wide) {
                const WCHAR *ws = VA_ARG(ap, const WCHAR *);
                if (ws == NULL) ws = (const WCHAR *)L"(null)";
                while (*ws) PUT((char)*ws++);
                break;
            }
            const char *s = VA_ARG(ap, const char *);
            if (s == NULL) s = "(null)";
            while (*s) PUT(*s++);
            break;
        }
        case 'Z': {
            PUNICODE_STRING u = VA_ARG(ap, PUNICODE_STRING);
            if (u != NULL && u->Buffer != NULL) {
                unsigned i;
                if (wide) {
                    unsigned n = u->Length / sizeof(WCHAR);
                    for (i = 0; i < n; i++) PUT((char)u->Buffer[i]);
                } else {
                    const char *b = (const char *)u->Buffer;
                    for (i = 0; i < u->Length; i++) PUT(b[i]);
                }
            }
            break;
        }
        case 'd':
        case 'i':
            sv = longf ? VA_ARG(ap, long) : (long)VA_ARG(ap, int);
            if (sv < 0) { neg = 1; uv = (unsigned long)(-sv); } else uv = (unsigned long)sv;
            np = emit_uint(numbuf, numbuf + sizeof(numbuf), uv, 10, 0);
            goto pad_num;
        case 'u':
            uv = longf ? VA_ARG(ap, unsigned long) : (unsigned long)VA_ARG(ap, unsigned int);
            np = emit_uint(numbuf, numbuf + sizeof(numbuf), uv, 10, 0);
            goto pad_num;
        case 'p':
            PUT('0'); PUT('x');
            uv = (unsigned long)VA_ARG(ap, void *);
            np = emit_uint(numbuf, numbuf + sizeof(numbuf), uv, 16, 0);
            goto pad_num;
        case 'x':
        case 'X':
            uv = longf ? VA_ARG(ap, unsigned long) : (unsigned long)VA_ARG(ap, unsigned int);
            np = emit_uint(numbuf, numbuf + sizeof(numbuf), uv, 16, (*fmt == 'X'));
            goto pad_num;
        default:
            PUT('%'); PUT(*fmt); break;
        pad_num: {
            int len = (int)(np - numbuf) + neg;
            for (; len < width; len++) PUT(zero ? '0' : ' ');
            if (neg) PUT('-');
            for (char *q = numbuf; q < np; q++) PUT(*q);
            break;
        }
        }
    }

    if (count > 0) *((out < end) ? out : end) = '\0';
#undef PUT
    return (int)(out - buf);
}

WEAK int _vsnprintf(char *buf, size_t count, const char *fmt, VA_LIST ap)
{
    return kvsnprintf(buf, count, fmt, ap);
}

WEAK int _snprintf(char *buf, size_t count, const char *fmt, ...)
{
    VA_LIST ap; int n;
    VA_START(ap, fmt);
    n = kvsnprintf(buf, count, fmt, ap);
    VA_END(ap);
    return n;
}

WEAK int sprintf(char *buf, const char *fmt, ...)
{
    VA_LIST ap; int n;
    VA_START(ap, fmt);
    n = kvsnprintf(buf, (size_t)-1, fmt, ap);
    VA_END(ap);
    return n;
}

WEAK int swprintf(WCHAR *buf, const WCHAR *fmt, ...)
{
    // Wide sprintf used only by a couple of init/version paths. Narrow the
    // format to ASCII, format into a scratch buffer, widen the result.
    char nfmt[256], nout[512];
    char *d = nfmt; const WCHAR *f = fmt;
    while (*f && d < nfmt + sizeof(nfmt) - 1) *d++ = (char)*f++;
    *d = '\0';
    VA_LIST ap; int n;
    VA_START(ap, fmt);
    n = kvsnprintf(nout, sizeof(nout), nfmt, ap);
    VA_END(ap);
    int i;
    for (i = 0; i < n; i++) buf[i] = (WCHAR)(unsigned char)nout[i];
    buf[n] = 0;
    return n;
}

WEAK int printf(const char *fmt, ...)
{
    char buf[512];
    VA_LIST ap; int n;
    VA_START(ap, fmt);
    n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    VA_END(ap);
    HalDisplayString(buf);
    return n;
}

// ---------------------------------------------------------------- misc ----
//
// max: a CPP macro (windef.h) that a farm-mangled TU lost, so it emitted an
// implicit call. Provide the integer form (signature is irrelevant to the link).
//

WEAK int max(int a, int b) { return a > b ? a : b; }
