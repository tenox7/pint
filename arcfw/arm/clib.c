//
// Freestanding C runtime for the loader: the string/memory routines the NT
// loader sources call (and that GCC may emit implicitly), a small sprintf, the
// BlPrint console helper, and RtlConvertUlongToLargeInteger (x86 _asm in the
// original NTRTL.H).
//
#include "string.h"
#include "stdio.h"
#include "bldr.h"

void console_puts(const char *s);

// ---------------------------------------------------------------- memory ----

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d == s || n == 0) return dst;
    if (d < s) { while (n--) *d++ = *s++; return dst; }
    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}

// ---------------------------------------------------------------- string ----

size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

char *strcpy(char *dst, const char *src) { char *d = dst; while ((*d++ = *src++)) ; return dst; }

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && (*d = *src)) { d++; src++; n--; }
    while (n--) *d++ = '\0';
    return dst;
}

char *strcat(char *dst, const char *src) { char *d = dst; while (*d) d++; while ((*d++ = *src++)) ; return dst; }

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- && (*d = *src)) { d++; src++; }
    *d = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (; *s; s++) if (*s == (char)c) last = s;
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return (char *)haystack;
    for (; *haystack; haystack++)
        if (*haystack == *needle && strncmp(haystack, needle, nl) == 0)
            return (char *)haystack;
    return NULL;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

int stricmp(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int strnicmp(const char *a, const char *b, size_t n)
{
    while (n && *a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

// ---------------------------------------------------------------- printf ----
//
// Minimal vsprintf: %% %c %s, signed/unsigned/hex integers with optional 'l'
// length and zero/space-padded field width (e.g. %08lx, %3d). Enough for the
// loader's diagnostics; extend as needed.
//
static char *emit_uint(char *out, unsigned long val, unsigned base, int upper)
{
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    while (val) { tmp[i++] = digits[val % base]; val /= base; }
    while (i) *out++ = tmp[--i];
    return out;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    char *out = buf;

    for (; *fmt; fmt++) {
        if (*fmt != '%') { *out++ = *fmt; continue; }
        fmt++;

        int zero = 0, width = 0, longf = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        while (*fmt == 'l' || *fmt == 'h') { if (*fmt == 'l') longf = 1; fmt++; }

        char numbuf[32];
        char *np;
        int neg = 0;
        unsigned long uv;
        long sv;

        switch (*fmt) {
        case '%': *out++ = '%'; break;
        case 'c': *out++ = (char)va_arg(ap, int); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            while (*s) *out++ = *s++;
            break;
        }
        case 'd':
        case 'i':
            sv = longf ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (sv < 0) { neg = 1; uv = (unsigned long)(-sv); } else uv = (unsigned long)sv;
            np = emit_uint(numbuf, uv, 10, 0);
            goto pad_num;
        case 'u':
            uv = longf ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            np = emit_uint(numbuf, uv, 10, 0);
            goto pad_num;
        case 'p':
            *out++ = '0'; *out++ = 'x';
            uv = (unsigned long)(unsigned long)va_arg(ap, void *);
            np = emit_uint(numbuf, uv, 16, 0);
            goto pad_num;
        case 'x':
        case 'X':
            uv = longf ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            np = emit_uint(numbuf, uv, 16, (*fmt == 'X'));
            goto pad_num;
        default:
            *out++ = '%';
            *out++ = *fmt;
            break;
        pad_num: {
            int len = (int)(np - numbuf) + neg;
            for (; len < width; len++) *out++ = zero ? '0' : ' ';
            if (neg) *out++ = '-';
            for (char *q = numbuf; q < np; q++) *out++ = *q;
            break;
        }
        }
    }

    *out = '\0';
    return (int)(out - buf);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

VOID BlPrint(PCHAR fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    console_puts(buf);
}

// ------------------------------------------------------------------- rtl ----

LARGE_INTEGER RtlConvertUlongToLargeInteger(IN ULONG UnsignedInteger)
{
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)(ULONGLONG)UnsignedInteger;
    return li;
}

//
// RtlInitString - point a counted STRING at a null-terminated C string (FATBOOT.C's
// FatOpen/FatRename use it to wrap the path). Length excludes the NUL, MaximumLength
// includes it; the buffer is not copied.
//
VOID RtlInitString(OUT PSTRING DestinationString, IN PCHAR SourceString)
{
    DestinationString->Buffer = SourceString;
    if (SourceString != NULL) {
        DestinationString->Length = (USHORT)strlen(SourceString);
        DestinationString->MaximumLength = (USHORT)(DestinationString->Length + 1);
    } else {
        DestinationString->Length = 0;
        DestinationString->MaximumLength = 0;
    }
}
