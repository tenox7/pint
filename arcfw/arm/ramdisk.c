//
// RAM-disk Arc block device - the ScsiDisk backend analog (BOOT/LIB/I386/SCSIDISK.C
// + the ScsiDiskEntryTable that I386/ARCEMUL.C installs). It serves a whole disk
// image (MBR + a FAT16 partition) as 512-byte sectors behind a BL_DEVICE_ENTRY_TABLE.
// The FAT layer (FATBOOT.C) reads sectors through it via ArcSeek/ArcRead with no idea
// the bytes come from RAM.
//
// The image reaches RAM one of two ways, chosen at build time by RAMDISK_INITRAMFS:
//
//   * QEMU (RAMDISK_INITRAMFS=0, default): the image is linked into the loader as a
//     blob (arcfw/ramdisk/make-ramdisk.sh -> obj/ramdisk.img, wrapped by the
//     Makefile). objcopy emits it in .rodata; with the MMU off that is ordinary RAM.
//     QEMU has no Pi firmware to stage an initramfs, so the embedded image is used.
//
//   * Real hardware (RAMDISK_INITRAMFS=1): the Raspberry Pi GPU firmware stages the
//     image in RAM for us, exactly as it loads an initrd for Linux. config.txt carries
//         initramfs ramdisk.img 0x00800000
//     so the firmware loads ramdisk.img to a fixed address before entering the loader
//     at 0x8000. The loader therefore needs NO SD driver of its own - the firmware's
//     proven SD+FAT reader does the device I/O, and we serve the staged image from RAM
//     through the same BL_DEVICE_ENTRY_TABLE. A real bcm2835 SD driver would drop in
//     behind this same table only if a later phase must read the card at runtime.
//
// A device fid's state lives in its BL_FILE_TABLE entry, exactly as on x86:
//   u.PartitionContext.StartingSector  - partition base LBA (0 for the whole disk)
//   u.PartitionContext.PartitionLength - partition byte length
//   Position                           - current byte offset within the partition
// Reads are pure memcpy out of the image (no sector alignment needed - it is RAM),
// so FatDiskRead's arbitrary byte counts are served directly.
//
#include "bootlib.h"
#include "string.h"

void BlPrint(PCHAR, ...);

#ifndef RAMDISK_INITRAMFS
#define RAMDISK_INITRAMFS 0
#endif

#if RAMDISK_INITRAMFS
//
// Fixed address the firmware stages the image at; MUST match config.txt's
// "initramfs ramdisk.img 0x00800000". 8 MiB sits above the (blob-less) loader image +
// stack - which end well under 1 MiB - and below the device tree at 0x02000000.
//
#define RAMDISK_LOAD_ADDR 0x00800000u
#else
extern unsigned char _binary_ramdisk_img_start[];
extern unsigned char _binary_ramdisk_img_end[];
#endif

//
// The image base and byte length, filled by RamdiskInit() before any read or the
// memory-map fixup. rd_bytes bounds every read (0 = no usable image -> reads fail EIO).
//
static unsigned char *rd_base;
static ULONG rd_bytes;

#if RAMDISK_INITRAMFS
//
// Read a little-endian 32-bit MBR field through a volatile byte pointer so GCC cannot
// fuse the four bytes into one LDR/LDRD at an unaligned address (ARCHITECTURE.md
// §4 "LDM/STM alignment"); partition LBAs at 0x1C6 etc. are unaligned.
//
static ULONG
le32(const unsigned char *p)
{
    const volatile unsigned char *v = p;
    return (ULONG)v[0] | ((ULONG)v[1] << 8) | ((ULONG)v[2] << 16) | ((ULONG)v[3] << 24);
}
#endif

//
// Establish rd_base/rd_bytes. Called from BlStartup after the ARC emulator init and
// before BlArmFixupMemoryMap (which reads RamdiskRegion to reserve the image from the
// loader heap). Idempotent.
//
VOID
RamdiskInit(
    VOID
    )
{
#if RAMDISK_INITRAMFS
    unsigned char *img = (unsigned char *)RAMDISK_LOAD_ADDR;
    ULONG maxEnd = 0;
    int i;

    rd_base = img;

    //
    // The firmware-staged image must carry the MBR make-ramdisk.sh wrote. A missing
    // signature means the firmware did not stage the file - almost always a config.txt
    // typo or a missing ramdisk.img on the boot partition. Say so plainly (this lands
    // on HDMI through console_putc) rather than failing later as an opaque FAT error.
    //
    if (img[510] != 0x55 || img[511] != 0xAA) {
        BlPrint("RAMDISK: MBR signature missing at 0x%08lx - check config.txt\n"
                "         'initramfs ramdisk.img 0x%08lx' and that ramdisk.img is on\n"
                "         the boot partition.\n",
                (unsigned long)RAMDISK_LOAD_ADDR, (unsigned long)RAMDISK_LOAD_ADDR);
        rd_bytes = 0;
        return;
    }

    //
    // Size the image from its own partition table: the highest partition end
    // (start LBA + sector count) is the last sector the image must contain.
    //
    for (i = 0; i < 4; i += 1) {
        const unsigned char *e = &img[0x1BE + i * 16];
        ULONG start = le32(e + 8);
        ULONG count = le32(e + 12);
        if (count != 0 && (start + count) > maxEnd) {
            maxEnd = start + count;
        }
    }
    rd_bytes = maxEnd * SECTOR_SIZE;
    BlPrint("RAMDISK: firmware-staged image at 0x%08lx, %lu KiB (initramfs)\n",
            (unsigned long)RAMDISK_LOAD_ADDR, (unsigned long)(rd_bytes / 1024));
#else
    rd_base  = _binary_ramdisk_img_start;
    rd_bytes = (ULONG)(_binary_ramdisk_img_end - _binary_ramdisk_img_start);
#endif
}

//
// Report the image's RAM span so BlArmFixupMemoryMap (memory.c) can keep the loader
// heap off it. In the QEMU build the span lies inside the loader image already; in the
// initramfs build it is a separate region the map must reserve.
//
VOID
RamdiskRegion(
    OUT unsigned long *Base,
    OUT unsigned long *Bytes
    )
{
    *Base  = (unsigned long)rd_base;
    *Bytes = (unsigned long)rd_bytes;
}

//
// Raw sector read straight off the image. lba/count are whole-disk sectors (the
// caller adds any partition offset). Returns ESUCCESS, or EIO past the end.
//
ARC_STATUS
RamdiskReadSectors(
    IN ULONG Lba,
    IN ULONG Count,
    OUT PVOID Buffer
    )
{
    ULONG offset = Lba * SECTOR_SIZE;
    ULONG bytes  = Count * SECTOR_SIZE;

    if ((offset + bytes) > rd_bytes) {
        return EIO;
    }

    memcpy(Buffer, rd_base + offset, bytes);
    return ESUCCESS;
}

ULONG
RamdiskTotalSectors(
    VOID
    )
{
    return rd_bytes / SECTOR_SIZE;
}

//
// BL_DEVICE_ENTRY_TABLE routines. These mirror ScsiDiskRead/Seek/Close: they act
// on the open fid's BL_FILE_TABLE entry and are reached only through the table
// (AERead/AESeek/AEClose dispatch into them for non-console fids).
//

static ARC_STATUS
RamdiskRead(
    IN ULONG FileId,
    OUT PVOID Buffer,
    IN ULONG Length,
    OUT PULONG Count
    )
{
    PBL_FILE_TABLE f = &BlFileTable[FileId];
    ULONG part = f->u.PartitionContext.StartingSector * SECTOR_SIZE;
    ULONG pos  = f->Position.LowPart;            // HighPart is 0 for our small disk

    if ((part + pos + Length) > rd_bytes) {
        *Count = 0;
        return EIO;
    }

    memcpy(Buffer, rd_base + part + pos, Length);
    f->Position.LowPart = pos + Length;
    *Count = Length;
    return ESUCCESS;
}

static ARC_STATUS
RamdiskSeek(
    IN ULONG FileId,
    IN PLARGE_INTEGER Offset,
    IN SEEK_MODE SeekMode
    )
{
    PBL_FILE_TABLE f = &BlFileTable[FileId];

    if (SeekMode == SeekRelative) {
        f->Position.LowPart += Offset->LowPart;
    } else {
        f->Position = *Offset;                   // SeekAbsolute (what FatDiskRead uses)
    }

    return ESUCCESS;
}

static ARC_STATUS
RamdiskClose(
    IN ULONG FileId
    )
{
    BlFileTable[FileId].Flags.Open = 0;
    return ESUCCESS;
}

static ARC_STATUS
RamdiskGetReadStatus(
    IN ULONG FileId
    )
{
    UNREFERENCED_PARAMETER(FileId);
    return ESUCCESS;                             // a disk always has data
}

//
// Raw-device GetFileInformation: report the partition byte length as the "file"
// size, the way ScsiDisk does for an opened disk. Keeps AEGetFileInformation from
// faulting if it is ever called on a device (rather than a FAT) fid.
//
static ARC_STATUS
RamdiskGetFileInformation(
    IN ULONG FileId,
    OUT PFILE_INFORMATION FileInformation
    )
{
    PBL_FILE_TABLE f = &BlFileTable[FileId];

    memset(FileInformation, 0, sizeof(FILE_INFORMATION));
    FileInformation->EndingAddress = f->u.PartitionContext.PartitionLength;
    FileInformation->CurrentPosition = f->Position;
    return ESUCCESS;
}

BL_DEVICE_ENTRY_TABLE RamdiskEntryTable = {
    RamdiskClose,                // Close
    (PARC_MOUNT_ROUTINE)NULL,    // Mount
    (PARC_OPEN_ROUTINE)NULL,     // Open (AEOpen opens the device directly)
    RamdiskRead,                 // Read
    RamdiskGetReadStatus,        // GetReadStatus
    RamdiskSeek,                 // Seek
    (PARC_WRITE_ROUTINE)NULL,    // Write
    RamdiskGetFileInformation,   // GetFileInformation
    (PARC_SET_FILE_INFO_ROUTINE)NULL,
    (PRENAME_ROUTINE)NULL,
    (PARC_GET_DIRECTORY_ENTRY_ROUTINE)NULL,
    (PBOOTFS_INFO)NULL
};
