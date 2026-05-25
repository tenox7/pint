/*++

Copyright (c) 1990-1993  Microsoft Corporation

Module Name:

    ntarm.h

Abstract:

    User-mode visible ARM (ARMv7-A) specific structures and constants.

    Port note (NT 3.5 ARM32 / Raspberry Pi 2): there was never a shipping NT 3.5
    ARM port, so this header is the ARMv7-A analog of PUBLIC/SDK/INC/NTMIPS.H,
    authored to the same contract the kernel and HAL consume. The register set is
    ARMv7-A (r0-r12, sp, lr, pc, cpsr + VFPv3/NEON d0-d31). PCR access is via the
    privileged per-CPU pointer register TPIDRPRW (cp15 c13) - the ARM analog of the
    MIPS fixed PCR virtual address and the x86 fs segment.

--*/

#ifndef _NTARM_
#define _NTARM_

#if defined(_ARM_)

typedef struct _KSYSTEM_TIME {
    ULONG LowPart;
    LONG High1Time;
    LONG High2Time;
} KSYSTEM_TIME, *PKSYSTEM_TIME;

#define _cdecl

#define DBGKD_MAXSTREAM 16

typedef struct _DBGKD_CONTROL_REPORT {
    ULONG InstructionCount;
    UCHAR InstructionStream[DBGKD_MAXSTREAM];
} DBGKD_CONTROL_REPORT, *PDBGKD_CONTROL_REPORT;

typedef ULONG DBGKD_CONTROL_SET, *PDBGKD_CONTROL_SET;

#define USER_BREAKPOINT 0
#define KERNEL_BREAKPOINT 1
#define BREAKIN_BREAKPOINT 2
#define BRANCH_TAKEN_BREAKPOINT 3
#define BRANCH_NOT_TAKEN_BREAKPOINT 4
#define SINGLE_STEP_BREAKPOINT 5
#define DIVIDE_OVERFLOW_BREAKPOINT 6
#define DIVIDE_BY_ZERO_BREAKPOINT 7
#define RANGE_CHECK_BREAKPOINT 8
#define STACK_OVERFLOW_BREAKPOINT 9
#define MULTIPLY_OVERFLOW_BREAKPOINT 10

#define DEBUG_PRINT_BREAKPOINT 20
#define DEBUG_PROMPT_BREAKPOINT 21
#define DEBUG_STOP_BREAKPOINT 22
#define DEBUG_LOAD_SYMBOLS_BREAKPOINT 23
#define DEBUG_UNLOAD_SYMBOLS_BREAKPOINT 24

//
// Size of kernel mode stack.
//

#define KERNEL_STACK_SIZE 16384

//
// Length of exception code dispatch vector. ARMv7-A has the eight-entry
// hardware exception vector (reset, undef, svc, prefetch abort, data abort,
// reserved, irq, fiq); rounded for the PCR table.
//

#define XCODE_VECTOR_LENGTH 8

//
// Length of the interrupt vector table (software IRQL/interrupt dispatch).
//

#define MAXIMUM_VECTOR 256

struct _EXCEPTION_RECORD;
struct _KEXCEPTION_FRAME;
struct _KTRAP_FRAME;

typedef
BOOLEAN
(*PKBUS_ERROR_ROUTINE) (
    IN struct _EXCEPTION_RECORD *ExceptionRecord,
    IN struct _KEXCEPTION_FRAME *ExceptionFrame,
    IN struct _KTRAP_FRAME *TrapFrame,
    IN PVOID VirtualAddress,
    IN PHYSICAL_ADDRESS PhysicalAddress
    );

//
// Processor Control Region Structure.
//

#define PCR_MINOR_VERSION 1
#define PCR_MAJOR_VERSION 1

typedef struct _KPCR {

    USHORT MinorVersion;
    USHORT MajorVersion;

//
// Architecturally defined section. May be directly addressed by vendor/
// platform specific HAL code; does not change from version to version.
//

    PKINTERRUPT_ROUTINE InterruptRoutine[MAXIMUM_VECTOR];
    PVOID XcodeDispatch[XCODE_VECTOR_LENGTH];

    ULONG FirstLevelDcacheSize;
    ULONG FirstLevelDcacheFillSize;
    ULONG FirstLevelIcacheSize;
    ULONG FirstLevelIcacheFillSize;
    ULONG SecondLevelDcacheSize;
    ULONG SecondLevelDcacheFillSize;
    ULONG SecondLevelIcacheSize;
    ULONG SecondLevelIcacheFillSize;

    struct _KPRCB *Prcb;
    PVOID Teb;

    ULONG DcacheAlignment;
    ULONG DcacheFillSize;
    ULONG IcacheAlignment;
    ULONG IcacheFillSize;

    ULONG ProcessorId;

    ULONG ProfileInterval;
    ULONG ProfileCount;

    ULONG StallExecutionCount;
    ULONG StallScaleFactor;

    CCHAR Number;
    CCHAR Spareb1;
    CCHAR Spareb2;
    CCHAR Spareb3;

    PKBUS_ERROR_ROUTINE DataBusError;
    PKBUS_ERROR_ROUTINE InstructionBusError;

    ULONG CachePolicy;

    UCHAR IrqlMask[32];
    UCHAR IrqlTable[9];

    UCHAR CurrentIrql;

    KAFFINITY SetMember;

    ULONG ReservedVectors;

    struct _KTHREAD *CurrentThread;

    ULONG AlignedCachePolicy;
    ULONG Spare0;

    ULONG SystemReserved[16];
    ULONG HalReserved[16];

//
// End of the architecturally defined section.
//
// OS release dependent section. May change from release to release and must
// not be addressed by vendor/platform specific HAL code.
//

    ULONG FirstLevelActive;
    ULONG DpcRoutineActive;

    ULONG Spare1;
    ULONG Spare2;

    PVOID RtlpLockRangeStart;
    PVOID RtlpLockRangeEnd;

    ULONG SystemServiceDispatchStart;
    ULONG SystemServiceDispatchEnd;

    ULONG InterruptStack;

    struct _KDPC *QuantumEndDpc;

//
// Exception handler values. FaultAddress / FaultStatus mirror the ARMv7-A
// DFAR/DFSR captured on a data abort (the MIPS BadVaddr analog).
//

    ULONG FaultAddress;
    ULONG FaultStatus;
    PVOID InitialStack;
    PVOID PanicStack;
    PVOID SystemTib;

    ULONG OnInterruptStack;
    ULONG SavedInitialStack;
} KPCR, *PKPCR;

#if defined(_ARM_)

//
// The following flags control the contents of the CONTEXT structure.
//

#define CONTEXT_ARM     0x00200000L

#define CONTEXT_CONTROL         (CONTEXT_ARM | 0x00000001L)
#define CONTEXT_INTEGER         (CONTEXT_ARM | 0x00000002L)
#define CONTEXT_FLOATING_POINT  (CONTEXT_ARM | 0x00000004L)
#define CONTEXT_DEBUG_REGISTERS (CONTEXT_ARM | 0x00000008L)

#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)

#define ARM_MAX_BREAKPOINTS 8
#define ARM_MAX_WATCHPOINTS 1

//
// Context Frame
//
//  N.B. This frame must be a multiple of 16 bytes in length, and conform to a
//  standard ARM AAPCS call frame: it is used as an argument to NtContinue, to
//  build an APC delivery frame, and to dispatch exceptions.
//

typedef struct _CONTEXT {

    ULONG ContextFlags;

    //
    // CONTEXT_INTEGER.
    //

    ULONG R0;
    ULONG R1;
    ULONG R2;
    ULONG R3;
    ULONG R4;
    ULONG R5;
    ULONG R6;
    ULONG R7;
    ULONG R8;
    ULONG R9;
    ULONG R10;
    ULONG R11;
    ULONG R12;

    //
    // CONTEXT_CONTROL. Sp/Lr/Pc are r13/r14/r15.
    //

    ULONG Sp;
    ULONG Lr;
    ULONG Pc;
    ULONG Cpsr;

    //
    // CONTEXT_FLOATING_POINT (VFPv3 / NEON).
    //

    ULONG Fpscr;
    ULONG Padding;
    union {
        ULONGLONG D[32];
        ULONG S[32];
    } u;

    //
    // CONTEXT_DEBUG_REGISTERS.
    //

    ULONG Bvr[ARM_MAX_BREAKPOINTS];
    ULONG Bcr[ARM_MAX_BREAKPOINTS];
    ULONG Wvr[ARM_MAX_WATCHPOINTS];
    ULONG Wcr[ARM_MAX_WATCHPOINTS];

    ULONG Padding2[2];
} CONTEXT, *PCONTEXT;

#endif // _ARM_

#define CONTEXT_TO_PROGRAM_COUNTER(Context) ((Context)->Pc)

#define CONTEXT_LENGTH (sizeof(CONTEXT))
#define CONTEXT_ALIGN (sizeof(ULONG))
#define CONTEXT_ROUND (CONTEXT_ALIGN - 1)

//
// Nonvolatile context pointer record. AAPCS callee-saved set: r4-r11, lr, and
// VFP d8-d15.
//

typedef struct _KNONVOLATILE_CONTEXT_POINTERS {
    PULONG R4;
    PULONG R5;
    PULONG R6;
    PULONG R7;
    PULONG R8;
    PULONG R9;
    PULONG R10;
    PULONG R11;
    PULONG Lr;
    PULONGLONG D8;
    PULONGLONG D9;
    PULONGLONG D10;
    PULONGLONG D11;
    PULONGLONG D12;
    PULONGLONG D13;
    PULONGLONG D14;
    PULONGLONG D15;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

//
// ARMv7-A short-descriptor second-level (small page) PTE for memory management.
//

typedef struct _HARDWARE_PTE {
    ULONG NoExecute : 1;
    ULONG Valid : 1;
    ULONG Buffered : 1;
    ULONG Cached : 1;
    ULONG Access : 2;
    ULONG TypeExtension : 3;
    ULONG Shared : 1;
    ULONG NotGlobal : 1;
    ULONG PageFrameNumber : 20;
} HARDWARE_PTE, *PHARDWARE_PTE;

//
// Address space layout. The kernel currently runs MMU-off with an identity
// map; KSEG0_BASE is defined here for the contract and is 0 until the MMU and
// the high-half kernel are brought up (see CLAUDE.md / ARCHITECTURE.md).
//

#define KUSEG_BASE 0x00000000
#define KSEG0_BASE 0x80000000
#define KSEG2_BASE 0xC0000000

//
// Exception handling structures (image-relative SEH unwind data).
//

typedef struct _RUNTIME_FUNCTION {
    ULONG BeginAddress;
    ULONG EndAddress;
    PEXCEPTION_ROUTINE ExceptionHandler;
    PVOID HandlerData;
    ULONG PrologEndAddress;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

typedef struct _SCOPE_TABLE {
    ULONG Count;
    struct
    {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE, *PSCOPE_TABLE;

VOID
RtlCaptureContext (
    OUT PCONTEXT ContextRecord
    );

PRUNTIME_FUNCTION
RtlLookupFunctionEntry (
    IN ULONG ControlPc
    );

ULONG
RtlVirtualUnwind (
    IN ULONG ControlPc,
    IN PRUNTIME_FUNCTION FunctionEntry,
    IN OUT PCONTEXT ContextRecord,
    OUT PBOOLEAN InFunction,
    OUT PULONG EstablisherFrame,
    IN OUT PKNONVOLATILE_CONTEXT_POINTERS ContextPointers OPTIONAL
    );

typedef struct _DISPATCHER_CONTEXT {
    ULONG ControlPc;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG EstablisherFrame;
    PCONTEXT ContextRecord;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

struct _EXCEPTION_POINTERS;

typedef
LONG
(*EXCEPTION_FILTER) (
    struct _EXCEPTION_POINTERS *ExceptionPointers
    );

typedef
VOID
(*TERMINATION_HANDLER) (
    BOOLEAN is_abnormal
    );

//
// Current thread environment block. The NTMIPS.H analog is PCR->Teb; our KPCR
// caches the current thread (Pcr->CurrentThread), whose KTHREAD.Teb holds it.
// Kernel-only form (this build never defines NTOS_KERNEL_RUNTIME / a user PCR).
// Used by ntpsapi.h's NtCurrentPeb() and any executive code reading the TEB.
//
#define NtCurrentTeb() ((PTEB)(PCR->CurrentThread->Teb))

#endif // defined(_ARM_)

#endif // _NTARM_
