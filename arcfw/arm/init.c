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
// (The OEM font blob + framebuffer geometry are handed to the kernel in BlSetupForNt,
// arcfw/arm/ntsetup.c, which owns those externs now.)

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
    "osloader=multi(0)disk(0)rdisk(0)partition(1)\\WINNT\\System32\\NTLDR",   // dir (\WINNT\System32) is where BlOsLoader looks for hal.dll
    "systempartition=multi(0)disk(0)rdisk(0)partition(1)",
    "osloadfilename=\\WINNT",
    "osloadpartition=multi(0)disk(0)rdisk(0)partition(1)",
    "osloadoptions=SOS",        // NT /SOS: print each file as BlOsLoader loads it
    "consolein=multi(0)serial(0)",
    "consoleout=multi(0)serial(0)"
};

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
// Interactive boot menu (like an ARC firmware boot selection): on startup, let the
// user pick [1] boot NT (via BlOsLoader) or [2] the arcdos shell, over the
// console (serial + USB keyboard), with a short countdown to a default. The friendly way
// to switch WITHOUT recompiling. When BOOT_MENU is 1 (default) the menu runs and the
// RUN_ARCDOS gate above is bypassed. Set BOOT_MENU 0 to boot straight to one fixed path
// (e.g. headless regression tests: BOOT_MENU 0 + RAMDISK_TEST 1 + RUN_ARCDOS 0).
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

#if BOOT_MENU
//
// Interactive boot menu - the runtime switch between booting NT (via BlOsLoader:
// loads \WINNT\System32\{ntoskrnl,hal}, hands off to the kernel) and the arcdos shell,
// modeled on an ARC firmware boot selection. Input comes
// through the emulated firmware vector (ArcGetReadStatus/ArcRead),
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
    BlPrint("  [1] Boot NT (OS Loader)  \\WINNT\\System32\\NTOSKRNL.EXE  (default)\n");
    BlPrint("  [2] ARC DOS shell\n");
    BlPrint("====================================================\n");

    haveConsole = (BOOLEAN)(ArcOpen("multi(0)serial(0)", ArcOpenReadOnly, &fid) == ESUCCESS);

    //
    // Poll the console one second at a time, redrawing the countdown (\r returns to
    // column 0 on both the serial terminal and the framebuffer console). 1/2/3 select;
    // Enter takes the default; any other key is ignored.
    //
    for (remaining = BOOT_MENU_SECONDS; remaining > 0 && choice == 0; remaining -= 1) {
        BlPrint("\rSelect 1-2, or wait %d s for [%d]: ", remaining, BOOT_MENU_DEFAULT);

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
            if (ch >= '1' && ch <= '2') {
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

    case 1:
    default:
        {
            //
            // Boot NT through the OS Loader: BlOsLoader loads
            // \WINNT\System32\{ntoskrnl.exe,hal.dll}, builds the loaded-module list,
            // resolves imports, loads the SYSTEM hive + NLS data, runs BlSetupForNt, and
            // transfers to the kernel (which renders the boot-status screen via the HAL).
            // Returns only on failure.
            //
            ARC_STATUS s;
            s = BlOsLoader(sizeof(BlArmArgv) / sizeof(BlArmArgv[0]), BlArmArgv, NULL);
            BlPrint("BlOsLoader returned 0x%lx.\n", (unsigned long)s);
        }
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

#if BOOT_MENU
    BlArmBootMenu();       // interactive NT (OS Loader) / arcdos selection; never returns
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
