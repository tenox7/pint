//
// ARM physical memory map - the data behind AEGetMemoryDescriptor (arcemul.c),
// i.e. the ARM analog of I386/MEMORY.C, which on x86 builds this table from the
// BIOS e820 map. The Pi has no BIOS, so we hardcode QEMU raspi2b's layout; a real
// map via mailbox GET_ARM_MEMORY (0x3F00B880) comes later.
//
// BlMemoryInitialize (BOOT/LIB/BLMEMORY.C) walks this list through the firmware
// vector. It REQUIRES exactly one MemoryLoadedProgram descriptor - the loader's
// own image - or it returns ENOMEM; it then carves its heap+stack from the top
// of a MemoryFree region. Page numbers are physical (>>12); with the MMU off and
// KSEG0_BASE == 0 (bldr.h), the loader touches them as identity addresses.
//
// Layout (QEMU raspi2b, -m 1G):
//   [0x00000000, 0x00010000)  64 KiB  low memory: page 0, QEMU boot stub,
//                                     exception vectors        -> FirmwarePermanent
//   [0x00010000, 0x00100000)  ~1 MiB  the loader image + BSS + start.S stack
//                                     (conservative fixed span) -> LoadedProgram
//   [0x00100000, 0x38000000)  ~895MiB free RAM (heap carved from its top)
//                                                              -> Free
//
// 0x38000000 (896 MiB) is a deliberately conservative ceiling below the 1 GiB
// top that the VideoCore reserves; mailbox query refines it later.
//
// NB (trap for the kernel-handoff phase): BlMemoryInitialize records a
// LoaderOsloaderStack descriptor near the top of the free region (EndPage -
// BL_STACK_PAGES), which is NOT our real C stack - that lives in the loader
// region, set by start.S and never switched. Harmless until kernel handoff, which
// must either switch sp to the recorded region before BlOsLoader or correct the
// descriptor.
//
#include "bldr.h"

#ifndef RAMDISK_INITRAMFS
#define RAMDISK_INITRAMFS 0
#endif

#if RAMDISK_INITRAMFS
void RamdiskRegion(unsigned long *Base, unsigned long *Bytes);
#endif

#define RAM_TOP 0x38000000u

MEMORY_DESCRIPTOR MDArray[] = {
    { MemoryFirmwarePermanent, 0x000, 0x010 },
    { MemoryLoadedProgram,     0x010, 0x0F0 },
    { MemoryFree,              0x100, (RAM_TOP >> PAGE_SHIFT) - 0x100 },
};

ULONG NumberDescriptors = sizeof(MDArray) / sizeof(MDArray[0]);

//
// Grow the LoadedProgram descriptor to cover the *actual* image span and push Free
// past it. The 0x100000 (1 MiB) figure above predates the embedded ramdisk image:
// the loader image + blobs + stack now reach _stack_top (~6.4 MiB, and growing with
// the blob), and that span physically lives inside what MDArray marks Free. Heap is
// carved from the top of Free so nothing collides today, but a low free-page
// allocation would silently corrupt the ramdisk blob or the stack. Resizing from the
// linker's _stack_top keeps the map correct regardless of image size. Call before
// BlMemoryInitialize (it walks MDArray through AEGetMemoryDescriptor).
//
extern char _stack_top[];

VOID
BlArmFixupMemoryMap(
    VOID
    )
{
    ULONG endPage;

#if RAMDISK_INITRAMFS
    //
    // The firmware-staged ramdisk lives at a fixed address OUTSIDE the loader image
    // (unlike the embedded blob, which the _stack_top span already covers). Extend the
    // single LoadedProgram descriptor to span the loader AND the staged image, so
    // BlAllocateHeap - which carves from Free - cannot hand out the image's pages. The
    // gap between _stack_top and the image is marked LoadedProgram too: harmless map
    // bookkeeping (not lost RAM), and exactly one LoadedProgram descriptor remains, as
    // BlMemoryInitialize requires.
    //
    unsigned long base, bytes;

    RamdiskRegion(&base, &bytes);
    endPage = (((ULONG)(base + bytes)) >> PAGE_SHIFT) + 0x40;     // +256 KiB headroom
#else
    endPage = (((ULONG)_stack_top) >> PAGE_SHIFT) + 0x40;        // +256 KiB headroom
#endif

    MDArray[1].PageCount = endPage - MDArray[1].BasePage;         // LoadedProgram: 0x10..endPage
    MDArray[2].BasePage  = endPage;                               // Free: endPage..RAM_TOP
    MDArray[2].PageCount = (RAM_TOP >> PAGE_SHIFT) - endPage;
}
