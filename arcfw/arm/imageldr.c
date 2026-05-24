//
// PE image helpers the OS loader's PE loader (BOOT/LIB/PELDR.C) calls. On NT
// these live in NTRTL/the loader (RtlImageNtHeader, RtlImageDirectoryEntryToData)
// and ntldr (LdrRelocateImage); none are in BOOT/LIB, so the ARM port supplies
// them here. They are small and arch-independent - just PE-header arithmetic.
//
// RtlImageNtHeader replaces the stubs.c placeholder (which returned NULL, so
// BlLoadImage rejected every image as EBADF). Now it parses a real PE.
//
#include "bldr.h"
#include "ntimage.h"

//
// Validate the DOS/PE signatures and return the PE header, or NULL. Mirrors the
// real RtlImageNtHeader closely enough for the boot loader's needs.
//
PIMAGE_NT_HEADERS
RtlImageNtHeader(
    IN PVOID Base
    )
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;

    DosHeader = (PIMAGE_DOS_HEADER)Base;
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }

    NtHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)Base + DosHeader->e_lfanew);
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }

    return NtHeaders;
}

//
// Return a pointer to the data of one of the image's data directories (and its
// size), or NULL if that directory is empty. BlLoadImage uses it only for the
// base-relocation directory; our images carry none, so this returns NULL and
// BlLoadImage skips the relocation-fixup bookkeeping. MappedAsImage is always
// TRUE for us (the image is mapped), so the directory RVA is an offset from Base.
//
PVOID
RtlImageDirectoryEntryToData(
    IN PVOID Base,
    IN BOOLEAN MappedAsImage,
    IN USHORT DirectoryEntry,
    OUT PULONG Size
    )
{
    PIMAGE_NT_HEADERS NtHeaders;
    ULONG VirtualAddress;

    UNREFERENCED_PARAMETER(MappedAsImage);

    NtHeaders = RtlImageNtHeader(Base);
    if (NtHeaders == NULL ||
        DirectoryEntry >= NtHeaders->OptionalHeader.NumberOfRvaAndSizes) {
        *Size = 0;
        return NULL;
    }

    VirtualAddress = NtHeaders->OptionalHeader.DataDirectory[DirectoryEntry].VirtualAddress;
    if (VirtualAddress == 0) {
        *Size = 0;
        return NULL;
    }

    *Size = NtHeaders->OptionalHeader.DataDirectory[DirectoryEntry].Size;
    return (PVOID)((PUCHAR)Base + VirtualAddress);
}

//
// Guard stub: BlLoadImage calls this only when an image cannot load at its
// preferred ImageBase (peldr.c:409, NewImageBase != ImageBase). The ARM port
// loads every image at its preferred base - BlAllocateDescriptor honors the exact
// page (blmemory.c) and the stand-in kernel's ImageBase (0x01000000) is free in
// every build - so this never runs. If it ever does, the kernel base collided
// with the arcfw/ramdisk and the right fix is to move the base, not to relocate.
//
ULONG
NTAPI
LdrRelocateImage(
    IN PVOID NewBase,
    IN PUCHAR LoaderName,
    IN ULONG Success,
    IN ULONG Conflict,
    IN ULONG Invalid
    )
{
    UNREFERENCED_PARAMETER(NewBase);
    UNREFERENCED_PARAMETER(LoaderName);
    UNREFERENCED_PARAMETER(Success);
    UNREFERENCED_PARAMETER(Conflict);

    BlPrint("LdrRelocateImage: image did not load at its preferred base - "
            "relocation is not supported (move the image base)\n");
    return Invalid;
}
