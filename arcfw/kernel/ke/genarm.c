/*++

Module Name:

    genarm.c

Abstract:

    This module generates the structure offsets and constants the ARM kernel
    assembly uses (the KE/MIPS/GENMIPS.C analog). It is compiled to assembly
    and each @@DEF line becomes a #define of ksarm.h (make-execlink.sh).

--*/

#include "ki.h"
#include "nturtl.h"
#include "setjmp.h"

#define DEF(Name, Value) const ULONG KsArm_##Name = (ULONG)(Value)

#define OFF(Name, Type, Field) DEF(Name, FIELD_OFFSET(Type, Field))

#define GEN_ASSERT2(e, l) typedef char GenAssert##l[(e) ? 1 : -1]
#define GEN_ASSERT1(e, l) GEN_ASSERT2(e, l)
#define GEN_ASSERT(e, m) GEN_ASSERT1(e, __LINE__)

//
// The request summary and the packet entry address above it are updated
// together by ldrexd/strexd, so they must be on a quadword boundary.
//

GEN_ASSERT((FIELD_OFFSET(KPRCB, RequestSummary) & 7) == 0 &&
               FIELD_OFFSET(KPRCB, SignalDone) ==
               FIELD_OFFSET(KPRCB, RequestSummary) + 4,
               "RequestSummary and SignalDone must be an aligned quadword");

#define EVENT_OFFSET \
    (FIELD_OFFSET(KEVENT_PAIR, EventHigh) - FIELD_OFFSET(KEVENT_PAIR, EventLow))

GEN_ASSERT((EVENT_OFFSET & (EVENT_OFFSET - 1)) == 0, "Event offset not log2N");
GEN_ASSERT(FIELD_OFFSET(EEVENT_PAIR, KernelEventPair) == 0,
               "the thread's event pair must be its kernel event pair");


    //
    // Interrupt request levels, thread states and bug check codes.
    //

    DEF(PASSIVE_LEVEL, PASSIVE_LEVEL);
    DEF(APC_LEVEL, APC_LEVEL);
    DEF(DISPATCH_LEVEL, DISPATCH_LEVEL);
    DEF(IRQ_VECTOR, IRQ_VECTOR);
    DEF(HIGH_LEVEL, HIGH_LEVEL);
    DEF(Running, Running);
    DEF(KERNEL_STACK_SIZE, KERNEL_STACK_SIZE);
    DEF(NO_USER_MODE_CONTEXT, NO_USER_MODE_CONTEXT);
    DEF(MM_USER_PROBE_ADDRESS, MM_USER_PROBE_ADDRESS);
    DEF(STATUS_INVALID_SYSTEM_SERVICE, STATUS_INVALID_SYSTEM_SERVICE);
    DEF(TRAP_CAUSE_UNKNOWN, TRAP_CAUSE_UNKNOWN);
    DEF(IPI_APC, IPI_APC);
    DEF(IPI_DPC, IPI_DPC);
    DEF(IPI_FREEZE, IPI_FREEZE);
    DEF(IPI_PACKET_READY, IPI_PACKET_READY);
    DEF(DEBUG_PRINT_BREAKPOINT, DEBUG_PRINT_BREAKPOINT);
    DEF(DEBUG_PROMPT_BREAKPOINT, DEBUG_PROMPT_BREAKPOINT);
    DEF(DEBUG_STOP_BREAKPOINT, DEBUG_STOP_BREAKPOINT);
    DEF(DEBUG_LOAD_SYMBOLS_BREAKPOINT, DEBUG_LOAD_SYMBOLS_BREAKPOINT);
    DEF(DEBUG_UNLOAD_SYMBOLS_BREAKPOINT, DEBUG_UNLOAD_SYMBOLS_BREAKPOINT);
    DEF(THUMB_BREAKPOINT, THUMB_BREAKPOINT);
    DEF(THUMB_DEBUG_SERVICE, THUMB_DEBUG_SERVICE);
    DEF(THUMB_ASSERT, THUMB_ASSERT);
    DEF(THUMB_FASTFAIL, THUMB_FASTFAIL);
    DEF(THUMB_READ_CYCLES, THUMB_READ_CYCLES);
    DEF(THUMB_DIVIDE_BY_0, THUMB_DIVIDE_BY_0);

    //
    // Loader parameter block.
    //

    OFF(LpbKernelStack, LOADER_PARAMETER_BLOCK, KernelStack);

    //
    // String.
    //

    OFF(StrLength, STRING, Length);
    OFF(StrMaximumLength, STRING, MaximumLength);
    OFF(StrBuffer, STRING, Buffer);

    //
    // List entry.
    //

    OFF(LsFlink, LIST_ENTRY, Flink);
    OFF(LsBlink, LIST_ENTRY, Blink);

    //
    // Interrupt object.
    //

    OFF(InInterruptListEntry, KINTERRUPT, InterruptListEntry);
    OFF(InServiceRoutine, KINTERRUPT, ServiceRoutine);
    OFF(InServiceContext, KINTERRUPT, ServiceContext);
    OFF(InDispatchAddress, KINTERRUPT, DispatchAddress);
    OFF(InIrql, KINTERRUPT, Irql);
    OFF(InSynchronizeIrql, KINTERRUPT, SynchronizeIrql);
    OFF(InFloatingSave, KINTERRUPT, FloatingSave);
    OFF(InMode, KINTERRUPT, Mode);
    OFF(InDispatchCode, KINTERRUPT, DispatchCode[0]);
    DEF(InLevelSensitive, LevelSensitive);
    DEF(InLatched, Latched);

    //
    // Processor control region.
    //

    OFF(PcInterruptRoutine, KPCR, InterruptRoutine);
    OFF(PcPrcb, KPCR, Prcb);
    OFF(PcTeb, KPCR, Teb);
    OFF(PcInterruptRequest, KPCR, InterruptRequest);
    OFF(PcCurrentIrql, KPCR, CurrentIrql);
    OFF(PcCurrentThread, KPCR, CurrentThread);
    OFF(PcDpcRoutineActive, KPCR, DpcRoutineActive);
    OFF(PcInterruptStack, KPCR, InterruptStack);
    OFF(PcFaultAddress, KPCR, FaultAddress);
    OFF(PcFaultStatus, KPCR, FaultStatus);
    OFF(PcSavedR0, KPCR, SavedR0);
    OFF(PcSavedR1, KPCR, SavedR1);
    OFF(PcSavedPc, KPCR, SavedPc);
    OFF(PcSavedCpsr, KPCR, SavedCpsr);
    OFF(PcInitialStack, KPCR, InitialStack);
    OFF(PcPanicStack, KPCR, PanicStack);
    OFF(PcOnInterruptStack, KPCR, OnInterruptStack);
    OFF(PcSavedInitialStack, KPCR, SavedInitialStack);

    //
    // Processor control block.
    //

    OFF(PbCurrentThread, KPRCB, CurrentThread);
    OFF(PbNextThread, KPRCB, NextThread);
    OFF(PbIdleThread, KPRCB, IdleThread);
    OFF(PbQuantumEnd, KPRCB, QuantumEnd);
    OFF(PbContextSwitches, KPRCB, KeContextSwitches);
    OFF(PbDpcListHead, KPRCB, DpcListHead);
    OFF(PbSystemCalls, KPRCB, KeSystemCalls);
    OFF(PbInterruptCount, KPRCB, InterruptCount);
    OFF(PbCurrentPacket, KPRCB, CurrentPacket);
    OFF(PbWorkerRoutine, KPRCB, WorkerRoutine);
    OFF(PbRequestSummary, KPRCB, RequestSummary);

    //
    // Process and thread objects, and APC state.
    //

    OFF(PrDirectoryTableBase, KPROCESS, DirectoryTableBase);
    OFF(AsProcess, KAPC_STATE, Process);
    OFF(AsKernelApcPending, KAPC_STATE, KernelApcPending);
    OFF(AsUserApcPending, KAPC_STATE, UserApcPending);
    OFF(ThApcState, KTHREAD, ApcState);
    OFF(ThInitialStack, KTHREAD, InitialStack);
    OFF(ThKernelStack, KTHREAD, KernelStack);
    OFF(ThTeb, KTHREAD, Teb);
    OFF(ThContextSwitches, KTHREAD, ContextSwitches);
    OFF(ThWaitStatus, KTHREAD, WaitStatus);
    OFF(ThState, KTHREAD, State);
    OFF(ThWaitIrql, KTHREAD, WaitIrql);
    OFF(ThPreviousMode, KTHREAD, PreviousMode);
    OFF(ThAlerted, KTHREAD, Alerted);

    //
    // Deferred procedure call object.
    //

    OFF(DpDpcListEntry, KDPC, DpcListEntry);
    OFF(DpDeferredRoutine, KDPC, DeferredRoutine);
    OFF(DpDeferredContext, KDPC, DeferredContext);
    OFF(DpSystemArgument1, KDPC, SystemArgument1);
    OFF(DpSystemArgument2, KDPC, SystemArgument2);
    OFF(DpLock, KDPC, Lock);

    //
    // Exception frame.
    //

    OFF(ExD8, KEXCEPTION_FRAME, D8);
    OFF(ExR4, KEXCEPTION_FRAME, R4);
    OFF(ExR5, KEXCEPTION_FRAME, R5);
    OFF(ExR6, KEXCEPTION_FRAME, R6);
    OFF(ExR7, KEXCEPTION_FRAME, R7);
    OFF(ExR8, KEXCEPTION_FRAME, R8);
    OFF(ExR9, KEXCEPTION_FRAME, R9);
    OFF(ExR10, KEXCEPTION_FRAME, R10);
    OFF(ExR11, KEXCEPTION_FRAME, R11);
    OFF(ExFpscr, KEXCEPTION_FRAME, Fpscr);
    OFF(ExPsr, KEXCEPTION_FRAME, Psr);
    OFF(ExSwapReturn, KEXCEPTION_FRAME, SwapReturn);
    OFF(ExLr, KEXCEPTION_FRAME, Lr);
    DEF(ExceptionFrameLength, (sizeof(KEXCEPTION_FRAME) + 7) & ~7);

    //
    // Trap frame.
    //

    OFF(TrR0, KTRAP_FRAME, R0);
    OFF(TrR1, KTRAP_FRAME, R1);
    OFF(TrR2, KTRAP_FRAME, R2);
    OFF(TrR3, KTRAP_FRAME, R3);
    OFF(TrR12, KTRAP_FRAME, R12);
    OFF(TrSp, KTRAP_FRAME, Sp);
    OFF(TrLr, KTRAP_FRAME, Lr);
    OFF(TrPc, KTRAP_FRAME, Pc);
    OFF(TrCpsr, KTRAP_FRAME, Cpsr);
    OFF(TrFpscr, KTRAP_FRAME, Fpscr);
    OFF(TrExceptionActive, KTRAP_FRAME, ExceptionActive);
    OFF(TrPreviousMode, KTRAP_FRAME, PreviousMode);
    OFF(TrOldIrql, KTRAP_FRAME, OldIrql);
    OFF(TrFaultAddress, KTRAP_FRAME, FaultAddress);
    OFF(TrFaultStatus, KTRAP_FRAME, FaultStatus);
    OFF(TrOnInterruptStack, KTRAP_FRAME, OnInterruptStack);
    OFF(TrVolatileD, KTRAP_FRAME, VolatileD);
    DEF(TrapFrameLength, KTRAP_FRAME_LENGTH);
    DEF(TrapFrameArguments, KTRAP_FRAME_ARGUMENTS);

    //
    // Context record.
    //

    DEF(CONTEXT_FULL, CONTEXT_FULL);
    DEF(ContextFrameLength, sizeof(CONTEXT));
    DEF(CONTEXT_UNWOUND_TO_CALL, CONTEXT_UNWOUND_TO_CALL);
    OFF(CxContextFlags, CONTEXT, ContextFlags);
    OFF(CxR0, CONTEXT, R0);
    OFF(CxR1, CONTEXT, R1);
    OFF(CxR2, CONTEXT, R2);
    OFF(CxR4, CONTEXT, R4);
    OFF(CxR5, CONTEXT, R5);
    OFF(CxR6, CONTEXT, R6);
    OFF(CxR7, CONTEXT, R7);
    OFF(CxR8, CONTEXT, R8);
    OFF(CxR9, CONTEXT, R9);
    OFF(CxR10, CONTEXT, R10);
    OFF(CxR11, CONTEXT, R11);
    OFF(CxR12, CONTEXT, R12);
    OFF(CxSp, CONTEXT, Sp);
    OFF(CxLr, CONTEXT, Lr);
    OFF(CxPc, CONTEXT, Pc);
    OFF(CxCpsr, CONTEXT, Cpsr);
    OFF(CxFpscr, CONTEXT, Fpscr);
    OFF(CxD, CONTEXT, u.D);

    //
    // Exception record.
    //

    DEF(ExceptionRecordLength, (sizeof(EXCEPTION_RECORD) + 7) & ~7);
    OFF(ErExceptionCode, EXCEPTION_RECORD, ExceptionCode);
    OFF(ErExceptionFlags, EXCEPTION_RECORD, ExceptionFlags);
    OFF(ErExceptionRecord, EXCEPTION_RECORD, ExceptionRecord);
    OFF(ErExceptionAddress, EXCEPTION_RECORD, ExceptionAddress);
    OFF(ErNumberParameters, EXCEPTION_RECORD, NumberParameters);
    DEF(EXCEPTION_NONCONTINUABLE, EXCEPTION_NONCONTINUABLE);
    OFF(ErExceptionInformation, EXCEPTION_RECORD, ExceptionInformation);
    DEF(EXCEPTION_UNWIND, EXCEPTION_UNWIND);
    DEF(ExceptionContinueSearch, ExceptionContinueSearch);
    DEF(ExceptionNestedException, ExceptionNestedException);
    DEF(ExceptionCollidedUnwind, ExceptionCollidedUnwind);
    DEF(STATUS_LONGJUMP, STATUS_LONGJUMP);
    DEF(CPSR_THUMB, 0x20);

    //
    // Dispatcher context.
    //

    OFF(DcEstablisherFrame, DISPATCHER_CONTEXT, EstablisherFrame);
    GEN_ASSERT(sizeof(DISPATCHER_CONTEXT) == 9 * 4, "the dispatcher context is copied as nine words");

    //
    // Jump buffer.
    //

    OFF(JbFrame, _JUMP_BUFFER, Frame);
    OFF(JbR4, _JUMP_BUFFER, R4);
    OFF(JbR5, _JUMP_BUFFER, R5);
    OFF(JbR6, _JUMP_BUFFER, R6);
    OFF(JbR7, _JUMP_BUFFER, R7);
    OFF(JbR8, _JUMP_BUFFER, R8);
    OFF(JbR9, _JUMP_BUFFER, R9);
    OFF(JbR10, _JUMP_BUFFER, R10);
    OFF(JbR11, _JUMP_BUFFER, R11);
    OFF(JbSp, _JUMP_BUFFER, Sp);
    OFF(JbPc, _JUMP_BUFFER, Pc);
    OFF(JbFpscr, _JUMP_BUFFER, Fpscr);
    OFF(JbD, _JUMP_BUFFER, D);

    //
    // Critical section and thread environment block.
    //

    OFF(CsLockCount, RTL_CRITICAL_SECTION, LockCount);
    OFF(CsRecursionCount, RTL_CRITICAL_SECTION, RecursionCount);
    OFF(CsOwningThread, RTL_CRITICAL_SECTION, OwningThread);
    OFF(TeClientId, TEB, ClientId);
    OFF(TeStackBase, TEB, NtTib.StackBase);
    OFF(TeStackLimit, TEB, NtTib.StackLimit);
    DEF(STATUS_SUCCESS, STATUS_SUCCESS);

    //
    // Fast event pair system services. The service code is negative and the
    // bit that selects which event is set and which is waited on is the
    // displacement from one event to the other.
    //

    OFF(EtEventPair, ETHREAD, EventPair);
    OFF(EpEventLow, KEVENT_PAIR, EventLow);
    OFF(EpEventHigh, KEVENT_PAIR, EventHigh);
    DEF(SET_LOW_WAIT_HIGH, -(EVENT_OFFSET * 2));
    DEF(SET_HIGH_WAIT_LOW, -EVENT_OFFSET);
    DEF(SET_EVENT_PAIR_MASK, EVENT_OFFSET);
    DEF(STATUS_NO_EVENT_PAIR, STATUS_NO_EVENT_PAIR);
