//
// ARM ARC firmware-vector emulation.
//
// The Raspberry Pi, like a PC, has no ARC ROM firmware. As NT does on x86
// (BOOT/LIB/I386/ARCEMUL.C), the loader emulates the firmware vector in its own
// memory: SYSTEM_BLOCK resolves to &GlobalSystemBlock (see the _ARM_ branch in
// arc.h), and the Arc* call macros dispatch through GlobalFirmwareVectors[].
//
// Every slot starts pointing at BlArcNotYetImplemented; the console/memory/halt
// subset is replaced with real AE* routines backed by RPi2 hardware, and the
// file/disk subset (M2) is a BlFileTable-backed device+file layer.
//
// Two firmware-vector consumers meet here, reaching files at different layers but
// over one FAT engine (see ARCHITECTURE.md §6):
//
//   * BlOsLoader opens a raw partition with ArcOpen, then layers FAT itself via
//     BlOpen (FAT *above* the vector). For it, AEOpen need only return a raw block
//     device - the x86 ScsiDiskOpen role.
//   * arcdos is an ARC application: it calls ArcOpen/ArcGetDirectoryEntry on a full
//     file path and expects the *firmware* to parse FAT and hand back a file (FAT
//     *below* the vector) - the MIPS firmware FwOpen role (FW/MIPS/FWIO.C). So
//     AEOpen also splits device/file, opens the device, and mounts+opens the file
//     through BlOpen, and AERead/AESeek/AEClose/AEGetFileInformation/
//     AEGetDirectoryEntry dispatch through BlFileTable[fid].DeviceEntryTable - the
//     pattern of x86 AERead (ARCEMUL.C:987) and FwRead/FwGetDirectoryEntry.
//
#include "bootlib.h"
#include "string.h"

//
// PL011 + console primitives (arcfw/arm/uart.c, console.c) and USB-keyboard input
// (arcfw/arm/usb.c). The console is the ARM analog of the RISC firmware serial
// driver FW/MIPS/JXSERIAL.C; its input side polls both the PL011 RX and a USB HID
// keyboard so the loader takes keystrokes from whichever is present.
//
void uart_putc(int c);
int uart_getc(void);
int uart_rx_ready(void);
void console_putc(int c);
int usb_kbd_present(void);
int usb_kbd_rx_ready(void);
int usb_kbd_getc(void);

//
// Framebuffer console cursor query (arcfw/arm/fbcon.c), 0-based. Backs
// AEGetDisplayStatus, the ArcGetDisplayStatus slot the arcdos line editor uses.
//
void fbcon_status(unsigned int *col, unsigned int *row,
                  unsigned int *maxcol, unsigned int *maxrow);

//
// The RAM-disk block device (arcfw/arm/ramdisk.c) - the ScsiDiskEntryTable analog
// AEOpen installs for a disk/partition name. RamdiskReadSectors backs the MBR read
// AEOpen does to find a partition's base LBA.
//
extern BL_DEVICE_ENTRY_TABLE RamdiskEntryTable;
ARC_STATUS RamdiskReadSectors(ULONG Lba, ULONG Count, PVOID Buffer);
ULONG RamdiskTotalSectors(VOID);

//
// The console is a headless serial line. The RISC ARC firmware names its serial
// ports "multi(0)serial(N)" (FW/MIPS/JXSERIAL.C); we expose port 0 as the one
// bidirectional console device, and init.c points both consolein= and consoleout=
// at it. A serial port carries both directions, so unlike the x86 split (video out
// + keyboard in) there is a single name; AEOpen maps the open mode to the in/out id.
//
#define SERIAL_CONSOLE_NAME "multi(0)serial(0)"

//
// Fixed console file ids, matching x86 (console-in 0, console-out 1). Disk/file fids
// start at BOOT_FILEID (2). Slots 0/1 are reserved in BlFileTable so BlOpen's
// free-slot search (which starts at 0) never hands a FAT file the console's slot.
//
#define ARC_CONSOLE_INPUT  0
#define ARC_CONSOLE_OUTPUT 1

#define IS_CONSOLE_FID(id) ((id) == ARC_CONSOLE_INPUT || (id) == ARC_CONSOLE_OUTPUT)

//
// The firmware vector and the system parameter block that points at it. Mirrors the
// static initialization in I386/ARCEMUL.C. GlobalFirmwareVectors lives in BSS, so
// every slot is NULL until BlArmInitializeArcEmulator() runs (BlStartup calls it
// before any Arc* call dispatches through the vector).
//
PVOID GlobalFirmwareVectors[MaximumRoutine];

SYSTEM_PARAMETER_BLOCK GlobalSystemBlock =
    {
        0,                              // Signature
        sizeof(SYSTEM_PARAMETER_BLOCK), // Length
        0,                              // Version
        0,                              // Revision
        NULL,                           // RestartBlock
        NULL,                           // DebugBlock
        NULL,                           // GenerateExceptionVector
        NULL,                           // TlbMissExceptionVector
        MaximumRoutine,                 // FirmwareVectorLength
        GlobalFirmwareVectors,          // Pointer to vector block
        0,                              // VendorVectorLength
        NULL                            // Pointer to vendor vector block
    };

ARC_STATUS
BlArcNotYetImplemented(
    IN ULONG FileId
    )
{
    BlPrint("ERROR - Unimplemented Firmware Vector called (FID %lx)\n", FileId);
    return EINVAL;
}

//
// ---- device/file open ------------------------------------------------------
//
// Read a little-endian 32-bit field out of a byte buffer (MBR fields are LE and
// unaligned).
//
static ULONG
le32(const unsigned char *p)
{
    //
    // MBR partition fields are unaligned (partition(1)'s LBA is at offset 0x1C6).
    // The bytes are read through a volatile pointer so the compiler cannot fuse them
    // into one 32-bit LDR/LDRD - that would alignment-fault on Cortex-A7 (ARCHITECTURE.md
    // §4 "LDM/STM alignment"). Volatile forces four independent LDRB, always safe.
    //
    const volatile unsigned char *v = p;
    return (ULONG)v[0] | ((ULONG)v[1] << 8) | ((ULONG)v[2] << 16) | ((ULONG)v[3] << 24);
}

//
// Pull the partition number out of an ARC device name "...partition(N)". Returns
// the number, or -1 if the name has no partition() component. Done by hand to avoid
// depending on strstr.
//
static int
ParsePartitionNumber(
    IN PCHAR Device
    )
{
    static const char tag[] = "partition(";
    PCHAR p;

    for (p = Device; *p; p += 1) {
        ULONG i = 0;
        while (tag[i] && p[i] == tag[i]) {
            i += 1;
        }
        if (tag[i] == 0) {
            PCHAR q = p + i;
            int n = 0, got = 0;
            while (*q >= '0' && *q <= '9') {
                n = n * 10 + (*q - '0');
                q += 1;
                got = 1;
            }
            if (got && *q == ')') {
                return n;
            }
        }
    }
    return -1;
}

//
// Open a disk/partition device by ARC name and return its file id. Mirrors the x86
// BiosPartitionOpen/ScsiDiskOpen role: recognize the name, find the partition's base
// LBA (parse the MBR for partition N >= 1; partition(0) is the whole disk), allocate
// a BlFileTable slot, and install &RamdiskEntryTable. An already-open device with the
// same base LBA is reused, so repeated file opens (arcdos dir/type) don't leak fids
// - the minimal analog of the MIPS firmware's OpenedPathTable.
//
static ARC_STATUS
AEDeviceOpen(
    IN PCHAR Device,
    OUT PULONG FileId
    )
{
    unsigned char mbr[SECTOR_SIZE];
    ULONG startLba, sectorCount;
    ULONG fid;
    int part;

    part = ParsePartitionNumber(Device);
    if (part < 0) {
        return ENODEV;
    }

    if (part == 0) {
        startLba = 0;
        sectorCount = RamdiskTotalSectors();
    } else {
        const unsigned char *entry;
        ARC_STATUS s = RamdiskReadSectors(0, 1, mbr);
        if (s != ESUCCESS) {
            return s;
        }
        if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
            return EIO;                 // no valid MBR -> no partitions
        }
        if (part > 4) {
            return ENODEV;              // only the 4 primary MBR entries
        }
        entry = &mbr[0x1BE + (part - 1) * 16];
        startLba = le32(entry + 8);
        sectorCount = le32(entry + 12);
        if (sectorCount == 0) {
            return ENODEV;              // unused partition slot
        }
    }

    //
    // Reuse an already-open device on the same base LBA.
    //
    for (fid = BOOT_FILEID; fid < BL_FILE_TABLE_SIZE; fid += 1) {
        if (BlFileTable[fid].Flags.Open &&
            BlFileTable[fid].DeviceEntryTable == &RamdiskEntryTable &&
            BlFileTable[fid].u.PartitionContext.StartingSector == startLba) {
            *FileId = fid;
            return ESUCCESS;
        }
    }

    //
    // Allocate a fresh slot (>= BOOT_FILEID; 0/1 are the console).
    //
    for (fid = BOOT_FILEID; fid < BL_FILE_TABLE_SIZE; fid += 1) {
        if (BlFileTable[fid].Flags.Open == 0) {
            break;
        }
    }
    if (fid == BL_FILE_TABLE_SIZE) {
        return EMFILE;
    }

    memset(&BlFileTable[fid], 0, sizeof(BL_FILE_TABLE));
    BlFileTable[fid].Flags.Open = 1;
    BlFileTable[fid].Flags.Read = 1;
    BlFileTable[fid].DeviceEntryTable = &RamdiskEntryTable;
    BlFileTable[fid].u.PartitionContext.StartingSector = startLba;
    BlFileTable[fid].u.PartitionContext.PartitionLength.LowPart = sectorCount * SECTOR_SIZE;
    BlFileTable[fid].u.PartitionContext.PartitionLength.HighPart = 0;

    *FileId = fid;
    return ESUCCESS;
}

//
// AEOpen - emulate ArcOpen. Console -> fixed in/out id; otherwise split the ARC path
// at the last ')' into a device name and a file path (the FW/MIPS/FWIO.C FwOpen
// model). With no file part, return the raw device (what BlOsLoader's
// ArcOpen("...partition(1)") at osloader.c:388 wants). With a file part, open the
// device then mount FAT and open the file through BlOpen (what arcdos's file opens
// want) - one FAT engine, both consumers.
//
ARC_STATUS
AEOpen(
    IN PCHAR OpenPath,
    IN OPEN_MODE OpenMode,
    OUT PULONG FileId
    )
{
    CHAR device[128];
    PCHAR lastParen, filePart, p;
    ULONG deviceFid;
    ARC_STATUS Status;

    //
    // Console - the one bidirectional serial device; open mode picks the direction.
    // Reserve the slot so BlOpen's allocator never reuses it for a FAT file.
    //
    if (stricmp(OpenPath, SERIAL_CONSOLE_NAME) == 0) {
        ULONG id = (OpenMode == ArcOpenWriteOnly) ? ARC_CONSOLE_OUTPUT : ARC_CONSOLE_INPUT;
        if (OpenMode == ArcOpenReadWrite) {
            return EACCES;
        }
        BlFileTable[id].Flags.Open = 1;
        BlFileTable[id].Flags.Read = (id == ARC_CONSOLE_INPUT);
        BlFileTable[id].Flags.Write = (id == ARC_CONSOLE_OUTPUT);
        BlFileTable[id].DeviceEntryTable = NULL;     // console is fid-special-cased
        *FileId = id;
        return ESUCCESS;
    }

    //
    // Split device / file at the last ')'.
    //
    lastParen = NULL;
    for (p = OpenPath; *p; p += 1) {
        if (*p == ')') {
            lastParen = p;
        }
    }
    if (lastParen == NULL) {
        return ENOENT;                  // not a console and not an ARC device path
    }

    {
        ULONG dlen = (ULONG)(lastParen - OpenPath) + 1;
        if (dlen >= sizeof(device)) {
            return ENAMETOOLONG;
        }
        memcpy(device, OpenPath, dlen);
        device[dlen] = 0;
        filePart = lastParen + 1;
    }

    Status = AEDeviceOpen(device, &deviceFid);
    if (Status != ESUCCESS) {
        return Status;
    }

    //
    // No file component: the open mode disambiguates the two consumers (as the MIPS
    // firmware's FwOpen does). A data open (read/write) of a bare partition wants the
    // RAW block device - this is BlOsLoader's ArcOpen("...partition(1)") at
    // osloader.c:388, which then layers FAT itself via BlOpen. A directory open of a
    // bare partition wants the FAT ROOT directory - this is arcdos's `dir`, which opens
    // "...partition(1)" (no trailing path) with OpenDirectory. Map the latter to "\".
    //
    if (*filePart == 0) {
        if (OpenMode != ArcOpenDirectory) {
            *FileId = deviceFid;
            return ESUCCESS;
        }
        filePart = "\\";
    }

    //
    // File/directory component -> recognize the filesystem on the device and open it.
    // BlOpen runs IsFatFileStructure -> FatOpen and records DeviceId = deviceFid, so
    // reads flow FatDiskRead -> ArcRead(deviceFid) -> RamdiskRead.
    //
    return BlOpen(deviceFid, filePart, OpenMode, FileId);
}

//
// AEClose - console closes succeed (no state); everything else dispatches to the
// device/FS Close. (A reused device fid is left open on purpose - see AEDeviceOpen.)
//
ARC_STATUS
AEClose(
    IN ULONG FileId
    )
{
    if (IS_CONSOLE_FID(FileId)) {
        return ESUCCESS;
    }
    if (BlFileTable[FileId].DeviceEntryTable == NULL) {
        return EBADF;
    }
    return (BlFileTable[FileId].DeviceEntryTable->Close)(FileId);
}

//
// AEWrite - console output pushes bytes to the unified console (serial + HDMI); any
// other fid dispatches to the device/FS Write.
//
ARC_STATUS
AEWrite(
    IN ULONG FileId,
    IN PVOID Buffer,
    IN ULONG Length,
    OUT PULONG Count
    )
{
    const unsigned char *p;
    ULONG i;

    if (FileId == ARC_CONSOLE_OUTPUT) {
        p = (const unsigned char *)Buffer;
        for (i = 0; i < Length; i += 1) {
            console_putc(p[i]);
        }
        *Count = Length;
        return ESUCCESS;
    }

    if (BlFileTable[FileId].DeviceEntryTable == NULL ||
        BlFileTable[FileId].DeviceEntryTable->Write == NULL) {
        return EACCES;
    }
    return (BlFileTable[FileId].DeviceEntryTable->Write)(FileId, Buffer, Length, Count);
}

//
// AERead - console input is a blocking read taken from whichever input backend has a
// byte first (PL011 RX or USB keyboard); any other fid dispatches to the device/FS
// Read (which serves the exact byte count, FatDiskRead-style).
//
ARC_STATUS
AERead(
    IN ULONG FileId,
    OUT PVOID Buffer,
    IN ULONG Length,
    OUT PULONG Count
    )
{
    unsigned char *p;
    ULONG i;

    if (FileId == ARC_CONSOLE_INPUT) {
        p = (unsigned char *)Buffer;
        for (i = 0; i < Length; i += 1) {
            for (;;) {
                if (uart_rx_ready()) {
                    p[i] = (unsigned char)uart_getc();
                    break;
                }
                if (usb_kbd_rx_ready()) {
                    int c = usb_kbd_getc();
                    if (c >= 0) {
                        p[i] = (unsigned char)c;
                        break;
                    }
                }
            }
        }
        *Count = Length;
        return ESUCCESS;
    }

    if (BlFileTable[FileId].DeviceEntryTable == NULL) {
        return EBADF;
    }
    return (BlFileTable[FileId].DeviceEntryTable->Read)(FileId, Buffer, Length, Count);
}

//
// AEReadStatus - ESUCCESS means a byte is available. Console: PL011 RX non-empty or
// a USB key buffered. Other fids dispatch if the device offers GetReadStatus, else
// report ready (a disk always has data).
//
ARC_STATUS
AEReadStatus(
    IN ULONG FileId
    )
{
    if (FileId == ARC_CONSOLE_INPUT) {
        return (uart_rx_ready() || usb_kbd_rx_ready()) ? ESUCCESS : EAGAIN;
    }
    if (BlFileTable[FileId].DeviceEntryTable != NULL &&
        BlFileTable[FileId].DeviceEntryTable->GetReadStatus != NULL) {
        return (BlFileTable[FileId].DeviceEntryTable->GetReadStatus)(FileId);
    }
    return ESUCCESS;
}

//
// AESeek - dispatch to the device/FS Seek (FatSeek for FAT files, RamdiskSeek for
// raw devices). FatDiskRead calls ArcSeek+ArcRead per sector, so this MUST be a real
// slot - it was BlArcNotYetImplemented before M2. Console seeks are a no-op.
//
ARC_STATUS
AESeek(
    IN ULONG FileId,
    IN PLARGE_INTEGER Offset,
    IN SEEK_MODE SeekMode
    )
{
    if (IS_CONSOLE_FID(FileId)) {
        return ESUCCESS;
    }
    if (BlFileTable[FileId].DeviceEntryTable == NULL ||
        BlFileTable[FileId].DeviceEntryTable->Seek == NULL) {
        return EBADF;
    }
    return (BlFileTable[FileId].DeviceEntryTable->Seek)(FileId, Offset, SeekMode);
}

//
// AEGetFileInformation - dispatch to the device/FS GetFileInformation (FatGet... for
// a FAT file gives size/attributes; arcdos type/copy/dir read it). NYI before M2.
//
ARC_STATUS
AEGetFileInformation(
    IN ULONG FileId,
    OUT PFILE_INFORMATION FileInformation
    )
{
    if (IS_CONSOLE_FID(FileId)) {
        return EINVAL;
    }
    if (BlFileTable[FileId].DeviceEntryTable == NULL ||
        BlFileTable[FileId].DeviceEntryTable->GetFileInformation == NULL) {
        return EBADF;
    }
    return (BlFileTable[FileId].DeviceEntryTable->GetFileInformation)(FileId, FileInformation);
}

//
// AESetFileInformation - dispatch to the device/FS SetFileInformation (arcdos uses it
// to set attributes). Read-only FAT volumes will refuse via FatSet...; that is fine.
//
ARC_STATUS
AESetFileInformation(
    IN ULONG FileId,
    IN ULONG AttributeFlags,
    IN ULONG AttributeMask
    )
{
    if (IS_CONSOLE_FID(FileId)) {
        return EINVAL;
    }
    if (BlFileTable[FileId].DeviceEntryTable == NULL ||
        BlFileTable[FileId].DeviceEntryTable->SetFileInformation == NULL) {
        return EACCES;
    }
    return (BlFileTable[FileId].DeviceEntryTable->SetFileInformation)(FileId, AttributeFlags, AttributeMask);
}

//
// AEGetDirectoryEntry - dispatch to the FS GetDirectoryEntry (FatGetDirectoryEntry).
// This is what arcdos's "dir" calls: open a directory, then enumerate its entries.
// NYI before M2 (the slot fell through to BlArcNotYetImplemented).
//
ARC_STATUS
AEGetDirectoryEntry(
    IN ULONG FileId,
    OUT PDIRECTORY_ENTRY Buffer,
    IN ULONG Length,
    OUT PULONG Count
    )
{
    if (IS_CONSOLE_FID(FileId)) {
        return EBADF;
    }
    if (BlFileTable[FileId].DeviceEntryTable == NULL ||
        BlFileTable[FileId].DeviceEntryTable->GetDirectoryEntry == NULL) {
        return EBADF;
    }
    return (BlFileTable[FileId].DeviceEntryTable->GetDirectoryEntry)(FileId, Buffer, Length, Count);
}

//
// AEGetMemoryDescriptor - walk the static MDArray[] built in arcfw/arm/memory.c
// (verbatim shape of I386/ARCEMUL.C): NULL returns the first descriptor, otherwise
// the next, NULL past the end.
//
extern MEMORY_DESCRIPTOR MDArray[];
extern ULONG NumberDescriptors;

PMEMORY_DESCRIPTOR
AEGetMemoryDescriptor(
    IN PMEMORY_DESCRIPTOR MemoryDescriptor OPTIONAL
    )
{
    if (MemoryDescriptor == NULL) {
        return MDArray;
    }

    if ((ULONG)(MemoryDescriptor - MDArray) >= (NumberDescriptors - 1)) {
        return NULL;
    }

    return ++MemoryDescriptor;
}

//
// AEReboot - emulate ArcReboot/ArcRestart via the BCM2836 PM watchdog (0x3F100000):
// arm a short watchdog, then request a full reset. All writes carry the 0x5A password
// in the high byte. Does not return.
//
#define PM_BASE     0x3F100000u
#define PM_RSTC     (*(volatile unsigned int *)(PM_BASE + 0x1c))
#define PM_WDOG     (*(volatile unsigned int *)(PM_BASE + 0x24))
#define PM_PASSWORD 0x5a000000u
#define PM_RSTC_WRCFG_CLR        0xffffffcfu
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u

VOID
AEReboot(
    VOID
    )
{
    PM_WDOG = PM_PASSWORD | 10;
    PM_RSTC = PM_PASSWORD | (PM_RSTC & PM_RSTC_WRCFG_CLR) | PM_RSTC_WRCFG_FULL_RESET;

    for (;;) {
    }
}

//
// AEGetDisplayStatus - emulate ArcGetDisplayStatus (the JXDISP.C FwGetDisplayStatus
// analog). Reports the framebuffer console cursor and extent; the arcdos editor reads
// it to anchor each redraw. fbcon tracks the cursor 0-based, so add 1 to match the ARC
// 1-based convention. The fixed white-on-blue scheme is reported as high-intensity
// white on blue. FileId is ignored (status is a property of the one screen).
//
static ARC_DISPLAY_STATUS DisplayStatus;

PARC_DISPLAY_STATUS
AEGetDisplayStatus(
    IN ULONG FileId
    )
{
    unsigned int col, row, maxcol, maxrow;

    (void)FileId;
    fbcon_status(&col, &row, &maxcol, &maxrow);

    DisplayStatus.CursorXPosition = (USHORT)(col + 1);
    DisplayStatus.CursorYPosition = (USHORT)(row + 1);
    DisplayStatus.CursorMaxXPosition = (USHORT)(maxcol + 1);
    DisplayStatus.CursorMaxYPosition = (USHORT)(maxrow + 1);
    DisplayStatus.ForegroundColor = 7;          // white
    DisplayStatus.BackgroundColor = 4;          // blue
    DisplayStatus.HighIntensity = 1;
    DisplayStatus.Underscored = 0;
    DisplayStatus.ReverseVideo = 0;

    return &DisplayStatus;
}

//
// AEGetEnvironmentVariable - emulate ArcGetEnvironmentVariable. No ARC environment is
// implemented, so every lookup misses (returns NULL). This MUST be a real slot (not
// BlArcNotYetImplemented): arcdos's ParseCommandLine calls it for any token with a
// ':' before reaching Open, and arcdos maps the NULL miss to "Undefined environment
// variable" and re-prompts cleanly.
//
PCHAR
AEGetEnvironmentVariable(
    IN PCHAR Variable
    )
{
    (void)Variable;
    return NULL;
}

//
// Populate the firmware vector. ARM analog of I386/ARCEMUL.C's vector fill. Every slot
// starts at BlArcNotYetImplemented; the implemented routines override the console,
// disk/file I/O (open/close/read/read-status/write/seek/file-info/dir-entry), memory,
// halt, and display slots. Reserve console fids 0/1 in BlFileTable up front so the
// arcdos path (which never calls BlIoInitialize) cannot allocate them to a file; the
// BlOsLoader path re-establishes them when it opens the console after BlIoInitialize.
//
VOID
BlArmInitializeArcEmulator(
    VOID
    )
{
    ULONG cnt;

    for (cnt = 0; cnt < MaximumRoutine; cnt += 1) {
        GlobalFirmwareVectors[cnt] = (PVOID)BlArcNotYetImplemented;
    }

    GlobalFirmwareVectors[OpenRoutine]       = (PVOID)AEOpen;
    GlobalFirmwareVectors[CloseRoutine]      = (PVOID)AEClose;
    GlobalFirmwareVectors[ReadRoutine]       = (PVOID)AERead;
    GlobalFirmwareVectors[ReadStatusRoutine] = (PVOID)AEReadStatus;
    GlobalFirmwareVectors[WriteRoutine]      = (PVOID)AEWrite;
    GlobalFirmwareVectors[SeekRoutine]       = (PVOID)AESeek;
    GlobalFirmwareVectors[GetFileInformationRoutine] = (PVOID)AEGetFileInformation;
    GlobalFirmwareVectors[SetFileInformationRoutine] = (PVOID)AESetFileInformation;
    GlobalFirmwareVectors[GetDirectoryEntryRoutine]  = (PVOID)AEGetDirectoryEntry;
    GlobalFirmwareVectors[MemoryRoutine]     = (PVOID)AEGetMemoryDescriptor;
    GlobalFirmwareVectors[RestartRoutine]    = (PVOID)AEReboot;
    GlobalFirmwareVectors[RebootRoutine]     = (PVOID)AEReboot;
    GlobalFirmwareVectors[GetDisplayStatusRoutine] = (PVOID)AEGetDisplayStatus;
    GlobalFirmwareVectors[GetEnvironmentRoutine]   = (PVOID)AEGetEnvironmentVariable;

    BlFileTable[ARC_CONSOLE_INPUT].Flags.Open = 1;
    BlFileTable[ARC_CONSOLE_INPUT].Flags.Read = 1;
    BlFileTable[ARC_CONSOLE_OUTPUT].Flags.Open = 1;
    BlFileTable[ARC_CONSOLE_OUTPUT].Flags.Write = 1;
}
