/*++

Module Name:

    rthelpc.c

Abstract:

    The 64-bit integer divisions of the run-time helpers of Microsoft C for ARM,
    worked in C (rthelp.asm gives them their calling convention). None of them
    uses an operation the compiler would call one of these helpers for. The
    conversions between 64-bit integers and floating point are in llcvt.c, apart:
    an image that divides need not take the floating point of the run-time.

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
