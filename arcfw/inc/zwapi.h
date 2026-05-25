#ifndef _ARM_ZWAPI_H_
#define _ARM_ZWAPI_H_

//
// ARM port substitute for the NT Zw* API header.
//
// cmp.h includes "zwapi.h" and embeds the registry query/value structures
// (KEY_*_INFORMATION, KEY_VALUE_*_INFORMATION) and PIO_STATUS_BLOCK as members
// of runtime-registry structures (CM_KEY_BODY, CM_POST_BLOCK, the KEY_INFORMATION
// unions). The boot-time CM subset (bconfig.lib's 13 files) references NONE of
// those types (verified), but C requires them complete for cmp.h to parse. So we
// supply just the type definitions here, copied verbatim from PUBLIC/SDK/INC/
// NTREGAPI.H (key query structures) + the NT IO_STATUS_BLOCK. We deliberately
// do NOT declare the Zw* entry points: the boot subset makes zero Zw calls.
//
// Dual use (loader vs kernel/executive): the LOADER build never includes the
// real NTIOAPI.H / NTREGAPI.H, so this shim provides those types. The EXECUTIVE
// build pulls the real NT.H chain first (which defines them and sets _NTIOAPI_ /
// _NTREGAPI_), so each block below DEFERS to the real header when its guard is
// set - avoiding the redefinition that otherwise breaks the executive compile.
//

//
// NT I/O status block (NTIOAPI.H). NT 3.5 form: Status + Information.
//
#ifndef _NTIOAPI_
typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
#endif // _NTIOAPI_

//
// Key query structures (PUBLIC/SDK/INC/NTREGAPI.H:134-211, verbatim).
//
#ifndef _NTREGAPI_
typedef struct _KEY_BASIC_INFORMATION {
    LARGE_INTEGER LastWriteTime;
    ULONG   TitleIndex;
    ULONG   NameLength;
    WCHAR   Name[1];            // Variable length string
} KEY_BASIC_INFORMATION, *PKEY_BASIC_INFORMATION;

typedef struct _KEY_NODE_INFORMATION {
    LARGE_INTEGER LastWriteTime;
    ULONG   TitleIndex;
    ULONG   ClassOffset;
    ULONG   ClassLength;
    ULONG   NameLength;
    WCHAR   Name[1];            // Variable length string
} KEY_NODE_INFORMATION, *PKEY_NODE_INFORMATION;

typedef struct _KEY_FULL_INFORMATION {
    LARGE_INTEGER LastWriteTime;
    ULONG   TitleIndex;
    ULONG   ClassOffset;
    ULONG   ClassLength;
    ULONG   SubKeys;
    ULONG   MaxNameLen;
    ULONG   MaxClassLen;
    ULONG   Values;
    ULONG   MaxValueNameLen;
    ULONG   MaxValueDataLen;
    WCHAR   Class[1];           // Variable length
} KEY_FULL_INFORMATION, *PKEY_FULL_INFORMATION;

typedef enum _KEY_INFORMATION_CLASS {
    KeyBasicInformation,
    KeyNodeInformation,
    KeyFullInformation
} KEY_INFORMATION_CLASS;

typedef struct _KEY_WRITE_TIME_INFORMATION {
    LARGE_INTEGER LastWriteTime;
} KEY_WRITE_TIME_INFORMATION, *PKEY_WRITE_TIME_INFORMATION;

typedef enum _KEY_SET_INFORMATION_CLASS {
    KeyWriteTimeInformation
} KEY_SET_INFORMATION_CLASS;

typedef struct _KEY_VALUE_BASIC_INFORMATION {
    ULONG   TitleIndex;
    ULONG   Type;
    ULONG   NameLength;
    WCHAR   Name[1];            // Variable size
} KEY_VALUE_BASIC_INFORMATION, *PKEY_VALUE_BASIC_INFORMATION;

typedef struct _KEY_VALUE_FULL_INFORMATION {
    ULONG   TitleIndex;
    ULONG   Type;
    ULONG   DataOffset;
    ULONG   DataLength;
    ULONG   NameLength;
    WCHAR   Name[1];            // Variable size
} KEY_VALUE_FULL_INFORMATION, *PKEY_VALUE_FULL_INFORMATION;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
    ULONG   TitleIndex;
    ULONG   Type;
    ULONG   DataLength;
    UCHAR   Data[1];            // Variable size
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

typedef enum _KEY_VALUE_INFORMATION_CLASS {
    KeyValueBasicInformation,
    KeyValueFullInformation,
    KeyValuePartialInformation
} KEY_VALUE_INFORMATION_CLASS;
#endif // _NTREGAPI_

#endif // _ARM_ZWAPI_H_
