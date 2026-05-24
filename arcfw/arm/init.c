//
// ARM loader startup (replaces BOOT/STARTUP/I386/SU.ASM + BLDR/I386/INITX86.C).
//
// The x86 BlStartup parses boot.ini, runs NTDETECT, and builds the ARC argument
// vector (PARSBOOT.C). None of that machinery is PC-portable, so this
// synthesizes a well-formed argument vector directly and calls the common,
// arch-independent BlOsLoader (arcfw/ported/osloader.c). The console, memory,
// and halt firmware vectors are live, so BlOsLoader opens the console,
// initializes the loader heap + memory descriptor list, and runs until it hits
// the still-stubbed storage/filesystem path.
//
#include "bldr.h"
#include "string.h"
#include "ntimage.h"     // PIMAGE_NT_HEADERS for the kernel-entry computation (BOOT_KERNEL)

VOID BlArmInitializeArcEmulator(VOID);
VOID BlArmFixupMemoryMap(VOID);

// Establish the Arc disk image's RAM location/size (the embedded blob under QEMU, or
// the firmware-staged initramfs on real hardware) before the memory-map fixup reads it.
VOID RamdiskInit(VOID);

// HDMI framebuffer console: allocate the VC framebuffer, then bring up the text
// console layered on it (white-on-black 8x8 font). After this every BlPrint /
// ArcWrite byte appears on both the serial line and the monitor.
int fb_init(void);
void fbcon_init(void);
extern unsigned int fb_width, fb_height;

// USB host bring-up (Phase 3 - read a USB keyboard). Reports over the console.
int usb_init(void);

// ARC DOS shell (arcfw/ported/arcdos.c) - an interactive ARC file/command shell
// running on the emulated firmware vector, the HDMI console, and the USB keyboard.
int ArcDosMain(void);

// BCM2835 system timer (arcfw/arm/timer.c) - used to bound the console echo test.
unsigned int timer_us(void);

//
// M2b bring-up self-test: prove the Arc disk reads sectors through the emulated
// firmware vector. Opens the load partition (AEOpen parses the MBR for its base LBA),
// reads its first sector, and prints the FAT16 boot-sector fingerprint. Exercises
// AEOpen(device) -> AERead -> RamdiskEntryTable end to end, independent of FAT/arcdos.
// Set to 0 once M2c (FAT) is verified.
//
#define RAMDISK_TEST 0

//
// Synthesized ARC argument vector. Argument names mirror what INITX86.C builds.
//
// Console: the RPi2 console is the PL011 UART, so we model it as a headless
// serial console - both consolein and consoleout name the one bidirectional
// serial device, exactly as the RISC ARC firmware does (FW/MIPS/JXSERIAL.C names
// its ports "multi(0)serial(0)"/"...(1)"). This is deliberately NOT the x86
// console (separate video(0)monitor(0) out + key(0)keyboard(0) in), which models
// a screen+keyboard the Pi serial line doesn't have. AEOpen tells the two
// directions apart by open mode, since the path string is the same for both.
//
static PCHAR BlArmArgv[] = {
    "load",
    "osloader=multi(0)disk(0)rdisk(0)partition(1)\\System32\\NTLDR",
    "systempartition=multi(0)disk(0)rdisk(0)partition(1)",
    "osloadfilename=\\WINNT",
    "osloadpartition=multi(0)disk(0)rdisk(0)partition(1)",
    "osloadoptions=",
    "consolein=multi(0)serial(0)",
    "consoleout=multi(0)serial(0)"
};

//
// Handoff smoke test. The loader's endgame is to load code and jump to it. No NT
// ARM kernel exists, so this copies an embedded stand-in payload (a minimal serial
// shell, arcfw/payload/) to a fixed RAM address and transfers control - proving
// the mechanism independently of the filesystem. Set SMOKE_HANDOFF to 0 to restore
// the normal BlOsLoader path. See ARCHITECTURE.md for the rationale.
//
#define SMOKE_HANDOFF 0

#if SMOKE_HANDOFF
// 0x00400000 MUST match the link base in build/payload.ld.
#define PAYLOAD_LOAD_BASE 0x00400000u

extern const unsigned char _binary_payload_bin_start[];
extern const unsigned char _binary_payload_bin_size[];

static VOID
BlArmSmokeHandoff(VOID)
{
    unsigned char *dst = (unsigned char *)PAYLOAD_LOAD_BASE;
    const unsigned char *src = _binary_payload_bin_start;
    // The linker's _size symbol is a VALUE, not a pointer: its address is the size.
    ULONG size = (ULONG)(unsigned long)_binary_payload_bin_size;
    ULONG i;

    for (i = 0; i < size; i += 1) {
        dst[i] = src[i];
    }

    //
    // Caches are off (start.S enables no MMU/cache), but make the freshly written
    // code visible regardless: drain writes, invalidate the I-cache, resync the
    // pipeline. Keep this if caches are ever enabled.
    //
    __asm__ volatile (
        "dsb\n\t"
        "mcr p15, 0, %0, c7, c5, 0\n\t"   // ICIALLU - invalidate entire I-cache
        "dsb\n\t"
        "isb\n\t"
        : : "r" (0) : "memory");

    BlPrint("Handing off to payload at 0x%lx (%lu bytes)...\n",
            (unsigned long)PAYLOAD_LOAD_BASE, (unsigned long)size);

    ((VOID (*)(VOID))PAYLOAD_LOAD_BASE)();   // ARM mode (low bit 0); does not return
}
#endif

//
// Keyboard / console-input demo+test. Once the ARC firmware vector is live, read the
// console through the real ArcGetReadStatus/ArcRead path and echo each byte until ESC
// (or a 20 s timeout), then resume booting. Proves a keystroke travels the whole way:
// USB keyboard -> HID poll -> AERead (the emulated vector) -> loader, or equally the
// PL011 serial RX. **This is how you confirm the USB keyboard on a real Pi 2** - set to
// 1, rebuild, reflash, then type on the keyboard and watch the HDMI console echo. Off
// by default so the QEMU dev loop boots straight through; drive on QEMU with monitor
// `sendkey`.
//
#define CONSOLE_ECHO_TEST 0

//
// Run the ARC DOS interactive shell instead of BlOsLoader. arcdos is itself an ARC
// application (launched, on a real ARC machine, from the firmware boot menu - a peer
// of osloader, not loaded by it), so it runs directly on the emulated firmware vector
// and never enters the disk/PE load path. Set to 0 to restore the BlOsLoader flow.
//
#define RUN_ARCDOS 0

//
// Load and boot the NT kernel (M3 - the loader's endgame). PE-loads the stand-in
// kernel (arcfw/kernel/, packaged on the Arc disk as \OS\NTOSKRNL.EXE) via the real
// BOOT/LIB PE loader, builds the kernel stack, and transfers control with r0 = the
// LOADER_PARAMETER_BLOCK - the same handoff osloader.c:792 performs on MIPS/Alpha.
// Unlike the full BlOsLoader path it skips HAL load, import resolution, NLS, the
// registry hive, and boot drivers (all still stubbed) - the "hello world or less"
// vertical slice.
//
#define BOOT_KERNEL 1

//
// Interactive boot menu (like a real ARC firmware boot selection): on startup, let
// the user pick the kernel handoff, the arcdos shell, or the full BlOsLoader path,
// over the console (serial + USB keyboard), with a short countdown to a default. This
// is the friendly way to switch between arcdos and the kernel WITHOUT recompiling.
// When BOOT_MENU is 1 (default) the menu runs and the BOOT_KERNEL/RUN_ARCDOS/
// SMOKE_HANDOFF gates below are bypassed (kept as compile-time overrides). Set
// BOOT_MENU 0 to boot straight to one fixed path (e.g. headless regression tests:
// BOOT_MENU 0 + RAMDISK_TEST 1 + BOOT_KERNEL 0 + RUN_ARCDOS 0).
//
#define BOOT_MENU 1

#if CONSOLE_ECHO_TEST
static VOID
BlArmConsoleEchoTest(VOID)
{
    ULONG fid, count;
    unsigned char ch;
    unsigned int t0;

    if (ArcOpen("multi(0)serial(0)", ArcOpenReadOnly, &fid) != ESUCCESS) {
        return;
    }

    BlPrint("\nKeyboard test - type to echo (USB keyboard or serial); ESC continues.\n");
    t0 = timer_us();
    while (timer_us() - t0 < 20000000u) {           // 20 s safety timeout if no ESC
        if (ArcGetReadStatus(fid) != ESUCCESS) {
            continue;
        }
        if (ArcRead(fid, &ch, 1, &count) != ESUCCESS || count != 1) {
            continue;
        }
        if (ch == 0x1b) {                           // ESC -> resume booting
            break;
        }
        BlPrint("echo: 0x%02x '%c'\n",
                (unsigned)ch, (ch >= 0x20 && ch < 0x7f) ? ch : '.');
    }
    BlPrint("Keyboard test done\n");
}
#endif

#if RAMDISK_TEST
static VOID
BlArmRamdiskTest(VOID)
{
    ULONG fid, count;
    unsigned char sec[512];
    char oem[9];
    ARC_STATUS s;

    s = ArcOpen("multi(0)disk(0)rdisk(0)partition(1)", ArcOpenReadOnly, &fid);
    BlPrint("RAMDISK: open partition(1) -> status %lx fid %lx\n",
            (unsigned long)s, (unsigned long)fid);
    if (s != ESUCCESS) {
        return;
    }

    s = ArcRead(fid, sec, sizeof(sec), &count);
    BlPrint("RAMDISK: read boot sector -> status %lx count %lx\n",
            (unsigned long)s, (unsigned long)count);
    if (s == ESUCCESS && count == sizeof(sec)) {
        memcpy(oem, &sec[3], 8);        // FAT BPB OEM name
        oem[8] = 0;
        BlPrint("RAMDISK: jmp=%02x%02x%02x OEM='%s' sig=%02x%02x\n",
                sec[0], sec[1], sec[2], oem, sec[510], sec[511]);
    }
    ArcClose(fid);

    //
    // FAT file read (the arcdos "type" path): open a real file by full ARC path
    // (AEOpen splits device/file -> BlOpen -> IsFatFileStructure -> FatOpen) and read
    // it (AERead -> FatRead). Proves the whole FAT engine end to end.
    //
    {
        ULONG ffid, fcount;
        char fbuf[80];
        s = ArcOpen("multi(0)disk(0)rdisk(0)partition(1)\\HELLO.TXT", ArcOpenReadOnly, &ffid);
        BlPrint("FAT: open \\HELLO.TXT -> status %lx fid %lx\n",
                (unsigned long)s, (unsigned long)ffid);
        if (s == ESUCCESS) {
            s = ArcRead(ffid, fbuf, sizeof(fbuf) - 1, &fcount);
            if (s == ESUCCESS) {
                fbuf[fcount] = 0;
                BlPrint("FAT: read %lx bytes: %s\n", (unsigned long)fcount, fbuf);
            }
            ArcClose(ffid);
        }
    }

    //
    // FAT directory listing (the arcdos "dir" path): open the root directory and
    // enumerate it (ArcGetDirectoryEntry -> FatGetDirectoryEntry).
    //
    {
        ULONG dfid, dcount;
        DIRECTORY_ENTRY de;
        s = ArcOpen("multi(0)disk(0)rdisk(0)partition(1)\\", ArcOpenDirectory, &dfid);
        BlPrint("FAT: open root dir -> status %lx fid %lx\n",
                (unsigned long)s, (unsigned long)dfid);
        if (s == ESUCCESS) {
            for (;;) {
                s = ArcGetDirectoryEntry(dfid, &de, 1, &dcount);
                if (s != ESUCCESS || dcount != 1) {
                    break;
                }
                de.FileName[de.FileNameLength < 32 ? de.FileNameLength : 31] = 0;
                BlPrint("FAT:   %s attr=%02x\n", de.FileName, de.FileAttribute);
            }
            ArcClose(dfid);
        }
    }
}
#endif

#if BOOT_KERNEL || BOOT_MENU
//
// The Arc disk + path of the stand-in kernel image. The partition is opened as a
// raw block device (data-mode open); BlLoadImage's internal BlOpen then mounts FAT
// on it and opens the file - the osloadpartition + \System32\ntoskrnl.exe pattern
// of osloader.c, here pointed at where make-ramdisk.sh packages our kernel.
//
#define KERNEL_DEVICE "multi(0)disk(0)rdisk(0)partition(1)"
#define KERNEL_FILE   "\\OS\\NTOSKRNL.EXE"

// BlLoaderBlock is built + owned by BlMemoryInitialize (arcfw/ported/blmemory.c).
extern PLOADER_PARAMETER_BLOCK BlLoaderBlock;

// The kernel entry contract (osloader.c PTRANSFER_ROUTINE / KE/MIPS X4START.S):
// VOID KiSystemStartup(PLOADER_PARAMETER_BLOCK), arg in r0.
typedef VOID (*PKERNEL_ENTRY)(PLOADER_PARAMETER_BLOCK);

static VOID
BlArmBootKernel(VOID)
{
    ARC_STATUS Status;
    ULONG DeviceId;
    PVOID KernelBase;
    PIMAGE_NT_HEADERS NtHeaders;
    PKERNEL_ENTRY KernelEntry;

    //
    // BlOsLoader prerequisites we are skipping the rest of: build the loader block +
    // memory-descriptor list + heap (BlMemoryInitialize, osloader.c:242), then the
    // file table + FS initializers (BlIoInitialize, osloader.c:292). Without
    // BlMemoryInitialize, BlLoaderBlock is NULL and BlAllocateDescriptor (inside
    // BlLoadImage) walks a NULL list forever.
    //
    Status = BlMemoryInitialize();
    if (Status != ESUCCESS) {
        BlPrint("  BlMemoryInitialize failed: 0x%lx\n", (unsigned long)Status);
        return;
    }
    Status = BlIoInitialize();
    if (Status != ESUCCESS) {
        BlPrint("  BlIoInitialize failed: 0x%lx\n", (unsigned long)Status);
        return;
    }

    BlPrint("\nLoading NT kernel: %s%s\n", KERNEL_DEVICE, KERNEL_FILE);

    Status = ArcOpen(KERNEL_DEVICE, ArcOpenReadOnly, &DeviceId);
    if (Status != ESUCCESS) {
        BlPrint("  ArcOpen(%s) failed: 0x%lx\n", KERNEL_DEVICE, (unsigned long)Status);
        return;
    }

    //
    // Load the PE through the real BOOT/LIB loader (peldr.c). LoaderSystemCode marks
    // the image's pages; TARGET_IMAGE (ARM_IMAGE, 0x1c0) is the machine type peldr
    // requires the PE to declare.
    //
    Status = BlLoadImage(DeviceId, LoaderSystemCode, KERNEL_FILE, TARGET_IMAGE, &KernelBase);
    if (Status != ESUCCESS) {
        BlPrint("  BlLoadImage failed: 0x%lx\n", (unsigned long)Status);
        return;
    }

    NtHeaders = RtlImageNtHeader(KernelBase);
    if (NtHeaders == NULL) {
        BlPrint("  loaded image has no PE header\n");
        return;
    }

    KernelEntry = (PKERNEL_ENTRY)((ULONG)KernelBase +
                                  NtHeaders->OptionalHeader.AddressOfEntryPoint);

    //
    // Arch setup before the jump (osloader.c:779) - allocates the kernel stack the
    // entry switches to (arcfw/arm/ntsetup.c).
    //
    Status = BlSetupForNt(BlLoaderBlock);
    if (Status != ESUCCESS) {
        BlPrint("  BlSetupForNt failed: 0x%lx\n", (unsigned long)Status);
        return;
    }

    //
    // Hand the kernel a recognizable options string so it can prove the loader block
    // (and the data it points at) crossed the handoff intact.
    //
    BlLoaderBlock->LoadOptions = "BOOT_KERNEL ARM hello";

    BlPrint("  image @ 0x%lx  entry 0x%lx  kernelstack 0x%lx\n",
            (unsigned long)KernelBase, (unsigned long)KernelEntry,
            (unsigned long)BlLoaderBlock->KernelStack);
    BlPrint("Transferring control to the kernel...\n");

    //
    // Make the freshly loaded image visible before executing it. Caches are off
    // (start.S enables no MMU/cache), so this is correctness insurance, identical to
    // the M3 smoke handoff.
    //
    __asm__ volatile (
        "dsb\n\t"
        "mcr p15, 0, %0, c7, c5, 0\n\t"   // ICIALLU - invalidate entire I-cache
        "dsb\n\t"
        "isb\n\t"
        : : "r" (0) : "memory");

    (KernelEntry)(BlLoaderBlock);          // r0 = LoaderBlock; does not return

    BlPrint("  kernel returned unexpectedly - halting\n");
}
#endif

#if BOOT_MENU
//
// Interactive boot menu - the friendly runtime switch between the kernel handoff and
// the arcdos shell (and the full BlOsLoader path), modeled on a real ARC firmware boot
// selection. Input comes through the emulated firmware vector (ArcGetReadStatus/ArcRead),
// so it works on the PL011 serial line AND a USB keyboard on the HDMI console - the same
// path arcdos itself reads. A short countdown auto-selects the default so a headless or
// unattended boot still proceeds.
//
#define BOOT_MENU_DEFAULT 1     // option chosen on timeout / Enter
#define BOOT_MENU_SECONDS 5     // countdown before auto-boot

static VOID
BlArmBootMenu(VOID)
{
    ULONG fid, count;
    unsigned char ch;
    int choice = 0;
    int remaining;
    unsigned int t0;
    BOOLEAN haveConsole;

    BlPrint("\n");
    BlPrint("===== NT 3.5 ARC Firmware Emulator - boot menu =====\n");
    BlPrint("  [1] Boot NT kernel   \\OS\\NTOSKRNL.EXE   (default)\n");
    BlPrint("  [2] ARC DOS shell\n");
    BlPrint("  [3] OS Loader        (BlOsLoader - full NT load path)\n");
    BlPrint("====================================================\n");

    haveConsole = (BOOLEAN)(ArcOpen("multi(0)serial(0)", ArcOpenReadOnly, &fid) == ESUCCESS);

    //
    // Poll the console one second at a time, redrawing the countdown (\r returns to
    // column 0 on both the serial terminal and the framebuffer console). 1/2/3 select;
    // Enter takes the default; any other key is ignored.
    //
    for (remaining = BOOT_MENU_SECONDS; remaining > 0 && choice == 0; remaining -= 1) {
        BlPrint("\rSelect 1-3, or wait %d s for [%d]: ", remaining, BOOT_MENU_DEFAULT);

        t0 = timer_us();
        while (timer_us() - t0 < 1000000u) {
            if (!haveConsole) {
                break;
            }
            if (ArcGetReadStatus(fid) != ESUCCESS) {
                continue;
            }
            if (ArcRead(fid, &ch, 1, &count) != ESUCCESS || count != 1) {
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                choice = BOOT_MENU_DEFAULT;
                break;
            }
            if (ch >= '1' && ch <= '3') {
                choice = ch - '0';
                break;
            }
        }

        if (!haveConsole) {
            break;          // no input device - go straight to the default
        }
    }

    if (haveConsole) {
        ArcClose(fid);
    }
    if (choice == 0) {
        choice = BOOT_MENU_DEFAULT;
    }

    BlPrint("\n\nBooting option [%d].\n", choice);

    switch (choice) {
    case 2:
        BlPrint("\nStarting ARC DOS...\n\n");
        ArcDosMain();
        BlPrint("\nArcDos exited.\n");
        break;

    case 3:
        {
            ARC_STATUS s;
            s = BlOsLoader(sizeof(BlArmArgv) / sizeof(BlArmArgv[0]), BlArmArgv, NULL);
            BlPrint("BlOsLoader returned 0x%lx.\n", (unsigned long)s);
        }
        break;

    case 1:
    default:
        BlArmBootKernel();      // PE-loads the kernel and transfers control; returns only on failure
        BlPrint("\nKernel boot failed.\n");
        break;
    }

    BlPrint("Halting.\n");
    for (;;) {
    }
}
#endif

VOID
BlStartup(
    IN PCHAR PartitionName
    )
{
    ARC_STATUS Status;

    //
    // The PL011 is already initialized (cmain -> uart_init, before the first
    // banner), so the serial console - TX and RX - is live by the time we get here.
    //
    // Bring up the HDMI framebuffer console. If the VC declines (no display, or a
    // mailbox failure) the console layer stays a no-op and output continues on
    // serial only - the loader does not depend on a screen being present.
    //
    fb_init();
    fbcon_init();       // self-guards on fb_ok(); also sets the cursor/extent that
                        // GetDisplayStatus reports, so the arcdos editor works headless too

    usb_init();

    //
    // Bring up the emulated ARC firmware vector (console/memory/halt real,
    // remaining slots -> BlArcNotYetImplemented).
    //
    BlArmInitializeArcEmulator();

    //
    // Locate the Arc disk image in RAM (embedded blob under QEMU, or the firmware-
    // staged initramfs on real hardware), then correct the physical memory map so the
    // loader heap stays off it (BlMemoryInitialize, inside BlOsLoader, walks the map).
    // RamdiskInit must run first: BlArmFixupMemoryMap reads its region.
    //
    RamdiskInit();
    BlArmFixupMemoryMap();

    BlPrint("\nNT 3.5 ARC Firmware Emulator (ARM32 / Raspberry Pi 2)\n");
    BlPrint("Loaded from: %s\n", PartitionName ? PartitionName : "(unknown)");

#if RAMDISK_TEST
    BlArmRamdiskTest();
#endif

#if CONSOLE_ECHO_TEST
    BlArmConsoleEchoTest();
#endif

#if SMOKE_HANDOFF
    BlArmSmokeHandoff();   // jump to the stand-in payload; does not return
#endif

#if BOOT_MENU
    BlArmBootMenu();       // interactive kernel/arcdos/osloader selection; never returns
#endif

#if BOOT_KERNEL
    BlArmBootKernel();     // PE-load the kernel and transfer control; returns only on failure
    BlPrint("\nKernel boot failed; halting.\n");
    for (;;) {
    }
#endif

#if RUN_ARCDOS
    BlPrint("\nStarting ARC DOS...\n\n");
    ArcDosMain();
    BlPrint("\nArcDos exited; halting.\n");
    for (;;) {
    }
#endif

    Status = BlOsLoader(sizeof(BlArmArgv) / sizeof(BlArmArgv[0]), BlArmArgv, NULL);

    BlPrint("BlOsLoader returned 0x%lx.\n", Status);

    for (;;) {
    }
}
