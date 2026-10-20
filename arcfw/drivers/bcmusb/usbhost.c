/*++

Module Name:

    usbhost.c

Abstract:

    This module implements the USB host the firmware and the keyboard and
    pointer port driver share: the Synopsys DesignWare OTG core of the
    BCM2835 in host mode with DMA, its root port, the hubs behind it (on the
    board the LAN9514, and those plugged into its ports), and the keyboards
    and pointers of the HID class. An xHCI (xhci.c) runs the transfers and
    the root ports instead of the core when the host is one; the
    enumeration, the hubs and the HID class are the same.

    Transfers run on host channel 0, one transaction group at a time, through
    the buffer of the host. A full or low speed device behind a high speed hub
    is reached with split transactions: the start split hands a transaction to
    the transaction translator of the hub, then complete splits collect its
    result, repeated while the hub answers NYET. A NAK or an error repeats the
    packet, not the transfer, so the data and the toggle of the packets done
    are kept. The start split of an interrupt transfer goes in microframes 0
    to 4, so that its complete splits, up to four microframes later, stay in
    the frame the translator keeps the result for. The core comes out of reset
    with the frame interval of full speed, so the interval of the speed of the
    root port is set once the port is enabled, as the microframes are timed by
    it. An interface whose reads fail is read again after a delay that
    doubles, as Linux does.

    The hubs are enumerated at initialization, then their status change
    endpoints polled, so that devices can come and go. A keyboard or a pointer
    uses the boot protocol if its interface has it, else (or if the device
    refuses it) the report protocol, with the layout of its report descriptor:
    an absolute pointer, such as that of a KVM or a tablet, reports its
    position, scaled to 0 - 0xFFFF.

--*/

#ifdef USB_FIRMWARE
#include "fwp.h"
#define READ_REG(Host, Offset) (*(volatile ULONG *)((Host)->Registers + (Offset)))
#define WRITE_REG(Host, Offset, Value) (*(volatile ULONG *)((Host)->Registers + (Offset)) = (Value))
#define MEMORY_BARRIER() __dsb(_ARM_BARRIER_SY)
#else
#include "ntddk.h"
#define READ_REG(Host, Offset) READ_REGISTER_ULONG((PULONG)((Host)->Registers + (Offset)))
#define WRITE_REG(Host, Offset, Value) WRITE_REGISTER_ULONG((PULONG)((Host)->Registers + (Offset)), (Value))
#define MEMORY_BARRIER() KeFlushWriteBuffer()
#endif

#include "usbhost.h"
#include "xhci.h"

//
// DWC2 core registers and their fields.
//

#define GAHBCFG             0x008
#define GUSBCFG             0x00C
#define GRSTCTL             0x010
#define GINTSTS             0x014
#define GINTMSK             0x018
#define GRXFSIZ             0x024
#define GNPTXFSIZ           0x028
#define GSNPSID             0x040
#define GHWCFG2             0x048
#define HPTXFSIZ            0x100
#define HCFG                0x400
#define HFIR                0x404
#define HFNUM               0x408
#define HAINT               0x414
#define HAINTMSK            0x418
#define HPRT0               0x440
#define HCCHAR              0x500
#define HCSPLT              0x504
#define HCINT               0x508
#define HCINTMSK            0x50C
#define HCTSIZ              0x510
#define HCDMA               0x514
#define PCGCCTL             0xE00

#define GAHBCFG_DMA_EN          0x00000020
#define GAHBCFG_HBSTLEN_INCR4   0x00000006
#define GAHBCFG_GLBL_INTR_EN    0x00000001
#define GUSBCFG_FORCEHOSTMODE   0x20000000
#define GUSBCFG_ULPI_CLK_SUSP_M 0x00080000
#define GUSBCFG_ULPI_FS_LS      0x00020000
#define GUSBCFG_PHYSEL          0x00000040
#define GUSBCFG_ULPI_UTMI_SEL   0x00000010
#define GUSBCFG_PHYIF16         0x00000008
#define GRSTCTL_AHBIDLE         0x80000000
#define GRSTCTL_TXFNUM_ALL      0x00000400
#define GRSTCTL_TXFFLSH         0x00000020
#define GRSTCTL_RXFFLSH         0x00000010
#define GRSTCTL_CSFTRST         0x00000001
#define GINTSTS_CURMODE_HOST    0x00000001
#define GINTSTS_HCHINT          0x02000000
#define GHWCFG2_CHANNELS_SHIFT  14
#define HCFG_FSLSPCLKSEL_30_60  0x00000000

#define HPRT0_SPD_SHIFT         17
#define HPRT0_SPD_MASK          0x00060000
#define HPRT0_PWR               0x00001000
#define HPRT0_RST               0x00000100
#define HPRT0_ENACHG            0x00000008
#define HPRT0_ENA               0x00000004
#define HPRT0_CONNDET           0x00000002
#define HPRT0_CONNSTS           0x00000001
#define HPRT0_W1C_MASK          (HPRT0_CONNDET | HPRT0_ENA | HPRT0_ENACHG | 0x20)

#define HCCHAR_CHENA            0x80000000
#define HCCHAR_CHDIS            0x40000000
#define HCCHAR_ODDFRM           0x20000000
#define HCCHAR_DEVADDR_SHIFT    22
#define HCCHAR_MULTICNT_ONE     0x00100000
#define HCCHAR_EPTYPE_SHIFT     18
#define HCCHAR_PERIODIC         0x00040000
#define HCCHAR_LSPDDEV          0x00020000
#define HCCHAR_EPDIR_IN         0x00008000
#define HCCHAR_EPNUM_SHIFT      11

#define HCSPLT_SPLTENA          0x80000000
#define HCSPLT_COMPSPLT         0x00010000
#define HCSPLT_XACTPOS_ALL      0x0000C000
#define HCSPLT_HUBADDR_SHIFT    7

#define HCINT_XFERCOMPL         0x00000001
#define HCINT_CHHLTD            0x00000002
#define HCINT_STALL             0x00000008
#define HCINT_NAK               0x00000010
#define HCINT_ACK               0x00000020
#define HCINT_NYET              0x00000040
#define HCINT_FRMOVRUN          0x00000200
#define HCINT_DATATGLERR        0x00000400
#define HCINT_ALL               0x000007FF
#define HCINT_TIMEOUT           0x80000000

#define HCTSIZ_PID_SHIFT        29
#define HCTSIZ_PKTCNT_SHIFT     19
#define HCTSIZ_PKTCNT_MASK      0x1FF80000
#define HCTSIZ_XFERSIZE_MASK    0x0007FFFF

#define HFNUM_FRNUM_MASK        0x00003FFF
#define HFNUM_FRREM_SHIFT       16

#define PID_DATA0               0
#define PID_DATA1               2
#define PID_SETUP               3

#define EPTYPE_CONTROL          0
#define EPTYPE_BULK             2
#define EPTYPE_INTERRUPT        3

//
// The results of a transaction, and of a transfer (else its length).
//

#define XACT_DONE               0
#define XACT_NAK                1
#define XACT_STALL              2
#define XACT_TOGGLE             3
#define XACT_ERROR              4

#define CONTROL_TIMEOUT_MS      1000
#define PERIODIC_TIMEOUT_MS     4
#define TRANSFER_ERRORS         3
#define SPLIT_WINDOW            4
#define START_SPLIT_FRAMES      16
#define RETRY_FIRST_MS          13
#define RETRY_LAST_MS           104
#define ENUMERATION_TRIES       4
#define POWER_CYCLE_MS          200
#define PHY_CLOCK_MHZ           60

//
// USB requests, descriptors and features of the standard, the hub class and
// the HID class.
//

#define REQUEST_GET_STATUS      0x00
#define REQUEST_CLEAR_FEATURE   0x01
#define REQUEST_SET_FEATURE     0x03
#define REQUEST_SET_ADDRESS     0x05
#define REQUEST_GET_DESCRIPTOR  0x06
#define REQUEST_SET_CONFIG      0x09
#define REQUEST_SET_REPORT      0x09
#define REQUEST_SET_IDLE        0x0A
#define REQUEST_SET_PROTOCOL    0x0B

#define DESCRIPTOR_DEVICE       0x01
#define DESCRIPTOR_CONFIG       0x02
#define DESCRIPTOR_INTERFACE    0x04
#define DESCRIPTOR_ENDPOINT     0x05
#define DESCRIPTOR_HID          0x21
#define DESCRIPTOR_REPORT       0x22
#define DESCRIPTOR_HUB          0x29

#define CLASS_HID               3
#define CLASS_HUB               9
#define SUBCLASS_BOOT           1
#define PROTOCOL_KEYBOARD       1
#define PROTOCOL_MOUSE          2

//
// The usages of the HID class the keyboards and pointers are read with.
//

#define PAGE_KEYBOARD           0x07
#define PAGE_BUTTON             0x09
#define USAGE_POINTER           0x00010001
#define USAGE_MOUSE             0x00010002
#define USAGE_KEYBOARD          0x00010006
#define USAGE_KEYPAD            0x00010007
#define USAGE_X                 0x00010030
#define USAGE_Y                 0x00010031
#define USAGE_NUM_LOCK          0x00080001
#define USAGE_ERROR_ROLLOVER    0x01
#define USAGE_FIRST_KEY         0x04
#define USAGE_FIRST_MODIFIER    0xE0

#define HID_USAGES              8
#define HID_REPORTS             16

//
// An interface of the HID class in the configuration of a device.
//

typedef struct _HID_INTERFACE {
    UCHAR Interface;
    UCHAR Subclass;
    UCHAR Protocol;
    UCHAR Endpoint;
    UCHAR Interval;
    USHORT Maximum;
    USHORT ReportLength;
} HID_INTERFACE, *PHID_INTERFACE;

#define DEVICE_HIDS             4

static VOID
UsbEnumerate(
    IN PUSB_HOST Host,
    IN ULONG Parent,
    IN ULONG Port,
    IN ULONG Speed,
    IN BOOLEAN Enabled
    );

//
// Wait up to a limit for the bits of the mask in a register to be set or
// clear.
//

static BOOLEAN
UsbWaitRegister(
    IN PUSB_HOST Host,
    IN ULONG Offset,
    IN ULONG Mask,
    IN BOOLEAN Set,
    IN ULONG Milliseconds
    )
{
    ULONG Start = UsbClock();

    while ((READ_REG(Host, Offset) & Mask) != (Set ? Mask : 0)) {
        if (UsbClock() - Start >= Milliseconds * Host->ClockRate) {
            return FALSE;
        }

        UsbStall(10);
    }

    return TRUE;
}

static BOOLEAN
UsbResetCore(
    IN PUSB_HOST Host
    )
{
    if (!UsbWaitRegister(Host, GRSTCTL, GRSTCTL_AHBIDLE, TRUE, CONTROL_TIMEOUT_MS)) {
        return FALSE;
    }

    WRITE_REG(Host, GRSTCTL, READ_REG(Host, GRSTCTL) | GRSTCTL_CSFTRST);
    if (!UsbWaitRegister(Host, GRSTCTL, GRSTCTL_CSFTRST, FALSE, CONTROL_TIMEOUT_MS)) {
        return FALSE;
    }

    UsbSleep(10);
    return TRUE;
}

//
// Wait for channel 0 to halt, up to a deadline of the clock: a short spin
// covers a transaction, then the thread sleeps between looks. A channel
// still running at the deadline (the core repeats the transactions the
// device does not acknowledge) is halted. A periodic channel without splits
// cannot be halted in DMA mode: the core halts it at the end of its
// (micro)frame.
//

static ULONG
UsbWaitChannel(
    IN PUSB_HOST Host,
    IN ULONG Limit
    )
{
    ULONG Start = UsbClock();
    ULONG Interrupt;
    ULONG Character;
    ULONG Now;

    for (;;) {
        Interrupt = READ_REG(Host, HCINT);
        if (Interrupt & HCINT_CHHLTD) {
            return Interrupt;
        }

        Now = UsbClock();
        if ((LONG)(Now - Limit) >= 0) {
            break;
        }

        if (Now - Start >= Host->ClockRate) {
            UsbSleep(1);
        }
    }

    Character = READ_REG(Host, HCCHAR);
    if ((Character & HCCHAR_CHENA) &&
        ((Character & HCCHAR_PERIODIC) == 0 || (READ_REG(Host, HCSPLT) & HCSPLT_SPLTENA))) {
        WRITE_REG(Host, HCCHAR, Character | HCCHAR_CHDIS);
    }

    UsbWaitRegister(Host, HCINT, HCINT_CHHLTD, TRUE, 10);
    return READ_REG(Host, HCINT) | HCINT_TIMEOUT;
}

//
// Run one transaction group on channel 0 and wait for it: the packets and
// the size of the transfer go through the buffer of the host, and a periodic
// transfer goes in the next microframe (or frame). A channel a deadline left
// running is halted first. What the channel sends must be in memory before it
// starts, and what it received is read after it halted: to the processor the
// buffer is ordinary memory, which it may reorder with the registers. Returns
// the interrupts of the channel, and its transfer size register at the halt.
//

static ULONG
UsbRunChannel(
    IN PUSB_HOST Host,
    IN ULONG Character,
    IN ULONG Split,
    IN ULONG Pid,
    IN ULONG Packets,
    IN ULONG Size,
    IN ULONG Limit,
    OUT PULONG Transfer
    )
{
    ULONG Interrupt;

    if (READ_REG(Host, HCCHAR) & HCCHAR_CHENA) {
        WRITE_REG(Host, HCCHAR, READ_REG(Host, HCCHAR) | HCCHAR_CHDIS);
        UsbWaitRegister(Host, HCCHAR, HCCHAR_CHENA, FALSE, 10);
    }

    WRITE_REG(Host, HCSPLT, Split);
    WRITE_REG(Host, HCTSIZ, (Pid << HCTSIZ_PID_SHIFT) | (Packets << HCTSIZ_PKTCNT_SHIFT) | Size);
    WRITE_REG(Host, HCDMA, Host->BufferAddress);
    WRITE_REG(Host, HCINT, HCINT_ALL);
    if ((Character & HCCHAR_PERIODIC) && (READ_REG(Host, HFNUM) & 1) == 0) {
        Character |= HCCHAR_ODDFRM;
    }

    MEMORY_BARRIER();
    WRITE_REG(Host, HCCHAR, Character | HCCHAR_MULTICNT_ONE | HCCHAR_CHENA);
    Interrupt = UsbWaitChannel(Host, Limit);
    MEMORY_BARRIER();
    *Transfer = READ_REG(Host, HCTSIZ);
    WRITE_REG(Host, HCINT, HCINT_ALL);
    Host->Interrupt = Interrupt;
    return Interrupt;
}

static LONG
UsbStatus(
    IN ULONG Interrupt
    )
{
    if (Interrupt & HCINT_STALL) {
        return XACT_STALL;
    }

    if (Interrupt & HCINT_DATATGLERR) {
        return XACT_TOGGLE;
    }

    if ((Interrupt & (HCINT_NAK | HCINT_TIMEOUT)) == HCINT_NAK) {
        return XACT_NAK;
    }

    return XACT_ERROR;
}

//
// Wait until the next microframe is one of 0 to 4, with an eighth of this
// one left to start the channel: the start split of an interrupt transfer
// goes there. If two frames go by without one, it goes in the next. Returns
// the number of that microframe.
//

static ULONG
UsbWaitStartSplit(
    IN PUSB_HOST Host,
    IN ULONG Limit
    )
{
    ULONG Margin = Host->FrameInterval / 8;
    ULONG First = READ_REG(Host, HFNUM);
    ULONG Frame;

    do {
        Frame = READ_REG(Host, HFNUM);
        if (((Frame + 1) & 7) <= 4 && (Frame >> HFNUM_FRREM_SHIFT) >= Margin) {
            break;
        }
    } while (((Frame - First) & HFNUM_FRNUM_MASK) < START_SPLIT_FRAMES && (LONG)(UsbClock() - Limit) < 0);

    return (Frame + 1) & HFNUM_FRNUM_MASK;
}

//
// One packet through the transaction translator of a hub: the start split,
// repeated while a periodic one misses its microframe, then the complete
// splits, repeated while the hub has no result yet (a periodic transaction's
// within the window the hub keeps it for).
//

static LONG
UsbSplit(
    IN PUSB_HOST Host,
    IN ULONG Character,
    IN ULONG Split,
    IN OUT PUCHAR Pid,
    IN ULONG Size,
    IN ULONG Limit,
    OUT PULONG Moved
    )
{
    BOOLEAN In = (Character & HCCHAR_EPDIR_IN) != 0;
    BOOLEAN Periodic = (Character & HCCHAR_PERIODIC) != 0;
    ULONG Interrupt;
    ULONG Transfer;
    ULONG Start = 0;

    do {
        if (Periodic) {
            Start = UsbWaitStartSplit(Host, Limit);
        }

        Interrupt = UsbRunChannel(Host, Character, Split, *Pid, 1, Size, Limit, &Transfer);
    } while ((Interrupt & (HCINT_FRMOVRUN | HCINT_TIMEOUT)) == HCINT_FRMOVRUN);

    if ((Interrupt & HCINT_ACK) == 0) {
        return UsbStatus(Interrupt);
    }

    for (;;) {
        Interrupt = UsbRunChannel(Host, Character, Split | HCSPLT_COMPSPLT, *Pid, 1,
                                  In ? Size : 0, Limit, &Transfer);
        if (Interrupt & HCINT_XFERCOMPL) {
            *Moved = In ? Size - (Transfer & HCTSIZ_XFERSIZE_MASK) : Size;
            *Pid = (UCHAR)((Transfer >> HCTSIZ_PID_SHIFT) & 3);
            return XACT_DONE;
        }

        if ((Interrupt & (HCINT_NYET | HCINT_FRMOVRUN)) == 0 || (Interrupt & HCINT_TIMEOUT)) {
            return UsbStatus(Interrupt);
        }

        if (Periodic && ((READ_REG(Host, HFNUM) - Start) & HFNUM_FRNUM_MASK) >= SPLIT_WINDOW) {
            return XACT_ERROR;
        }
    }
}

//
// A transfer to or from an endpoint of a device, in packet groups through
// the buffer of the host (a packet at a time if split, or periodic) until
// the length is moved or a short packet ends it. A NAK, a toggle error or a
// transaction error repeats the packet group within the time limit (up to
// three errors in a row); a periodic transfer is tried once. Returns the
// length moved, or XFER_NAK, XFER_STALL or XFER_ERROR.
//

static LONG
UsbTransfer(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint,
    IN ULONG Type,
    IN BOOLEAN In,
    IN ULONG Maximum,
    IN OUT PUCHAR Pid,
    IN OUT PUCHAR Buffer,
    IN ULONG Length,
    IN ULONG Milliseconds
    )
{
    ULONG Limit = UsbClock() + Milliseconds * Host->ClockRate;
    ULONG Character;
    ULONG Split = 0;
    ULONG Group;
    ULONG Done = 0;
    ULONG Errors = 0;
    ULONG Naks = 0;
    ULONG Chunk;
    ULONG Packets;
    ULONG Size;
    ULONG Moved;
    ULONG Count;
    ULONG Interrupt;
    ULONG Transfer;
    LONG Status;

    if (Maximum == 0 || Maximum > USB_BUFFER_SIZE) {
        return XFER_ERROR;
    }

    Character = ((ULONG)Device->Address << HCCHAR_DEVADDR_SHIFT) | (Endpoint << HCCHAR_EPNUM_SHIFT) |
                (Type << HCCHAR_EPTYPE_SHIFT) | (In ? HCCHAR_EPDIR_IN : 0) | Maximum |
                (Device->Speed == SPEED_LOW ? HCCHAR_LSPDDEV : 0);
    if (Device->TtAddress != 0) {
        Split = HCSPLT_SPLTENA | HCSPLT_XACTPOS_ALL |
                ((ULONG)Device->TtAddress << HCSPLT_HUBADDR_SHIFT) | Device->TtPort;
    }

    Group = (Split != 0 || (Character & HCCHAR_PERIODIC)) ? Maximum : (USB_BUFFER_SIZE / Maximum) * Maximum;
    for (;;) {
        Chunk = Length - Done < Group ? Length - Done : Group;
        Packets = Chunk == 0 ? 1 : (Chunk + Maximum - 1) / Maximum;
        Size = In ? Packets * Maximum : Chunk;
        if (!In && Chunk != 0) {
            RtlMoveMemory(Host->Buffer, Buffer + Done, Chunk);
        }

        Moved = 0;
        if (Split != 0) {
            Status = UsbSplit(Host, Character, Split, Pid, Size, Limit, &Moved);
        } else {
            Interrupt = UsbRunChannel(Host, Character, 0, *Pid, Packets, Size, Limit, &Transfer);
            if (Interrupt & HCINT_XFERCOMPL) {
                Status = XACT_DONE;
                Moved = In ? Size - (Transfer & HCTSIZ_XFERSIZE_MASK) : Size;
            } else {
                Status = UsbStatus(Interrupt);
                Moved = (Packets - ((Transfer & HCTSIZ_PKTCNT_MASK) >> HCTSIZ_PKTCNT_SHIFT)) * Maximum;
            }

            *Pid = (UCHAR)((Transfer >> HCTSIZ_PID_SHIFT) & 3);
        }

        Count = Moved < Length - Done ? Moved : Length - Done;
        if (In && Count != 0) {
            RtlMoveMemory(Buffer + Done, Host->Buffer, Count);
        }

        Done += Count;
        if (Status == XACT_DONE) {
            Errors = 0;
            Naks = 0;
            if (Done >= Length || (In && Moved < Size)) {
                return (LONG)Done;
            }

            continue;
        }

        if (Status == XACT_STALL) {
            return XFER_STALL;
        }

        if (Character & HCCHAR_PERIODIC) {
            return Status == XACT_ERROR ? XFER_ERROR : XFER_NAK;
        }

        if ((LONG)(UsbClock() - Limit) >= 0 ||
            (Status == XACT_ERROR && ++Errors == TRANSFER_ERRORS)) {
            return XFER_ERROR;
        }

        if (Status == XACT_NAK && ++Naks >= 3 && !(Type == EPTYPE_CONTROL && In)) {
            UsbSleep(1);
        }
    }
}

//
// A control transfer: the setup stage, the data stage if there is data, and
// the status stage. Returns the length of the data, or an XFER_ result.
//

static LONG
UsbControl(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG RequestType,
    IN ULONG Request,
    IN ULONG Value,
    IN ULONG Index,
    IN OUT PVOID Data,
    IN ULONG Length
    )
{
    UCHAR Setup[8];
    UCHAR Pid = PID_SETUP;
    BOOLEAN In = (RequestType & 0x80) != 0;
    LONG Actual = 0;
    LONG Status;

    Setup[0] = (UCHAR)RequestType;
    Setup[1] = (UCHAR)Request;
    Setup[2] = (UCHAR)Value;
    Setup[3] = (UCHAR)(Value >> 8);
    Setup[4] = (UCHAR)Index;
    Setup[5] = (UCHAR)(Index >> 8);
    Setup[6] = (UCHAR)Length;
    Setup[7] = (UCHAR)(Length >> 8);
    if (Host->Xhci) {
        return XhciControl(Host, Device, Setup, Data, Length);
    }

    Status = UsbTransfer(Host, Device, 0, EPTYPE_CONTROL, FALSE, Device->ControlMaximum, &Pid,
                         Setup, sizeof(Setup), CONTROL_TIMEOUT_MS);
    if (Status < 0) {
        return Status;
    }

    if (Length != 0) {
        Pid = PID_DATA1;
        Actual = UsbTransfer(Host, Device, 0, EPTYPE_CONTROL, In, Device->ControlMaximum, &Pid,
                             Data, Length, CONTROL_TIMEOUT_MS);
        if (Actual < 0) {
            return Actual;
        }
    }

    Pid = PID_DATA1;
    Status = UsbTransfer(Host, Device, 0, EPTYPE_CONTROL, Length != 0 ? !In : TRUE,
                         Device->ControlMaximum, &Pid, NULL, 0, CONTROL_TIMEOUT_MS);
    return Status < 0 ? Status : Actual;
}

//
// Read the interrupt endpoint of a device, a packet at most: an endpoint the
// device halted is cleared.
//

static LONG
UsbInterrupt(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint,
    IN ULONG Maximum,
    IN OUT PUCHAR Toggle,
    OUT PUCHAR Buffer,
    IN ULONG Length
    )
{
    LONG Status;

    if (Host->Xhci) {
        Status = XhciInterrupt(Host, Device, Endpoint, Buffer, Length);
    } else {
        Status = UsbTransfer(Host, Device, Endpoint, EPTYPE_INTERRUPT, TRUE, Maximum, Toggle,
                             Buffer, Length < Maximum ? Length : Maximum, PERIODIC_TIMEOUT_MS);
    }

    if (Status == XFER_STALL) {
        UsbControl(Host, Device, 0x02, REQUEST_CLEAR_FEATURE, 0, 0x80 | Endpoint, NULL, 0);
        *Toggle = PID_DATA0;
    }

    return Status;
}

//
// The devices: the device on a port of a hub (USB_ROOT and port 0 for that
// on the root port), and the removal of a device, the devices behind it if
// it is a hub, and its keyboards and pointers.
//

static ULONG
UsbFindDevice(
    IN PUSB_HOST Host,
    IN ULONG Parent,
    IN ULONG Port
    )
{
    ULONG Index;

    for (Index = 0; Index < USB_MAXIMUM_DEVICES; Index += 1) {
        if (Host->Devices[Index].Present && Host->Devices[Index].Parent == Parent &&
            Host->Devices[Index].Port == Port) {
            return Index;
        }
    }

    return USB_NONE;
}

static VOID
UsbRemoveDevice(
    IN PUSB_HOST Host,
    IN ULONG Index
    )
{
    ULONG Child;

    for (Child = 0; Child < USB_MAXIMUM_DEVICES; Child += 1) {
        if (Host->Devices[Child].Present && Host->Devices[Child].Parent == Index) {
            UsbRemoveDevice(Host, Child);
        }
    }

    for (Child = 0; Child < USB_MAXIMUM_HIDS; Child += 1) {
        if (Host->Hids[Child].Present && Host->Hids[Child].Device == Index) {
            Host->Hids[Child].Present = FALSE;
        }
    }

    if (Host->Xhci) {
        XhciRemoveDevice(Host, &Host->Devices[Index]);
    }

    Host->Devices[Index].Present = FALSE;
}

//
// The report descriptor of a HID interface: the fields of the keyboard and
// of the pointer in its input reports, and the indicators in the output
// report of the keyboard.
//

typedef struct _HID_GLOBALS {
    ULONG UsagePage;
    LONG Minimum;
    ULONG Maximum;
    ULONG MaximumSize;
    ULONG Size;
    ULONG Count;
    ULONG Id;
} HID_GLOBALS, *PHID_GLOBALS;

typedef struct _HID_LOCALS {
    ULONG Usages[HID_USAGES];
    ULONG Count;
    ULONG Minimum;
} HID_LOCALS, *PHID_LOCALS;

typedef struct _HID_OFFSETS {
    UCHAR Id[HID_REPORTS];
    USHORT Bits[HID_REPORTS];
    ULONG Count;
} HID_OFFSETS, *PHID_OFFSETS;

static PUSHORT
UsbReportBits(
    IN PHID_OFFSETS Offsets,
    IN ULONG Id
    )
{
    ULONG Index;

    for (Index = 0; Index < Offsets->Count; Index += 1) {
        if (Offsets->Id[Index] == Id) {
            return &Offsets->Bits[Index];
        }
    }

    if (Offsets->Count < HID_REPORTS) {
        Offsets->Count += 1;
    }

    Offsets->Id[Offsets->Count - 1] = (UCHAR)Id;
    Offsets->Bits[Offsets->Count - 1] = 0;
    return &Offsets->Bits[Offsets->Count - 1];
}

static VOID
UsbSetField(
    OUT PHID_FIELD Field,
    IN PHID_GLOBALS Global,
    IN ULONG Offset,
    IN ULONG Count,
    IN ULONG Usage
    )
{
    Field->Offset = (USHORT)Offset;
    Field->Size = (UCHAR)Global->Size;
    Field->Count = (UCHAR)(Count < 255 ? Count : 255);
    Field->Minimum = Global->Minimum;
    Field->Maximum = (LONG)Global->Maximum;
    if (Global->Minimum < 0 && Global->MaximumSize < 4) {
        Field->Maximum = Global->MaximumSize == 1 ? (LONG)(CHAR)Global->Maximum : (LONG)(SHORT)Global->Maximum;
    }

    Field->Usage = Usage & 0xFFFF;
}

static VOID
UsbParseInput(
    IN OUT PUSB_HID Hid,
    IN ULONG Application,
    IN PHID_GLOBALS Global,
    IN PHID_LOCALS Local,
    IN ULONG Offset,
    IN ULONG Flags
    )
{
    BOOLEAN Variable = (Flags & 2) != 0;
    ULONG First = Local->Count != 0 ? Local->Usages[0] : Local->Minimum;
    ULONG Index;
    ULONG Usage;
    PHID_FIELD Field;

    if (Application == USAGE_KEYBOARD || Application == USAGE_KEYPAD) {
        if ((First >> 16) != PAGE_KEYBOARD ||
            ((Hid->Kinds & HID_KEYBOARD) && Hid->KeyboardId != Global->Id)) {
            return;
        }

        if (!Variable) {
            Field = &Hid->KeyArray;
        } else if (Global->Size != 1) {
            return;
        } else {
            Field = (First & 0xFFFF) >= USAGE_FIRST_MODIFIER ? &Hid->Modifiers : &Hid->KeyBitmap;
        }

        if (Field->Count == 0) {
            UsbSetField(Field, Global, Offset, Global->Count, First);
        }

        Hid->KeyboardId = (UCHAR)Global->Id;
        Hid->Kinds |= HID_KEYBOARD;
        return;
    }

    if ((Application != USAGE_MOUSE && Application != USAGE_POINTER) || !Variable ||
        (Hid->Buttons.Count + Hid->X.Size + Hid->Y.Size != 0 && Hid->PointerId != Global->Id)) {
        return;
    }

    if ((First >> 16) == PAGE_BUTTON) {
        if (Hid->Buttons.Count == 0 && Global->Size == 1) {
            UsbSetField(&Hid->Buttons, Global, Offset, Global->Count, First);
            Hid->PointerId = (UCHAR)Global->Id;
        }

        return;
    }

    for (Index = 0; Index < Global->Count; Index += 1) {
        Usage = Index < Local->Count ? Local->Usages[Index] :
                Local->Count != 0 ? Local->Usages[Local->Count - 1] : Local->Minimum + Index;
        Field = Usage == USAGE_X ? &Hid->X : Usage == USAGE_Y ? &Hid->Y : NULL;
        if (Field == NULL || Field->Size != 0 || Global->Size > 32) {
            continue;
        }

        UsbSetField(Field, Global, Offset + Index * Global->Size, 1, Usage);
        Hid->Absolute = (Flags & 4) == 0;
        Hid->PointerId = (UCHAR)Global->Id;
    }

    if (Hid->X.Size != 0 && Hid->Y.Size != 0) {
        Hid->Kinds |= HID_POINTER;
    }
}

static VOID
UsbParseReport(
    IN OUT PUSB_HID Hid,
    IN PUCHAR Descriptor,
    IN ULONG Length
    )
{
    HID_GLOBALS Global;
    HID_GLOBALS Saved;
    HID_LOCALS Local;
    HID_OFFSETS Input;
    HID_OFFSETS Output;
    ULONG Application = 0;
    ULONG Depth = 0;
    ULONG Offset = 0;
    ULONG Prefix;
    ULONG Size;
    ULONG Value;
    ULONG Index;
    ULONG Usage;
    PUSHORT Bits;

    RtlZeroMemory(&Global, sizeof(Global));
    RtlZeroMemory(&Saved, sizeof(Saved));
    RtlZeroMemory(&Local, sizeof(Local));
    RtlZeroMemory(&Input, sizeof(Input));
    RtlZeroMemory(&Output, sizeof(Output));
    while (Offset < Length) {
        Prefix = Descriptor[Offset];
        if (Prefix == 0xFE) {
            Offset += Offset + 1 < Length ? 3 + Descriptor[Offset + 1] : Length;
            continue;
        }

        Size = (Prefix & 3) == 3 ? 4 : Prefix & 3;
        if (Offset + 1 + Size > Length) {
            break;
        }

        Value = 0;
        for (Index = 0; Index < Size; Index += 1) {
            Value |= (ULONG)Descriptor[Offset + 1 + Index] << (8 * Index);
        }

        Offset += 1 + Size;
        Usage = Size == 4 ? Value : (Global.UsagePage << 16) | Value;
        switch (Prefix & 0xFC) {
        case 0x04:
            Global.UsagePage = Value;
            break;

        case 0x14:
            Global.Minimum = Size == 1 ? (LONG)(CHAR)Value : Size == 2 ? (LONG)(SHORT)Value : (LONG)Value;
            break;

        case 0x24:
            Global.Maximum = Value;
            Global.MaximumSize = Size;
            break;

        case 0x74:
            Global.Size = Value;
            break;

        case 0x84:
            Global.Id = Value;
            Hid->ReportIds = TRUE;
            break;

        case 0x94:
            Global.Count = Value;
            break;

        case 0xA4:
            Saved = Global;
            break;

        case 0xB4:
            Global = Saved;
            break;

        case 0x08:
            if (Local.Count < HID_USAGES) {
                Local.Usages[Local.Count++] = Usage;
            }

            break;

        case 0x18:
            Local.Minimum = Usage;
            break;

        case 0xA0:
            if (Value == 1) {
                Application = Local.Count != 0 ? Local.Usages[0] : 0;
            }

            Depth += 1;
            RtlZeroMemory(&Local, sizeof(Local));
            break;

        case 0xC0:
            if (Depth != 0 && --Depth == 0) {
                Application = 0;
            }

            RtlZeroMemory(&Local, sizeof(Local));
            break;

        case 0x80:
            Bits = UsbReportBits(&Input, Global.Id);
            if ((Value & 1) == 0 && Global.Size != 0 && Global.Count != 0) {
                UsbParseInput(Hid, Application, &Global, &Local, *Bits, Value);
            }

            *Bits += (USHORT)(Global.Size * Global.Count);
            RtlZeroMemory(&Local, sizeof(Local));
            break;

        case 0x90:
            Bits = UsbReportBits(&Output, Global.Id);
            if ((Value & 3) == 2 && Global.Size == 1 && Hid->LedLength == 0 &&
                (Local.Count != 0 ? Local.Usages[0] : Local.Minimum) == USAGE_NUM_LOCK) {
                Hid->LedId = (UCHAR)Global.Id;
                Hid->LedOffset = *Bits;
                Hid->LedLength = 1;
            }

            *Bits += (USHORT)(Global.Size * Global.Count);
            RtlZeroMemory(&Local, sizeof(Local));
            break;

        case 0xB0:
            RtlZeroMemory(&Local, sizeof(Local));
            break;
        }
    }

    if (Hid->LedLength != 0) {
        Hid->LedLength = (UCHAR)((*UsbReportBits(&Output, Hid->LedId) + 7) / 8);
    }
}

//
// The layouts of the reports of the boot protocol: a keyboard's modifiers in
// byte 0 and six keys from byte 2, and its indicators in an output byte; a
// mouse's buttons in byte 0 and its motion in bytes 1 and 2.
//

static VOID
UsbBootLayout(
    IN OUT PUSB_HID Hid,
    IN ULONG Protocol
    )
{
    HID_GLOBALS Global;

    RtlZeroMemory(&Hid->KeyboardId, sizeof(USB_HID) - FIELD_OFFSET(USB_HID, KeyboardId));
    RtlZeroMemory(&Global, sizeof(Global));
    Hid->Boot = TRUE;
    Hid->ReportIds = FALSE;
    Global.Size = 1;
    Global.Maximum = 1;
    if (Protocol == PROTOCOL_KEYBOARD) {
        Hid->Kinds = HID_KEYBOARD;
        UsbSetField(&Hid->Modifiers, &Global, 0, 8, USAGE_FIRST_MODIFIER);
        Global.Size = 8;
        Global.Maximum = 255;
        UsbSetField(&Hid->KeyArray, &Global, 16, 6, 0);
        Hid->LedLength = 1;
        return;
    }

    Hid->Kinds = HID_POINTER;
    UsbSetField(&Hid->Buttons, &Global, 0, 3, 1);
    Global.Size = 8;
    Global.Minimum = -127;
    Global.Maximum = 127;
    UsbSetField(&Hid->X, &Global, 8, 1, USAGE_X);
    UsbSetField(&Hid->Y, &Global, 16, 1, USAGE_Y);
}

//
// Start a HID interface of a configured device: have it report only changes,
// read its report descriptor, put it in the boot protocol if it has one, and
// keep it if it gives a keyboard or a pointer.
//

static VOID
UsbStartHid(
    IN PUSB_HOST Host,
    IN ULONG DeviceIndex,
    IN PHID_INTERFACE Interface
    )
{
    PUSB_DEVICE Device = &Host->Devices[DeviceIndex];
    PUSB_HID Hid = NULL;
    ULONG Index;
    ULONG Length;
    UCHAR Generation;
    LONG Actual;

    for (Index = 0; Index < USB_MAXIMUM_HIDS && Hid == NULL; Index += 1) {
        if (!Host->Hids[Index].Present) {
            Hid = &Host->Hids[Index];
        }
    }

    if (Hid == NULL || Interface->Endpoint == 0 || Interface->Maximum == 0) {
        return;
    }

    Generation = Hid->Generation;
    RtlZeroMemory(Hid, sizeof(USB_HID));
    Hid->Generation = Generation == 255 ? 1 : Generation + 1;
    Hid->Device = (UCHAR)DeviceIndex;
    Hid->Interface = Interface->Interface;
    Hid->Endpoint = Interface->Endpoint;
    Hid->EndpointMaximum = Interface->Maximum;
    Hid->Toggle = PID_DATA0;

    UsbControl(Host, Device, 0x21, REQUEST_SET_IDLE, 0, Interface->Interface, NULL, 0);
    Length = Interface->ReportLength < sizeof(Host->Descriptor) ? Interface->ReportLength : sizeof(Host->Descriptor);
    if (Length != 0) {
        Actual = UsbControl(Host, Device, 0x81, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_REPORT << 8,
                            Interface->Interface, Host->Descriptor, Length);
        if (Actual > 0) {
            UsbParseReport(Hid, Host->Descriptor, (ULONG)Actual);
        }
    }

    if (Interface->Subclass == SUBCLASS_BOOT &&
        (Interface->Protocol == PROTOCOL_KEYBOARD || Interface->Protocol == PROTOCOL_MOUSE) &&
        UsbControl(Host, Device, 0x21, REQUEST_SET_PROTOCOL, 0, Interface->Interface, NULL, 0) >= 0) {
        UsbBootLayout(Hid, Interface->Protocol);
    }

    if (Hid->Kinds == 0) {
        return;
    }

    Device->Kinds |= Hid->Kinds;
    Hid->Present = TRUE;
}

//
// The ports of a hub, or the root ports of an xHCI (USB_ROOT): a feature of
// one set or cleared, and its status read and its changes acknowledged.
//

static VOID
UsbPortFeature(
    IN PUSB_HOST Host,
    IN ULONG HubIndex,
    IN ULONG Port,
    IN BOOLEAN Set,
    IN ULONG Feature
    )
{
    if (HubIndex == USB_ROOT) {
        XhciPortFeature(Host, Port, Set, Feature);
        return;
    }

    UsbControl(Host, &Host->Devices[HubIndex], 0x23, Set ? REQUEST_SET_FEATURE : REQUEST_CLEAR_FEATURE,
               Feature, Port, NULL, 0);
}

static BOOLEAN
UsbPortStatus(
    IN PUSB_HOST Host,
    IN ULONG HubIndex,
    IN ULONG Port,
    OUT PULONG Status,
    OUT PULONG Change
    )
{
    UCHAR Data[4];
    ULONG Feature;

    if (HubIndex == USB_ROOT) {
        return XhciPortStatus(Host, Port, Status, Change);
    }

    if (UsbControl(Host, &Host->Devices[HubIndex], 0xA3, REQUEST_GET_STATUS, 0, Port, Data, 4) < 4) {
        return FALSE;
    }

    *Status = Data[0] | (Data[1] << 8);
    *Change = Data[2] | (Data[3] << 8);
    for (Feature = 0; Feature < 5; Feature += 1) {
        if (*Change & (1 << Feature)) {
            UsbPortFeature(Host, HubIndex, Port, FALSE, C_PORT_CONNECTION + Feature);
        }
    }

    return TRUE;
}

//
// A hub: its descriptor, the power of its ports, and the devices on them.
//

static VOID
UsbCheckPort(
    IN PUSB_HOST Host,
    IN ULONG HubIndex,
    IN ULONG Port
    );

static BOOLEAN
UsbStartHub(
    IN PUSB_HOST Host,
    IN ULONG Index
    )
{
    PUSB_DEVICE Hub = &Host->Devices[Index];
    UCHAR Data[16];
    ULONG Port;

    if (UsbControl(Host, Hub, 0xA0, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_HUB << 8, 0, Data, sizeof(Data)) < 7) {
        return FALSE;
    }

    Hub->Ports = Data[2] < 15 ? Data[2] : 15;
    if (Host->Xhci && !XhciConfigureHub(Host, Hub, (Data[3] >> 5) & 3)) {
        return FALSE;
    }

    for (Port = 1; Port <= Hub->Ports; Port += 1) {
        UsbPortFeature(Host, Index, Port, TRUE, PORT_POWER);
    }

    UsbSleep(Data[5] * 2 + 100);
    Hub->Stage = USB_STAGE_READY;
    for (Port = 1; Port <= Hub->Ports; Port += 1) {
        UsbCheckPort(Host, Index, Port);
    }

    return TRUE;
}

//
// The configuration of a device: its hub interface and status change
// endpoint, or its HID interfaces with their report descriptor lengths and
// interrupt endpoints (those of their first alternate settings).
//

static ULONG
UsbParseConfiguration(
    IN OUT PUSB_DEVICE Device,
    IN PUCHAR Data,
    IN ULONG Length,
    OUT PHID_INTERFACE Hids
    )
{
    PHID_INTERFACE Current = NULL;
    ULONG Count = 0;
    ULONG Offset;
    ULONG Index;
    PUCHAR Item;

    for (Offset = 0; Offset + 2 <= Length && Data[Offset] >= 2 && Offset + Data[Offset] <= Length;
         Offset += Data[Offset]) {
        Item = Data + Offset;
        if (Item[1] == DESCRIPTOR_INTERFACE && Item[0] >= 9) {
            Current = NULL;
            if (Item[3] != 0) {
                continue;
            }

            if (Item[5] == CLASS_HUB) {
                Device->Class = CLASS_HUB;
            } else if (Item[5] == CLASS_HID && Count < DEVICE_HIDS) {
                Current = &Hids[Count++];
                RtlZeroMemory(Current, sizeof(HID_INTERFACE));
                Current->Interface = Item[2];
                Current->Subclass = Item[6];
                Current->Protocol = Item[7];
            }

            continue;
        }

        if (Item[1] == DESCRIPTOR_HID && Current != NULL) {
            for (Index = 6; Index + 3 <= Item[0]; Index += 3) {
                if (Item[Index] == DESCRIPTOR_REPORT) {
                    Current->ReportLength = Item[Index + 1] | (Item[Index + 2] << 8);
                }
            }

            continue;
        }

        if (Item[1] != DESCRIPTOR_ENDPOINT || Item[0] < 7 || (Item[2] & 0x80) == 0 ||
            (Item[3] & 3) != EPTYPE_INTERRUPT) {
            continue;
        }

        if (Device->Class == CLASS_HUB && Device->HubEndpoint == 0) {
            Device->HubEndpoint = Item[2] & 0xF;
            Device->HubMaximum = (Item[4] | (Item[5] << 8)) & 0x7FF;
            Device->HubInterval = Item[6];
        } else if (Current != NULL && Current->Endpoint == 0) {
            Current->Endpoint = Item[2] & 0xF;
            Current->Maximum = (Item[4] | (Item[5] << 8)) & 0x7FF;
            Current->Interval = Item[6];
        }
    }

    return Count;
}

//
// Configure the endpoints of a configuration of a device on an xHCI before it
// is set: the status change endpoint of a hub, or the interrupt endpoints of
// the HID interfaces.
//

static BOOLEAN
UsbConfigureEndpoints(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN PHID_INTERFACE Hids,
    IN ULONG Count
    )
{
    ULONG Index;

    if (Device->Class == CLASS_HUB) {
        return Device->HubEndpoint != 0 &&
               XhciConfigureEndpoint(Host, Device, Device->HubEndpoint, Device->HubMaximum, Device->HubInterval);
    }

    for (Index = 0; Index < Count; Index += 1) {
        if (Hids[Index].Endpoint != 0 &&
            !XhciConfigureEndpoint(Host, Device, Hids[Index].Endpoint, Hids[Index].Maximum, Hids[Index].Interval)) {
            return FALSE;
        }
    }

    return TRUE;
}

//
// Configure the device on a port the hub (or the root port) reset, which
// answers at address 0 until it is given its own (on an xHCI, by the
// controller, before its descriptor is read): a hub is configured and its
// ports enumerated, a device with keyboards or pointers is configured and
// they are started, another device (and a super speed hub, which has only
// super speed devices behind it) is left unconfigured. The stage of the
// device is the one it failed at.
//

static BOOLEAN
UsbConfigure(
    IN PUSB_HOST Host,
    IN ULONG Index
    )
{
    PUSB_DEVICE Device = &Host->Devices[Index];
    PUCHAR Data = Host->Descriptor;
    HID_INTERFACE Hids[DEVICE_HIDS];
    ULONG Count;
    ULONG Configuration;
    ULONG Total;
    LONG Length;

    Device->Stage = USB_STAGE_ADDRESS;
    if (Host->Xhci && !XhciAddressDevice(Host, Index)) {
        return FALSE;
    }

    Device->Stage = USB_STAGE_DESCRIPTOR;
    if (UsbControl(Host, Device, 0x80, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_DEVICE << 8, 0, Data, 8) < 8) {
        return FALSE;
    }

    if (Data[7] == 8 || Data[7] == 16 || Data[7] == 32 || Data[7] == 64) {
        Device->ControlMaximum = Data[7];
    } else if (Data[7] == 9 && Device->Speed == SPEED_SUPER) {
        Device->ControlMaximum = 512;
    }

    if (Host->Xhci) {
        if (!XhciSetControlMaximum(Host, Device)) {
            return FALSE;
        }
    } else {
        Device->Stage = USB_STAGE_ADDRESS;
        if (UsbControl(Host, Device, 0x00, REQUEST_SET_ADDRESS, Index + 1, 0, NULL, 0) < 0) {
            return FALSE;
        }

        Device->Address = (UCHAR)(Index + 1);
        UsbSleep(10);
    }

    Device->Stage = USB_STAGE_DEVICE;
    if (UsbControl(Host, Device, 0x80, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_DEVICE << 8, 0, Data, 18) < 18) {
        return FALSE;
    }

    Device->Class = Data[4];
    Device->Vendor = Data[8] | (Data[9] << 8);
    Device->Product = Data[10] | (Data[11] << 8);
    Device->Stage = USB_STAGE_CONFIGURATION;
    if (UsbControl(Host, Device, 0x80, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_CONFIG << 8, 0, Data, 9) < 9) {
        return FALSE;
    }

    Total = Data[2] | (Data[3] << 8);
    Configuration = Data[5];
    Length = UsbControl(Host, Device, 0x80, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_CONFIG << 8, 0, Data,
                        Total < sizeof(Host->Descriptor) ? Total : sizeof(Host->Descriptor));
    if (Length < 9) {
        return FALSE;
    }

    Count = UsbParseConfiguration(Device, Data, (ULONG)Length, Hids);
    if (Device->Speed == SPEED_SUPER || (Device->Class != CLASS_HUB && Count == 0)) {
        Device->Stage = USB_STAGE_READY;
        return TRUE;
    }

    Device->Stage = USB_STAGE_SET;
    if (Host->Xhci && !UsbConfigureEndpoints(Host, Device, Hids, Count)) {
        return FALSE;
    }

    if (UsbControl(Host, Device, 0x00, REQUEST_SET_CONFIG, Configuration, 0, NULL, 0) < 0) {
        return FALSE;
    }

    if (Device->Class == CLASS_HUB) {
        Device->Stage = USB_STAGE_HUB;
        Device->HubToggle = PID_DATA0;
        return Device->Depth + 1 < USB_MAXIMUM_DEPTH && UsbStartHub(Host, Index);
    }

    while (Count != 0) {
        Count -= 1;
        UsbStartHid(Host, Index, &Hids[Count]);
    }

    Device->Stage = USB_STAGE_READY;
    return TRUE;
}

//
// Enumerate the device on a port the hub (or the root port) reset: it gets
// a slot (and its address) even if it fails, so that it is listed with the
// stage it failed at and the last channel interrupts; a device that did not
// take its address is disabled at its port.
//

static VOID
UsbEnumerate(
    IN PUSB_HOST Host,
    IN ULONG Parent,
    IN ULONG Port,
    IN ULONG Speed,
    IN BOOLEAN Enabled
    )
{
    PUSB_DEVICE Hub = Parent == USB_ROOT ? NULL : &Host->Devices[Parent];
    PUSB_DEVICE Device;
    ULONG Index;

    for (Index = 0; Index < USB_MAXIMUM_DEVICES && Host->Devices[Index].Present; Index += 1) {
    }

    if (Index == USB_MAXIMUM_DEVICES) {
        return;
    }

    Device = &Host->Devices[Index];
    RtlZeroMemory(Device, sizeof(USB_DEVICE));
    Device->Present = TRUE;
    Device->Speed = (UCHAR)Speed;
    Device->Parent = (UCHAR)Parent;
    Device->Port = (UCHAR)Port;
    Device->Depth = Hub != NULL ? Hub->Depth + 1 : 0;
    Device->ControlMaximum = 8;
    Device->Stage = USB_STAGE_RESET;
    if (!Host->Xhci && Hub != NULL && Speed != SPEED_HIGH && Host->RootSpeed == SPEED_HIGH) {
        Device->TtAddress = Hub->Speed == SPEED_HIGH ? Hub->Address : Hub->TtAddress;
        Device->TtPort = Hub->Speed == SPEED_HIGH ? (UCHAR)Port : Hub->TtPort;
    }

    if (!Enabled || UsbConfigure(Host, Index)) {
        return;
    }

    Device->Interrupt = Host->Interrupt;
    if (Device->Address == 0 && (Hub != NULL || Host->Xhci)) {
        UsbPortFeature(Host, Parent, Port, FALSE, PORT_ENABLE);
    }
}

//
// Look at a port of a hub (or a root port of an xHCI): acknowledge its
// changes, remove the device that was on it if it went (or came again), and
// reset and enumerate the device connected to it if there is none yet. A
// device that fails is tried again from a reset of its port, with the power
// of the port cycled halfway, as Linux does; one that fails every try stays
// listed with the step it failed at.
//

static VOID
UsbCheckPort(
    IN PUSB_HOST Host,
    IN ULONG HubIndex,
    IN ULONG Port
    )
{
    ULONG Child = UsbFindDevice(Host, HubIndex, Port);
    ULONG Status;
    ULONG Change;
    ULONG Time;
    ULONG Try;

    if (!UsbPortStatus(Host, HubIndex, Port, &Status, &Change)) {
        return;
    }

    if (Child != USB_NONE && ((Status & PORT_STATUS_CONNECTION) == 0 || (Change & PORT_CHANGE_CONNECTION))) {
        UsbRemoveDevice(Host, Child);
        Child = USB_NONE;
    }

    if (Child != USB_NONE || (Status & PORT_STATUS_CONNECTION) == 0) {
        return;
    }

    for (Try = 1; ; Try += 1) {
        UsbSleep(100);
        UsbPortFeature(Host, HubIndex, Port, TRUE, PORT_RESET);
        for (Time = 0; Time < 500; Time += 10) {
            UsbSleep(10);
            if (!UsbPortStatus(Host, HubIndex, Port, &Status, &Change)) {
                return;
            }

            if ((Status & PORT_STATUS_RESET) == 0 || (Change & PORT_CHANGE_RESET)) {
                break;
            }
        }

        UsbPortFeature(Host, HubIndex, Port, FALSE, C_PORT_CONNECTION + 4);
        UsbSleep(10);
        UsbEnumerate(Host, HubIndex, Port,
                     (Status & PORT_STATUS_LOW_SPEED) ? SPEED_LOW :
                     (Status & PORT_STATUS_HIGH_SPEED) ? SPEED_HIGH :
                     (Status & PORT_STATUS_SUPER_SPEED) ? SPEED_SUPER : SPEED_FULL,
                     (Status & PORT_STATUS_ENABLE) != 0);
        Child = UsbFindDevice(Host, HubIndex, Port);
        if (Child == USB_NONE || Host->Devices[Child].Stage == USB_STAGE_READY || Try == ENUMERATION_TRIES) {
            return;
        }

        UsbRemoveDevice(Host, Child);
        if (Try == ENUMERATION_TRIES / 2) {
            UsbPortFeature(Host, HubIndex, Port, FALSE, PORT_POWER);
            UsbSleep(POWER_CYCLE_MS);
            UsbPortFeature(Host, HubIndex, Port, TRUE, PORT_POWER);
            UsbSleep(POWER_CYCLE_MS);
        }

        if (!UsbPortStatus(Host, HubIndex, Port, &Status, &Change) || (Status & PORT_STATUS_CONNECTION) == 0) {
            return;
        }
    }
}

//
// The frame interval of the enabled root port, in clocks of the UTMI+ PHY:
// a microframe of 125 us at high speed, else a frame of 1 ms, less one (the
// remaining time counts down to zero). The frames in a millisecond are
// counted before and after.
//

static ULONG
UsbFramesInMillisecond(
    IN PUSB_HOST Host
    )
{
    ULONG Frame = READ_REG(Host, HFNUM);

    UsbStall(1000);
    return (READ_REG(Host, HFNUM) - Frame) & HFNUM_FRNUM_MASK;
}

static VOID
UsbSetFrameInterval(
    IN PUSB_HOST Host
    )
{
    ULONG Interval = READ_REG(Host, HFIR);

    Host->FrameInterval = (Host->RootSpeed == SPEED_HIGH ? 125 : 1000) * PHY_CLOCK_MHZ;
    Host->ResetFrameInterval = Interval & 0xFFFF;
    Host->ResetFrames = (UCHAR)UsbFramesInMillisecond(Host);
    WRITE_REG(Host, HFIR, (Interval & ~0xFFFF) | (Host->FrameInterval - 1));
    Host->Frames = (UCHAR)UsbFramesInMillisecond(Host);
}

//
// Look at the root port: remove the device on it if it went or the port was
// disabled, and reset and enumerate the device connected to it if there is
// none, trying a device that fails again from a reset of the port.
//

static VOID
UsbCheckRoot(
    IN PUSB_HOST Host
    )
{
    ULONG Port = READ_REG(Host, HPRT0);
    ULONG Root = UsbFindDevice(Host, USB_ROOT, 0);
    ULONG Try;

    if (Port & (HPRT0_CONNDET | HPRT0_ENACHG)) {
        WRITE_REG(Host, HPRT0, (Port & ~HPRT0_W1C_MASK) | (Port & (HPRT0_CONNDET | HPRT0_ENACHG)));
    }

    if (Root != USB_NONE &&
        ((Port & (HPRT0_CONNSTS | HPRT0_ENA)) != (HPRT0_CONNSTS | HPRT0_ENA) || (Port & HPRT0_CONNDET))) {
        UsbRemoveDevice(Host, Root);
        Root = USB_NONE;
    }

    if (Root != USB_NONE || (Port & HPRT0_CONNSTS) == 0) {
        return;
    }

    for (Try = 1; ; Try += 1) {
        WRITE_REG(Host, HPRT0, (READ_REG(Host, HPRT0) & ~HPRT0_W1C_MASK) | HPRT0_RST);
        UsbSleep(60);
        WRITE_REG(Host, HPRT0, READ_REG(Host, HPRT0) & ~(HPRT0_W1C_MASK | HPRT0_RST));
        UsbSleep(50);
        Port = READ_REG(Host, HPRT0);
        WRITE_REG(Host, HPRT0, (Port & ~HPRT0_W1C_MASK) | (Port & (HPRT0_CONNDET | HPRT0_ENACHG)));
        Host->RootSpeed = (UCHAR)((Port & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT);
        if (Port & HPRT0_ENA) {
            UsbSetFrameInterval(Host);
        }

        UsbEnumerate(Host, USB_ROOT, 0, Host->RootSpeed, (Port & HPRT0_ENA) != 0);
        Root = UsbFindDevice(Host, USB_ROOT, 0);
        if (Root == USB_NONE || Host->Devices[Root].Stage == USB_STAGE_READY || Try == ENUMERATION_TRIES ||
            (READ_REG(Host, HPRT0) & HPRT0_CONNSTS) == 0) {
            return;
        }

        UsbRemoveDevice(Host, Root);
    }
}

BOOLEAN
UsbHostInitialize(
    IN PUSB_HOST Host
    )

/*++

Routine Description:

    This routine brings up the host, which the user powered, then enumerates
    the devices behind its root ports. The core: it selects the high speed
    PHY of the chip, forces host mode, sets up DMA and the FIFOs and powers
    the root port. An xHCI is reset and run (xhci.c).

Arguments:

    Host - Supplies the host, with its registers and buffer, and an xHCI
        with its memory.

Return Value:

    TRUE if the host is there and works, else FALSE.

--*/

{
    ULONG Port;

    RtlZeroMemory(Host->Devices, sizeof(Host->Devices));
    RtlZeroMemory(Host->Hids, sizeof(Host->Hids));
    if (Host->Xhci) {
        if (!XhciInitialize(Host)) {
            return FALSE;
        }

        for (Port = 1; Port <= Host->Xhc.Ports; Port += 1) {
            UsbCheckPort(Host, USB_ROOT, Port);
        }

        return TRUE;
    }

    if ((READ_REG(Host, GSNPSID) & 0xFFFFF000) != 0x4F542000 || !UsbResetCore(Host)) {
        return FALSE;
    }

    WRITE_REG(Host, GUSBCFG, READ_REG(Host, GUSBCFG) &
              ~(GUSBCFG_PHYSEL | GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16));
    if (!UsbResetCore(Host)) {
        return FALSE;
    }

    WRITE_REG(Host, GUSBCFG, (READ_REG(Host, GUSBCFG) &
              ~(GUSBCFG_ULPI_FS_LS | GUSBCFG_ULPI_CLK_SUSP_M)) | GUSBCFG_FORCEHOSTMODE);
    if (!UsbWaitRegister(Host, GINTSTS, GINTSTS_CURMODE_HOST, TRUE, 200)) {
        return FALSE;
    }

    WRITE_REG(Host, GAHBCFG, GAHBCFG_DMA_EN | GAHBCFG_HBSTLEN_INCR4);
    WRITE_REG(Host, GINTMSK, 0);
    WRITE_REG(Host, PCGCCTL, 0);
    WRITE_REG(Host, HCFG, (READ_REG(Host, HCFG) & ~7) | HCFG_FSLSPCLKSEL_30_60);
    WRITE_REG(Host, GRXFSIZ, 516 + 16);
    WRITE_REG(Host, GNPTXFSIZ, (0x100 << 16) | (516 + 16));
    WRITE_REG(Host, HPTXFSIZ, (0x200 << 16) | (516 + 16 + 0x100));
    WRITE_REG(Host, GRSTCTL, GRSTCTL_TXFNUM_ALL | GRSTCTL_TXFFLSH);
    UsbWaitRegister(Host, GRSTCTL, GRSTCTL_TXFFLSH, FALSE, CONTROL_TIMEOUT_MS);
    WRITE_REG(Host, GRSTCTL, GRSTCTL_RXFFLSH);
    UsbWaitRegister(Host, GRSTCTL, GRSTCTL_RXFFLSH, FALSE, CONTROL_TIMEOUT_MS);

    WRITE_REG(Host, HPRT0, (READ_REG(Host, HPRT0) & ~HPRT0_W1C_MASK) | HPRT0_PWR);
    UsbSleep(50);
    UsbWaitRegister(Host, HPRT0, HPRT0_CONNSTS, TRUE, CONTROL_TIMEOUT_MS);
    UsbCheckRoot(Host);
    return TRUE;
}

VOID
UsbHostScan(
    IN PUSB_HOST Host
    )

/*++

Routine Description:

    This routine looks for devices that came or went: it looks at the root
    ports, and reads the status change endpoint of each hub, then looks at
    the ports it reports.

Arguments:

    Host - Supplies the host.

Return Value:

    None.

--*/

{
    PUSB_DEVICE Hub;
    UCHAR Change[4];
    ULONG Index;
    ULONG Port;
    LONG Length;

    if (Host->Xhci) {
        for (Port = 1; Port <= Host->Xhc.Ports; Port += 1) {
            UsbCheckPort(Host, USB_ROOT, Port);
        }
    } else {
        UsbCheckRoot(Host);
    }
    for (Index = 0; Index < USB_MAXIMUM_DEVICES; Index += 1) {
        Hub = &Host->Devices[Index];
        if (!Hub->Present || Hub->Ports == 0 || Hub->Stage != USB_STAGE_READY || Hub->HubEndpoint == 0) {
            continue;
        }

        Length = UsbInterrupt(Host, Hub, Hub->HubEndpoint, Hub->HubMaximum, &Hub->HubToggle,
                              Change, sizeof(Change));
        for (Port = 1; Port <= Hub->Ports && Length > 0; Port += 1) {
            if (Port / 8 < (ULONG)Length && (Change[Port / 8] & (1 << (Port & 7)))) {
                UsbCheckPort(Host, Index, Port);
            }
        }
    }
}

LONG
UsbHidRead(
    IN PUSB_HOST Host,
    IN PUSB_HID Hid,
    OUT PUCHAR Report
    )

/*++

Routine Description:

    This routine reads a report from the interrupt endpoint of a keyboard or
    pointer, and counts the outcome. After a read fails, the interface is
    read again only after a delay, which doubles up to 104 ms while the
    reads keep failing.

Arguments:

    Host - Supplies the host.

    Hid - Supplies the interface.

    Report - Receives the report, up to USB_REPORT_LENGTH bytes.

Return Value:

    The length of the report, 0 or a negative value if there is none.

--*/

{
    LONG Length;

    if (Hid->RetryDelay != 0 && (LONG)(UsbClock() - Hid->RetryTime) < 0) {
        return XFER_NAK;
    }

    Length = UsbInterrupt(Host, &Host->Devices[Hid->Device], Hid->Endpoint, Hid->EndpointMaximum,
                          &Hid->Toggle, Report, USB_REPORT_LENGTH);
    Hid->LastInterrupt = Host->Interrupt;
    if (Host->Interrupt & HCINT_TIMEOUT) {
        Hid->Late += 1;
    }

    if (Length >= 0) {
        Hid->Reports += 1;
    } else if (Length == XFER_NAK) {
        Hid->Naks += 1;
    } else {
        Hid->Failures += 1;
    }

    if (Length != XFER_ERROR) {
        Hid->RetryDelay = 0;
        return Length;
    }

    Hid->RetryDelay = Hid->RetryDelay == 0 ? RETRY_FIRST_MS :
                      Hid->RetryDelay * 2 < RETRY_LAST_MS ? Hid->RetryDelay * 2 : RETRY_LAST_MS;
    Hid->RetryTime = UsbClock() + Hid->RetryDelay * Host->ClockRate;
    return Length;
}

//
// The values of the fields of a report.
//

static ULONG
UsbBits(
    IN PUCHAR Report,
    IN ULONG Length,
    IN ULONG Offset,
    IN ULONG Size
    )
{
    ULONG Value = 0;
    ULONG Index;

    for (Index = 0; Index < Size && Index < 32; Index += 1) {
        if ((Offset + Index) / 8 < Length && (Report[(Offset + Index) / 8] & (1 << ((Offset + Index) & 7)))) {
            Value |= 1 << Index;
        }
    }

    return Value;
}

static LONG
UsbFieldValue(
    IN PHID_FIELD Field,
    IN PUCHAR Report,
    IN ULONG Length,
    IN ULONG Index
    )
{
    ULONG Value = UsbBits(Report, Length, Field->Offset + Index * Field->Size, Field->Size);

    if (Field->Minimum < 0 && Field->Size < 32 && (Value & (1 << (Field->Size - 1)))) {
        Value |= ~0u << Field->Size;
    }

    return (LONG)Value;
}

static BOOLEAN
UsbReportId(
    IN PUSB_HID Hid,
    IN ULONG Id,
    IN OUT PUCHAR *Report,
    IN OUT PULONG Length
    )
{
    if (!Hid->ReportIds) {
        return TRUE;
    }

    if (*Length == 0 || (*Report)[0] != Id) {
        return FALSE;
    }

    *Report += 1;
    *Length -= 1;
    return TRUE;
}

static VOID
UsbSetKey(
    OUT PUCHAR Keys,
    IN ULONG Usage
    )
{
    if (Usage >= USAGE_FIRST_KEY && Usage < USB_KEY_BYTES * 8) {
        Keys[Usage / 8] |= 1 << (Usage & 7);
    }
}

static BOOLEAN
UsbBitmapKeys(
    IN PHID_FIELD Field,
    IN PUCHAR Report,
    IN ULONG Length,
    IN OUT PUCHAR Keys
    )
{
    ULONG Index;

    for (Index = 0; Index < Field->Count; Index += 1) {
        if (!UsbBits(Report, Length, Field->Offset + Index, 1)) {
            continue;
        }

        if (Field->Usage + Index == USAGE_ERROR_ROLLOVER) {
            return FALSE;
        }

        UsbSetKey(Keys, Field->Usage + Index);
    }

    return TRUE;
}

BOOLEAN
UsbHidKeys(
    IN PUSB_HID Hid,
    IN PUCHAR Report,
    IN ULONG Length,
    OUT PUCHAR Keys
    )

/*++

Routine Description:

    This routine gives the keys a keyboard report holds down.

Arguments:

    Hid - Supplies the interface.

    Report - Supplies the report.

    Length - Supplies its length.

    Keys - Receives the keys down, a bit for each usage of the keyboard page.

Return Value:

    TRUE if the report is one of the keyboard with the state of its keys,
    else (another report, or too many keys down) FALSE.

--*/

{
    PHID_FIELD Field;
    ULONG Index;
    LONG Value;

    if (!UsbReportId(Hid, Hid->KeyboardId, &Report, &Length)) {
        return FALSE;
    }

    RtlZeroMemory(Keys, USB_KEY_BYTES);
    if (!UsbBitmapKeys(&Hid->Modifiers, Report, Length, Keys) ||
        !UsbBitmapKeys(&Hid->KeyBitmap, Report, Length, Keys)) {
        return FALSE;
    }

    Field = &Hid->KeyArray;
    for (Index = 0; Index < Field->Count; Index += 1) {
        Value = UsbFieldValue(Field, Report, Length, Index);
        if (Value < Field->Minimum || Value > Field->Maximum) {
            continue;
        }

        if ((ULONG)(Value - Field->Minimum) + Field->Usage == USAGE_ERROR_ROLLOVER) {
            return FALSE;
        }

        UsbSetKey(Keys, (ULONG)(Value - Field->Minimum) + Field->Usage);
    }

    return TRUE;
}

static LONG
UsbScale(
    IN PHID_FIELD Field,
    IN LONG Value
    )
{
    ULONG Range = (ULONG)(Field->Maximum - Field->Minimum);
    ULONG Position;

    if (Value < Field->Minimum || Field->Maximum <= Field->Minimum) {
        return 0;
    }

    Position = Value > Field->Maximum ? Range : (ULONG)(Value - Field->Minimum);
    while (Range > 0xFFFF) {
        Range >>= 1;
        Position >>= 1;
    }

    return (LONG)(Position * 0xFFFF / Range);
}

BOOLEAN
UsbHidPointer(
    IN PUSB_HID Hid,
    IN PUCHAR Report,
    IN ULONG Length,
    OUT PUSB_POINTER Pointer
    )

/*++

Routine Description:

    This routine gives the state of a pointer from its report.

Arguments:

    Hid - Supplies the interface.

    Report - Supplies the report.

    Length - Supplies its length.

    Pointer - Receives the buttons down (bit 0 the first) and the motion,
        or the position of an absolute pointer scaled to 0 - 0xFFFF.

Return Value:

    TRUE if the report is one of the pointer, else FALSE.

--*/

{
    if (!UsbReportId(Hid, Hid->PointerId, &Report, &Length)) {
        return FALSE;
    }

    Pointer->Buttons = UsbBits(Report, Length, Hid->Buttons.Offset, Hid->Buttons.Count);
    Pointer->X = UsbFieldValue(&Hid->X, Report, Length, 0);
    Pointer->Y = UsbFieldValue(&Hid->Y, Report, Length, 0);
    Pointer->Absolute = Hid->Absolute;
    if (Hid->Absolute) {
        Pointer->X = UsbScale(&Hid->X, Pointer->X);
        Pointer->Y = UsbScale(&Hid->Y, Pointer->Y);
    }

    return TRUE;
}

VOID
UsbHidSetIndicators(
    IN PUSB_HOST Host,
    IN PUSB_HID Hid,
    IN ULONG Leds
    )

/*++

Routine Description:

    This routine sets the indicators of a keyboard with its output report.

Arguments:

    Host - Supplies the host.

    Hid - Supplies the interface.

    Leds - Supplies the indicators: bit 0 num lock, bit 1 caps lock and bit 2
        scroll lock.

Return Value:

    None.

--*/

{
    UCHAR Report[8];
    ULONG Offset = Hid->ReportIds ? 8 : 0;
    ULONG Index;

    if ((Hid->Kinds & HID_KEYBOARD) == 0 || Hid->LedLength == 0 || Hid->LedLength >= sizeof(Report)) {
        return;
    }

    RtlZeroMemory(Report, sizeof(Report));
    Report[0] = Hid->ReportIds ? Hid->LedId : 0;
    for (Index = 0; Index < 3; Index += 1) {
        if (Leds & (1 << Index)) {
            Report[(Offset + Hid->LedOffset + Index) / 8] |= 1 << ((Offset + Hid->LedOffset + Index) & 7);
        }
    }

    UsbControl(Host, &Host->Devices[Hid->Device], 0x21, REQUEST_SET_REPORT, 0x200 | Hid->LedId,
               Hid->Interface, Report, Hid->LedLength + Offset / 8);
}

#ifndef USB_FIRMWARE

//
// The function drivers of the devices the host leaves unconfigured: control
// transfers on channel 0, and pipes, each a bulk endpoint on a channel of its
// own that interrupts when it halts. The core repeats a transaction the device
// does not acknowledge, so an IN pipe waits for data without the processor.
//

#define CHANNEL(Offset, Channel) ((Offset) + (Channel) * 0x20)

LONG
UsbDeviceControl(
    IN PUSB_HOST Host,
    IN ULONG Index,
    IN ULONG RequestType,
    IN ULONG Request,
    IN ULONG Value,
    IN ULONG Interface,
    IN OUT PVOID Data,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine runs a control transfer to a device.

Arguments:

    Host - Supplies the host.

    Index - Supplies the index of the device.

    RequestType, Request, Value, Interface, Length - Supply the setup packet.

    Data - Supplies the data of the transfer, or receives it.

Return Value:

    The length of the data, or a negative XFER_ status.

--*/

{
    if (Index >= USB_MAXIMUM_DEVICES || !Host->Devices[Index].Present) {
        return XFER_ERROR;
    }

    return UsbControl(Host, &Host->Devices[Index], RequestType, Request, Value, Interface, Data, Length);
}

BOOLEAN
UsbOpenPipe(
    IN PUSB_HOST Host,
    IN ULONG Index,
    IN ULONG Endpoint,
    IN BOOLEAN In,
    IN ULONG Maximum,
    OUT PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine gives a bulk endpoint of a device a free channel and lets
    the channel interrupt when it halts. A device reached through a
    transaction translator, or behind an xHCI, has no pipes.

Arguments:

    Host - Supplies the host.

    Index - Supplies the index of the device.

    Endpoint - Supplies the endpoint number.

    In - Supplies TRUE for an IN endpoint.

    Maximum - Supplies the maximum packet size of the endpoint.

    Pipe - Receives the pipe.

Return Value:

    TRUE if the pipe was opened, else FALSE.

--*/

{
    PUSB_DEVICE Device = &Host->Devices[Index];
    ULONG Channels;
    ULONG Channel;

    if (Host->Xhci || Index >= USB_MAXIMUM_DEVICES || !Device->Present || Device->TtAddress != 0 ||
        Maximum == 0 || Maximum > 0x7FF) {
        return FALSE;
    }

    Channels = ((READ_REG(Host, GHWCFG2) >> GHWCFG2_CHANNELS_SHIFT) & 0xF) + 1;
    for (Channel = 1; Channel < Channels && (Host->Channels & (1 << Channel)); Channel += 1) {
    }

    if (Channel >= Channels) {
        return FALSE;
    }

    Host->Channels |= 1 << Channel;
    RtlZeroMemory(Pipe, sizeof(USB_PIPE));
    Pipe->Device = (UCHAR)Index;
    Pipe->Channel = (UCHAR)Channel;
    Pipe->Endpoint = (UCHAR)Endpoint;
    Pipe->In = In;
    Pipe->Maximum = (USHORT)Maximum;
    Pipe->Pid = PID_DATA0;
    Pipe->Character = ((ULONG)Device->Address << HCCHAR_DEVADDR_SHIFT) | (Endpoint << HCCHAR_EPNUM_SHIFT) |
                      (EPTYPE_BULK << HCCHAR_EPTYPE_SHIFT) | (In ? HCCHAR_EPDIR_IN : 0) | Maximum |
                      HCCHAR_MULTICNT_ONE;
    WRITE_REG(Host, CHANNEL(HCINTMSK, Channel), HCINT_CHHLTD);
    WRITE_REG(Host, CHANNEL(HCINT, Channel), HCINT_ALL);
    WRITE_REG(Host, HAINTMSK, READ_REG(Host, HAINTMSK) | (1 << Channel));
    WRITE_REG(Host, GINTMSK, READ_REG(Host, GINTMSK) | GINTSTS_HCHINT);
    WRITE_REG(Host, GAHBCFG, READ_REG(Host, GAHBCFG) | GAHBCFG_GLBL_INTR_EN);
    return TRUE;
}

VOID
UsbClosePipe(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine halts the channel of a pipe and frees it.

Arguments:

    Host - Supplies the host.

    Pipe - Supplies the pipe.

Return Value:

    None.

--*/

{
    ULONG Channel = Pipe->Channel;
    ULONG Start = UsbClock();

    WRITE_REG(Host, HAINTMSK, READ_REG(Host, HAINTMSK) & ~(1 << Channel));
    if (READ_REG(Host, CHANNEL(HCCHAR, Channel)) & HCCHAR_CHENA) {
        WRITE_REG(Host, CHANNEL(HCCHAR, Channel), READ_REG(Host, CHANNEL(HCCHAR, Channel)) | HCCHAR_CHDIS);
        while ((READ_REG(Host, CHANNEL(HCINT, Channel)) & HCINT_CHHLTD) == 0 &&
               UsbClock() - Start < 10 * Host->ClockRate) {
        }
    }

    WRITE_REG(Host, CHANNEL(HCINTMSK, Channel), 0);
    WRITE_REG(Host, CHANNEL(HCINT, Channel), HCINT_ALL);
    Host->Channels &= ~(1 << Channel);
}

VOID
UsbStartPipe(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe,
    IN ULONG Address,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine starts a transfer on a pipe, through a buffer the core
    reaches with its bus address. An IN transfer is of whole packets, so the
    buffer must hold the length rounded up to the packet size; it ends at a
    short packet. An OUT transfer of no length sends a zero length packet.

Arguments:

    Host - Supplies the host.

    Pipe - Supplies the pipe.

    Address - Supplies the bus address of the buffer, aligned to a word.

    Length - Supplies the length of the transfer.

Return Value:

    None.

--*/

{
    ULONG Channel = Pipe->Channel;
    ULONG Packets = Length == 0 ? 1 : (Length + Pipe->Maximum - 1) / Pipe->Maximum;

    Pipe->Packets = Packets;
    Pipe->Length = Pipe->In ? Packets * Pipe->Maximum : Length;
    WRITE_REG(Host, CHANNEL(HCSPLT, Channel), 0);
    WRITE_REG(Host, CHANNEL(HCTSIZ, Channel),
              ((ULONG)Pipe->Pid << HCTSIZ_PID_SHIFT) | (Packets << HCTSIZ_PKTCNT_SHIFT) | Pipe->Length);
    WRITE_REG(Host, CHANNEL(HCDMA, Channel), Address);
    WRITE_REG(Host, CHANNEL(HCINT, Channel), HCINT_ALL);
    MEMORY_BARRIER();
    WRITE_REG(Host, CHANNEL(HCCHAR, Channel), Pipe->Character | HCCHAR_CHENA);
}

VOID
UsbPipeInterrupt(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe,
    IN BOOLEAN Enable
    )

/*++

Routine Description:

    This routine lets the channel of a pipe interrupt when it halts, or
    stops it; a halt while it may not interrupt does once it may.

Arguments:

    Host - Supplies the host.

    Pipe - Supplies the pipe.

    Enable - Supplies TRUE to let the channel interrupt.

Return Value:

    None.

--*/

{
    WRITE_REG(Host, CHANNEL(HCINTMSK, Pipe->Channel), Enable ? HCINT_CHHLTD : 0);
}

BOOLEAN
UsbPipeHalted(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine, of the interrupt service routine of the function driver,
    sees if the channel of a pipe halted: it keeps the interrupts and the
    transfer size of the halt and acknowledges them.

Arguments:

    Host - Supplies the host.

    Pipe - Supplies the pipe.

Return Value:

    TRUE if the channel halted, else FALSE.

--*/

{
    ULONG Channel = Pipe->Channel;
    ULONG Interrupt;

    if ((READ_REG(Host, HAINT) & (1 << Channel)) == 0) {
        return FALSE;
    }

    Interrupt = READ_REG(Host, CHANNEL(HCINT, Channel));
    if ((Interrupt & HCINT_CHHLTD) == 0) {
        return FALSE;
    }

    Pipe->Interrupt = Interrupt;
    Pipe->Transfer = READ_REG(Host, CHANNEL(HCTSIZ, Channel));
    WRITE_REG(Host, CHANNEL(HCINT, Channel), HCINT_ALL);
    MEMORY_BARRIER();
    return TRUE;
}

LONG
UsbPipeResult(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine gives the result of the transfer a pipe halted at, and
    keeps the toggle of the next. A NAK ends a transfer early in some cores:
    the length of the whole packets moved is kept (Moved), for the caller to
    start the rest. A stalled endpoint starts again at DATA0 once the caller
    cleared its halt.

Arguments:

    Host - Supplies the host.

    Pipe - Supplies the pipe.

Return Value:

    The length moved, or XFER_NAK, XFER_STALL or XFER_ERROR.

--*/

{
    ULONG Interrupt = Pipe->Interrupt;
    ULONG Transfer = Pipe->Transfer;
    ULONG Left = (Transfer & HCTSIZ_PKTCNT_MASK) >> HCTSIZ_PKTCNT_SHIFT;

    if (Interrupt & HCINT_STALL) {
        Pipe->Pid = PID_DATA0;
        return XFER_STALL;
    }

    Pipe->Pid = (UCHAR)((Transfer >> HCTSIZ_PID_SHIFT) & 3);
    if (Interrupt & HCINT_XFERCOMPL) {
        return (LONG)(Pipe->In ? Pipe->Length - (Transfer & HCTSIZ_XFERSIZE_MASK) : Pipe->Length);
    }

    Pipe->Moved = (Pipe->Packets - Left) * Pipe->Maximum;
    return UsbStatus(Interrupt) == XACT_NAK ? XFER_NAK : XFER_ERROR;
}

#endif // USB_FIRMWARE
