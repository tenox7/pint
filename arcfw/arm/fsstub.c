//
// Filesystem stubs for the ported BLIO.C. This port boots from FAT only (the NT
// system partition on the Pi is FAT), so the non-FAT filesystems are never
// implemented: their initializers succeed silently (nothing to register) and their
// detectors report "not my filesystem" (NULL).
//
// FAT itself is now REAL: FatInitialize and IsFatFileStructure live in the ported
// arcfw/ported/fatboot.c (M2c) and are NOT stubbed here.
//
// Signatures match the declarations in bldr.h (Is*FileStructure) and fsboot.h
// (the *Initialize routines); the detector StructureContext arg is PVOID there.
//
#include "bldr.h"

ARC_STATUS HpfsInitialize(VOID) { return ESUCCESS; }
ARC_STATUS NtfsInitialize(VOID) { return ESUCCESS; }
ARC_STATUS CdfsInitialize(VOID) { return ESUCCESS; }

PBL_DEVICE_ENTRY_TABLE IsHpfsFileStructure(ULONG DeviceId, PVOID StructureContext) { return NULL; }
PBL_DEVICE_ENTRY_TABLE IsNtfsFileStructure(ULONG DeviceId, PVOID StructureContext) { return NULL; }
PBL_DEVICE_ENTRY_TABLE IsCdfsFileStructure(ULONG DeviceId, PVOID StructureContext) { return NULL; }
PBL_DEVICE_ENTRY_TABLE IsOfsFileStructure(ULONG DeviceId, PVOID StructureContext)  { return NULL; }
