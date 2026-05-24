//
// arcdos runtime bridge. arcdos.c (arcfw/ported/arcdos.c) is written against the
// ADK ARC/vendor API - plain Open/Read/Write/Print/StallExecution/... - which the
// real ADK backed with a precompiled, Alpha-only adk.lib. This file supplies that
// API for the ARM loader by forwarding to the emulated ARC firmware vector.
//
// It is the ONLY translation unit that sees both worlds' header conventions
// indirectly: it includes the loader's NT headers (the Arc* dispatch macros), and
// defines functions whose names (Open/Read/Print/...) are exactly what arcdos.c
// calls. The NT side spells these ArcOpen/ArcRead (macros) and FwOpen (protos), so
// the plain names do not collide. The ADK and NT structs/enums are layout- and
// value-compatible (verified: OPEN_MODE order, FILE_INFORMATION fields, the E*
// codes), so pointers and enums pass straight through the firmware vector.
//
#include "bldr.h"
#include "stdio.h"

void console_puts(const char *s);
int  vsprintf(char *buf, const char *fmt, va_list ap);
void udelay(unsigned int us);
void mdelay(unsigned int ms);

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
// Print/printf render into a stack buffer and push through the unified console
// (serial + the HDMI framebuffer's ANSI engine), the same sink BlPrint and the
// ArcWrite backend use - so the CSI cursor/erase sequences arcdos emits are
// interpreted by fbcon.
//
ULONG Print(char *Format, ...)
{
    char buf[512];
    va_list ap;
    int n;

    va_start(ap, Format);
    n = vsprintf(buf, Format, ap);
    va_end(ap);
    console_puts(buf);
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
    console_puts(buf);
    return n;
}

//
// StallExecution - busy-wait the requested microseconds over the BCM2835 system
// timer (arcfw/arm/timer.c). arcdos uses it for the escape-key debounce (10 ms)
// and the exit message pause (4 s); split on the ms boundary so neither path
// overflows the µs counter.
//
void StallExecution(ULONG Microseconds)
{
    if (Microseconds >= 1000)
        mdelay(Microseconds / 1000);
    else
        udelay(Microseconds);
}
