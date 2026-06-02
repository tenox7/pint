/*++

Copyright (c) 2026

Module Name:

    rtlarm.c

Abstract:

    ARMv7-A runtime-library arch primitives. NT declares these in NTRTL.H for
    non-x86 (x86 gets inline _asm) and supplies the implementation per-arch
    (i386 asm / a MIPS rtl asm file). These are the arch half of the RTL link
    closure: the 64-bit large-integer math, the bulk memory ops, and the
    context/exception entry points.

    The four DIVIDE primitives (RtlEnlargedUnsignedDivide / RtlExtended*Divide /
    RtlLargeIntegerDivide) are deferred: 64-bit division emits a libgcc helper
    (__aeabi_uldivmod) that this freestanding ld-only link does not provide yet -
    they come with a software-divmod (or __aeabi) runtime step. Add/sub/shift/
    multiply are inlined by gcc, so they need no runtime support.

Environment:

    Kernel mode.

--*/

#include "ki.h"
#include "kiseh.h"

//
// ---- Large-integer math (64-bit; add/sub/shift/multiply inline, no libcall) ----
//

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
// ---- Bulk memory ----
//

#undef RtlFillMemory
#undef RtlMoveMemory
#undef RtlCompareMemory

VOID
RtlFillMemory (PVOID Destination, ULONG Length, UCHAR Fill)
{
    volatile UCHAR *d = (volatile UCHAR *)Destination;
    while (Length--)
        *d++ = Fill;
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

//
// ---- Context / exception entry points ----
// Software-raise routes into the ARM32 SEH dispatcher (ke/seh.c), the faithful
// mechanism for this port (no compiler MSVC SEH). The full RtlDispatchException
// / RtlVirtualUnwind context-record path is a later refinement.
//

VOID
RtlRaiseStatus (IN NTSTATUS Status)
{
    KiSehRaise((unsigned long)Status);
}

VOID
RtlRaiseException (IN PEXCEPTION_RECORD ExceptionRecord)
{
    KiSehRaise((unsigned long)ExceptionRecord->ExceptionCode);
}

VOID
RtlCaptureContext (OUT PCONTEXT ContextRecord)
{
    //
    // Capture the integer + control registers of the caller. Minimal but
    // honest: sp/lr/pc and the callee-saved set; the SEH path uses setjmp, so
    // this is for the eventual RtlVirtualUnwind callers.
    //
    ContextRecord->ContextFlags = CONTEXT_FULL;
    __asm__ __volatile__("str r0,  %0" : "=m"(ContextRecord->R0));
    __asm__ __volatile__("str r4,  %0" : "=m"(ContextRecord->R4));
    __asm__ __volatile__("str r5,  %0" : "=m"(ContextRecord->R5));
    __asm__ __volatile__("str r6,  %0" : "=m"(ContextRecord->R6));
    __asm__ __volatile__("str r7,  %0" : "=m"(ContextRecord->R7));
    __asm__ __volatile__("str r8,  %0" : "=m"(ContextRecord->R8));
    __asm__ __volatile__("str r9,  %0" : "=m"(ContextRecord->R9));
    __asm__ __volatile__("str r10, %0" : "=m"(ContextRecord->R10));
    __asm__ __volatile__("str r11, %0" : "=m"(ContextRecord->R11));
    __asm__ __volatile__("str sp,  %0" : "=m"(ContextRecord->Sp));
    __asm__ __volatile__("str lr,  %0" : "=m"(ContextRecord->Lr));
    __asm__ __volatile__("str lr,  %0" : "=m"(ContextRecord->Pc));   // return address
    __asm__ __volatile__("mrs r0, cpsr ; str r0, %0" : "=m"(ContextRecord->Cpsr) :: "r0");
}

VOID
RtlInitializeContext (IN HANDLE Process, OUT PCONTEXT Context,
                      IN PVOID Parameter OPTIONAL, IN PVOID InitialPc OPTIONAL,
                      IN PVOID InitialSp OPTIONAL)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_FULL;
    Context->R0 = (ULONG)Parameter;             // first argument to the thread start
    Context->Pc = (ULONG)InitialPc;
    Context->Sp = (ULONG)InitialSp;
    Context->Cpsr = 0x13;                        // SVC mode, interrupts enabled
}
