/*++

Module Name:

    hello.c

Abstract:

    A native NT program. The Session Manager runs it from BootExecute, where
    it runs autochk, before any subsystem starts; it writes on the boot screen
    and waits a minute.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>

NTSTATUS
_CRTAPI1
main(
    int argc,
    char *argv[],
    char *envp[],
    ULONG DebugParameter OPTIONAL
    )
{
    UNICODE_STRING Hello;
    LARGE_INTEGER Minute;

    RtlInitUnicodeString( &Hello, L"Hello from hello.exe, a native NT program\n" );
    NtDisplayString( &Hello );
    Minute.QuadPart = -60 * 10 * 1000 * 1000;
    return NtDelayExecution( FALSE, &Minute );
}
