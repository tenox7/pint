/*++

Module Name:

    smptest.c

Abstract:

    A Win32 console program that exercises a multiprocessor system: a thread
    bound to each processor runs user code there; threads that update shared
    counters under a critical section and with interlocked increments find no
    lost update; and a computation split over one thread per processor is
    timed against the same work on one thread.

    smptest [threads [iterations]]

--*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXIMUM_THREADS 32

static CRITICAL_SECTION Lock;
static HANDLE StartEvent;
static LONG Shared;
static LONG Atomic;
static ULONG Iterations = 100000;
static ULONG Work = 400000000;
static ULONG Results[MAXIMUM_THREADS];

static DWORD WINAPI
Counter (
    LPVOID Parameter
    )
{
    ULONG Index;

    WaitForSingleObject(StartEvent, INFINITE);
    for (Index = 0; Index < Iterations; Index += 1) {
        EnterCriticalSection(&Lock);
        Shared += 1;
        LeaveCriticalSection(&Lock);
        InterlockedIncrement(&Atomic);
    }

    return 0;
}

static DWORD WINAPI
Compute (
    LPVOID Parameter
    )
{
    ULONG Slot = (ULONG)Parameter >> 16;
    ULONG Count = Work / ((ULONG)Parameter & 0xFFFF);
    ULONG Value = Slot + 1;

    WaitForSingleObject(StartEvent, INFINITE);
    while (Count-- != 0) {
        Value = Value * 1103515245 + 12345;
    }

    Results[Slot] = Value;
    return 0;
}

static ULONG
Run (
    IN LPTHREAD_START_ROUTINE Routine,
    IN ULONG Threads,
    IN DWORD Mask
    )

/*++

Routine Description:

    This function runs threads of a routine started at once and returns the
    milliseconds until the last ended. With a mask, thread n is bound to
    processor n.

--*/

{
    HANDLE Handles[MAXIMUM_THREADS];
    DWORD Id;
    ULONG Index;
    ULONG Start;

    ResetEvent(StartEvent);
    for (Index = 0; Index < Threads; Index += 1) {
        Handles[Index] = CreateThread(NULL, 0, Routine, (LPVOID)((Index << 16) | Threads), 0, &Id);
        if (Mask != 0) {
            SetThreadAffinityMask(Handles[Index], Mask & (1 << Index));
        }
    }

    Start = GetTickCount();
    SetEvent(StartEvent);
    WaitForMultipleObjects(Threads, Handles, TRUE, INFINITE);
    Start = GetTickCount() - Start;
    for (Index = 0; Index < Threads; Index += 1) {
        CloseHandle(Handles[Index]);
    }

    return Start;
}

int __cdecl
main (
    int argc,
    char **argv
    )
{
    SYSTEM_INFO Info;
    ULONG Processors;
    ULONG Threads;
    ULONG One;
    ULONG All;
    ULONG Index;
    DWORD Mask = 0;

    GetSystemInfo(&Info);
    Processors = Info.dwNumberOfProcessors;
    Threads = argc > 1 ? atoi(argv[1]) : 2 * Processors;
    if (argc > 2) {
        Iterations = atoi(argv[2]);
    }

    if (Threads == 0 || Threads > MAXIMUM_THREADS) {
        printf("usage: smptest [threads [iterations]]\n");
        return 1;
    }

    printf("%u processors, active mask %08x\n", Processors, Info.dwActiveProcessorMask);
    InitializeCriticalSection(&Lock);
    StartEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    //
    // A thread bound to each processor.
    //

    for (Index = 0; Index < Processors; Index += 1) {
        Mask |= 1 << Index;
    }

    printf("bound threads: %u ms\n", Run(Compute, Processors, Mask));

    //
    // Counters.
    //

    Run(Counter, Threads, 0);
    printf("%u threads x %u: critical section %u, interlocked %u: %s\n",
           Threads, Iterations, Shared, Atomic,
           (ULONG)Shared == Threads * Iterations && (ULONG)Atomic == Threads * Iterations ?
           "ok" : "LOST UPDATES");

    //
    // The same computation on one thread and on one thread per processor.
    //

    One = Run(Compute, 1, 0);
    All = Run(Compute, Processors, 0);
    printf("compute: 1 thread %u ms, %u threads %u ms, speedup %u.%02u\n",
           One, Processors, All, One / (All ? All : 1), (One * 100 / (All ? All : 1)) % 100);

    return 0;
}
