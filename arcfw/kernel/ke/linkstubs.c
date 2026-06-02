/*++

Copyright (c) 2026

Module Name:

    linkstubs.c

Abstract:

    Bring-up stubs for the executive link-closure functions that ARE executed on
    the ExpInitializeExecutive Phase 0 path and whose result is consumed - so the
    auto-stub fixed point's uniform `return 0` would be WRONG (a bugcheck on FALSE,
    or a dereference of an uninitialized OUT parameter on STATUS_SUCCESS). These
    get a benign-but-correct return so Phase 0 proceeds. Every other closure
    function is Phase 1 / not reached in a Phase 0 boot and is left to the auto
    stub. Each is replaced by its genuine implementation as the subsystem (the
    Pi 2 HAL, the message-table resource path) is brought up.

Environment:

    Kernel mode (executive bring-up).

--*/

#include "ki.h"

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

//
// HAL phase initialization (HAL.H: BOOLEAN HalInitSystem(Phase, LoaderBlock)).
// INIT.C bugchecks (HAL_INITIALIZATION_FAILED) if this returns FALSE. Our ARM
// arch layer (kearm.c / mmuarm.c / jxdisp.c) already brought up the BCM2835
// system timer, the interrupt path, and the framebuffer + serial console - the
// work a real HAL's HalInitSystem would do - so report success for both phases.
//

BOOLEAN
HalInitSystem (
    IN ULONG Phase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    UNREFERENCED_PARAMETER(Phase);
    UNREFERENCED_PARAMETER(LoaderBlock);
    return TRUE;
}

//
// Resource message lookup. The kernel image carries no .mc message-table
// resource (mkpe.py builds a single-section PE with no .rsrc), so report
// not-found - ExpInitializeExecutive then uses its built-in literal banner
// string instead of dereferencing an uninitialized MESSAGE_RESOURCE_ENTRY that
// a STATUS_SUCCESS return would imply was filled in.
//

NTSTATUS
RtlFindMessage (
    IN PVOID DllHandle,
    IN ULONG MessageTableId,
    IN ULONG MessageLanguageId,
    IN ULONG MessageId,
    OUT PMESSAGE_RESOURCE_ENTRY *MessageEntry
    )
{
    UNREFERENCED_PARAMETER(DllHandle);
    UNREFERENCED_PARAMETER(MessageTableId);
    UNREFERENCED_PARAMETER(MessageLanguageId);
    UNREFERENCED_PARAMETER(MessageId);
    if (MessageEntry) *MessageEntry = NULL;
    return STATUS_UNSUCCESSFUL;
}
