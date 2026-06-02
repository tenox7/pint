/*++

Copyright (c) 2026

Module Name:

    exarm.c

Abstract:

    ARMv7-A "foundational arch layer" for the NT 3.5 / Raspberry Pi 2 port: the
    Ex/Ke/Ki/Hal primitives the portable executive links against but that are
    supplied per-architecture (assembly on MIPS/x86, the HAL on others). These are
    the symbols the executive-link closure (exec-link-probe.sh) reports as the
    foundational layer - everything else in ExpInitializeExecutive depends on them.

    Uniprocessor (NT_UP): interlocked sequences and spin locks reduce to interrupt
    masking / IRQL management; the spin-lock argument is unused. The TLB/cache ops
    are ARMv7-A cp15 (the MMU is hardware-walked, so "fill the TLB" is just an
    invalidate after MM has written the descriptor - the same op the demand-fill
    path in mmuarm.c performs). Signatures match the prototypes the executive
    compiles against (ke.h's RISC arch section, exposed for _ARM_ by make-exec.sh's
    arch-gate sed; arm.h; ex.h; hal.h).

Environment:

    Kernel mode, uniprocessor.

--*/

#include "ki.h"
#include "arccodes.h"

//
// Interrupt-state save/restore for the uniprocessor interlocked sequences
// (correct even when called with interrupts already masked, unlike a bare
// cpsid/cpsie pair).
//

static ULONG ExpIntSave(VOID)
{
    ULONG cpsr;
    __asm__ __volatile__("mrs %0, cpsr" : "=r"(cpsr));
    __asm__ __volatile__("cpsid i" ::: "memory");
    return cpsr;
}

static VOID ExpIntRestore(ULONG cpsr)
{
    __asm__ __volatile__("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
}

//
// ---- Ex interlocked sequences (uniprocessor) ----
//

ULONG FASTCALL
ExInterlockedAddUlong (IN PULONG Addend, IN ULONG Increment, IN PKSPIN_LOCK Lock)
{
    ULONG old, s = ExpIntSave();
    old = *Addend;
    *Addend = old + Increment;
    ExpIntRestore(s);
    return old;
}

LARGE_INTEGER FASTCALL
ExInterlockedAddLargeInteger (IN PLARGE_INTEGER Addend, IN LARGE_INTEGER Increment, IN PKSPIN_LOCK Lock)
{
    LARGE_INTEGER old;
    ULONG s = ExpIntSave();
    old = *Addend;
    Addend->QuadPart = old.QuadPart + Increment.QuadPart;
    ExpIntRestore(s);
    return old;
}

VOID FASTCALL
ExInterlockedAddLargeStatistic (IN PLARGE_INTEGER Addend, IN ULONG Increment)
{
    ULONG s = ExpIntSave();
    Addend->QuadPart += Increment;
    ExpIntRestore(s);
}

PLIST_ENTRY FASTCALL
ExInterlockedInsertTailList (IN PLIST_ENTRY ListHead, IN PLIST_ENTRY ListEntry, IN PKSPIN_LOCK Lock)
{
    PLIST_ENTRY last;
    ULONG s = ExpIntSave();
    last = ListHead->Blink;
    ListEntry->Flink = ListHead;
    ListEntry->Blink = last;
    last->Flink = ListEntry;
    ListHead->Blink = ListEntry;
    ExpIntRestore(s);
    return (last == ListHead) ? NULL : last;
}

PLIST_ENTRY FASTCALL
ExInterlockedInsertHeadList (IN PLIST_ENTRY ListHead, IN PLIST_ENTRY ListEntry, IN PKSPIN_LOCK Lock)
{
    PLIST_ENTRY first;
    ULONG s = ExpIntSave();
    first = ListHead->Flink;
    ListEntry->Flink = first;
    ListEntry->Blink = ListHead;
    first->Blink = ListEntry;
    ListHead->Flink = ListEntry;
    ExpIntRestore(s);
    return (first == ListHead) ? NULL : first;
}

PLIST_ENTRY FASTCALL
ExInterlockedRemoveHeadList (IN PLIST_ENTRY ListHead, IN PKSPIN_LOCK Lock)
{
    PLIST_ENTRY entry;
    ULONG s = ExpIntSave();
    if (ListHead->Flink == ListHead) {
        entry = NULL;
    } else {
        entry = ListHead->Flink;
        ListHead->Flink = entry->Flink;
        entry->Flink->Blink = ListHead;
    }
    ExpIntRestore(s);
    return entry;
}

PSINGLE_LIST_ENTRY FASTCALL
ExInterlockedPopEntryList (IN PSINGLE_LIST_ENTRY ListHead, IN PKSPIN_LOCK Lock)
{
    PSINGLE_LIST_ENTRY first;
    ULONG s = ExpIntSave();
    first = ListHead->Next;
    if (first != NULL)
        ListHead->Next = first->Next;
    ExpIntRestore(s);
    return first;
}

PSINGLE_LIST_ENTRY FASTCALL
ExInterlockedPushEntryList (IN PSINGLE_LIST_ENTRY ListHead, IN PSINGLE_LIST_ENTRY ListEntry, IN PKSPIN_LOCK Lock)
{
    PSINGLE_LIST_ENTRY first;
    ULONG s = ExpIntSave();
    first = ListHead->Next;
    ListEntry->Next = first;
    ListHead->Next = ListEntry;
    ExpIntRestore(s);
    return first;
}

//
// ---- Ex fast mutex (uniprocessor) ----
// Count: 1 == free, <= 0 == owned/contended. Acquired at APC_LEVEL.
//

VOID FASTCALL
ExAcquireFastMutex (IN PFAST_MUTEX FastMutex)
{
    KIRQL old;
    KeRaiseIrql(APC_LEVEL, &old);
    if (InterlockedDecrement(&FastMutex->Count) != 0) {
        FastMutex->Contention += 1;
        KeWaitForSingleObject(&FastMutex->Event, WrExecutive, KernelMode, FALSE, NULL);
    }
    FastMutex->OldIrql = old;
    FastMutex->Owner = KeGetCurrentThread();
}

VOID FASTCALL
ExReleaseFastMutex (IN PFAST_MUTEX FastMutex)
{
    KIRQL old = (KIRQL)FastMutex->OldIrql;
    FastMutex->Owner = NULL;
    if (InterlockedIncrement(&FastMutex->Count) <= 0)
        KeSetEventBoostPriority(&FastMutex->Event, NULL);
    KeLowerIrql(old);
}

BOOLEAN FASTCALL
ExTryToAcquireFastMutex (IN PFAST_MUTEX FastMutex)
{
    KIRQL old;
    KeRaiseIrql(APC_LEVEL, &old);
    if (FastMutex->Count == 1) {
        FastMutex->Count = 0;
        FastMutex->OldIrql = old;
        FastMutex->Owner = KeGetCurrentThread();
        return TRUE;
    }
    KeLowerIrql(old);
    return FALSE;
}

//
// ---- Ke spin locks (uniprocessor == IRQL) ----
//

VOID
KeAcquireSpinLock (IN PKSPIN_LOCK SpinLock, OUT PKIRQL OldIrql)
{
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
}

VOID
KeReleaseSpinLock (IN PKSPIN_LOCK SpinLock, IN KIRQL NewIrql)
{
    KeLowerIrql(NewIrql);
}

BOOLEAN
KeTryToAcquireSpinLock (IN PKSPIN_LOCK SpinLock, OUT PKIRQL OldIrql)
{
    KeRaiseIrql(DISPATCH_LEVEL, OldIrql);
    return TRUE;
}

//
// ---- Ke TLB management (ARMv7-A, hardware page-table walk) ----
// MM writes the descriptor into the page table; these just invalidate the TLB
// (and, for the *SingleTb variants, write the descriptor MM passed). There is no
// software-loaded TLB to fill as on MIPS.
//

static VOID ExpTlbiAll(VOID)
{
    __asm__ __volatile__("mcr p15,0,%0,c8,c7,0 ; dsb ; isb" :: "r"(0) : "memory");
}

static VOID ExpTlbiMva(PVOID Va)
{
    __asm__ __volatile__("mcr p15,0,%0,c8,c7,1 ; dsb ; isb"
                         :: "r"((ULONG)Va & 0xFFFFF000) : "memory");
}

VOID
KeFlushEntireTb (IN BOOLEAN Invalid, IN BOOLEAN AllProcessors)
{
    ExpTlbiAll();
}

VOID
KeFillEntryTb (IN PHARDWARE_PTE Pte, IN PVOID Virtual, IN BOOLEAN Invalid)
{
    ExpTlbiMva(Virtual);
}

HARDWARE_PTE
KeFlushSingleTb (IN PVOID Virtual, IN BOOLEAN Invalid, IN BOOLEAN AllProcessors,
                 IN PHARDWARE_PTE PtePointer, IN HARDWARE_PTE PteValue)
{
    HARDWARE_PTE old = *PtePointer;
    *PtePointer = PteValue;
    ExpTlbiMva(Virtual);
    return old;
}

VOID
KeFlushMultipleTb (IN ULONG Number, IN PVOID *Virtual, IN BOOLEAN Invalid,
                   IN BOOLEAN AllProcessors, IN PHARDWARE_PTE *PtePointer,
                   IN HARDWARE_PTE PteValue)
{
    ULONG i;
    for (i = 0; i < Number; i += 1) {
        if (ARGUMENT_PRESENT(PtePointer))
            *PtePointer[i] = PteValue;
        ExpTlbiMva(Virtual[i]);
    }
}

VOID
KeChangeColorPage (IN PVOID NewColor, IN PVOID OldColor, IN ULONG PageFrame)
{
    //
    // ARMv7-A is physically-indexed/physically-tagged (PIPT) on the A7 data
    // cache, so there is no page-color cache-aliasing to fix up.
    //
}

//
// ---- Ke cache maintenance (ARMv7-A) ----
//

VOID
KeSweepDcache (IN BOOLEAN AllProcessors)
{
    HalSweepDcache();
}

VOID
KeSweepIcache (IN BOOLEAN AllProcessors)
{
    HalSweepIcache();
}

VOID
KeSweepIcacheRange (IN BOOLEAN AllProcessors, IN PVOID BaseAddress, IN ULONG Length)
{
    HalSweepIcache();
}

//
// ---- Ke time / counter queries ----
//

VOID
KeQuerySystemTime (OUT PLARGE_INTEGER CurrentTime)
{
    //
    // Derive system time from the live tick count until the RTC/boot-time path
    // is wired (100-ns units; KeTimeIncrement is the per-tick advance).
    //
    CurrentTime->QuadPart = (LONGLONG)KeTickCount.LowPart * (LONGLONG)KeMaximumIncrement;
}

VOID
KeQueryTickCount (OUT PLARGE_INTEGER CurrentCount)
{
    KiQueryTickCount(CurrentCount);
}

LARGE_INTEGER
KeQueryPerformanceCounter (IN PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER r;
    if (ARGUMENT_PRESENT(PerformanceFrequency))
        PerformanceFrequency->QuadPart = 1000000;       // BCM system timer: 1 MHz
    r.HighPart = 0;
    r.LowPart = *(volatile ULONG *)0x3F003004u;          // system timer CLO
    return r;
}

ULONG
KeQueryIntervalProfile (VOID)
{
    return KeMaximumIncrement;
}

VOID
KeSetTimeIncrement (IN ULONG MaximumIncrement, IN ULONG MinimumIncrement)
{
}

//
// ---- HAL surface (Pi 2) ----
//

BOOLEAN
HalQueryRealTimeClock (OUT PTIME_FIELDS TimeFields)
{
    //
    // The Pi has no battery-backed RTC; report a fixed epoch until an NTP/SD
    // time source is wired. Year 1995 (NT 3.5 era), 1 Jan 00:00:00.
    //
    TimeFields->Year = 1995;
    TimeFields->Month = 1;
    TimeFields->Day = 1;
    TimeFields->Hour = 0;
    TimeFields->Minute = 0;
    TimeFields->Second = 0;
    TimeFields->Milliseconds = 0;
    TimeFields->Weekday = 0;
    return TRUE;
}

BOOLEAN
HalSetRealTimeClock (IN PTIME_FIELDS TimeFields)
{
    return TRUE;                                          // no writable RTC
}

ARC_STATUS
HalGetEnvironmentVariable (IN PCHAR Variable, IN USHORT Length, OUT PCHAR Buffer)
{
    if (Length > 0)
        Buffer[0] = '\0';
    return ENOENT;
}

ARC_STATUS
HalSetEnvironmentVariable (IN PCHAR Variable, IN PCHAR Value)
{
    return ESUCCESS;
}

ULONG
HalSetTimeIncrement (IN ULONG DesiredIncrement)
{
    return DesiredIncrement;
}

VOID
HalReturnToFirmware (IN FIRMWARE_REENTRY Routine)
{
    //
    // Reboot via the BCM2835 PM watchdog (the loader's reboot path). Halt for
    // the halt/power-down reentries.
    //
    if (Routine == HalRebootRoutine || Routine == HalRestartRoutine) {
        volatile ULONG *pm = (volatile ULONG *)0x3F100000u;
        pm[0x24 / 4] = 0x5A000000u | 0x20u;              // RSTC: full reset
        pm[0x1C / 4] = 0x5A000000u | 10u;                // WDOG: 10 ticks
    }
    __asm__ __volatile__("cpsid i" ::: "memory");
    for (;;)
        __asm__ __volatile__("wfi");
}

VOID
HalZeroPage (IN PVOID NewColor, IN PVOID OldColor, IN ULONG PageFrame)
{
    //
    // Zero a physical page through the KSEG0 direct map (PIPT cache, no color
    // aliasing to manage on the A7).
    //
    volatile ULONG *p = (volatile ULONG *)(KSEG0_BASE + (PageFrame << PAGE_SHIFT));
    ULONG i;
    for (i = 0; i < (PAGE_SIZE / sizeof(ULONG)); i += 1)
        p[i] = 0;
}

//
// ---- Ki globals + the single-entry TLB flush ----
//

ULONG KiBugCheckData[5];
KSPIN_LOCK KiDispatcherLock;

VOID
KiFlushSingleTb (IN BOOLEAN Invalid, IN PVOID Virtual)
{
    ExpTlbiMva(Virtual);
}
