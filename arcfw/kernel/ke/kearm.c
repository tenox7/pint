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

#undef RtlZeroMemory

//
// Boot-time self-test of the structured-exception runtime (kiseh.h / ke/seh.c /
// ke/trap.S). Exercises software raise, hardware-fault recovery, nested
// try/finally unwind, and the normal path. Set to 0 once the executive runs.
//

#ifndef KI_SEH_SELFTEST
#define KI_SEH_SELFTEST 1
#endif

#define UART0   0x3F201000u
#define UART_DR (*(volatile ULONG *)(UART0 + 0x00))
#define UART_FR (*(volatile ULONG *)(UART0 + 0x18))
#define FR_TXFF 0x20u

extern VOID HalDisplayString(PUCHAR String);
extern ULONG HalpInitializeDisplay0(PLOADER_PARAMETER_BLOCK LoaderBlock);
extern VOID KiArmInitializeVectors(VOID);
extern VOID KiArmStartClock(VOID);

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
}

VOID KiRequestSoftwareInterrupt(KIRQL RequestIrql)
{
    UNREFERENCED_PARAMETER(RequestIrql);
}

//
// Interrupt dispatch routines stored in the PCR vector table. They are placed
// during KiInitializeKernel but are not entered until real interrupt dispatch
// is wired; reaching one now means an unexpected fault, so they bug check.
//

VOID KiUnexpectedInterrupt(VOID) { KeBugCheck(TRAP_CAUSE_UNKNOWN); }
VOID KiPassiveRelease(VOID)      { KeBugCheck(TRAP_CAUSE_UNKNOWN); }
VOID KiApcInterrupt(VOID)        { KeBugCheck(TRAP_CAUSE_UNKNOWN); }
VOID KiDispatchInterrupt(VOID)   { KeBugCheck(TRAP_CAUSE_UNKNOWN); }

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

    emit("KE/ARM up. Executive (Ex/Mm/Ob/Ps/Io/Se/Cm) not yet ported.\n");
    emit("Starting the clock interrupt and entering the idle loop.\n\n");

    //
    // Enable the periodic clock interrupt and drop to PASSIVE_LEVEL (which
    // unmasks interrupts). The idle thread's body: sleep until an interrupt and
    // report the system tick as it advances - a live demonstration that the ARM
    // IRQ path and the clock are working (KeTickCount is driven by KiClockTick
    // from the real interrupt dispatch). The scheduler will replace this loop.
    //

    KiArmStartClock();
    KeLowerIrql(PASSIVE_LEVEL);

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
