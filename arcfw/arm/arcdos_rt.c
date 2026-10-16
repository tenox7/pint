//
// arcdos runtime bridge. arcdos.c (arcfw/ported/arcdos.c) is written against the
// ADK ARC/vendor API - plain Open/Read/Write/Print/StallExecution/... - which the
// real ADK backed with a precompiled, Alpha-only adk.lib. This file supplies that
// API for the ARM loader by forwarding to the ARC firmware vector.
//
// It is the ONLY translation unit that sees both worlds' header conventions
// indirectly: it includes the loader's NT headers (the Arc* dispatch macros), and
// defines functions whose names (Open/Read/Print/...) are exactly what arcdos.c
// calls. The NT side spells these ArcOpen/ArcRead (macros) and FwOpen (protos), so
// the plain names do not collide. The ADK and NT structs/enums are layout- and
// value-compatible (verified: OPEN_MODE order, FILE_INFORMATION fields, the E*
// codes), so pointers and enums pass straight through the firmware vector.
//
#include "fwp.h"
#include <stdarg.h>
#include "stdio.h"
#include "string.h"

// ---- ARC I/O + query, forwarded to the firmware vector ---------------------

ARC_STATUS Open(const CHAR *Path, OPEN_MODE OpenMode, PULONG FileId)
{
    return ArcOpen((PCHAR)Path, OpenMode, FileId);
}

ARC_STATUS Close(ULONG FileId)
{
    return ArcClose(FileId);
}

ARC_STATUS Read(ULONG FileId, PVOID Buffer, ULONG N, PULONG Count)
{
    return ArcRead(FileId, Buffer, N, Count);
}

ARC_STATUS GetReadStatus(ULONG FileId)
{
    return ArcGetReadStatus(FileId);
}

ARC_STATUS Write(ULONG FileId, PVOID Buffer, ULONG N, PULONG Count)
{
    return ArcWrite(FileId, Buffer, N, Count);
}

ARC_STATUS GetFileInformation(ULONG FileId, PFILE_INFORMATION Information)
{
    return ArcGetFileInformation(FileId, Information);
}

ARC_STATUS SetFileInformation(ULONG FileId, ULONG AttributeFlags, ULONG AttributeMask)
{
    return ArcSetFileInformation(FileId, AttributeFlags, AttributeMask);
}

ARC_STATUS GetDirectoryEntry(ULONG FileId, PDIRECTORY_ENTRY Buffer, ULONG Length, PULONG Count)
{
    return ArcGetDirectoryEntry(FileId, Buffer, Length, Count);
}

PCHAR GetEnvironmentVariable(PCHAR Name)
{
    return ArcGetEnvironmentVariable(Name);
}

PARC_DISPLAY_STATUS GetDisplayStatus(ULONG FileId)
{
    return ArcGetDisplayStatus(FileId);
}

// ---- vendor services -------------------------------------------------------
//
// Print and printf write the formatted text to the console.
//
ULONG Print(char *Format, ...)
{
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, Format);
    n = vsprintf(buf, Format, ap);
    va_end(ap);
    Write(ARC_CONSOLE_OUTPUT, buf, (ULONG)n, (PULONG)&n);
    return (ULONG)n;
}

int printf(const char *Format, ...)
{
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, Format);
    n = vsprintf(buf, (char *)Format, ap);
    va_end(ap);
    Write(ARC_CONSOLE_OUTPUT, buf, (ULONG)n, (PULONG)&n);
    return n;
}

//
// StallExecution - the vendor stall routine.
//
void StallExecution(ULONG Microseconds)
{
    FwStallExecution(Microseconds);
}
