/*++

Module Name:

    trapc.c

Abstract:

    The C handlers of the ARM trap entry (ke\trap.asm KiTrapDispatch): aborts
    the TB miss handler did not satisfy, and undefined instructions. A memory
    management fault is offered to memory management (the MIPS
    KiVirtualMemoryDispatch path); every other trap, and a fault memory
    management cannot resolve, is described in an exception record for
    KiExceptionDispatch. Also the exception handler of the system service
    dispatcher (the MIPS KiSystemServiceHandler).

Environment:

    Kernel mode only.

--*/

#include "ki.h"

//
// Fault status (DFSR/IFSR short-descriptor format).
//

#define FSR_WRITE                   (1 << 11)
#define FSR_STATUS(Fsr)             (((Fsr) & 0xF) | (((Fsr) >> 6) & 0x10))

#define FS_ALIGNMENT                0x01
#define FS_DEBUG_EVENT              0x02
#define FS_ACCESS_FLAG_SECTION      0x03
#define FS_TRANSLATION_SECTION      0x05
#define FS_ACCESS_FLAG_PAGE         0x06
#define FS_TRANSLATION_PAGE         0x07
#define FS_EXTERNAL_ABORT           0x08
#define FS_PERMISSION_SECTION       0x0D
#define FS_PERMISSION_PAGE          0x0F

extern BOOLEAN PsWatchEnabled;

//
// The bounds of the argument capture of the system service dispatcher, and
// its exit (ke\trap.asm).
//

VOID KiSystemServiceStartAddress(VOID);
VOID KiSystemServiceEndAddress(VOID);
VOID KiSystemServiceExit(VOID);

static BOOLEAN
KiMemoryManagementFault (
    IN PKTRAP_FRAME TrapFrame,
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN BOOLEAN StoreInstruction
    )

/*++

Routine Description:

    This function calls memory management to resolve a translation or
    permission fault. If memory management cannot resolve it, the exception
    is described as the MIPS memory management exception path describes it.

Return Value:

    FALSE if the fault was resolved, TRUE if the exception is to be
    dispatched.

--*/

{
    NTSTATUS Status;

    Status = MmAccessFault(StoreInstruction,
                           (PVOID)TrapFrame->FaultAddress,
                           (KPROCESSOR_MODE)TrapFrame->PreviousMode);

    if (NT_SUCCESS(Status)) {
        if (PsWatchEnabled && (TrapFrame->PreviousMode == UserMode)) {
            PsWatchWorkingSet(Status,
                              (PVOID)TrapFrame->Pc,
                              (PVOID)TrapFrame->FaultAddress);
        }

        return FALSE;
    }

    //
    // A page fault at an IRQL greater than APC_LEVEL.
    //

    if (Status == (STATUS_IN_PAGE_ERROR | 0x10000000)) {
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL,
                     TrapFrame->FaultAddress,
                     KeGetCurrentIrql(),
                     StoreInstruction,
                     TrapFrame->Pc);
    }

    ExceptionRecord->NumberParameters = 2;
    ExceptionRecord->ExceptionInformation[0] = StoreInstruction;
    ExceptionRecord->ExceptionInformation[1] = TrapFrame->FaultAddress;
    if ((Status != STATUS_ACCESS_VIOLATION) &&
        (Status != STATUS_GUARD_PAGE_VIOLATION) &&
        (Status != STATUS_STACK_OVERFLOW)) {
        ExceptionRecord->NumberParameters = 3;
        ExceptionRecord->ExceptionInformation[2] = Status;
        Status = STATUS_IN_PAGE_ERROR;
    }

    ExceptionRecord->ExceptionCode = Status;
    return TRUE;
}

static VOID
KiInitializeExceptionRecord (
    IN PKTRAP_FRAME TrapFrame,
    OUT PEXCEPTION_RECORD ExceptionRecord
    )
{
    ExceptionRecord->ExceptionFlags = 0;
    ExceptionRecord->ExceptionRecord = NULL;
    ExceptionRecord->ExceptionAddress = (PVOID)TrapFrame->Pc;
    ExceptionRecord->NumberParameters = 0;
}

BOOLEAN
KiDataAbortException (
    IN PKTRAP_FRAME TrapFrame,
    OUT PEXCEPTION_RECORD ExceptionRecord
    )

/*++

Routine Description:

    This function handles a data abort: a memory management fault, a
    misaligned reference, or an external abort (a data bus error).

--*/

{
    BOOLEAN StoreInstruction = (TrapFrame->FaultStatus & FSR_WRITE) != 0;

    KiInitializeExceptionRecord(TrapFrame, ExceptionRecord);
    switch (FSR_STATUS(TrapFrame->FaultStatus)) {
    case FS_TRANSLATION_SECTION:
    case FS_TRANSLATION_PAGE:
    case FS_PERMISSION_SECTION:
    case FS_PERMISSION_PAGE:
    case FS_ACCESS_FLAG_SECTION:
    case FS_ACCESS_FLAG_PAGE:
        return KiMemoryManagementFault(TrapFrame, ExceptionRecord, StoreInstruction);

    case FS_ALIGNMENT:
        ExceptionRecord->ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
        break;

    case FS_EXTERNAL_ABORT:
        ExceptionRecord->ExceptionCode = DATA_BUS_ERROR | 0xdfff0000;
        break;

    default:
        ExceptionRecord->ExceptionCode = STATUS_ACCESS_VIOLATION;
        break;
    }

    ExceptionRecord->NumberParameters = 2;
    ExceptionRecord->ExceptionInformation[0] = StoreInstruction;
    ExceptionRecord->ExceptionInformation[1] = TrapFrame->FaultAddress;
    return TRUE;
}

BOOLEAN
KiPrefetchAbortException (
    IN PKTRAP_FRAME TrapFrame,
    OUT PEXCEPTION_RECORD ExceptionRecord
    )

/*++

Routine Description:

    This function handles a prefetch abort: a memory management fault on an
    instruction fetch, a breakpoint (a BKPT debug event), or an external
    abort (an instruction bus error).

--*/

{
    KiInitializeExceptionRecord(TrapFrame, ExceptionRecord);
    switch (FSR_STATUS(TrapFrame->FaultStatus)) {
    case FS_TRANSLATION_SECTION:
    case FS_TRANSLATION_PAGE:
    case FS_PERMISSION_SECTION:
    case FS_PERMISSION_PAGE:
    case FS_ACCESS_FLAG_SECTION:
    case FS_ACCESS_FLAG_PAGE:
        return KiMemoryManagementFault(TrapFrame, ExceptionRecord, FALSE);

    case FS_DEBUG_EVENT:
        ExceptionRecord->ExceptionCode = STATUS_BREAKPOINT;
        ExceptionRecord->NumberParameters = 1;
        ExceptionRecord->ExceptionInformation[0] = *(PULONG)TrapFrame->Pc;
        return TRUE;

    case FS_EXTERNAL_ABORT:
        ExceptionRecord->ExceptionCode = INSTRUCTION_BUS_ERROR | 0xdfff0000;
        break;

    default:
        ExceptionRecord->ExceptionCode = STATUS_ACCESS_VIOLATION;
        break;
    }

    ExceptionRecord->NumberParameters = 2;
    ExceptionRecord->ExceptionInformation[0] = FALSE;
    ExceptionRecord->ExceptionInformation[1] = TrapFrame->FaultAddress;
    return TRUE;
}

BOOLEAN
KiUndefinedInstruction (
    IN PKTRAP_FRAME TrapFrame,
    OUT PEXCEPTION_RECORD ExceptionRecord
    )

/*++

Routine Description:

    This function handles an undefined instruction. The undefined Thumb
    instructions Microsoft C for ARM and the debug service stubs use as traps
    are a breakpoint, a debug service, with its code in r12, and an integer
    divide by zero; every other undefined instruction is an illegal
    instruction exception. The code of a breakpoint is in the first
    parameter of its exception, as the MIPS break code is.

--*/

{
    USHORT Instruction;

    KiInitializeExceptionRecord(TrapFrame, ExceptionRecord);
    ExceptionRecord->ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
    if ((TrapFrame->Cpsr & PSR_THUMB) == 0) {
        return TRUE;
    }

    try {
        Instruction = *(PUSHORT)TrapFrame->Pc;

    } except (EXCEPTION_EXECUTE_HANDLER) {
        return TRUE;
    }

    switch (Instruction) {
    case THUMB_BREAKPOINT:
        ExceptionRecord->ExceptionCode = STATUS_BREAKPOINT;
        ExceptionRecord->NumberParameters = 1;
        ExceptionRecord->ExceptionInformation[0] = KERNEL_BREAKPOINT;
        break;

    case THUMB_DEBUG_SERVICE:
        ExceptionRecord->ExceptionCode = STATUS_BREAKPOINT;
        ExceptionRecord->NumberParameters = 1;
        ExceptionRecord->ExceptionInformation[0] = TrapFrame->R12;
        break;

    case THUMB_DIVIDE_BY_0:
        ExceptionRecord->ExceptionCode = STATUS_INTEGER_DIVIDE_BY_ZERO;
        break;
    }

    return TRUE;
}

EXCEPTION_DISPOSITION
KiSystemServiceHandler (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PVOID EstablisherFrame,
    IN OUT PCONTEXT ContextRecord,
    IN OUT PDISPATCHER_CONTEXT DispatcherContext
    )

/*++

Routine Description:

    This function is the exception handler of the system service dispatcher
    (ke\trap.asm KiSystemService), whose frame holds the trap frame in r4.

    If an exception occurs while the dispatcher captures the arguments passed
    in memory, the service completes with the status of the exception: an
    unwind is made to KiSystemServiceExit. An exception that a service raised
    and did not handle crashes the system if the service was called from
    user mode; from kernel mode the search continues.

    During an unwind through the dispatcher, the previous mode and the trap
    frame address of the thread are restored from the trap frame, if the
    previous mode was kernel mode. An unwind into user mode crashes the
    system.

Arguments:

    ExceptionRecord - Supplies a pointer to an exception record.

    EstablisherFrame - Supplies the frame pointer of the dispatcher.

    ContextRecord - Supplies a pointer to a context record.

    DispatcherContext - Supplies a pointer to the dispatcher context record.

Return Value:

    ExceptionContinueSearch, if the system is not crashed and no unwind is
    made.

--*/

{
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame;

    Thread = KeGetCurrentThread();
    if (IS_DISPATCHING(ExceptionRecord->ExceptionFlags)) {
        if (((ULONG)ExceptionRecord->ExceptionAddress >= ((ULONG)KiSystemServiceStartAddress & ~1)) &&
            ((ULONG)ExceptionRecord->ExceptionAddress < ((ULONG)KiSystemServiceEndAddress & ~1))) {
            RtlUnwind(EstablisherFrame,
                      (PVOID)KiSystemServiceExit,
                      NULL,
                      (PVOID)ExceptionRecord->ExceptionCode);
        }

        if (Thread->PreviousMode != KernelMode) {
            KeBugCheck(SYSTEM_SERVICE_EXCEPTION);
        }

        return ExceptionContinueSearch;
    }

    if (IS_TARGET_UNWIND(ExceptionRecord->ExceptionFlags)) {
        return ExceptionContinueSearch;
    }

    if (Thread->PreviousMode != KernelMode) {
        KeBugCheck(SYSTEM_UNWIND_PREVIOUS_USER);
    }

    TrapFrame = (PKTRAP_FRAME)DispatcherContext->NonVolatileRegisters->R[0];
    Thread->PreviousMode = (KPROCESSOR_MODE)TrapFrame->PreviousMode;
    Thread->TrapFrame = (PKTRAP_FRAME)TrapFrame->u.TrapFrame;
    return ExceptionContinueSearch;
}

VOID
KiPanicStackSwitch (
    VOID
    )

/*++

Routine Description:

    This function is entered on the panic stack when a trap finds the kernel
    stack overflowed, and bug checks.

--*/

{
    KeBugCheckEx(PANIC_STACK_SWITCH,
                 PCR->FaultAddress,
                 PCR->SavedPc,
                 KernelMode,
                 0);
}
