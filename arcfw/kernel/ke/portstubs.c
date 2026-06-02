/*++

Copyright (c) 2026

Module Name:

    portstubs.c

Abstract:

    Bring-up stubs for executive entry points that are either not applicable to
    ARMv7 (the x86 LDT/VDM process services) or belong to subsystems not yet
    brought up (the kernel debugger, the debug port). They are part of the
    executive link closure; defining them as no-ops / STATUS_NOT_IMPLEMENTED lets
    ExpInitializeExecutive link. Each is replaced by its genuine implementation
    (or stays a correct ARM no-op for the x86-only ones) as the subsystems land.

    NOT stubbed here: real functions that must work (e.g. PspCreateProcess) - they
    come from their genuine source file, not a stub.

Environment:

    Kernel mode.

--*/

#include "ki.h"
#include "ps.h"

#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif

//
// Kernel debugger / debug print - no debugger attached during bring-up.
//

VOID DbgBreakPoint (VOID) { }

ULONG DbgPrint (PCH Format, ...) { UNREFERENCED_PARAMETER(Format); return 0; }

VOID DbgLoadImageSymbols (PSTRING FileName, PVOID ImageBase, ULONG ProcessId) { }

VOID DbgUnLoadImageSymbols (PSTRING FileName, PVOID ImageBase, ULONG ProcessId) { }

//
// Debug subsystem (no debug port) - process/thread exit + section-unmap notices.
//

VOID DbgkExitThread (NTSTATUS ExitStatus) { }

VOID DbgkExitProcess (NTSTATUS ExitStatus) { }

VOID DbgkUnMapViewOfSection (IN PVOID BaseAddress) { }

//
// Configuration Manager machine-dependent setup - ARM has no x86/HAL-specific
// registry hardware tree to build beyond the firmware config tree.
//

NTSTATUS
CmpInitializeMachineDependentConfiguration (IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    return STATUS_SUCCESS;
}

//
// x86 LDT / V86 (VDM) process services - no such hardware on ARMv7.
//

VOID PspDeleteLdt (IN PEPROCESS Process) { }

NTSTATUS
PspQueryLdtInformation (IN PEPROCESS Process, OUT PVOID LdtInformation,
                        IN ULONG LdtInformationLength, OUT PULONG ReturnLength)
{
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
PspSetLdtInformation (IN PEPROCESS Process, IN PVOID LdtInformation, IN ULONG LdtInformationLength)
{
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
PspSetLdtSize (IN PEPROCESS Process, IN PVOID LdtSize, IN ULONG LdtSizeLength)
{
    return STATUS_NOT_IMPLEMENTED;
}

VOID PspDeleteVdmObjects (IN PEPROCESS Process) { }

VOID
PspGetSetContextSpecialApc (IN PKAPC Apc, IN OUT PKNORMAL_ROUTINE *NormalRoutine,
                            IN OUT PVOID *NormalContext, IN OUT PVOID *SystemArgument1,
                            IN OUT PVOID *SystemArgument2)
{
}

NTSTATUS
PspQueryDescriptorThread (PETHREAD Thread, PVOID ThreadInformation,
                          ULONG ThreadInformationLength, PULONG ReturnLength)
{
    if (ReturnLength) *ReturnLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

//
// Cache manager - not brought up in Phase 0.
//

VOID CcZeroEndOfLastPage (IN PFILE_OBJECT FileObject) { }

//
// FsRtl file locking / size - no file-system drivers in Phase 0.
//

VOID FsRtlAcquireFileExclusive (IN PFILE_OBJECT FileObject) { }
VOID FsRtlReleaseFile (IN PFILE_OBJECT FileObject) { }
NTSTATUS FsRtlGetFileSize (IN PFILE_OBJECT FileObject, IN OUT PLARGE_INTEGER FileSize) { return STATUS_NOT_IMPLEMENTED; }
NTSTATUS FsRtlSetFileSize (IN PFILE_OBJECT FileObject, IN OUT PLARGE_INTEGER FileSize) { return STATUS_NOT_IMPLEMENTED; }
BOOLEAN FsRtlIsTotalDeviceFailure (IN NTSTATUS Status) { return FALSE; }

//
// HAL profiling - no profile timer wired (PROFOBJ.C references these).
//

ULONG HalSetProfileInterval (IN ULONG Interval) { return Interval; }
VOID HalStartProfileInterrupt (ULONG Reserved) { }
VOID HalStopProfileInterrupt (ULONG Reserved) { }

//
// Ke: ARM has no per-process alignment toggle (alignment faults handled in trap);
// the balance-set manager (working-set trim thread) is not run during bring-up.
//

BOOLEAN KeSetAutoAlignmentProcess (IN PKPROCESS Process, IN BOOLEAN Enable) { return FALSE; }
BOOLEAN KeSetAutoAlignmentThread (IN PKTHREAD Thread, IN BOOLEAN Enable) { return FALSE; }
VOID KeBalanceSetManager (IN PVOID Context) { }
