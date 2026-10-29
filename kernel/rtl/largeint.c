/*++

Module Name:

    largeint.c

Abstract:

    This module implements the large integer arithmetic routines (the ARM
    port of RTL\MIPS\LARGEINT.S).

--*/

#include <ntos.h>

LARGE_INTEGER
RtlLargeIntegerAdd (IN LARGE_INTEGER Addend1, IN LARGE_INTEGER Addend2)
{
    LARGE_INTEGER r;
    r.QuadPart = Addend1.QuadPart + Addend2.QuadPart;
    return r;
}

LARGE_INTEGER
RtlLargeIntegerSubtract (IN LARGE_INTEGER Minuend, IN LARGE_INTEGER Subtrahend)
{
    LARGE_INTEGER r;
    r.QuadPart = Minuend.QuadPart - Subtrahend.QuadPart;
    return r;
}

LARGE_INTEGER
RtlLargeIntegerNegate (IN LARGE_INTEGER Subtrahend)
{
    LARGE_INTEGER r;
    r.QuadPart = -Subtrahend.QuadPart;
    return r;
}

LARGE_INTEGER
RtlLargeIntegerShiftLeft (IN LARGE_INTEGER LargeInteger, IN CCHAR ShiftCount)
{
    LARGE_INTEGER r;
    r.QuadPart = LargeInteger.QuadPart << (ShiftCount & 0x3F);
    return r;
}

LARGE_INTEGER
RtlLargeIntegerShiftRight (IN LARGE_INTEGER LargeInteger, IN CCHAR ShiftCount)
{
    LARGE_INTEGER r;
    r.QuadPart = (LONGLONG)((ULONGLONG)LargeInteger.QuadPart >> (ShiftCount & 0x3F));
    return r;
}

LARGE_INTEGER
RtlLargeIntegerArithmeticShift (IN LARGE_INTEGER LargeInteger, IN CCHAR ShiftCount)
{
    LARGE_INTEGER r;
    r.QuadPart = LargeInteger.QuadPart >> (ShiftCount & 0x3F);   // signed -> arithmetic
    return r;
}

LARGE_INTEGER
RtlConvertLongToLargeInteger (IN LONG SignedInteger)
{
    LARGE_INTEGER r;
    r.QuadPart = (LONGLONG)SignedInteger;
    return r;
}

LARGE_INTEGER
RtlConvertUlongToLargeInteger (IN ULONG UnsignedInteger)
{
    LARGE_INTEGER r;
    r.QuadPart = (LONGLONG)UnsignedInteger;
    return r;
}

LARGE_INTEGER
RtlEnlargedIntegerMultiply (IN LONG Multiplicand, IN LONG Multiplier)
{
    LARGE_INTEGER r;
    r.QuadPart = (LONGLONG)Multiplicand * (LONGLONG)Multiplier;
    return r;
}

LARGE_INTEGER
RtlEnlargedUnsignedMultiply (IN ULONG Multiplicand, IN ULONG Multiplier)
{
    LARGE_INTEGER r;
    r.QuadPart = (LONGLONG)((ULONGLONG)Multiplicand * (ULONGLONG)Multiplier);
    return r;
}

LARGE_INTEGER
RtlExtendedIntegerMultiply (IN LARGE_INTEGER Multiplicand, IN LONG Multiplier)
{
    LARGE_INTEGER r;
    r.QuadPart = Multiplicand.QuadPart * (LONGLONG)Multiplier;
    return r;
}

//
// The high 64 bits of the 128-bit product of two unsigned 64-bit values.
//

static ULONGLONG
RtlpMultiplyHigh (ULONGLONG a, ULONGLONG b)
{
    ULONG al = (ULONG)a, ah = (ULONG)(a >> 32);
    ULONG bl = (ULONG)b, bh = (ULONG)(b >> 32);
    ULONGLONG ll = (ULONGLONG)al * bl;
    ULONGLONG lh = (ULONGLONG)al * bh;
    ULONGLONG hl = (ULONGLONG)ah * bl;
    ULONGLONG hh = (ULONGLONG)ah * bh;
    ULONGLONG mid = (ll >> 32) + (lh & 0xFFFFFFFF) + (hl & 0xFFFFFFFF);
    return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

ULONG
RtlEnlargedUnsignedDivide (IN ULARGE_INTEGER Dividend, IN ULONG Divisor, IN PULONG Remainder)
{
    if (Remainder) *Remainder = (ULONG)(Dividend.QuadPart % Divisor);
    return (ULONG)(Dividend.QuadPart / Divisor);
}

LARGE_INTEGER
RtlExtendedLargeIntegerDivide (IN LARGE_INTEGER Dividend, IN ULONG Divisor, IN PULONG Remainder)
{
    LARGE_INTEGER r;
    r.QuadPart = (LONGLONG)((ULONGLONG)Dividend.QuadPart / Divisor);
    if (Remainder) *Remainder = (ULONG)((ULONGLONG)Dividend.QuadPart % Divisor);
    return r;
}

LARGE_INTEGER
RtlLargeIntegerDivide (IN LARGE_INTEGER Dividend, IN LARGE_INTEGER Divisor, IN PLARGE_INTEGER Remainder)
{
    LARGE_INTEGER r;
    r.QuadPart = Dividend.QuadPart / Divisor.QuadPart;
    if (Remainder) Remainder->QuadPart = Dividend.QuadPart % Divisor.QuadPart;
    return r;
}

//
// Divide a signed large integer by reciprocal multiplication with the most
// significant 64 bits of the reciprocal divisor, then truncate the fraction.
//

LARGE_INTEGER
RtlExtendedMagicDivide (IN LARGE_INTEGER Dividend, IN LARGE_INTEGER MagicDivisor, IN CCHAR ShiftCount)
{
    LARGE_INTEGER r;
    BOOLEAN Negative = (BOOLEAN)(Dividend.QuadPart < 0);
    ULONGLONG Value = Negative ? -(ULONGLONG)Dividend.QuadPart : (ULONGLONG)Dividend.QuadPart;

    Value = RtlpMultiplyHigh(Value, (ULONGLONG)MagicDivisor.QuadPart) >> ShiftCount;
    r.QuadPart = Negative ? -(LONGLONG)Value : (LONGLONG)Value;
    return r;
}
