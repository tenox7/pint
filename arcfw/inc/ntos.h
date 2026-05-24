#ifndef _NTOS_
#define _NTOS_

//
// ARM port substitute for the NT kernel umbrella header.
//
// The real PRIVATE/NTOS/INC/NTOS.H pulls in the entire kernel (ke/io/mm/ob/cm/
// hal/...) plus a per-architecture machine header (i386.h/mips.h/alpha.h/ppc.h
// — there is no arm.h). None of that is needed to build the ARC loader, and
// most of it cannot compile without a full ARM kernel MD layer.
//
// The loader needs only: the base NT types, the PE image format, the loader
// data-table entry, and the ARC firmware contract. We include exactly those.
// All headers here are lowercase-named copies under arcfw/inc so the build is
// case-correct on Linux (Docker) where the original UPPERCASE.H names and the
// lowercase #include "..." spellings would not match.
//

#include "ntshim.h"     // neutralize MSVC keywords (must precede the NT headers)
#include "ntdef.h"      // base types: ULONG, PVOID, UNICODE_STRING, LARGE_INTEGER, ...
#include "nttypes.h"    // DEVICE_FLAGS, TIME_FIELDS, CONTEXT (normally from NTCONFIG/NTRTL/NT<arch>)
#include "ntimage.h"    // PE/COFF image format: IMAGE_NT_HEADERS, ...
#include "ntldr.h"      // LDR_DATA_TABLE_ENTRY (uses PCONTEXT)
#include "arc.h"        // ARC firmware contract: FIRMWARE_ENTRY, SYSTEM_BLOCK, Arc*, LOADER_PARAMETER_BLOCK (uses DEVICE_FLAGS, PTIME_FIELDS)
#include "portdecls.h"  // ke.h/ntrtl.h/hal.h helpers the loader references

#endif // _NTOS_
