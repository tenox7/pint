#ifndef _NTTYPES_EXT_
#define _NTTYPES_EXT_

//
// A handful of NT types that ARC.H / NTLDR.H reference but that normally arrive
// from headers included earlier in the full NTOS.H chain (NTCONFIG.H, NTRTL.H,
// the per-arch NT<arch>.H). We pull in those whole headers' worth of machinery
// is unnecessary for the loader, so the few needed types are reproduced here
// verbatim from their originals. Requires ntdef.h first (ULONG, CSHORT).
//

//
// DEVICE_FLAGS - from PUBLIC/SDK/INC/NTCONFIG.H. Embedded in
// CONFIGURATION_COMPONENT (arc.h); layout must match the original.
//
typedef struct _DEVICE_FLAGS {
    ULONG Failed : 1;
    ULONG ReadOnly : 1;
    ULONG Removable : 1;
    ULONG ConsoleIn : 1;
    ULONG ConsoleOut : 1;
    ULONG Input : 1;
    ULONG Output : 1;
} DEVICE_FLAGS, *PDEVICE_FLAGS;

//
// TIME_FIELDS - from PUBLIC/SDK/INC/NTRTL.H. Used by the ARC GetTime routine
// type in arc.h.
//
typedef struct _TIME_FIELDS {
    CSHORT Year;        // range [1601...]
    CSHORT Month;       // range [1..12]
    CSHORT Day;         // range [1..31]
    CSHORT Hour;        // range [0..23]
    CSHORT Minute;      // range [0..59]
    CSHORT Second;      // range [0..59]
    CSHORT Milliseconds;// range [0..999]
    CSHORT Weekday;     // range [0..6] == [Sunday..Saturday]
} TIME_FIELDS;
typedef TIME_FIELDS *PTIME_FIELDS;

//
// CONTEXT - the machine register context. Real definition is per-architecture
// (NTI386.H etc.); there is no ARM variant in this NT 3.5 tree. The loader only
// needs the name to type-check a DLL-init thunk pointer it never invokes, so an
// incomplete type (opaque pointer) is sufficient.
//
typedef struct _CONTEXT CONTEXT, *PCONTEXT;

#endif // _NTTYPES_EXT_
