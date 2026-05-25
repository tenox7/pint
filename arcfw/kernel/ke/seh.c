/*++

Module Name:  seh.c

Abstract:

    The ARM32 structured-exception runtime: the dispatcher behind the setjmp
    frame chain (kiseh.h) and the CPU-fault entry behind ke/trap.S. This is the
    faithful PSEH-style mechanism chosen for the port (no ARM32 compiler codegens
    MSVC SEH); see ARM32/TOOLCHAIN-FINDINGS.md.

      KiSehRaise         - software raise (ExRaiseStatus, RtlRaiseStatus): walk the
                           frame chain, delivering the code to the top frame; the
                           macro's else arm runs the filter/finally and re-raises
                           on continue-search. Unhandled -> KeBugCheckEx, NT's
                           KMODE_EXCEPTION_NOT_HANDLED.
      KiArmDispatchFault - hardware-fault path: ke/trap.S calls this from abort
                           mode; it returns the jmpbuf to resume (the caller drops
                           to SVC mode and tail-calls KiSehLongjmp) or NULL when no
                           frame is registered (the caller then reports/bugchecks).
      KiSehExceptionCode / KiSehExceptionInformation - back GetExceptionCode() and
                           GetExceptionInformation() in filters/handlers.

Environment:

    Kernel mode, uniprocessor (the frame-chain head is a global; it moves to the
    PCR with SMP).

--*/

#include "ki.h"
#include "kiseh.h"

#ifndef STATUS_ACCESS_VIOLATION
#define STATUS_ACCESS_VIOLATION ((ULONG)0xC0000005L)
#endif

//
// The exception-registration chain head. NT keeps this per-thread (NtTib.
// ExceptionList); on the uniprocessor bring-up kernel a single global suffices
// and is honest about the mechanism (a setjmp chain, not table-based SEH).
//

KSEH_FRAME *KiSehTopFrame = NULL;

static ULONG KiSehCurrentCode = 0;

//
// Minimal EXCEPTION_POINTERS for GetExceptionInformation() filters. The captured
// context is the exception code only; a full ContextRecord arrives with the real
// trap frame.
//

static EXCEPTION_RECORD   KiSehRecord;
static EXCEPTION_POINTERS KiSehPointers;

unsigned long KiSehExceptionCode(void)
{
    return KiSehCurrentCode;
}

void *KiSehExceptionInformation(void)
{
    KiSehRecord.ExceptionCode = (NTSTATUS)KiSehCurrentCode;
    KiSehRecord.ExceptionFlags = 0;
    KiSehRecord.ExceptionRecord = NULL;
    KiSehRecord.ExceptionAddress = NULL;
    KiSehRecord.NumberParameters = 0;
    KiSehPointers.ExceptionRecord = &KiSehRecord;
    KiSehPointers.ContextRecord = NULL;
    return &KiSehPointers;
}

VOID KiSehRaise(unsigned long Code)
{
    KSEH_FRAME *Frame = KiSehTopFrame;

    KiSehCurrentCode = Code;
    if (Frame == NULL)
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, Code, 0, 0, 0);

    KiSehTopFrame = Frame->Prev;        // pop before transferring control
    Frame->Code = Code;
    KiSehLongjmp(&Frame->Buf, 1);       // -> the try's else arm (filter / finally)
}

KSEH_JMPBUF *KiArmDispatchFault(unsigned long FaultAddress)
{
    KSEH_FRAME *Frame = KiSehTopFrame;

    UNREFERENCED_PARAMETER(FaultAddress);

    if (Frame == NULL)
        return NULL;                    // no handler: trap.S reports + bugchecks

    KiSehCurrentCode = STATUS_ACCESS_VIOLATION;
    KiSehTopFrame = Frame->Prev;
    Frame->Code = STATUS_ACCESS_VIOLATION;
    return &Frame->Buf;
}

//
// Language-specific handler. The excpt.h _ARM_ proto declares it (mirroring the
// RISC ports); the setjmp dispatcher above is what actually unwinds, so this only
// satisfies references and never selects a handler.
//

EXCEPTION_DISPOSITION
__C_specific_handler(struct _EXCEPTION_RECORD *ExceptionRecord,
                     void *EstablisherFrame,
                     struct _CONTEXT *ContextRecord,
                     struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(EstablisherFrame);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(DispatcherContext);
    return ExceptionContinueSearch;
}
