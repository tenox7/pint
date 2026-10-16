/*++

Module Name:

    movemem.c

Abstract:

    This module implements the memory move, fill, zero and compare routines
    (the ARM port of RTL\MIPS\XXMVMEM.S).

--*/

#include <ntos.h>

#undef RtlFillMemory
#undef RtlMoveMemory
#undef RtlCompareMemory
#undef RtlZeroMemory

VOID
RtlFillMemory (PVOID Destination, ULONG Length, UCHAR Fill)
{
    volatile UCHAR *d = (volatile UCHAR *)Destination;
    while (Length--)
        *d++ = Fill;
}

VOID
RtlZeroMemory (PVOID Destination, ULONG Length)
{
    volatile UCHAR *d = (volatile UCHAR *)Destination;
    while (Length--)
        *d++ = 0;
}

VOID
RtlFillMemoryUlong (PVOID Destination, ULONG Length, ULONG Pattern)
{
    PULONG d = (PULONG)Destination;
    Length /= sizeof(ULONG);
    while (Length--)
        *d++ = Pattern;
}

VOID
RtlMoveMemory (PVOID Destination, CONST VOID *Source, ULONG Length)
{
    UCHAR *d = (UCHAR *)Destination;
    const UCHAR *s = (const UCHAR *)Source;
    if (d <= s || d >= s + Length) {
        while (Length--) *d++ = *s++;
    } else {
        d += Length; s += Length;
        while (Length--) *--d = *--s;
    }
}

ULONG
RtlCompareMemory (PVOID Source1, PVOID Source2, ULONG Length)
{
    const UCHAR *p1 = (const UCHAR *)Source1;
    const UCHAR *p2 = (const UCHAR *)Source2;
    ULONG n = 0;
    while (n < Length && p1[n] == p2[n])
        n++;
    return n;
}
