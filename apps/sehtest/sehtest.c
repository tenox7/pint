/*++

Module Name:

    sehtest.c

Abstract:

    A native NT program that exercises structured exception handling: filters
    and handlers of hardware and software exceptions, termination handlers in
    normal, leave and unwind exits, continuable exceptions, nested scopes, long
    jumps, and exceptions a system service raises and handles in kernel mode. The
    Session Manager runs it from BootExecute; it writes the result of each case
    on the boot screen.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <setjmp.h>

static ULONG Failures;
static ULONG Trace;

static VOID
Report (
    IN PCHAR Name,
    IN BOOLEAN Passed
    )
{
    CHAR Buffer[128];
    ANSI_STRING Ansi;
    UNICODE_STRING Unicode;

    Failures += !Passed;
    sprintf(Buffer, "sehtest: %s %s\n", Name, Passed ? "ok" : "FAILED");
    RtlInitAnsiString(&Ansi, Buffer);
    RtlAnsiStringToUnicodeString(&Unicode, &Ansi, TRUE);
    NtDisplayString(&Unicode);
    RtlFreeUnicodeString(&Unicode);
}

static LONG
Filter (
    IN NTSTATUS Code,
    IN NTSTATUS Expected,
    IN PULONG Seen
    )
{
    *Seen += 1;
    return Code == Expected ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

static VOID
RaiseThroughFinally (
    IN NTSTATUS Status
    )
{
    try {
        Trace = Trace * 10 + 1;
        RtlRaiseStatus(Status);
        Trace = 0;
    } finally {
        Trace = Trace * 10 + (AbnormalTermination() ? 2 : 9);
    }
}

static ULONG
Divide (
    IN ULONG Numerator,
    IN ULONG Denominator
    )
{
    return Numerator / Denominator;
}

static jmp_buf Jump;

static VOID
JumpBack (
    IN int Value
    )
{
    longjmp(Jump, Value);
}

NTSTATUS
_CRTAPI1
main(
    int argc,
    char *argv[],
    char *envp[],
    ULONG DebugParameter OPTIONAL
    )
{
    volatile PULONG Null = NULL;
    volatile ULONG Zero = 0;
    ULONG Seen = 0;
    ULONG Local1 = 0x1234;
    ULONG Local2 = 0x5678;
    NTSTATUS Code = 0;
    EXCEPTION_RECORD Record;
    LARGE_INTEGER Delay;
    int Value;

    //
    // A data abort, its filter and handler, with the locals of the frame.
    //

    try {
        *Null = 1;
        Code = -1;
    } except (Filter(GetExceptionCode(), STATUS_ACCESS_VIOLATION, &Seen)) {
        Code = GetExceptionCode();
    }

    Report("access violation", Code == STATUS_ACCESS_VIOLATION && Seen == 1 && Local1 == 0x1234 && Local2 == 0x5678);

    //
    // A software exception raised through a termination handler two frames
    // down, the inner filter declining it.
    //

    Seen = 0;
    Trace = 0;
    Code = 0;
    try {
        try {
            RaiseThroughFinally(STATUS_INVALID_PARAMETER);
        } except (Filter(GetExceptionCode(), STATUS_NO_MEMORY, &Seen)) {
            Code = -1;
        }
    } except (Filter(GetExceptionCode(), STATUS_INVALID_PARAMETER, &Seen)) {
        Code = GetExceptionCode();
    }

    Report("raise through finally", Code == STATUS_INVALID_PARAMETER && Seen == 2 && Trace == 12);

    //
    // Termination handlers on the normal and leave exits.
    //

    Trace = 0;
    try {
        Trace = 1;
    } finally {
        Trace = Trace * 10 + (AbnormalTermination() ? 2 : 3);
    }

    try {
        Trace = Trace * 10 + 4;
        if (Trace != 0) {
            leave;
        }
        Trace = 0;
    } finally {
        Trace = Trace * 10 + (AbnormalTermination() ? 2 : 5);
    }

    Report("finally and leave", Trace == 1345);

    //
    // A continuable exception continued by its filter.
    //

    Seen = 0;
    try {
        Record.ExceptionCode = 0xe0000001;
        Record.ExceptionFlags = 0;
        Record.ExceptionRecord = NULL;
        Record.NumberParameters = 0;
        RtlRaiseException(&Record);
        Seen += 10;
    } except (Seen += 1, EXCEPTION_CONTINUE_EXECUTION) {
        Seen = 0;
    }

    Report("continue execution", Seen == 11);

    //
    // An integer divide by zero.
    //

    Code = 0;
    try {
        Zero = Divide(1, Zero);
    } except (EXCEPTION_EXECUTE_HANDLER) {
        Code = GetExceptionCode();
    }

    Report("divide by zero", Code == STATUS_INTEGER_DIVIDE_BY_ZERO);

    //
    // Exceptions in kernel mode: a system service probes an inaccessible or
    // misaligned argument, which raises, and its handler returns the status.
    //

    Report("kernel access violation",
           NtQuerySystemTime((PLARGE_INTEGER)0x10) == STATUS_ACCESS_VIOLATION);

    Report("kernel misalignment",
           NtQuerySystemTime((PLARGE_INTEGER)0x7f0001) == STATUS_DATATYPE_MISALIGNMENT);

    //
    // A long jump.
    //

    Value = setjmp(Jump);
    if (Value == 0) {
        JumpBack(7);
    }

    Report("long jump", Value == 7);

    Report(Failures == 0 ? "all cases passed:" : "cases failed:", Failures == 0);
    Delay.QuadPart = -10 * 10 * 1000 * 1000;
    return NtDelayExecution(FALSE, &Delay);
}
