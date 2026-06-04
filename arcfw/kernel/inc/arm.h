/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    arm.h

Abstract:

    ARM (ARMv7-A) hardware specific kernel header file. The ARMv7-A analog of
    PRIVATE/NTOS/INC/MIPS.H, authored to the same contract (NT 3.5 never shipped
    an ARM port). Defines the PCR access mechanism, IRQL model, interlocked and
    spinlock primitives, register frames, and the PRCB for ARM.

    PCR access: the privileged per-CPU pointer register TPIDRPRW (cp15 c13 op2 4)
    holds the address of this processor's KPCR - the ARM analog of the MIPS fixed
    PCR virtual address and the x86 fs segment base. It is a CPU register, valid
    with the MMU off.

--*/

#ifndef _ARMH_
#define _ARMH_

#if defined(_ARM_)

#define ALLOC_PRAGMA 1

#if defined(_NTDDK_) || defined(_NTIFS_) || defined(_NTHAL_)
#define NTKERNELAPI DECLSPEC_IMPORT
#else
#define NTKERNELAPI
#endif

#if !defined(_NTHAL_)
#define NTHALAPI DECLSPEC_IMPORT
#else
#define NTHALAPI
#endif

#define IMPORT_NAME(name) __imp_##name

#define FASTCALL

//
// ARM specific interlocked operation result values.
//

#define RESULT_ZERO 0
#define RESULT_NEGATIVE 1
#define RESULT_POSITIVE 2

typedef enum _INTERLOCKED_RESULT {
    ResultNegative = RESULT_NEGATIVE,
    ResultZero     = RESULT_ZERO,
    ResultPositive = RESULT_POSITIVE
} INTERLOCKED_RESULT;

#define ExInterlockedIncrementLong(Addend, Lock) \
    ExArmInterlockedIncrementLong(Addend)

#define ExInterlockedDecrementLong(Addend, Lock) \
    ExArmInterlockedDecrementLong(Addend)

#define ExInterlockedExchangeUlong(Target, Value, Lock) \
    ExArmInterlockedExchangeUlong(Target, Value)

NTKERNELAPI
INTERLOCKED_RESULT
ExArmInterlockedIncrementLong (
    IN PLONG Addend
    );

NTKERNELAPI
INTERLOCKED_RESULT
ExArmInterlockedDecrementLong (
    IN PLONG Addend
    );

NTKERNELAPI
ULONG
ExArmInterlockedExchangeUlong (
    IN PULONG Target,
    IN ULONG Value
    );

//
// ARM Interrupt Definitions. Length of the interrupt-object dispatch code
// template, in instructions.
//

#define DISPATCH_LENGTH 4

//
// Interrupt Request Levels.
//

#define PASSIVE_LEVEL 0
#define LOW_LEVEL 0
#define APC_LEVEL 1
#define DISPATCH_LEVEL 2
#define IPI_LEVEL 7
#define POWER_LEVEL 7
#define PROFILE_LEVEL 8
#define HIGH_LEVEL 8

//
// The BCM system-timer interval clock runs at the Jazz CLOCK2_LEVEL (level 7, the
// POWER/IPI band): above every peripheral device, below PROFILE/HIGH, so it is not
// preemptable by devices and does not nest in the single-clock configuration.
//

#define CLOCK2_LEVEL POWER_LEVEL

#define DEFAULT_PROFILE_INTERVAL (10 * 1000)
#define MAXIMUM_PROFILE_INTERVAL (10 * 1000 * 1000)
#define MINIMUM_PROFILE_INTERVAL (10 * 1000)

//
// Default thread and process quantum values.
//

#define PROCESS_QUANTUM 2
#define THREAD_QUANTUM 2

#define CLOCK_QUANTUM_DECREMENT 1
#define WAIT_QUANTUM_DECREMENT 1

extern ULONG KiInterruptTemplate[];

//
// Pointer to the Processor Control Region via TPIDRPRW.
//

__inline struct _KPCR *KiProcessorControlRegion(VOID)
{
    struct _KPCR *Pcr;
    __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 4" : "=r" (Pcr));
    return Pcr;
}

__inline VOID KiSetProcessorControlRegion(struct _KPCR *Pcr)
{
    __asm__ __volatile__("mcr p15, 0, %0, c13, c0, 4" : : "r" (Pcr));
}

#define PCR (KiProcessorControlRegion())
#define KeGetPcr() KiProcessorControlRegion()

//
// User shared data page. Mapped at a fixed kernel address once the MMU is up
// (the MIPS KIPCR2 analog); the kernel currently runs MMU-off and the init path
// halts before any time query dereferences it.
//

#define KI_USER_SHARED_DATA 0xFFFF8000
#define SharedUserData ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)

#define KeGetCurrentIrql() PCR->CurrentIrql
#define KeGetCurrentPrcb() PCR->Prcb
#define KeGetCurrentThread() PCR->CurrentThread
#define KeGetCurrentProcessorNumber() ((ULONG)PCR->Number)
#define KeGetDcacheFillSize() PCR->DcacheFillSize
#define KeGetPreviousMode() (KPROCESSOR_MODE)PCR->CurrentThread->PreviousMode
#define KeIsExecutingDpc() (PCR->DpcRoutineActive != FALSE)

//
// Cache and write buffer flush routine prototypes.
//

NTKERNELAPI
VOID
KeSweepDcache (
    IN BOOLEAN AllProcessors
    );

#define KeSweepCurrentDcache() \
    HalSweepDcache();

NTKERNELAPI
VOID
KeSweepIcache (
    IN BOOLEAN AllProcessors
    );

#define KeSweepCurrentIcache() \
    HalSweepIcache();          \
    HalSweepDcache();

NTKERNELAPI
VOID
KeSweepIcacheRange (
    IN BOOLEAN AllProcessors,
    IN PVOID BaseAddress,
    IN ULONG Length
    );

NTKERNELAPI
VOID
KeFlushIoBuffers (
    IN PMDL Mdl,
    IN BOOLEAN ReadOperation,
    IN BOOLEAN DmaOperation
    );

NTKERNELAPI
VOID
KeFlushWriteBuffer (
    VOID
    );

struct _KEXCEPTION_FRAME;
struct _KTRAP_FRAME;

NTKERNELAPI
VOID
KeIpiInterrupt (
    IN struct _KTRAP_FRAME *TrapFrame
    );

NTKERNELAPI
VOID
KeProfileInterrupt (
    IN struct _KTRAP_FRAME *TrapFrame
    );

NTKERNELAPI
VOID
KeUpdateRuntime (
    IN struct _KTRAP_FRAME *TrapFrame
    );

NTKERNELAPI
VOID
KeUpdateSystemTime (
    IN struct _KTRAP_FRAME *TrapFrame,
    IN ULONG TimeIncrement
    );

//
// Uniprocessor spinlocks reduce to IRQL management.
//

#if defined(NT_UP)
#define KiAcquireSpinLock(SpinLock)
#define KiReleaseSpinLock(SpinLock)
#else
NTKERNELAPI VOID KiAcquireSpinLock (IN PKSPIN_LOCK SpinLock);
NTKERNELAPI VOID KiReleaseSpinLock (IN PKSPIN_LOCK SpinLock);
#endif

#if defined(NT_UP)
#define ExAcquireSpinLock(Lock, OldIrql) KeRaiseIrql(DISPATCH_LEVEL, (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeLowerIrql((OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock)
#else
#define ExAcquireSpinLock(Lock, OldIrql) KeAcquireSpinLock((Lock), (OldIrql))
#define ExReleaseSpinLock(Lock, OldIrql) KeReleaseSpinLock((Lock), (OldIrql))
#define ExAcquireSpinLockAtDpcLevel(Lock) KeAcquireSpinLockAtDpcLevel(Lock)
#define ExReleaseSpinLockFromDpcLevel(Lock) KeReleaseSpinLockFromDpcLevel(Lock)
#endif

VOID _disable (VOID);
VOID _enable (VOID);

#if defined(NT_UP) && !DBG
#define ExAcquireFastLock(Lock, OldIrql) _disable()
#else
#define ExAcquireFastLock(Lock, OldIrql) ExAcquireSpinLock(Lock, OldIrql)
#endif

#if defined(NT_UP) && !DBG
#define ExReleaseFastLock(Lock, OldIrql) _enable()
#else
#define ExReleaseFastLock(Lock, OldIrql) ExReleaseSpinLock(Lock, OldIrql)
#endif

BOOLEAN
KeBusError (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN struct _KEXCEPTION_FRAME *ExceptionFrame,
    IN struct _KTRAP_FRAME *TrapFrame,
    IN PVOID VirtualAddress,
    IN PHYSICAL_ADDRESS PhysicalAddress
    );

VOID
KiDataBusError (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN struct _KEXCEPTION_FRAME *ExceptionFrame,
    IN struct _KTRAP_FRAME *TrapFrame
    );

VOID
KiInstructionBusError (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN struct _KEXCEPTION_FRAME *ExceptionFrame,
    IN struct _KTRAP_FRAME *TrapFrame
    );

#define KiQuerySystemTime(CurrentTime)                                  \
    do {                                                                \
        (CurrentTime)->HighPart = SharedUserData->SystemTime.High1Time; \
        (CurrentTime)->LowPart = SharedUserData->SystemTime.LowPart;    \
    } while ((CurrentTime)->HighPart != SharedUserData->SystemTime.High2Time)

#if defined(_NTDDK_) || defined(_NTIFS_)

#define KeQueryTickCount(CurrentCount ) { \
    PKSYSTEM_TIME _TickCount = *((PKSYSTEM_TIME *)(&KeTickCount)); \
    do {                                                          \
        (CurrentCount)->HighPart = _TickCount->High1Time;          \
        (CurrentCount)->LowPart = _TickCount->LowPart;             \
    } while ((CurrentCount)->HighPart != _TickCount->High2Time);   \
}

#else

#define KiQueryTickCount(CurrentCount) \
    do {                                                        \
        (CurrentCount)->HighPart = KeTickCount.High1Time;       \
        (CurrentCount)->LowPart = KeTickCount.LowPart;          \
    } while ((CurrentCount)->HighPart != KeTickCount.High2Time)

NTKERNELAPI
VOID
KeQueryTickCount (
    OUT PLARGE_INTEGER CurrentCount
    );

#endif

#define KiQueryLowTickCount() KeTickCount.LowPart

#define KiQueryInterruptTime(CurrentTime)                                   \
    do {                                                                    \
        (CurrentTime)->HighPart = SharedUserData->InterruptTime.High1Time;  \
        (CurrentTime)->LowPart = SharedUserData->InterruptTime.LowPart;     \
    } while ((CurrentTime)->HighPart != SharedUserData->InterruptTime.High2Time)

VOID
KiRequestSoftwareInterrupt (
    KIRQL RequestIrql
    );

//
// I/O space read and write macros. ARM peripheral access is memory mapped;
// KeFlushWriteBuffer issues a data synchronization barrier.
//

#define READ_REGISTER_UCHAR(x)  (*(volatile UCHAR * const)(x))
#define READ_REGISTER_USHORT(x) (*(volatile USHORT * const)(x))
#define READ_REGISTER_ULONG(x)  (*(volatile ULONG * const)(x))

#define WRITE_REGISTER_UCHAR(x, y)  { *(volatile UCHAR * const)(x) = y; KeFlushWriteBuffer(); }
#define WRITE_REGISTER_USHORT(x, y) { *(volatile USHORT * const)(x) = y; KeFlushWriteBuffer(); }
#define WRITE_REGISTER_ULONG(x, y)  { *(volatile ULONG * const)(x) = y; KeFlushWriteBuffer(); }

#define READ_PORT_UCHAR(x)  (*(volatile UCHAR * const)(x))
#define READ_PORT_USHORT(x) (*(volatile USHORT * const)(x))
#define READ_PORT_ULONG(x)  (*(volatile ULONG * const)(x))

#define WRITE_PORT_UCHAR(x, y)  { *(volatile UCHAR * const)(x) = y; KeFlushWriteBuffer(); }
#define WRITE_PORT_USHORT(x, y) { *(volatile USHORT * const)(x) = y; KeFlushWriteBuffer(); }
#define WRITE_PORT_ULONG(x, y)  { *(volatile ULONG * const)(x) = y; KeFlushWriteBuffer(); }

//
// Exception frame. Holds the AAPCS callee-saved registers (r4-r11, lr) and the
// callee-saved VFP registers (d8-d15) preserved across a context switch.
//
//  N.B. This frame must be a multiple of 8 bytes in length.
//

typedef struct _KEXCEPTION_FRAME {
    ULONGLONG D8;
    ULONGLONG D9;
    ULONGLONG D10;
    ULONGLONG D11;
    ULONGLONG D12;
    ULONGLONG D13;
    ULONGLONG D14;
    ULONGLONG D15;
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG R8;
    ULONG R9;
    ULONG R10;
    ULONG R11;
    ULONG Fpscr;
    ULONG Psr;
    ULONG SwapReturn;
    ULONG Lr;
} KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

//
// Trap frame. Holds the volatile state captured on entry to an exception or
// interrupt: r0-r3, r12, the banked sp/lr, the return address and saved psr.
//
//  N.B. This frame must be a multiple of 8 bytes in length.
//

typedef struct _KTRAP_FRAME {
    ULONG R0;
    ULONG R1;
    ULONG R2;
    ULONG R3;
    ULONG R12;
    ULONG Sp;
    ULONG Lr;
    ULONG Pc;
    ULONG Cpsr;
    ULONG Fpscr;
    ULONG ExceptionActive;
    ULONG PreviousMode;
    ULONG OldIrql;
    ULONG FaultAddress;
    ULONG FaultStatus;
    ULONG OnInterruptStack;
} KTRAP_FRAME, *PKTRAP_FRAME;

#define KTRAP_FRAME_LENGTH ((sizeof(KTRAP_FRAME) + 7) & (~7))
#define KTRAP_FRAME_ALIGN (sizeof(DOUBLE))
#define KTRAP_FRAME_ROUND (KTRAP_FRAME_ALIGN - 1)

//
// Processor State structure.
//

typedef struct _KPROCESSOR_STATE {
    struct _CONTEXT ContextFrame;
    ULONG Sctlr;
    ULONG Ttbr0;
    ULONG Ttbr1;
    ULONG Ttbcr;
    ULONG Dacr;
    ULONG Contextidr;
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;

//
// Processor Control Block (PRCB)
//

#define PRCB_MINOR_VERSION 1
#define PRCB_MAJOR_VERSION 1
#define PRCB_BUILD_DEBUG        0x0001
#define PRCB_BUILD_UNIPROCESSOR 0x0002

struct _RESTART_BLOCK;

typedef struct _KPRCB {

    USHORT MinorVersion;
    USHORT MajorVersion;

    struct _KTHREAD *CurrentThread;
    struct _KTHREAD *NextThread;
    struct _KTHREAD *IdleThread;
    CCHAR Number;
    CCHAR Reserved;
    USHORT BuildType;
    KAFFINITY SetMember;
    struct _RESTART_BLOCK *RestartBlock;
    ULONG PcrPage;
    ULONG QuantumEnd;

    ULONG SystemReserved[15];
    ULONG HalReserved[16];

    ULONG DpcTime;
    ULONG InterruptTime;
    ULONG KernelTime;
    ULONG UserTime;
    ULONG InterruptCount;
    KDPC QuantumEndDpc;

    PVOID Spare1;
    PVOID Spare2;
    PVOID Spare3;
    volatile ULONG IpiFrozen;
    struct _KPROCESSOR_STATE ProcessorState;

    SINGLE_LIST_ENTRY FsRtlFreeLockList;
    SINGLE_LIST_ENTRY FsRtlFreeWaitingLockList;

    ULONG CcFastReadNoWait;
    ULONG CcFastReadWait;
    ULONG CcFastReadNotPossible;
    ULONG CcCopyReadNoWait;
    ULONG CcCopyReadWait;
    ULONG CcCopyReadNoWaitMiss;

    ULONG KeAlignmentFixupCount;
    ULONG KeContextSwitches;
    ULONG KeDcacheFlushCount;
    ULONG KeExceptionDispatchCount;
    ULONG KeFirstLevelTbFills;
    ULONG KeFloatingEmulationCount;
    ULONG KeIcacheFlushCount;
    ULONG KeSecondLevelTbFills;
    ULONG KeSystemCalls;

    ULONG ReservedCounter[18];

    volatile PVOID RequestPacket[MAXIMUM_PROCESSORS];

    volatile PVOID CurrentPacket[3];
    volatile PKIPI_WORKER WorkerRoutine;
    ULONG CachePad1[4];

    volatile ULONG RequestSummary;
    volatile struct _KPRCB *SignalDone;
    ULONG CachePad2[5];

    PKIPI_COUNTS IpiCounts;
    LARGE_INTEGER StartCount;

    LIST_ENTRY DpcListHead;
    KSPIN_LOCK DpcLock;
    ULONG DpcCount;
    ULONG DpcVictim;
    BOOLEAN SkipTick;

} KPRCB, *PKPRCB;

//
// ARMv7-A page geometry: 4 KiB small pages, 1 MiB sections.
//

#define PAGE_SIZE (ULONG)0x1000
#define PAGE_SHIFT 12L

//
// First-level (section / page-table) index shift and second-level (page table)
// index shift for the ARMv7-A short-descriptor format.
//

#define PDI_SHIFT 20
#define PTI_SHIFT 12

#define MM_HIGHEST_USER_ADDRESS (PVOID)0x7FFEFFFF
#define MM_USER_PROBE_ADDRESS 0x7FFF0000
#define MM_LOWEST_USER_ADDRESS  (PVOID)0x00010000

//
// Page directory / page table self-map bases (used once the MMU and high-half
// kernel are brought up; the kernel currently runs MMU-off, identity mapped).
//

#define PDE_BASE (ULONG)0xC0400000
#define PTE_BASE (ULONG)0xC0000000

#define MM_LOWEST_SYSTEM_ADDRESS (PVOID)0xC0C00000
#define SYSTEM_BASE 0xc0c00000

#define UNCACHED_POLICY 0

//++
//
// BOOLEAN
// KiIsThreadNumericStateSaved(
//     IN PKTHREAD Address
//     )
//
//  On ARM the numeric (VFP) state is saved in the thread context.
//
//--
#define KiIsThreadNumericStateSaved(a)      TRUE

//++
//
// VOID
// KiRundownThread(
//     IN PKTHREAD Address
//     )
//
//--
#define KiRundownThread(a)

#define ProbeForWriteBoolean(Address) {                                      \
    if ((Address) >= (BOOLEAN * const)MM_USER_PROBE_ADDRESS) {               \
        *(volatile BOOLEAN * const)MM_USER_PROBE_ADDRESS = 0;                \
    }                                                                        \
    *(volatile BOOLEAN *)(Address) = *(volatile BOOLEAN *)(Address);         \
}

#define ProbeForWriteChar(Address) {                                         \
    if ((Address) >= (CHAR * const)MM_USER_PROBE_ADDRESS) {                  \
        *(volatile CHAR * const)MM_USER_PROBE_ADDRESS = 0;                   \
    }                                                                        \
    *(volatile CHAR *)(Address) = *(volatile CHAR *)(Address);               \
}

#define ProbeForWriteUchar(Address) {                                        \
    if ((Address) >= (UCHAR * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile UCHAR * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(volatile UCHAR *)(Address) = *(volatile UCHAR *)(Address);             \
}

#define ProbeForWriteIoStatus(Address) {                                     \
    if ((Address) >= (IO_STATUS_BLOCK * const)MM_USER_PROBE_ADDRESS) {       \
        *(volatile ULONG * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(volatile IO_STATUS_BLOCK *)(Address) = *(volatile IO_STATUS_BLOCK *)(Address); \
}

#define ProbeForWriteShort(Address) {                                        \
    if ((Address) >= (SHORT * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile SHORT * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(volatile SHORT *)(Address) = *(volatile SHORT *)(Address);             \
}

#define ProbeForWriteUshort(Address) {                                       \
    if ((Address) >= (USHORT * const)MM_USER_PROBE_ADDRESS) {                \
        *(volatile USHORT * const)MM_USER_PROBE_ADDRESS = 0;                 \
    }                                                                        \
    *(volatile USHORT *)(Address) = *(volatile USHORT *)(Address);           \
}

#define ProbeForWriteHandle(Address) {                                       \
    if ((Address) >= (HANDLE * const)MM_USER_PROBE_ADDRESS) {                \
        *(volatile HANDLE * const)MM_USER_PROBE_ADDRESS = 0;                 \
    }                                                                        \
    *(volatile HANDLE *)(Address) = *(volatile HANDLE *)(Address);           \
}

#define ProbeForWriteLong(Address) {                                        \
    if ((Address) >= (LONG * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile LONG * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                       \
    *(volatile LONG *)(Address) = *(volatile LONG *)(Address);              \
}

#define ProbeForWriteUlong(Address) {                                        \
    if ((Address) >= (ULONG * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile ULONG * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(volatile ULONG *)(Address) = *(volatile ULONG *)(Address);             \
}

#define ProbeForWriteQuad(Address) {                                         \
    if ((Address) >= (QUAD * const)MM_USER_PROBE_ADDRESS) {                  \
        *(volatile LONG * const)MM_USER_PROBE_ADDRESS = 0;                   \
    }                                                                        \
    *(volatile QUAD *)(Address) = *(volatile QUAD *)(Address);               \
}

#define ProbeForWriteUquad(Address) {                                        \
    if ((Address) >= (QUAD * const)MM_USER_PROBE_ADDRESS) {                  \
        *(volatile ULONG * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(volatile UQUAD *)(Address) = *(volatile UQUAD *)(Address);             \
}

#define ProbeAndWriteBoolean(Address, Value) {                               \
    if ((Address) >= (BOOLEAN * const)MM_USER_PROBE_ADDRESS) {               \
        *(volatile BOOLEAN * const)MM_USER_PROBE_ADDRESS = 0;                \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteChar(Address, Value) {                                  \
    if ((Address) >= (CHAR * const)MM_USER_PROBE_ADDRESS) {                  \
        *(volatile CHAR * const)MM_USER_PROBE_ADDRESS = 0;                   \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteUchar(Address, Value) {                                 \
    if ((Address) >= (UCHAR * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile UCHAR * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteShort(Address, Value) {                                 \
    if ((Address) >= (SHORT * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile SHORT * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteUshort(Address, Value) {                                \
    if ((Address) >= (USHORT * const)MM_USER_PROBE_ADDRESS) {                \
        *(volatile USHORT * const)MM_USER_PROBE_ADDRESS = 0;                 \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteHandle(Address, Value) {                                \
    if ((Address) >= (HANDLE * const)MM_USER_PROBE_ADDRESS) {                \
        *(volatile HANDLE * const)MM_USER_PROBE_ADDRESS = 0;                 \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteLong(Address, Value) {                                  \
    if ((Address) >= (LONG * const)MM_USER_PROBE_ADDRESS) {                  \
        *(volatile LONG * const)MM_USER_PROBE_ADDRESS = 0;                   \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteUlong(Address, Value) {                                 \
    if ((Address) >= (ULONG * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile ULONG * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteQuad(Address, Value) {                                  \
    if ((Address) >= (QUAD * const)MM_USER_PROBE_ADDRESS) {                  \
        *(volatile LONG * const)MM_USER_PROBE_ADDRESS = 0;                   \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#define ProbeAndWriteUquad(Address, Value) {                                 \
    if ((Address) >= (UQUAD * const)MM_USER_PROBE_ADDRESS) {                 \
        *(volatile ULONG * const)MM_USER_PROBE_ADDRESS = 0;                  \
    }                                                                        \
    *(Address) = (Value);                                                    \
}

#endif // defined(_ARM_)

#endif // _ARMH_
