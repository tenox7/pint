#ifndef _FSBOOT_SHIM_
#define _FSBOOT_SHIM_

//
// Filesystem context types for the ported boot-lib (BLIO.C, FATBOOT.C).
//
// Each FS defines two context types: a *_STRUCTURE_CONTEXT (per-volume) and a
// *_FILE_CONTEXT (per-open-file, a member of the BL_FILE_TABLE union in bootlib.h).
//
// FAT is now REAL (M2c): fatboot.h supplies FAT_STRUCTURE_CONTEXT / FAT_FILE_CONTEXT
// (and FatInitialize), built on the on-disk structures in fat.h. This port boots a
// FAT system partition only; HPFS/NTFS/CDFS/OFS stay opaque placeholders sized so an
// accidental access stays in bounds, and their detectors (declared in bldr.h) are
// stubbed to NULL in fsstub.c.
//
#include "fatboot.h"

#define FS_CTX_PLACEHOLDER_BYTES 512

typedef struct _HPFS_STRUCTURE_CONTEXT { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } HPFS_STRUCTURE_CONTEXT, *PHPFS_STRUCTURE_CONTEXT;
typedef struct _NTFS_STRUCTURE_CONTEXT { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } NTFS_STRUCTURE_CONTEXT, *PNTFS_STRUCTURE_CONTEXT;
typedef struct _CDFS_STRUCTURE_CONTEXT { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } CDFS_STRUCTURE_CONTEXT, *PCDFS_STRUCTURE_CONTEXT;
typedef struct _OFS_STRUCTURE_CONTEXT  { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } OFS_STRUCTURE_CONTEXT,  *POFS_STRUCTURE_CONTEXT;

typedef struct _HPFS_FILE_CONTEXT { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } HPFS_FILE_CONTEXT, *PHPFS_FILE_CONTEXT;
typedef struct _NTFS_FILE_CONTEXT { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } NTFS_FILE_CONTEXT, *PNTFS_FILE_CONTEXT;
typedef struct _CDFS_FILE_CONTEXT { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } CDFS_FILE_CONTEXT, *PCDFS_FILE_CONTEXT;
typedef struct _OFS_FILE_CONTEXT  { UCHAR Opaque[FS_CTX_PLACEHOLDER_BYTES]; } OFS_FILE_CONTEXT,  *POFS_FILE_CONTEXT;

//
// FS init prototypes BlIoInitialize calls. (The Is*FileStructure detectors are
// declared in bldr.h; FatInitialize is declared in fatboot.h.) The non-FAT ones are
// stubbed in arcfw/arm/fsstub.c: succeed silently, registering no filesystem.
//
ARC_STATUS HpfsInitialize(VOID);
ARC_STATUS NtfsInitialize(VOID);
ARC_STATUS CdfsInitialize(VOID);

#endif // _FSBOOT_SHIM_
