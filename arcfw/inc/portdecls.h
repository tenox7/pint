#ifndef _PORTDECLS_
#define _PORTDECLS_

//
// Prototypes/constants the loader references that normally arrive via the full
// kernel header set (ke.h, ntrtl.h, hal.h) which our minimal ntos.h does not
// pull in. Signatures match the originals. Implemented in arcfw/arm/{clib,
// stubs}.c. Requires the NT type headers (ntimage, arc) first.
//

//
// MBR NTFT disk-signature location, as a ULONG index into the boot sector.
// From PRIVATE/NTOS/INC/HAL.H and BOOT/DETECT/I386/DISK.H.
//
#define PARTITION_TABLE_OFFSET (0x1be / 2)

//
// ntrtl.h. RtlConvertUlongToLargeInteger is inline x86 _asm in the original,
// so the loader provides its own definition (clib.c).
//
PIMAGE_NT_HEADERS RtlImageNtHeader(IN PVOID Base);
LARGE_INTEGER RtlConvertUlongToLargeInteger(IN ULONG UnsignedInteger);
VOID RtlInitString(OUT PSTRING DestinationString, IN PCHAR SourceString);

//
// ntos image helper BlLoadImage (BOOT/LIB/PELDR.C) calls; implemented real in
// arcfw/arm/imageldr.c. (LdrRelocateImage is already declared in ntldr.h; the
// imageldr.c guard definition matches that signature.)
//
PVOID RtlImageDirectoryEntryToData(IN PVOID Base, IN BOOLEAN MappedAsImage,
                                   IN USHORT DirectoryEntry, OUT PULONG Size);

//
// ke.h - searches the ARC configuration tree.
//
PCONFIGURATION_COMPONENT_DATA KeFindConfigurationEntry(
    IN PCONFIGURATION_COMPONENT_DATA Child,
    IN CONFIGURATION_CLASS Class,
    IN CONFIGURATION_TYPE Type,
    IN PULONG Key OPTIONAL
    );

//
// ntrtl.h doubly-linked list macros, copied verbatim from PUBLIC/SDK/INC/NTRTL.H
// (BLMEMORY.C builds the LoaderBlock memory-descriptor list with these). They
// operate on the LIST_ENTRY defined in ntdef.h, included earlier in the chain.
//
#define InitializeListHead(ListHead) (\
    (ListHead)->Flink = (ListHead)->Blink = (ListHead))

#define IsListEmpty(ListHead) \
    ((ListHead)->Flink == (ListHead))

#define RemoveEntryList(Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_Flink;\
    _EX_Flink = (Entry)->Flink;\
    _EX_Blink = (Entry)->Blink;\
    _EX_Blink->Flink = _EX_Flink;\
    _EX_Flink->Blink = _EX_Blink;\
    }

#define InsertTailList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Blink = _EX_ListHead->Blink;\
    (Entry)->Flink = _EX_ListHead;\
    (Entry)->Blink = _EX_Blink;\
    _EX_Blink->Flink = (Entry);\
    _EX_ListHead->Blink = (Entry);\
    }

#define InsertHeadList(ListHead,Entry) {\
    PLIST_ENTRY _EX_Flink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Flink = _EX_ListHead->Flink;\
    (Entry)->Flink = _EX_Flink;\
    (Entry)->Blink = _EX_ListHead;\
    _EX_Flink->Blink = (Entry);\
    _EX_ListHead->Flink = (Entry);\
    }

//
// ntrtl.h memory macros (NTRTL.H forms). They expand to the freestanding
// mem* in clib.c, so a translation unit using them must include "string.h".
//
#define RtlMoveMemory(Destination,Source,Length) memmove((Destination),(Source),(Length))
#define RtlCopyMemory(Destination,Source,Length) memcpy((Destination),(Source),(Length))
#define RtlFillMemory(Destination,Length,Fill)   memset((Destination),(Fill),(Length))
#define RtlZeroMemory(Destination,Length)         memset((Destination),0,(Length))

#endif // _PORTDECLS_
