#ifndef _BOOTLIB_SHIM_
#define _BOOTLIB_SHIM_

//
// Shim for the NT boot-lib umbrella header (BOOT/LIB/BOOTLIB.H). The original
// pulls ntos.h + bldr.h + the five FS headers and then defines the per-device
// context structs and the BL_FILE_TABLE that the Arc I/O dispatch runs on.
// Here bldr.h chains ntos.h, fsboot.h supplies the FS context placeholders, and
// the context structs + BL_FILE_TABLE below are copied verbatim from the real
// BOOTLIB.H so ported sources that include "bootlib.h" (blio.c, later fatboot.c)
// compile unchanged.
//
#include "bldr.h"
#include "fsboot.h"

//
// Per-device context structures (verbatim from BOOTLIB.H). PARTITION_CONTEXT's
// PortDeviceObject is a pointer to an incomplete type - no definition needed.
//
typedef struct _PARTITION_CONTEXT {
    LARGE_INTEGER PartitionLength;
    ULONG StartingSector;
    ULONG EndingSector;
    UCHAR DiskId;
    UCHAR DeviceUnit;
    UCHAR TargetId;
    UCHAR PathId;
    ULONG SectorShift;
    ULONG Size;
    struct _DEVICE_OBJECT *PortDeviceObject;
} PARTITION_CONTEXT, *PPARTITION_CONTEXT;

typedef struct _SERIAL_CONTEXT {
    ULONG PortBase;
    ULONG PortNumber;
} SERIAL_CONTEXT, *PSERIAL_CONTEXT;

typedef struct _DRIVE_CONTEXT {
    ULONG Drive;
    ULONG Cylinders;
    ULONG Heads;
    ULONG Sectors;
} DRIVE_CONTEXT, *PDRIVE_CONTEXT;

typedef struct _FLOPPY_CONTEXT {
    ULONG DriveType;
    ULONG SectorsPerTrack;
    UCHAR DiskId;
} FLOPPY_CONTEXT, *PFLOPPY_CONTEXT;

typedef struct _KEYBOARD_CONTEXT {
    BOOLEAN ScanCodes;
} KEYBOARD_CONTEXT, *PKEYBOARD_CONTEXT;

typedef struct _CONSOLE_CONTEXT {
    ULONG ConsoleNumber;
} CONSOLE_CONTEXT, *PCONSOLE_CONTEXT;

//
// File table structure (verbatim from BOOTLIB.H).
//
typedef struct _BL_FILE_FLAGS {
    ULONG Open : 1;
    ULONG Read : 1;
    ULONG Write : 1;
} BL_FILE_FLAGS, *PBL_FILE_FLAGS;

#define MAXIMUM_FILE_NAME_LENGTH 32

typedef struct _BL_FILE_TABLE {
    BL_FILE_FLAGS Flags;
    ULONG DeviceId;
    LARGE_INTEGER Position;
    PVOID StructureContext;
    PBL_DEVICE_ENTRY_TABLE DeviceEntryTable;
    UCHAR FileNameLength;
    CHAR FileName[MAXIMUM_FILE_NAME_LENGTH];
    union {
        HPFS_FILE_CONTEXT HpfsFileContext;
        NTFS_FILE_CONTEXT NtfsFileContext;
        OFS_FILE_CONTEXT OfsFileContext;
        FAT_FILE_CONTEXT FatFileContext;
        CDFS_FILE_CONTEXT CdfsFileContext;
        PARTITION_CONTEXT PartitionContext;
        SERIAL_CONTEXT SerialContext;
        DRIVE_CONTEXT DriveContext;
        FLOPPY_CONTEXT FloppyContext;
        KEYBOARD_CONTEXT KeyboardContext;
        CONSOLE_CONTEXT ConsoleContext;
    } u;
} BL_FILE_TABLE, *PBL_FILE_TABLE;

extern BL_FILE_TABLE BlFileTable[BL_FILE_TABLE_SIZE];

#endif // _BOOTLIB_SHIM_
