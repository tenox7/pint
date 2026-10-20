/*++

Module Name:

    usb.c

Abstract:

    This module is the USB host of the firmware (usbhost.c, shared with the
    keyboard and pointer port driver): the DesignWare OTG core of the BCM2835
    peripherals (0x980000 into them), powered through the mailbox, or on a
    Raspberry Pi 4 Model B or 400 the VL805 USB 3 host of the USB-A ports,
    an xHCI (xhci.c) on the PCI Express link (pcie.c); and the keyboards
    behind its root ports, which the keyboard driver (jxkbd.c) reads as one
    keyboard of the boot protocol. The hubs are looked at now and then, so
    that a keyboard can be plugged in at the boot menu, which lists the
    devices. On a Raspberry Pi 4 the core drives the USB-C port, and on a
    Compute Module 4 its USB 2.0 port.

    The core masters the bus, so its buffer is given to it at its uncached
    VideoCore bus address, as the mailbox does; the PCI Express bus reaches
    memory at its physical addresses.

--*/

#include "fwp.h"
#include <stdio.h>
#include <string.h>
#include "../drivers/bcmusb/usbhost.h"
#include "bcmethhw.h"

#define USB_BASE                (PERIPHERAL_BASE + 0x980000)
#define POWER_DEVICE_USB        3
#define SCAN_INTERVAL_MS        250
#define KEYBOARD_WAIT_MS        2000
#define XHCI_PORTSC(Port)       (0x400 + ((Port) - 1) * 0x10)

static USB_HOST UsbHost;
static __declspec(align(64)) UCHAR UsbBuffer[USB_BUFFER_SIZE];
static __declspec(align(4096)) UCHAR UsbXhciMemory[XHCI_MEMORY_SIZE];
static UCHAR UsbKeys[USB_MAXIMUM_HIDS][USB_KEY_BYTES];
static UCHAR UsbGeneration[USB_MAXIMUM_HIDS];
static ULONG UsbScanTime;
static BOOLEAN UsbReady;

VOID
UsbStall(
    IN ULONG Microseconds
    )
{
    FwStallExecution(Microseconds);
}

VOID
UsbSleep(
    IN ULONG Milliseconds
    )
{
    FwStallExecution(Milliseconds * 1000);
}

ULONG
UsbClock(
    VOID
    )
{
    return FwGetMicroseconds();
}

//
// The Ethernet controller among the devices: the board's (the function of
// its LAN9514 or LAN7515), or a CDC Ethernet device, described in the
// configuration tree for the network driver, once it is ready (that of
// the LAN7515 comes up after the keyboards).
//

static VOID
UsbFindNetworkController (
    VOID
    )
{
    PUSB_DEVICE Device;
    ULONG Index;

    for (Index = 0; Index < USB_MAXIMUM_DEVICES; Index += 1) {
        Device = &UsbHost.Devices[Index];
        if (!Device->Present || Device->Stage != USB_STAGE_READY) {
            continue;
        }

        if (Device->Vendor == USB_VENDOR_SMSC && Device->Product == USB_PRODUCT_LAN9500) {
            FwAddNetworkController(BCMETH_ID_LAN9500);
            return;
        }

        if (Device->Vendor == USB_VENDOR_SMSC && Device->Product == USB_PRODUCT_LAN7800) {
            FwAddNetworkController(BCMETH_ID_LAN7800);
            return;
        }

        if (Device->Class == USB_CLASS_COMMUNICATION) {
            FwAddNetworkController(BCMETH_ID_CDC);
            return;
        }
    }
}

BOOLEAN
UsbInitialize (
    VOID
    )

/*++

Routine Description:

    This routine brings up the USB host, which finds the devices behind its
    root ports: on a Raspberry Pi 4 the USB 3 host on the PCI Express link if
    there is one (the VL805 of the USB-A ports), else the OTG core, powered
    through the mailbox. If no keyboard is among the devices, it looks for one
    coming up for a while, as the console is chosen by it.

Return Value:

    TRUE if the host works, else FALSE.

--*/

{
    ULONG Registers;
    ULONG Start;

    if (FwBcm2711 && PcieInitialize(&Registers)) {
        UsbHost.Xhci = TRUE;
        UsbHost.Registers = (PUCHAR)Registers;
        UsbHost.Memory = UsbXhciMemory;
        UsbHost.MemoryAddress = (ULONG)UsbXhciMemory;
        UsbHost.BufferAddress = (ULONG)UsbBuffer;
    } else {
        if (MailboxSetPowerState(POWER_DEVICE_USB, TRUE) != 0) {
            FwPrint("USB: power on failed\r\n");
        }

        FwStallExecution(40 * 1000);
        UsbHost.Registers = (PUCHAR)USB_BASE;
        UsbHost.BufferAddress = MailboxPhysicalToBus((ULONG)UsbBuffer);
    }

    UsbHost.Buffer = UsbBuffer;
    UsbHost.ClockRate = 1000;
    UsbReady = UsbHostInitialize(&UsbHost);
    if (!UsbReady && UsbHost.Xhci) {
        FwPrint("USB: the xHCI failed at step %lu\r\n", UsbHost.Interrupt);
    } else if (!UsbReady) {
        FwPrint("USB: no host\r\n");
    }

    Start = FwGetMicroseconds();
    while (UsbReady && !UsbKeyboardPresent() && FwGetMicroseconds() - Start < KEYBOARD_WAIT_MS * 1000) {
        FwStallExecution(SCAN_INTERVAL_MS * 1000);
        UsbHostScan(&UsbHost);
    }

    UsbFindNetworkController();
    UsbScanTime = FwGetMicroseconds();
    return UsbReady;
}

BOOLEAN
UsbKeyboardPresent (
    VOID
    )
{
    ULONG Index;

    for (Index = 0; Index < USB_MAXIMUM_HIDS; Index += 1) {
        if (UsbHost.Hids[Index].Present && (UsbHost.Hids[Index].Kinds & HID_KEYBOARD)) {
            return TRUE;
        }
    }

    return FALSE;
}

//
// Forget the keys of a keyboard that went (or whose slot a new one took):
// TRUE if it held any.
//

static BOOLEAN
UsbReleaseKeys(
    IN ULONG Index
    )
{
    ULONG Byte;
    BOOLEAN Held = FALSE;

    for (Byte = 0; Byte < USB_KEY_BYTES; Byte += 1) {
        Held |= UsbKeys[Index][Byte] != 0;
        UsbKeys[Index][Byte] = 0;
    }

    return Held;
}

BOOLEAN
UsbKeyboardReport (
    OUT UCHAR Report[8]
    )

/*++

Routine Description:

    This routine polls the keyboards, and the hubs if it is time to.

Arguments:

    Report - Receives a report of the boot protocol of the keys all the
        keyboards hold down: the modifiers in byte 0 and up to six keys from
        byte 2.

Return Value:

    TRUE if the keys down changed, else FALSE.

--*/

{
    UCHAR Data[USB_REPORT_LENGTH];
    UCHAR Keys[USB_KEY_BYTES];
    PUSB_HID Hid;
    BOOLEAN Changed = FALSE;
    ULONG Index;
    ULONG Usage;
    ULONG Count = 2;
    LONG Length;

    if (!UsbReady) {
        return FALSE;
    }

    if (FwGetMicroseconds() - UsbScanTime >= SCAN_INTERVAL_MS * 1000) {
        UsbHostScan(&UsbHost);
        UsbFindNetworkController();
        UsbScanTime = FwGetMicroseconds();
    }

    for (Index = 0; Index < USB_MAXIMUM_HIDS; Index += 1) {
        Hid = &UsbHost.Hids[Index];
        if (!Hid->Present || Hid->Generation != UsbGeneration[Index]) {
            Changed |= UsbReleaseKeys(Index);
            UsbGeneration[Index] = Hid->Generation;
        }

        if (!Hid->Present || (Hid->Kinds & HID_KEYBOARD) == 0) {
            continue;
        }

        Length = UsbHidRead(&UsbHost, Hid, Data);
        if (Length <= 0 || !UsbHidKeys(Hid, Data, Length, Keys) ||
            memcmp(Keys, UsbKeys[Index], USB_KEY_BYTES) == 0) {
            continue;
        }

        memcpy(UsbKeys[Index], Keys, USB_KEY_BYTES);
        Changed = TRUE;
    }

    if (!Changed) {
        return FALSE;
    }

    memset(Report, 0, 8);
    for (Usage = 0; Usage < USB_KEY_BYTES * 8; Usage += 1) {
        for (Index = 0; Index < USB_MAXIMUM_HIDS; Index += 1) {
            if (UsbKeys[Index][Usage / 8] & (1 << (Usage & 7))) {
                break;
            }
        }

        if (Index == USB_MAXIMUM_HIDS) {
            continue;
        }

        if (Usage >= 0xE0 && Usage <= 0xE7) {
            Report[0] |= 1 << (Usage - 0xE0);
        } else if (Count < 8) {
            Report[Count++] = (UCHAR)Usage;
        }
    }

    return TRUE;
}

VOID
UsbPrintDevices (
    VOID
    )

/*++

Routine Description:

    This routine lists the USB devices for the boot menu, which redraws it
    every second: the frame interval of the core (or the version of the xHCI
    and the status registers of its root ports), then for each device the
    path of hub ports to it (0 is the hub on the root port of the core; on an
    xHCI the path starts at its root port), its vendor and product, speed and
    use, or the step of its enumeration it failed at, with the interrupts of
    the host channel (on an xHCI the type and completion code of the command
    or transfer that failed), and the counts of the reads of each of its
    keyboards and pointers. The lines of devices that went are cleared.

--*/

{
    static PCHAR Speeds[] = {"high", "full", "low", "super"};
    static ULONG LastLines;
    PUSB_DEVICE Device;
    PUSB_HID Hid;
    CHAR Path[3 * USB_MAXIMUM_DEPTH + 8];
    UCHAR Ports[USB_MAXIMUM_DEPTH + 2];
    ULONG Index;
    ULONG Parent;
    ULONG Depth;
    ULONG Length;
    ULONG Lines = 1;

    if (!UsbReady) {
        return;
    }

    if (UsbHost.Xhci) {
        FwPrint(" USB xHCI %lx.%02lx, ports", UsbHost.Xhc.Version >> 8, UsbHost.Xhc.Version & 0xFF);
        for (Index = 1; Index <= UsbHost.Xhc.Ports; Index += 1) {
            FwPrint(" %08lx", *(volatile ULONG *)(UsbHost.Registers + UsbHost.Xhc.Operational + XHCI_PORTSC(Index)));
        }

        FwPrint("\x9bK\r\n");
    } else {
        FwPrint(" USB frame interval %lu, %u frames in 1 ms (at reset %lu, %u)\x9bK\r\n",
                UsbHost.FrameInterval - 1, UsbHost.Frames, UsbHost.ResetFrameInterval,
                UsbHost.ResetFrames);
    }

    for (Index = 0; Index < USB_MAXIMUM_DEVICES; Index += 1) {
        Device = &UsbHost.Devices[Index];
        if (!Device->Present) {
            continue;
        }

        Depth = 0;
        for (Parent = Index; UsbHost.Devices[Parent].Parent != USB_ROOT && Depth <= USB_MAXIMUM_DEPTH;
             Parent = UsbHost.Devices[Parent].Parent) {
            Ports[Depth++] = UsbHost.Devices[Parent].Port;
        }

        if (UsbHost.Xhci) {
            Ports[Depth++] = UsbHost.Devices[Parent].Port;
        }

        Length = sprintf(Path, "%s", Depth == 0 ? "0" : "");
        while (Depth != 0) {
            Depth -= 1;
            Length += sprintf(Path + Length, Depth != 0 ? "%u." : "%u", Ports[Depth]);
        }

        Lines += 1;
        if (Device->Stage != USB_STAGE_READY) {
            FwPrint(" USB %s: %s speed, failed at step %u (%08lx)\x9bK\r\n",
                    Path, Speeds[Device->Speed], Device->Stage, Device->Interrupt);
            continue;
        }

        FwPrint(" USB %s: %04x:%04x %s%s%s%s\x9bK\r\n", Path, Device->Vendor, Device->Product,
                Speeds[Device->Speed], Device->Ports != 0 ? " hub" : "",
                (Device->Kinds & HID_KEYBOARD) ? " keyboard" : "",
                (Device->Kinds & HID_POINTER) ? " pointer" : "");
        for (Hid = UsbHost.Hids; Hid < UsbHost.Hids + USB_MAXIMUM_HIDS; Hid += 1) {
            if (!Hid->Present || Hid->Device != Index) {
                continue;
            }

            Lines += 1;
            FwPrint("    interface %u: %u reports, %u naks, %u failed, %u late (%08lx)\x9bK\r\n",
                    Hid->Interface, Hid->Reports, Hid->Naks, Hid->Failures, Hid->Late,
                    Hid->LastInterrupt);
        }
    }

    for (Index = Lines; Index < LastLines; Index += 1) {
        FwPrint("\x9bK\r\n");
    }

    LastLines = Lines;
}
