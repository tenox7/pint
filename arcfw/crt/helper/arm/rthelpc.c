/*++

Module Name:

    rthelpc.c

Abstract:

    The run-time helpers of Microsoft C for ARM worked in C: the 64-bit integer
    divisions (rthelp.asm gives them their calling convention) and the
    conversions between 64-bit integers and floating point, which the floating
    point unit does not do. None of them uses an operation the compiler would
    call one of these helpers for. A conversion out of range saturates and a NaN
    converts to zero, as the conversions of the floating point unit do.

--*/

typedef unsigned long ULONG;
typedef unsigned __int64 ULONGLONG;
typedef __int64 LONGLONG;

unsigned int _CountLeadingZeros(unsigned long);
#pragma intrinsic(_CountLeadingZeros)

static int
Clz64 (
    ULONGLONG Value
    )
{
    if ((ULONG)(Value >> 32) != 0) {
        return _CountLeadingZeros((ULONG)(Value >> 32));
    }
    return 32 + _CountLeadingZeros((ULONG)Value);
}

ULONGLONG
RtlpDivideUnsigned64 (
    ULONGLONG Dividend,
    ULONGLONG Divisor,
    ULONGLONG *Remainder
    )
{
    ULONGLONG Quotient = 0;
    ULONGLONG Bit;
    int Shift;

    if (Divisor > Dividend) {
        *Remainder = Dividend;
        return 0;
    }
    if ((ULONG)(Dividend >> 32) == 0) {
        *Remainder = (ULONG)Dividend % (ULONG)Divisor;
        return (ULONG)Dividend / (ULONG)Divisor;
    }
    Shift = Clz64(Divisor) - Clz64(Dividend);
    Divisor <<= Shift;
    Bit = (ULONGLONG)1 << Shift;
    while (Bit != 0) {
        if (Dividend >= Divisor) {
            Dividend -= Divisor;
            Quotient |= Bit;
        }
        Divisor >>= 1;
        Bit >>= 1;
    }
    *Remainder = Dividend;
    return Quotient;
}

LONGLONG
RtlpDivideSigned64 (
    LONGLONG Dividend,
    LONGLONG Divisor,
    LONGLONG *Remainder
    )
{
    ULONGLONG Quotient;
    ULONGLONG Rest;
    int Negative = 0;
    ULONGLONG N = (ULONGLONG)Dividend;
    ULONGLONG D = (ULONGLONG)Divisor;

    if (Dividend < 0) {
        N = 0 - N;
        Negative = 1;
    }
    if (Divisor < 0) {
        D = 0 - D;
        Negative ^= 1;
    }
    Quotient = RtlpDivideUnsigned64(N, D, &Rest);
    *Remainder = Dividend < 0 ? (LONGLONG)(0 - Rest) : (LONGLONG)Rest;
    return Negative ? (LONGLONG)(0 - Quotient) : (LONGLONG)Quotient;
}

typedef union _DBITS {
    double D;
    ULONGLONG U;
} DBITS;

//
// The magnitude of a double truncated to a 64-bit integer, saturated at the
// limit given; a NaN is zero.
//

static ULONGLONG
DoubleMagnitude (
    double Value,
    int *Negative,
    ULONGLONG Limit
    )
{
    DBITS Bits;
    int Exponent;
    ULONGLONG Mantissa;

    Bits.D = Value;
    *Negative = (int)(ULONG)(Bits.U >> 63);
    Exponent = (int)((ULONG)(Bits.U >> 52) & 0x7ff) - 1023;
    Mantissa = (Bits.U & 0xfffffffffffffULL) | 0x10000000000000ULL;
    if (Exponent < 0) {
        return 0;
    }
    if (Exponent == 1024 && (Bits.U & 0xfffffffffffffULL) != 0) {
        return 0;
    }
    if (Exponent > 63) {
        return Limit;
    }
    Mantissa = Exponent >= 52 ? Mantissa << (Exponent - 52) : Mantissa >> (52 - Exponent);
    return Mantissa > Limit ? Limit : Mantissa;
}

LONGLONG
__dtoi64 (
    double Value
    )
{
    int Negative;
    ULONGLONG Magnitude = DoubleMagnitude(Value, &Negative, 0x7fffffffffffffffULL);

    if (Negative) {
        Magnitude = DoubleMagnitude(Value, &Negative, 0x8000000000000000ULL);
        return (LONGLONG)(0 - Magnitude);
    }
    return (LONGLONG)Magnitude;
}

ULONGLONG
__dtou64 (
    double Value
    )
{
    int Negative;
    ULONGLONG Magnitude = DoubleMagnitude(Value, &Negative, ~0ULL);

    return Negative ? 0 : Magnitude;
}

LONGLONG
__stoi64 (
    float Value
    )
{
    return __dtoi64((double)Value);
}

ULONGLONG
__stou64 (
    float Value
    )
{
    return __dtou64((double)Value);
}

double
__u64tod (
    ULONGLONG Value
    )
{
    return (double)(ULONG)(Value >> 32) * 4294967296.0 + (double)(ULONG)Value;
}

double
__i64tod (
    LONGLONG Value
    )
{
    return (double)(long)(ULONG)((ULONGLONG)Value >> 32) * 4294967296.0 + (double)(ULONG)Value;
}

//
// To single precision with a single rounding: a magnitude of more than 53 bits is
// shifted into 53, the bits shifted out kept as a sticky low bit, so that the
// double holds it exactly and rounds to single precision as the integer would.
//

static float
U64ToFloat (
    ULONGLONG Value
    )
{
    int Shift = 0;
    double Result;

    if ((Value >> 53) != 0) {
        Shift = 11 - Clz64(Value);
        Value = (Value >> Shift) | ((Value & (((ULONGLONG)1 << Shift) - 1)) != 0);
    }
    Result = __u64tod(Value);
    while (Shift-- > 0) {
        Result *= 2.0;
    }
    return (float)Result;
}

float
__u64tos (
    ULONGLONG Value
    )
{
    return U64ToFloat(Value);
}

float
__i64tos (
    LONGLONG Value
    )
{
    return Value < 0 ? -U64ToFloat(0 - (ULONGLONG)Value) : U64ToFloat((ULONGLONG)Value);
}
