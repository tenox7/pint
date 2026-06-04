/*++

Copyright (c) 2026

Module Name:

    kearm.c

Abstract:

    ARMv7-A kernel architecture glue for the NT 3.5 / Raspberry Pi 2 port: the
    pieces KE/MIPS supplies in assembly and the HAL supplies per-platform, here
    written for ARMv7-A. Provides the system-startup C entry that builds the
    boot processor's PCR/PRCB and idle thread/process and calls the real
    KiInitializeKernel; IRQL management over the PCR; the unexpected-interrupt
    dispatch template and routines; bug check; and the minimal HAL processor and
    cache services KiInitializeKernel calls.

    Everything here is real ARM code. What is not yet present (full interrupt
    dispatch / context switch / the executive) is reached only after the point
    where this phase halts, and is marked where relevant - nothing is faked.

Environment:

    Kernel mode, MMU off, uniprocessor, interrupts masked (early boot).

--*/

#include "ki.h"
#include "kiseh.h"
#include "halirq.h"

#undef RtlZeroMemory

//
// Boot-time self-test of the structured-exception runtime (kiseh.h / ke/seh.c /
// ke/trap.S). Exercises software raise, hardware-fault recovery, nested
// try/finally unwind, and the normal path. Set to 0 once the executive runs.
//

// Disabled now that paging is enabled from ke/armstart.S before KiInitializeKernel:
// the self-test's fault generator is an unaligned LDR, which data-aborts only with
// the MMU off (Strongly-ordered memory). The SEH layer is proven (see git history);
// re-enable with an unmapped-address fault generator if an MMU-on regression check
// is wanted.
#ifndef KI_SEH_SELFTEST
#define KI_SEH_SELFTEST 0
#endif

//
// Boot-time build + verify of the ARMv7-A boot page table (ke/mmuarm.c). MMU-off:
// constructs and self-checks the descriptors only; the live MMU-on/high-half
// switch is a later step. Set to 0 once MM owns the page tables.
//

#ifndef KI_MMU_BUILD_TEST
#define KI_MMU_BUILD_TEST 1
#endif

// KI_RUN_EXECUTIVE: when 1 (the make-execlink.sh full-executive kernel), call
// the genuine ExpInitializeExecutive Phase 0 after the arch is up. The minimal
// make-kernel.sh kernel leaves it 0 - ExpInitializeExecutive is not compiled
// there, so the symbol must not be referenced or that link breaks.
#ifndef KI_RUN_EXECUTIVE
#define KI_RUN_EXECUTIVE 0
#endif

//
// Boot-time self-test of the HAL controller-gating API (ke/halirq.c). With the
// clock running, disable the system-timer interrupt at the BCM controller and
// confirm the tick count freezes, then re-enable it and confirm it resumes. Set
// to 0 once the interrupt-controller path is exercised by real devices.
//

#ifndef KI_HAL_IRQ_SELFTEST
#define KI_HAL_IRQ_SELFTEST 1
#endif

//
// Boot-time self-test of the synthesized software-interrupt / DPC path
// (KiRequestSoftwareInterrupt latches a per-PCR bit, KiCheckSoftwareInterrupts
// drains it at the KeLowerIrql tail, KiDispatchInterrupt runs the DPC queue).
// Queues a DPC at PASSIVE_LEVEL and at DISPATCH_LEVEL and confirms it runs
// exactly once at the right moment. Set to 0 once DPCs are exercised by real
// timers / drivers.
//

#ifndef KI_DPC_SELFTEST
#define KI_DPC_SELFTEST 1
#endif

#define UART0   0x3F201000u
#define UART_DR (*(volatile ULONG *)(UART0 + 0x00))
#define UART_FR (*(volatile ULONG *)(UART0 + 0x18))
#define FR_TXFF 0x20u

extern VOID HalDisplayString(PUCHAR String);
extern ULONG HalpInitializeDisplay0(PLOADER_PARAMETER_BLOCK LoaderBlock);
extern VOID KiArmInitializeVectors(VOID);
extern VOID KiArmStartClock(VOID);
extern VOID KiArmSpinMicroseconds(ULONG Microseconds);
extern VOID HalpInitializeInterrupts(VOID);
extern VOID HalpClockInterrupt0(VOID);
extern VOID MiArmReportPaging(VOID);
extern VOID MiArmInitMachineDependent(PLOADER_PARAMETER_BLOCK LoaderBlock);
#if KI_RUN_EXECUTIVE
extern VOID ExpInitializeExecutive(ULONG Number, PLOADER_PARAMETER_BLOCK LoaderBlock);
#endif

//
// Boot processor structures. KiInitializeKernel addresses the PCR through
// TPIDRPRW (set in KiSystemStartup) and is handed the PRCB / idle objects.
//

KPCR KiPcrStorage;
KPRCB KiPrcbStorage;
KPROCESS KiIdleProcessStorage;
KTHREAD KiIdleThreadStorage;
static UCHAR KiIdleStack[KERNEL_STACK_SIZE] __attribute__((aligned(8)));

//
// The ARC system parameter block. The kernel reads SYSTEM_BLOCK->RestartBlock
// (NULL on a uniprocessor, which the kernel handles); arc.h maps SYSTEM_BLOCK
// to &GlobalSystemBlock for ARM, exactly as on x86.
//

SYSTEM_PARAMETER_BLOCK GlobalSystemBlock;

//
// Unexpected-interrupt object and the dispatch code template copied into it.
// The template is four ARM instructions that load the dispatch routine address
// (appended after the code) into pc. It is wired during KiInitializeKernel but
// only executes once real interrupt dispatch is enabled (a later phase).
//
//   ldr  ip, [pc]          ; ip = word at template[2] (filled with handler)
//   ldr  pc, [pc, #-4]     ; jump through it
//   .word KiUnexpectedInterrupt   (template[2], patched by the connect code)
//   .word 0
//

ULONG KiInterruptTemplate[DISPATCH_LENGTH] = {
    0xe59fc000,
    0xe51ff004,
    0x00000000,
    0x00000000
};

KINTERRUPT KxUnexpectedInterrupt;

//
// Minimal real runtime/synchronization primitives the init closure references.
// RtlZeroMemory here is the out-of-line function (the ntrtl.h memset macro is
// not active in the kernel build); KeInitializeSpinLock is the uniprocessor
// form (a spin lock is just a zeroed word with interrupts/IRQL doing the work).
//

VOID RtlZeroMemory(PVOID Destination, ULONG Length)
{
    volatile unsigned char *p = (volatile unsigned char *)Destination;
    while (Length--)
        *p++ = 0;
}

VOID KeInitializeSpinLock(PKSPIN_LOCK SpinLock)
{
    *SpinLock = 0;
}

static void kputc(int c)
{
    while (UART_FR & FR_TXFF)
        ;
    UART_DR = (ULONG)(unsigned char)c;
}

static void kputs(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            kputc('\r');
        kputc(*s);
    }
}

static int KiDisplayUp;

static void emit(const char *s)
{
    kputs(s);
    if (KiDisplayUp)
        HalDisplayString((PUCHAR)s);
}

static void emit_hex(ULONG v)
{
    static const char d[] = "0123456789abcdef";
    char b[11];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 8; i += 1)
        b[2 + i] = d[(v >> ((7 - i) * 4)) & 0xf];
    b[10] = 0;
    emit(b);
}

//
// Non-static wrappers so the page-table builder (ke/mmuarm.c) shares the one
// serial+HDMI output path.
//

void KiEmit(const char *s)   { emit(s); }
void KiEmitHex(ULONG v)      { emit_hex(v); }

//
// Platform processor initialization. On ARMv7-A the boot processor is already
// in a defined state from the firmware emulator / start.S (SVC mode, MMU off,
// VFP enabled); nothing further is required for the uniprocessor boot path.
//

VOID HalInitializeProcessor(ULONG Number)
{
    UNREFERENCED_PARAMETER(Number);
}

//
// Cache and write-buffer maintenance. With the MMU off QEMU treats memory as
// uncached, so a data/instruction synchronization barrier is sufficient to make
// stores (the copied dispatch code) visible; the real set/way clean is added
// with the cached MMU-on configuration.
//

VOID HalSweepDcache(VOID)   { __asm__ __volatile__("dsb" ::: "memory"); }
VOID HalSweepIcache(VOID)   { __asm__ __volatile__("isb" ::: "memory"); }
VOID KeFlushWriteBuffer(VOID) { __asm__ __volatile__("dsb" ::: "memory"); }

VOID _disable(VOID) { __asm__ __volatile__("cpsid i" ::: "memory"); }
VOID _enable(VOID)  { __asm__ __volatile__("cpsie i" ::: "memory"); }

//
// IRQL management. The current IRQL lives in the PCR. Device interrupts (clock,
// etc.) run at or above DISPATCH_LEVEL, so masking all CPU interrupts when IRQL
// reaches DISPATCH_LEVEL and unmasking below it is the uniprocessor threshold
// control (a coarse model; per-level interrupt-controller masking arrives with a
// full HAL). The PCR tracks the precise level for the rest of the kernel.
//
// Software interrupts (APC_LEVEL, DISPATCH_LEVEL) have no hardware cause bit on
// ARM, so they are emulated the i386 HAL way (HALX86 IXSWINT.ASM/IXIRQL.ASM):
// the per-PCR request byte PCR->SoftwareInterrupt holds a pending bitmask, set
// by KiRequestSoftwareInterrupt(irql) and drained at the KeLowerIrql tail by
// KiCheckSoftwareInterrupts, which dispatches PCR->InterruptRoutine[level]
// (KiDispatchInterrupt for DPCs at DISPATCH_LEVEL, KiApcInterrupt for kernel
// APCs at APC_LEVEL) highest level first. MIPS/Alpha instead carry the request
// in a hardware cause/Sirr register the CPU re-takes when the mask widens; ARM
// (like x86) lacks that, hence the explicit software drain.
//

static VOID KiCheckSoftwareInterrupts(KIRQL NewIrql)
{
    PKPCR Pcr = PCR;
    ULONG Pending;

    //
    // Deliver every software interrupt pending strictly above NewIrql, highest
    // level first (the i386 KfLowerIrql discipline: pending = IRR AND the mask of
    // levels above NewIrql; BSR the highest; clear it; call the handler). Only
    // APC_LEVEL (1) and DISPATCH_LEVEL (2) are ever requested, so this runs at
    // most twice.
    //

    while ((Pending = ((ULONG)(UCHAR)Pcr->SoftwareInterrupt) &
                      ~((1u << (NewIrql + 1)) - 1)) != 0) {
        ULONG Level = 31 - __builtin_clz(Pending);

        //
        // Claim the level with interrupts masked: clear the request bit BEFORE
        // dispatch (a re-request inside the handler re-arms it), raise the IRQL
        // to the handler's level, and unmask only if that level is below
        // DISPATCH_LEVEL (a DPC runs at DISPATCH_LEVEL with devices masked in the
        // coarse model; two-tier masking that lets the clock preempt a DPC is
        // increment 6).
        //

        __asm__ __volatile__("cpsid i" ::: "memory");
        Pcr->SoftwareInterrupt &= (CCHAR)~(1u << Level);
        Pcr->CurrentIrql = (UCHAR)Level;
        if (Level < DISPATCH_LEVEL)
            __asm__ __volatile__("cpsie i" ::: "memory");

        (Pcr->InterruptRoutine[Level])();

        //
        // Drop back to NewIrql and re-evaluate: a handler may have requested a
        // level that is still above NewIrql (e.g. an APC handler that queues a
        // DPC). The cleared bit keeps the just-serviced level from re-firing.
        //

        __asm__ __volatile__("cpsid i" ::: "memory");
        Pcr->CurrentIrql = NewIrql;
        if (NewIrql < DISPATCH_LEVEL)
            __asm__ __volatile__("cpsie i" ::: "memory");
    }
}

VOID KeRaiseIrql(KIRQL NewIrql, PKIRQL OldIrql)
{
    *OldIrql = PCR->CurrentIrql;
    PCR->CurrentIrql = NewIrql;
    if (NewIrql >= DISPATCH_LEVEL)
        __asm__ __volatile__("cpsid i" ::: "memory");
}

VOID KeLowerIrql(KIRQL NewIrql)
{
    PCR->CurrentIrql = NewIrql;
    if (NewIrql < DISPATCH_LEVEL)
        __asm__ __volatile__("cpsie i" ::: "memory");

    //
    // Tail: now that the IRQL has dropped, deliver any software interrupt that
    // became eligible (the MIPS "the pending cause bit re-traps as the PSR mask
    // widens" effected in software). Cheap on the common path: one byte read, a
    // mask, a compare against zero.
    //

    KiCheckSoftwareInterrupts(NewIrql);
}

VOID KiRequestSoftwareInterrupt(KIRQL RequestIrql)
{
    ULONG Cpsr;

    //
    // Latch the request in the per-PCR pending byte under interrupt-disable (an
    // atomic read-modify-write against a nested interrupt). The bit is honored
    // when the IRQL next drops below RequestIrql at the KeLowerIrql tail;
    // KeInsertQueueDpc / KiInsertQueueApc always lower right after requesting, so
    // no eager dispatch is needed here (i386 HalRequestSoftwareInterrupt does an
    // eager check; the drain-at-lower path alone is sufficient and simpler).
    //

    __asm__ __volatile__("mrs %0, cpsr" : "=r"(Cpsr));
    __asm__ __volatile__("cpsid i" ::: "memory");
    PCR->SoftwareInterrupt |= (CCHAR)(1u << RequestIrql);
    if (!(Cpsr & 0x80u))
        __asm__ __volatile__("cpsie i" ::: "memory");
}

//
// Interrupt dispatch routines stored in the PCR vector table (placed during
// KiInitializeKernel at InterruptRoutine[0/APC_LEVEL/DISPATCH_LEVEL]).
//
//   KiUnexpectedInterrupt / KiPassiveRelease - reaching one is an unexpected
//     fault, so they bug check.
//   KiDispatchInterrupt - the DISPATCH_LEVEL software interrupt: drain the
//     processor's DPC queue (KE/MIPS X4CTXSW.S KiProcessDpcList). The
//     quantum-end / next-thread context-switch tail is deferred until the
//     scheduler exists (Phase 1). Real in both builds - pure KE, no executive.
//   KiApcInterrupt - the APC_LEVEL software interrupt: deliver kernel-mode APCs
//     through the genuine KiDeliverApc (KE/APCSUP.C). Present only under
//     KI_RUN_EXECUTIVE (KiDeliverApc links from libexec.a); the minimal kernel
//     never requests APC_LEVEL, so it keeps the bug-check stub.
//

VOID KiUnexpectedInterrupt(VOID) { KeBugCheck(TRAP_CAUSE_UNKNOWN); }
VOID KiPassiveRelease(VOID)      { KeBugCheck(TRAP_CAUSE_UNKNOWN); }

VOID KiDispatchInterrupt(VOID)
{
    PKPRCB Prcb = PCR->Prcb;

    //
    // Entered at DISPATCH_LEVEL with device interrupts masked (coarse model).
    // Drain the DPC queue head first; each DPC runs exactly once. The entry is
    // unlinked and Dpc->Lock cleared BEFORE the call so a routine may re-queue
    // itself (matches KE/MIPS KiProcessDpcList). DpcCount is a cumulative
    // statistic in NT 3.5 (only KeInsertQueueDpc touches it - never decremented),
    // so the list-empty test, not a count, bounds the drain.
    //

    while (!IsListEmpty(&Prcb->DpcListHead)) {
        PLIST_ENTRY Entry = Prcb->DpcListHead.Flink;
        PKDPC Dpc = CONTAINING_RECORD(Entry, KDPC, DpcListEntry);
        PKDEFERRED_ROUTINE Routine = Dpc->DeferredRoutine;
        PVOID Context = Dpc->DeferredContext;
        PVOID Arg1 = Dpc->SystemArgument1;
        PVOID Arg2 = Dpc->SystemArgument2;

        RemoveEntryList(Entry);
        Dpc->Lock = NULL;
        PCR->DpcRoutineActive = TRUE;
        (Routine)(Dpc, Context, Arg1, Arg2);
        PCR->DpcRoutineActive = FALSE;
    }
}

#if KI_RUN_EXECUTIVE

VOID KiApcInterrupt(VOID)
{
    //
    // Deliver kernel-mode APCs. We are at the KeLowerIrql tail at APC_LEVEL, not
    // returning to user mode, so there is no trap/exception frame - and the
    // genuine KiDeliverApc never dereferences them unless PreviousMode==UserMode
    // (KE/MIPS XXAPCINT.S skips the frame build for kernel mode, label 20). NULL
    // is the faithful value here; user-APC delivery (and a real frame) is
    // deferred until the port runs user mode.
    //

    KiDeliverApc(KernelMode, NULL, NULL);
}

#else

VOID KiApcInterrupt(VOID)        { KeBugCheck(TRAP_CAUSE_UNKNOWN); }

#endif

BOOLEAN
KeBusError (
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PKTRAP_FRAME TrapFrame,
    IN PVOID VirtualAddress,
    IN PHYSICAL_ADDRESS PhysicalAddress
    )
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(PhysicalAddress);
    return FALSE;
}

VOID KeBugCheckEx(ULONG Code, ULONG P1, ULONG P2, ULONG P3, ULONG P4)
{
    emit("\n*** STOP: ");
    emit_hex(Code);
    emit(" (");
    emit_hex(P1); emit(", ");
    emit_hex(P2); emit(", ");
    emit_hex(P3); emit(", ");
    emit_hex(P4);
    emit(")\nKeBugCheck - system halted.\n");
    _disable();
    for (;;)
        __asm__ __volatile__("wfi");
}

VOID KeBugCheck(ULONG Code)
{
    KeBugCheckEx(Code, 0, 0, 0, 0);
}

//
// Report a CPU exception captured by the ARM vector table (trap.S) and halt.
// Until the full trap-frame / exception-dispatch layer is built, an unhandled
// kernel exception terminates here - but with the faulting address, status, and
// PC, instead of a silent reset.
//

VOID KiArmTrapReport(ULONG Type, ULONG Status, ULONG Address, ULONG Pc)
{
    static const char *const Names[] = {
        "undefined instruction", "prefetch abort", "data abort",
        "supervisor call", "unexpected IRQ", "unexpected FIQ"
    };

    emit("\n*** ARM exception: ");
    emit(Type < 6 ? Names[Type] : "unknown");
    emit("\n    fault address : ");
    emit_hex(Address);
    emit("\n    fault status  : ");
    emit_hex(Status);
    emit("\n    faulting pc   : ");
    emit_hex(Pc);
    emit("\n");
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, Type, Address, Status, Pc);
}

//
// Raise a structured exception. Routes into the ARM32 SEH dispatcher (ke/seh.c):
// it walks the _SEH_ frame chain (kiseh.h), running filters and finally blocks.
// With no frame registered the dispatcher terminates in KeBugCheckEx(KMODE_
// EXCEPTION_NOT_HANDLED) - NT's real unhandled-kernel-exception outcome. The
// full RtlDispatchException / RtlVirtualUnwind context-record path is a later
// refinement; this setjmp chain is the faithful mechanism for ARM32 (no compiler
// codegens MSVC SEH - see ARM32/TOOLCHAIN-FINDINGS.md).
//

VOID ExRaiseStatus(NTSTATUS ExceptionCode)
{
    KiSehRaise((unsigned long)ExceptionCode);
}

//
// Self-test of the SEH runtime. Faults are forced with an unaligned load: with
// the MMU off, memory is Strongly-ordered and an unaligned LDR always data-aborts
// (the project's documented gotcha) - a deterministic fault generator. The odd
// address is derived at run time so the compiler cannot prove alignment and elide
// the access. Locals that cross the setjmp boundary are volatile (the NT idiom).
//

#if KI_SEH_SELFTEST

#ifndef STATUS_ACCESS_VIOLATION
#define STATUS_ACCESS_VIOLATION ((ULONG)0xC0000005L)
#endif

ULONG KiSehFaultCell = 0xABCD1234;

static ULONG KiSehUnalignedRead(void)
{
    volatile ULONG *p = (volatile ULONG *)(((ULONG)&KiSehFaultCell) | 1u);
    return *p;                          // unaligned word load -> data abort
}

static ULONG KiSehAlignedRead(volatile ULONG *p)
{
    return *p;
}

static void KiSehResult(int pass)
{
    emit(pass ? "   OK\n" : "   *** FAIL ***\n");
}

static VOID KiSehSelfTest(VOID)
{
    emit("\nSEH runtime self-test (kiseh.h / ke/seh.c / ke/trap.S):\n");

    //
    // 1. Software raise (ExRaiseStatus) caught by a filtering except.
    //
    {
        volatile ULONG v = 0;
        _SEH_TRY
            ExRaiseStatus((NTSTATUS)STATUS_ACCESS_VIOLATION);
            v = 0xBAD;
        _SEH_EXCEPT(GetExceptionCode() == STATUS_ACCESS_VIOLATION ?
                    EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            v = 0xC0DE;
        _SEH_END_EXCEPT
        emit("  1. ExRaiseStatus -> filter -> handler  v="); emit_hex(v);
        KiSehResult(v == 0xC0DE);
    }

    //
    // 2. Hardware fault (data abort via unaligned load) caught by except.
    //
    {
        volatile ULONG v = 0;
        _SEH_TRY
            v = KiSehUnalignedRead();
            v = 0xBAD;
        _SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            v = 0xFA17;
        _SEH_END_EXCEPT
        emit("  2. data abort -> handler               v="); emit_hex(v);
        KiSehResult(v == 0xFA17);
    }

    //
    // 3. Fault through a nested try/finally: the finally runs abnormally, then
    //    the outer except handles it.
    //
    {
        volatile ULONG v = 0, finallyRan = 0, abnormal = 0;
        _SEH_TRY
            _SEH_TRY_FINALLY
                v = KiSehUnalignedRead();
                v = 0xBAD;
            _SEH_FINALLY
                finallyRan = 1;
                abnormal = (ULONG)AbnormalTermination();
            _SEH_END_FINALLY
        _SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            v = 0xF1A1;
        _SEH_END_EXCEPT
        emit("  3. fault thru finally -> except        v="); emit_hex(v);
        emit(" finallyRan="); emit_hex(finallyRan);
        emit(" abnormal="); emit_hex(abnormal);
        KiSehResult(v == 0xF1A1 && finallyRan == 1 && abnormal == 1);
    }

    //
    // 4. Normal path: leave skips the rest of the body; the finally runs normally
    //    (AbnormalTermination() == FALSE).
    //
    {
        volatile ULONG v, reached = 0, finallyRan = 0, abnormal = 9;
        v = KiSehAlignedRead(&KiSehFaultCell);
        _SEH_TRY_FINALLY
            _SEH_LEAVE;
            reached = 1;                // skipped by leave
        _SEH_FINALLY
            finallyRan = 1;
            abnormal = (ULONG)AbnormalTermination();
        _SEH_END_FINALLY
        emit("  4. read + leave + normal finally       v="); emit_hex(v);
        emit(" reached="); emit_hex(reached);
        emit(" finallyRan="); emit_hex(finallyRan);
        emit(" abnormal="); emit_hex(abnormal);
        KiSehResult(v == 0xABCD1234 && reached == 0 && finallyRan == 1 && abnormal == 0);
    }

    emit("  frame chain head after tests = ");
    emit_hex((ULONG)KiSehTopFrame);
    KiSehResult(KiSehTopFrame == NULL);
    emit("SEH self-test complete.\n\n");
}

#endif

//
// Self-test of the HAL controller-gating API (HalEnable/DisableSystemInterrupt,
// ke/halirq.c). Run at PASSIVE_LEVEL with the clock already ticking: confirm the
// clock is alive, disable the system-timer interrupt at the BCM controller and
// confirm the tick count freezes (the interrupt is gated), then re-enable it and
// confirm the tick count resumes. Time is measured on the free-running 1 MHz
// system-timer counter (KiArmSpinMicroseconds), which keeps running while its
// compare-3 interrupt is masked, so it is an independent reference for the test.
//

#if KI_HAL_IRQ_SELFTEST

static VOID KiArmInterruptGatingTest(VOID)
{
    ULONG before, alive, atDisable, frozen, resumed, guard;

    emit("HAL interrupt-gating self-test (HalEnable/DisableSystemInterrupt):\n");

    //
    // Confirm the clock is alive: wait until the tick count advances. Bounded by
    // a guard (~2 s) so a dead clock still terminates the test instead of hanging.
    // The wait is period-agnostic (the minimal kernel ticks at 250 ms, the
    // executive at 10 ms).
    //

    before = (ULONG)KeTickCount.LowPart;
    for (guard = 0; (ULONG)KeTickCount.LowPart == before && guard < 20; guard += 1)
        KiArmSpinMicroseconds(100000);              // 0.1 s per probe
    alive = (ULONG)KeTickCount.LowPart;

    HalDisableSystemInterrupt(HAL_TIMER_IRQ, CLOCK2_LEVEL);
    atDisable = (ULONG)KeTickCount.LowPart;
    KiArmSpinMicroseconds(500000);                  // 0.5 s masked: ticks must freeze
    frozen = (ULONG)KeTickCount.LowPart;

    HalEnableSystemInterrupt(HAL_TIMER_IRQ, CLOCK2_LEVEL, Latched);
    KiArmSpinMicroseconds(500000);                  // 0.5 s enabled: ticks must resume
    resumed = (ULONG)KeTickCount.LowPart;

    emit("  running   : ticks "); emit_hex(before); emit(" -> "); emit_hex(alive);
    emit(alive != before ? "  (clock alive)\n" : "  *** clock NOT ticking ***\n");
    emit("  disabled  : ticks "); emit_hex(atDisable); emit(" -> "); emit_hex(frozen);
    emit(frozen == atDisable ? "  (frozen - gated)\n" : "  *** still ticking - NOT gated ***\n");
    emit("  re-enabled: ticks "); emit_hex(frozen); emit(" -> "); emit_hex(resumed);
    emit(resumed != frozen ? "  (resumed)\n" : "  *** did NOT resume ***\n");
    emit("  HAL gating self-test ");
    emit((alive != before && frozen == atDisable && resumed != frozen) ?
         "OK\n\n" : "*** FAIL ***\n\n");
}

#endif

//
// Software-interrupt / DPC self-test. Proves the increment-4 path end to end:
// a DPC queued at PASSIVE_LEVEL drains synchronously at the KeLowerIrql tail
// inside KeInsertQueueDpc; a DPC queued at DISPATCH_LEVEL stays deferred until
// the IRQL drops, then runs exactly once. Mirrors the KI_HAL_IRQ_SELFTEST shape.
//

#if KI_DPC_SELFTEST

static volatile ULONG KiDpcTestRuns;
static volatile ULONG KiDpcTestActiveSeen;

static VOID
KiArmDpcTestRoutine (
    IN struct _KDPC *Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KiDpcTestRuns += 1;
    if (PCR->DpcRoutineActive)
        KiDpcTestActiveSeen = 1;            // confirm we run inside the DPC bracket
}

static VOID KiArmDpcSelfTest(VOID)
{
    static KDPC TestDpc;
    KIRQL OldIrql;
    BOOLEAN Queued1, Queued2;
    ULONG RanImmediate, DeferredWhileHigh, RanAfterLower;

    emit("DPC software-interrupt self-test (KiRequestSoftwareInterrupt drain):\n");

    KeInitializeDpc(&TestDpc, KiArmDpcTestRoutine, NULL);

    //
    // Case 1: queue at PASSIVE_LEVEL. KeInsertQueueDpc requests a DISPATCH_LEVEL
    // software interrupt and then lowers IRQL back to PASSIVE - the KeLowerIrql
    // tail must drain it and run the routine exactly once, synchronously, before
    // KeInsertQueueDpc even returns.
    //

    KiDpcTestRuns = 0;
    KiDpcTestActiveSeen = 0;
    Queued1 = KeInsertQueueDpc(&TestDpc, NULL, NULL);
    RanImmediate = KiDpcTestRuns;

    //
    // Case 2: at DISPATCH_LEVEL the DPC must stay queued (nothing pends above
    // DISPATCH), then run exactly once when IRQL drops back below it.
    //

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KiDpcTestRuns = 0;
    Queued2 = KeInsertQueueDpc(&TestDpc, NULL, NULL);
    DeferredWhileHigh = KiDpcTestRuns;
    KeLowerIrql(OldIrql);
    RanAfterLower = KiDpcTestRuns;

    emit("  queue@PASSIVE  : inserted=");
    emit_hex((ULONG)Queued1);
    emit(" ran=");
    emit_hex(RanImmediate);
    emit(RanImmediate == 1 ? "  (ran once at the KeLowerIrql tail)\n"
                           : "  *** expected exactly 1 ***\n");
    emit("  queue@DISPATCH : inserted=");
    emit_hex((ULONG)Queued2);
    emit(" deferred=");
    emit_hex((ULONG)(DeferredWhileHigh == 0));
    emit(" ranAfterLower=");
    emit_hex(RanAfterLower);
    emit((DeferredWhileHigh == 0 && RanAfterLower == 1) ?
         "  (deferred, then ran once)\n" : "  *** wrong ***\n");
    emit("  DpcRoutineActive during DPC = ");
    emit_hex(KiDpcTestActiveSeen);
    emit("\n  DPC self-test ");
    emit((Queued1 && RanImmediate == 1 && Queued2 &&
          DeferredWhileHigh == 0 && RanAfterLower == 1 && KiDpcTestActiveSeen) ?
         "OK\n\n" : "*** FAIL ***\n\n");
}

#endif

//
// KiInitializeKernel halts here once all real kernel-architecture
// initialization has completed - the point at which it would otherwise call
// ExpInitializeExecutive (not yet ported). Report the initialized state from
// the live PCR/PRCB/idle objects to serial and the HDMI framebuffer.
//

VOID
KiArmReportInitialized (
    IN CCHAR Number,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    PKPCR Pcr = PCR;
    PKPRCB Prcb = Pcr->Prcb;

    KiDisplayUp = HalpInitializeDisplay0(LoaderBlock) ? 1 : 0;

    emit("Microsoft (R) Windows NT (TM)    Version 3.5  Build 782\n");
    emit("ARM32 / Raspberry Pi 2  -  ARMv7-A Cortex-A7 (BCM2836)\n");
    emit("==================================================================\n\n");
    emit("KiInitializeKernel: kernel-architecture initialization complete.\n\n");

    emit("  processor number     : ");
    emit_hex((ULONG)Number);
    emit("\n  PCR (via TPIDRPRW)   : ");
    emit_hex((ULONG)Pcr);
    emit("\n  PRCB                 : ");
    emit_hex((ULONG)Prcb);
    emit("\n  current IRQL         : ");
    emit_hex(Pcr->CurrentIrql);
    emit("\n  idle thread          : ");
    emit_hex((ULONG)Prcb->CurrentThread);
    emit("\n  idle process         : ");
    emit_hex((ULONG)Prcb->CurrentThread->ApcState.Process);
    emit("\n  idle thread state    : ");
    emit_hex((ULONG)Prcb->CurrentThread->State);
    emit("\n  active processors    : ");
    emit_hex((ULONG)KeActiveProcessors);
    emit("\n  number of processors : ");
    emit_hex((ULONG)KeNumberProcessors);
    emit("\n  interrupt vector 0   : ");
    emit_hex((ULONG)Pcr->InterruptRoutine[0]);
    emit("\n  loader block         : ");
    emit_hex((ULONG)LoaderBlock);
    emit("\n\n");

#if KI_SEH_SELFTEST
    KiSehSelfTest();
#endif

#if KI_MMU_BUILD_TEST
    MiArmReportPaging();
    MiArmInitMachineDependent(LoaderBlock);
#endif

#if KI_RUN_EXECUTIVE
    //
    // Hand off to the genuine NT executive. ExpInitializeExecutive (INIT/INIT.C)
    // runs Phase 0: HAL init, NLS translation tables, ExInitSystem, MmInitSystem,
    // ObInitSystem, SeInitSystem, PsInitSystem - the real executive subsystems,
    // bottoming out at the link-closure stubs. Interrupts are still masked (we
    // have not lowered IRQL or started the clock yet), which is exactly the Phase 0
    // contract. This is the KE/MIPS INITKR.C call, the next milestone boundary.
    //
    emit("------------------------------------------------------------------\n");
    emit("Calling the genuine ExpInitializeExecutive (Phase 0)...\n\n");
    ExpInitializeExecutive(0, LoaderBlock);
    emit("\nExpInitializeExecutive (Phase 0) RETURNED to KE/ARM.\n");
    {
        extern ULONG MiArmGetExecHonored(VOID);
        extern ULONG MiArmGetExecZeroed(VOID);
        extern ULONG MiArmGetExecPdL1Fills(VOID);
        extern ULONG MiArmGetExecPdL1Demand(VOID);
        extern ULONG MiArmGetLogPdValid(VOID);
        emit("MM logical page directory: valid PDEs MM wrote = ");
        emit_hex(MiArmGetLogPdValid());
        emit("\n  hardware L1 fills: authorized by a logical PDE = ");
        emit_hex(MiArmGetExecPdL1Fills());
        emit(", demand (no PDE yet) = ");
        emit_hex(MiArmGetExecPdL1Demand());
        emit("\n  hardware L2 fills: logical-PTE honored = ");
        emit_hex(MiArmGetExecHonored());
        emit(", demand-zero = ");
        emit_hex(MiArmGetExecZeroed());
        {
            extern ULONG MiArmGetExecDemandZero(VOID);
            extern ULONG MiArmGetExecNoPte(VOID);
            extern ULONG MiArmGetExecNoAccess(VOID);
            extern ULONG MiArmGetExecProto(VOID);
            extern ULONG MiArmGetExecTrans(VOID);
            extern ULONG MiArmGetExecPagefile(VOID);
            emit("\n  invalid-PTE classify: demand-zero = ");
            emit_hex(MiArmGetExecDemandZero());
            emit(", no-PTE = ");
            emit_hex(MiArmGetExecNoPte());
            emit(", no-access = ");
            emit_hex(MiArmGetExecNoAccess());
            emit(", proto = ");
            emit_hex(MiArmGetExecProto());
            emit(", trans = ");
            emit_hex(MiArmGetExecTrans());
            emit(", pagefile = ");
            emit_hex(MiArmGetExecPagefile());
        }
        {
            extern ULONG MiArmGetExecLogValidWrites(VOID);
            emit("\n  demand-zero resolved with a real VALID logical PTE written back = ");
            emit_hex(MiArmGetExecLogValidWrites());
        }
        {
            extern ULONG MiArmGetExecSharedData(VOID);
            extern ULONG MiArmGetExecWildL1(VOID);
            extern ULONG MiArmGetExecWildNoPte(VOID);
            extern ULONG MiArmGetExecWildVa(VOID);
            emit("\n  shared-data page faults (KUSER_SHARED_DATA scaffold) = ");
            emit_hex(MiArmGetExecSharedData());
            emit("\n  B3 wild faults (no PDE/PTE, outside any expected region): L1 = ");
            emit_hex(MiArmGetExecWildL1());
            emit(", no-PTE = ");
            emit_hex(MiArmGetExecWildNoPte());
            emit(", first VA = ");
            emit_hex(MiArmGetExecWildVa());
        }
        emit("\n");
    }
    emit("------------------------------------------------------------------\n\n");
#endif

#if KI_RUN_EXECUTIVE
    emit("KE/ARM up. Executive Phase 0 complete (Mm/Ob/Se/Ps init + version banner).\n");
    emit("Phase 1 (scheduler dispatches the Phase1Initialization thread) is next.\n");
#else
    emit("KE/ARM up. Executive (Ex/Mm/Ob/Ps/Io/Se/Cm) not yet ported.\n");
#endif
    emit("Starting the clock interrupt and entering the idle loop.\n\n");

    //
    // Enable the periodic clock interrupt and drop to PASSIVE_LEVEL (which
    // unmasks interrupts). The idle thread's body: sleep until an interrupt and
    // report the system tick as it advances - a live demonstration that the ARM
    // IRQ path and the clock are working (KeTickCount is driven by KiClockTick
    // from the real interrupt dispatch). The scheduler will replace this loop.
    //

    HalpInitializeInterrupts();
    emit("  clock vector InterruptRoutine[7] = ");
    emit_hex((ULONG)PCR->InterruptRoutine[CLOCK2_LEVEL]);
    emit(" (HalpClockInterrupt0 = ");
    emit_hex((ULONG)HalpClockInterrupt0);
    emit(")\n\n");

    KiArmStartClock();
    KeLowerIrql(PASSIVE_LEVEL);

#if KI_HAL_IRQ_SELFTEST
    KiArmInterruptGatingTest();
#endif

#if KI_DPC_SELFTEST
    KiArmDpcSelfTest();
#endif

#if KI_RUN_EXECUTIVE
    //
    // At the genuine ~10 ms NT tick (100 Hz) a per-tick line would flood the
    // console, so report once per ~1 s and show the LIVE system time the genuine
    // KeUpdateSystemTime is now writing into the KUSER_SHARED_DATA page: a
    // non-zero, monotonically-advancing KeQuerySystemTime / InterruptTime is the
    // proof that the page is live (it returned 0 before this increment).
    //
    {
        ULONG LastReport = (ULONG)KeTickCount.LowPart;
        for (;;) {
            ULONG Tick = (ULONG)KeTickCount.LowPart;
            if (Tick - LastReport >= 100) {
                LARGE_INTEGER SystemTime, InterruptTime;
                LastReport = Tick;
                KeQuerySystemTime(&SystemTime);
                KiQueryInterruptTime(&InterruptTime);
                emit("  tick=");
                emit_hex(Tick);
                emit("  SystemTime=");
                emit_hex((ULONG)SystemTime.HighPart);
                emit_hex((ULONG)SystemTime.LowPart);
                emit("  InterruptTime=");
                emit_hex((ULONG)InterruptTime.HighPart);
                emit_hex((ULONG)InterruptTime.LowPart);
                emit("\n");
            }
            __asm__ __volatile__("wfi");
        }
    }
#else
    {
        ULONG Last = 0;
        for (;;) {
            ULONG Tick = (ULONG)KeTickCount.LowPart;
            if (Tick != Last) {
                Last = Tick;
                emit("  clock tick : ");
                emit_hex(Tick);
                emit("\n");
            }
            __asm__ __volatile__("wfi");
        }
    }
#endif
}

//
// System startup C entry, called from armstart.S with the loader parameter
// block. Establishes the PCR pointer (TPIDRPRW), seeds the boot PCR/PRCB and
// idle thread enough for the PCR accessors, then enters the real
// KiInitializeKernel - the KE/MIPS X4START.S -> KiInitializeKernel contract.
//

VOID KiArmInitialize(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKPCR Pcr = &KiPcrStorage;

    KiArmInitializeVectors();
    KiSetProcessorControlRegion(Pcr);

    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;
    Pcr->Prcb = &KiPrcbStorage;
    Pcr->CurrentThread = &KiIdleThreadStorage;
    Pcr->Number = 0;
    Pcr->CurrentIrql = HIGH_LEVEL;

    KiInitializeKernel(&KiIdleProcessStorage,
                       &KiIdleThreadStorage,
                       (PVOID)(KiIdleStack + sizeof(KiIdleStack)),
                       &KiPrcbStorage,
                       0,
                       LoaderBlock);

    KeBugCheck(PHASE0_INITIALIZATION_FAILED);
}
