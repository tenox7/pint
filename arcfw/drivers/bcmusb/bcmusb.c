/*++

Module Name:

    bcmusb.c

Abstract:

    This is the keyboard and pointer port driver of the Raspberry Pi 2. Its
    keyboard and mouse are USB devices of the HID boot protocol behind the hub
    on the root port of the USB host of the BCM2835, a Synopsys DesignWare OTG
    core (the hub is the board's LAN9514, or that of the emulator). The
    firmware describes the host as the keyboard and the pointer controller of
    the internal bus, with its registers, its interrupt and the VideoCore
    mailbox through which it is powered.

    The driver resets the host, enumerates the hub and finds the keyboard and
    the mouse behind it (one receiver may be both), then a system thread polls
    their interrupt endpoints. The keys a keyboard report presses and releases
    become the scan codes (set 1) of an enhanced keyboard, and a mouse report
    its button transitions and relative motion, queued for the keyboard and
    mouse class drivers as i8042prt queues them; as i8042prt, the driver has
    one device, the keyboard port, also named the pointer port. A USB keyboard
    reports only changes, so the thread repeats a held key at the typematic
    rate, and it sets the keyboard's indicators. Transfers run on host channel
    0 through a common buffer; a full or low speed device behind the high
    speed hub is reached with split transactions.

Environment:

    kernel mode only

--*/

#include "ntddk.h"
#include "ntddkbd.h"
#include "ntddmou.h"
#include "kbdmou.h"

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
#define HPTXFSIZ            0x100
#define HCFG                0x400
#define HFNUM               0x408
#define HPRT0               0x440
#define HCCHAR              0x500
#define HCSPLT              0x504
#define HCINT               0x508
#define HCTSIZ              0x510
#define HCDMA               0x514
#define PCGCCTL             0xE00

#define GAHBCFG_DMA_EN          0x00000020
#define GAHBCFG_HBSTLEN_INCR4   0x00000006
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
#define HCCHAR_MULTICNT_SHIFT   20
#define HCCHAR_MULTICNT_MASK    0x00300000
#define HCCHAR_EPTYPE_SHIFT     18
#define HCCHAR_LSPDDEV          0x00020000
#define HCCHAR_EPDIR_IN         0x00008000
#define HCCHAR_EPNUM_SHIFT      11

#define HCSPLT_SPLTENA          0x80000000
#define HCSPLT_COMPSPLT         0x00010000
#define HCSPLT_HUBADDR_SHIFT    7

#define HCINT_XFERCOMPL         0x00000001
#define HCINT_CHHLTD            0x00000002
#define HCINT_NAK               0x00000010
#define HCINT_ACK               0x00000020
#define HCINT_NYET              0x00000040
#define HCINT_FRMOVRUN          0x00000200
#define HCINT_ALL               0x00003FFF

#define HCTSIZ_PID_SHIFT        29
#define HCTSIZ_PKTCNT_SHIFT     19
#define HCTSIZ_XFERSIZE_MASK    0x0007FFFF

#define HFNUM_FRNUM_MASK        0x0000FFFF

#define PID_DATA0               0
#define PID_DATA1               2
#define PID_SETUP               3

#define EPTYPE_CONTROL          0
#define EPTYPE_INTERRUPT        3

#define SPEED_HIGH              0
#define SPEED_FULL              1
#define SPEED_LOW               2

#define MAXIMUM_PACKET_COUNT    511
#define MAXIMUM_TRANSFER        512

//
// Transfer results: done, the device not ready (NAK, a lost frame or an
// incomplete split), or an error.
//

#define XFER_DONE               0
#define XFER_AGAIN              (-2)
#define XFER_ERROR              (-1)

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
#define DESCRIPTOR_HUB          0x29

#define PORT_RESET              4
#define PORT_POWER              8
#define C_PORT_CONNECTION       16
#define C_PORT_RESET            20
#define PORT_STATUS_CONNECTION  0x0001
#define PORT_STATUS_LOW_SPEED   0x0200
#define PORT_STATUS_HIGH_SPEED  0x0400

#define CLASS_HID               3
#define SUBCLASS_BOOT           1
#define PROTOCOL_KEYBOARD       1
#define PROTOCOL_MOUSE          2

#define HUB_ADDRESS             1
#define FIRST_DEVICE_ADDRESS    2

//
// VideoCore mailbox registers, the property channel and the power of the USB
// host.
//

#define MBOX_READ               0x00
#define MBOX_STATUS             0x18
#define MBOX_WRITE              0x20
#define MBOX_WR_FULL            0x80000000
#define MBOX_RD_EMPTY           0x40000000
#define MBOX_PROPERTY           8
#define TAG_SET_POWER_STATE     0x00028001
#define POWER_DEVICE_USB        3
#define POWER_ON_WAIT           3
#define PROPERTY_LENGTH         8

//
// The common buffer: the transfer bounce buffer, then the mailbox property
// buffer.
//

#define BOUNCE_OFFSET           0
#define PROPERTY_OFFSET         MAXIMUM_TRANSFER
#define COMMON_BUFFER_SIZE      PAGE_SIZE

#define TIMEOUT_US              1000000
#define TIMEOUT_MS              1000
#define SPIN_US                 250
#define CONTROL_RETRIES         500
#define POLL_INTERVAL_MS        8
#define INPUT_QUEUE_LENGTH      100

#define KEYBOARD_TYPE_ENHANCED  4

//
// The mouse: the identifier of a mouse of the HID class (the value later NT
// gives it), and its buttons, bits 0 to 2 of a boot report (left, right and
// middle, NT's buttons 1 to 3).
//

#define MOUSE_HID_HARDWARE      0x0080
#define MOUSE_BUTTONS           3

//
// The modifier bits of a boot report and their keys, and the usage of the
// first key a report can carry.
//

#define USAGE_ERROR_ROLLOVER    0x01
#define USAGE_FIRST_KEY         0x04
#define REPORT_LENGTH           8

//
// A function of a device behind the hub: the device's address, its hub port,
// its speed and the packet size of its control endpoint, and the interface
// and interrupt endpoint of the function with the data toggle of the latter.
// A receiver of both a keyboard and a mouse is one device with two functions.
//

typedef struct _USB_FUNCTION {
    BOOLEAN Present;
    ULONG Address;
    ULONG HubPort;
    ULONG Speed;
    ULONG ControlMaximum;
    ULONG Interface;
    ULONG Endpoint;
    ULONG EndpointMaximum;
    ULONG Toggle;
} USB_FUNCTION, *PUSB_FUNCTION;

//
// The input data queue of a class driver connection, as i8042prt keeps one
// for the keyboard and one for the mouse: records of the class's input data,
// added by the thread under the queue lock and given to the class driver by
// the DPC.
//

typedef struct _INPUT_QUEUE {
    CONNECT_DATA ConnectData;
    LONG EnableCount;
    KSPIN_LOCK Lock;
    KDPC Dpc;
    ULONG RecordSize;
    PUCHAR Data;
    PUCHAR DataIn;
    PUCHAR DataOut;
    PUCHAR DataEnd;
    ULONG Count;
} INPUT_QUEUE, *PINPUT_QUEUE;

typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    PULONG Registers;
    PULONG Mailbox;
    PUCHAR Buffer;
    ULONG BufferAddress;

    //
    // The keyboard and the mouse, and their input data queues.
    //

    USB_FUNCTION Keyboard;
    USB_FUNCTION Mouse;
    INPUT_QUEUE KeyboardQueue;
    INPUT_QUEUE MouseQueue;

    //
    // The last keyboard report, the attributes, indicators and typematic
    // parameters of the keyboard, and the key the thread repeats.
    //

    UCHAR Report[REPORT_LENGTH];
    KEYBOARD_ATTRIBUTES KeyboardAttributes;
    KEYBOARD_INDICATOR_PARAMETERS Indicators;
    KEYBOARD_TYPEMATIC_PARAMETERS Typematic;
    BOOLEAN IndicatorsChanged;
    UCHAR RepeatUsage;
    LARGE_INTEGER RepeatTime;

    //
    // The attributes of the mouse and the buttons of its last report.
    //

    MOUSE_ATTRIBUTES MouseAttributes;
    UCHAR MouseButtons;

    HANDLE Thread;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

static const INDICATOR_LIST IndicatorList[] = {
    {0x3A, KEYBOARD_CAPS_LOCK_ON},
    {0x45, KEYBOARD_NUM_LOCK_ON},
    {0x46, KEYBOARD_SCROLL_LOCK_ON}};

//
// The scan code (set 1) of each usage of the keyboard page, from usage 4:
// KEY_E0 above the code marks the keys of the enhanced keyboard that send an
// E0 prefix; zero is a usage the keyboard does not have.
//

#define E0(Code)                ((KEY_E0 << 8) | (Code))
#define USAGE_PRINT_SCREEN      0x46
#define USAGE_PAUSE             0x48

static const USHORT UsageScanCode[] = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,             // 04 a - h
    0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,             // 0C i - p
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,             // 14 q - x
    0x15, 0x2C, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,             // 1C y z 1 - 6
    0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F,             // 24 7 - 0 enter esc bs tab
    0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27,             // 2C space - = [ ] \ # ;
    0x28, 0x29, 0x33, 0x34, 0x35, 0x3A, 0x3B, 0x3C,             // 34 ' ` , . / caps f1 f2
    0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,             // 3C f3 - f10
    0x57, 0x58, E0(0x37), 0x46, 0x1D, E0(0x52), E0(0x47), E0(0x49), // 44 f11 f12 prtsc scroll pause ins home pgup
    E0(0x53), E0(0x4F), E0(0x51), E0(0x4D), E0(0x4B), E0(0x50), E0(0x48), 0x45, // 4C del end pgdn right left down up num
    E0(0x35), 0x37, 0x4A, 0x4E, E0(0x1C), 0x4F, 0x50, 0x51,     // 54 kp/ kp* kp- kp+ kpenter kp1 - kp3
    0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53,             // 5C kp4 - kp9 kp0 kp.
    0x56, E0(0x5D)};                                            // 64 \ (non-US) application

//
// The keys of the modifier bits of a report, from bit 0: left control,
// shift, alt and windows, right control, shift, alt and windows.
//

static const USHORT ModifierScanCode[8] = {
    0x1D, 0x2A, 0x38, E0(0x5B), E0(0x1D), 0x36, E0(0x38), E0(0x5C)};

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

//
// Register access.
//

#define READ_REG(Extension, Offset) \
    READ_REGISTER_ULONG((Extension)->Registers + (Offset) / sizeof(ULONG))

#define WRITE_REG(Extension, Offset, Value) \
    WRITE_REGISTER_ULONG((Extension)->Registers + (Offset) / sizeof(ULONG), (Value))

static VOID
UsbDelay(
    IN ULONG Milliseconds
    )
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000 * (LONGLONG)Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

//
// Wait up to a limit for the bits of the mask in a register to be set or
// clear: a short spin covers a bus transaction, then the thread sleeps between
// looks, so a device that does not answer does not hold the processor.
//

static BOOLEAN
UsbWaitBits(
    IN PDEVICE_EXTENSION Extension,
    IN ULONG Offset,
    IN ULONG Mask,
    IN BOOLEAN Set,
    IN ULONG Milliseconds
    )
{
    LARGE_INTEGER Limit;
    LARGE_INTEGER Now;
    ULONG Time;

    for (Time = 0; Time < SPIN_US; Time += 10) {
        if ((READ_REG(Extension, Offset) & Mask) == (Set ? Mask : 0)) {
            return TRUE;
        }

        KeStallExecutionProcessor(10);
    }

    KeQuerySystemTime(&Limit);
    Limit.QuadPart += 10000 * (LONGLONG)Milliseconds;
    for (;;) {
        if ((READ_REG(Extension, Offset) & Mask) == (Set ? Mask : 0)) {
            return TRUE;
        }

        KeQuerySystemTime(&Now);
        if (Now.QuadPart >= Limit.QuadPart) {
            return FALSE;
        }

        UsbDelay(1);
    }
}

//
// Power the USB host through the VideoCore mailbox. The property buffer is in
// the common buffer, which the VideoCore reads and writes.
//

static BOOLEAN
UsbPowerOn(
    IN PDEVICE_EXTENSION Extension
    )
{
    PULONG Property = (PULONG)(Extension->Buffer + PROPERTY_OFFSET);
    PULONG Mailbox = Extension->Mailbox;
    ULONG Time;
    ULONG Message;

    Property[0] = PROPERTY_LENGTH * sizeof(ULONG);
    Property[1] = 0;
    Property[2] = TAG_SET_POWER_STATE;
    Property[3] = 2 * sizeof(ULONG);
    Property[4] = 2 * sizeof(ULONG);
    Property[5] = POWER_DEVICE_USB;
    Property[6] = POWER_ON_WAIT;
    Property[7] = 0;

    for (Time = 0; READ_REGISTER_ULONG(Mailbox + MBOX_STATUS / sizeof(ULONG)) & MBOX_WR_FULL; Time += 1) {
        if (Time == TIMEOUT_US) {
            return FALSE;
        }

        KeStallExecutionProcessor(1);
    }

    WRITE_REGISTER_ULONG(Mailbox + MBOX_WRITE / sizeof(ULONG),
                         (Extension->BufferAddress + PROPERTY_OFFSET) | MBOX_PROPERTY);

    do {
        for (Time = 0; READ_REGISTER_ULONG(Mailbox + MBOX_STATUS / sizeof(ULONG)) & MBOX_RD_EMPTY; Time += 1) {
            if (Time == TIMEOUT_US) {
                return FALSE;
            }

            KeStallExecutionProcessor(1);
        }

        Message = READ_REGISTER_ULONG(Mailbox + MBOX_READ / sizeof(ULONG));
    } while ((Message & 0xF) != MBOX_PROPERTY);

    return TRUE;
}

static BOOLEAN
UsbResetCore(
    IN PDEVICE_EXTENSION Extension
    )
{
    if (!UsbWaitBits(Extension, GRSTCTL, GRSTCTL_AHBIDLE, TRUE, TIMEOUT_MS)) {
        return FALSE;
    }

    WRITE_REG(Extension, GRSTCTL, READ_REG(Extension, GRSTCTL) | GRSTCTL_CSFTRST);
    if (!UsbWaitBits(Extension, GRSTCTL, GRSTCTL_CSFTRST, FALSE, TIMEOUT_MS)) {
        return FALSE;
    }

    UsbDelay(10);
    return TRUE;
}

//
// Run one packet group on channel 0 (the channel is set up for the device and
// endpoint) and wait up to a limit for the channel to halt. A channel the
// device leaves running past the limit (the core retries the transactions it
// does not acknowledge) is halted, and the device not ready. The data goes
// through the bounce buffer of the common buffer.
//

static LONG
UsbTransferChunk(
    IN PDEVICE_EXTENSION Extension,
    IN OUT PULONG Pid,
    IN BOOLEAN In,
    IN PUCHAR Buffer,
    IN ULONG Packets,
    IN ULONG Length,
    OUT PULONG Actual,
    IN BOOLEAN OddFrame,
    IN ULONG Milliseconds
    )
{
    BOOLEAN Halted;
    ULONG Character;
    ULONG Interrupt;
    ULONG Size;

    if (!In && Length != 0) {
        RtlMoveMemory(Extension->Buffer + BOUNCE_OFFSET, Buffer, Length);
    }

    WRITE_REG(Extension, HCTSIZ, (*Pid << HCTSIZ_PID_SHIFT) |
                                 (Packets << HCTSIZ_PKTCNT_SHIFT) |
                                 (Length & HCTSIZ_XFERSIZE_MASK));
    WRITE_REG(Extension, HCDMA, Extension->BufferAddress + BOUNCE_OFFSET);
    WRITE_REG(Extension, HCINT, HCINT_ALL);

    Character = READ_REG(Extension, HCCHAR);
    Character &= ~(HCCHAR_MULTICNT_MASK | HCCHAR_CHENA | HCCHAR_CHDIS | HCCHAR_ODDFRM);
    Character |= (1 << HCCHAR_MULTICNT_SHIFT) | HCCHAR_CHENA;
    if (OddFrame) {
        Character |= HCCHAR_ODDFRM;
    }

    WRITE_REG(Extension, HCCHAR, Character);

    Halted = UsbWaitBits(Extension, HCINT, HCINT_CHHLTD, TRUE, Milliseconds);
    if (!Halted) {
        WRITE_REG(Extension, HCCHAR, READ_REG(Extension, HCCHAR) | HCCHAR_CHENA | HCCHAR_CHDIS);
        if (!UsbWaitBits(Extension, HCINT, HCINT_CHHLTD, TRUE, TIMEOUT_MS)) {
            return XFER_ERROR;
        }
    }

    Interrupt = READ_REG(Extension, HCINT);
    Size = READ_REG(Extension, HCTSIZ);
    *Pid = (Size >> HCTSIZ_PID_SHIFT) & 3;
    if ((Interrupt & HCINT_XFERCOMPL) == 0) {
        return !Halted || (Interrupt & (HCINT_NAK | HCINT_FRMOVRUN)) != 0 ? XFER_AGAIN : XFER_ERROR;
    }

    if (In) {
        Length -= Size & HCTSIZ_XFERSIZE_MASK;
        if (Length != 0) {
            RtlMoveMemory(Buffer, Extension->Buffer + BOUNCE_OFFSET, Length);
        }
    }

    *Actual = Length;
    return XFER_DONE;
}

//
// One transfer to or from an endpoint of a function: the channel is set up
// for it, and the packet groups run until the length is moved or a short
// packet ends it. A full or low speed device behind the high speed hub is
// reached with a start split followed by complete splits, retried while the
// hub answers NYET within the frames the transaction may take.
//

static LONG
UsbTransfer(
    IN PDEVICE_EXTENSION Extension,
    IN PUSB_FUNCTION Function,
    IN ULONG Endpoint,
    IN BOOLEAN In,
    IN ULONG Type,
    IN ULONG Maximum,
    IN OUT PULONG Pid,
    IN PUCHAR Buffer,
    IN ULONG Length,
    OUT PULONG Actual
    )
{
    ULONG Done = 0;
    ULONG MaximumLength;
    ULONG Packets;
    ULONG RootSpeed;
    ULONG Split;
    ULONG StartFrame = 0;
    BOOLEAN DoSplit = FALSE;
    BOOLEAN CompleteSplit = FALSE;
    BOOLEAN Stop = FALSE;
    LONG Status = XFER_DONE;

    WRITE_REG(Extension, HCCHAR, ((Function->Address & 0x7F) << HCCHAR_DEVADDR_SHIFT) |
                                 ((Endpoint & 0xF) << HCCHAR_EPNUM_SHIFT) |
                                 (In ? HCCHAR_EPDIR_IN : 0) |
                                 ((Type & 3) << HCCHAR_EPTYPE_SHIFT) |
                                 (Maximum & 0x7FF) |
                                 (Function->Speed == SPEED_LOW ? HCCHAR_LSPDDEV : 0));
    WRITE_REG(Extension, HCSPLT, 0);

    MaximumLength = MAXIMUM_PACKET_COUNT * Maximum;
    if (MaximumLength > MAXIMUM_TRANSFER) {
        MaximumLength = MAXIMUM_TRANSFER;
    }

    Packets = MaximumLength / Maximum;
    MaximumLength = Packets * Maximum;

    RootSpeed = (READ_REG(Extension, HPRT0) & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
    if (Function->Speed != SPEED_HIGH && RootSpeed == SPEED_HIGH) {
        WRITE_REG(Extension, HCSPLT, HCSPLT_SPLTENA |
                                     (HUB_ADDRESS << HCSPLT_HUBADDR_SHIFT) |
                                     (Function->HubPort & 0x7F));
        DoSplit = TRUE;
        Packets = 1;
        MaximumLength = Maximum;
    }

    do {
        ULONG Chunk = Length - Done;
        ULONG Moved = 0;
        ULONG Interrupt;

        if (Chunk > MaximumLength) {
            Chunk = MaximumLength;
        } else if (Chunk > Maximum) {
            Packets = (Chunk + Maximum - 1) / Maximum;
        } else {
            Packets = 1;
        }

        Split = READ_REG(Extension, HCSPLT);
        if (CompleteSplit) {
            WRITE_REG(Extension, HCSPLT, Split | HCSPLT_COMPSPLT);
        } else if (DoSplit) {
            WRITE_REG(Extension, HCSPLT, Split & ~HCSPLT_COMPSPLT);
        }

        Status = UsbTransferChunk(Extension, Pid, In, Buffer + Done, Packets, Chunk, &Moved,
                                  Type == EPTYPE_INTERRUPT && (READ_REG(Extension, HFNUM) & 1) == 0,
                                  Type == EPTYPE_INTERRUPT ? POLL_INTERVAL_MS : TIMEOUT_MS);

        Interrupt = READ_REG(Extension, HCINT);
        if (CompleteSplit) {
            Stop = FALSE;
            if (Interrupt & HCINT_NYET) {
                Status = XFER_DONE;
                if (((READ_REG(Extension, HFNUM) - StartFrame) & HFNUM_FRNUM_MASK) > 4) {
                    Status = XFER_AGAIN;
                }

            } else {
                CompleteSplit = FALSE;
            }

        } else if (DoSplit && (Interrupt & HCINT_ACK)) {
            StartFrame = READ_REG(Extension, HFNUM) & HFNUM_FRNUM_MASK;
            Status = XFER_DONE;
            CompleteSplit = TRUE;
        }

        if (Status != XFER_DONE) {
            break;
        }

        if (Moved < Chunk) {
            Stop = TRUE;
        }

        Done += Moved;
    } while ((Done < Length && !Stop) || CompleteSplit);

    WRITE_REG(Extension, HCINT, HCINT_ALL);
    *Actual = Done;
    return Status;
}

//
// A stage of a control transfer, retried while the device is not ready.
//

static LONG
UsbControlStage(
    IN PDEVICE_EXTENSION Extension,
    IN PUSB_FUNCTION Function,
    IN BOOLEAN In,
    IN ULONG Pid,
    IN PUCHAR Buffer,
    IN ULONG Length,
    OUT PULONG Actual
    )
{
    ULONG Retries;
    LONG Status;

    for (Retries = 0; ; Retries += 1) {
        Status = UsbTransfer(Extension, Function, 0, In, EPTYPE_CONTROL, Function->ControlMaximum,
                             &Pid, Buffer, Length, Actual);
        if (Status != XFER_AGAIN || Retries == CONTROL_RETRIES) {
            return Status;
        }

        KeStallExecutionProcessor(125);
    }
}

//
// A control transfer: the setup stage, the data stage if there is data, and
// the status stage. Returns the length of the data, or -1.
//

static LONG
UsbControl(
    IN PDEVICE_EXTENSION Extension,
    IN PUSB_FUNCTION Function,
    IN ULONG RequestType,
    IN ULONG Request,
    IN ULONG Value,
    IN ULONG Index,
    IN PUCHAR Data,
    IN ULONG Length
    )
{
    UCHAR Setup[8];
    UCHAR Empty[4];
    ULONG Actual = 0;
    ULONG Moved;
    BOOLEAN In = (RequestType & 0x80) != 0;

    Setup[0] = (UCHAR)RequestType;
    Setup[1] = (UCHAR)Request;
    Setup[2] = (UCHAR)Value;
    Setup[3] = (UCHAR)(Value >> 8);
    Setup[4] = (UCHAR)Index;
    Setup[5] = (UCHAR)(Index >> 8);
    Setup[6] = (UCHAR)Length;
    Setup[7] = (UCHAR)(Length >> 8);
    if (UsbControlStage(Extension, Function, FALSE, PID_SETUP, Setup, 8, &Moved) != XFER_DONE) {
        return -1;
    }

    if (Length != 0) {
        if (UsbControlStage(Extension, Function, In, PID_DATA1, Data, Length, &Actual) != XFER_DONE) {
            return -1;
        }
    }

    if (UsbControlStage(Extension, Function, Length != 0 ? !In : TRUE, PID_DATA1, Empty, 0, &Moved) != XFER_DONE) {
        return -1;
    }

    return (LONG)Actual;
}

//
// Put the interface of a function in the boot protocol, reporting only
// changes. Some devices stall these requests and report anyway.
//

static VOID
UsbSetBootProtocol(
    IN PDEVICE_EXTENSION Extension,
    IN PUSB_FUNCTION Function
    )
{
    UsbControl(Extension, Function, 0x21, REQUEST_SET_PROTOCOL, 0, Function->Interface, NULL, 0);
    UsbControl(Extension, Function, 0x21, REQUEST_SET_IDLE, 0, Function->Interface, NULL, 0);
    Function->Toggle = PID_DATA0;
}

//
// Enumerate the device the hub reset on one of its ports, at address 0: give
// it its address, and if it has a keyboard or a mouse of the boot protocol
// the driver does not have yet, select its configuration, put those
// interfaces in the boot protocol and keep their interrupt endpoints.
//

static VOID
UsbEnumerateDevice(
    IN PDEVICE_EXTENSION Extension,
    IN ULONG Port,
    IN ULONG Speed,
    IN ULONG Address
    )
{
    UCHAR Data[256];
    USB_FUNCTION Device;
    USB_FUNCTION Keyboard;
    USB_FUNCTION Mouse;
    PUSB_FUNCTION Function = NULL;
    ULONG Maximum;
    ULONG Total;
    ULONG Offset;
    ULONG Configuration;

    RtlZeroMemory(&Device, sizeof(Device));
    Device.HubPort = Port;
    Device.Speed = Speed;
    Device.ControlMaximum = 8;
    if (UsbControl(Extension, &Device, 0x80, REQUEST_GET_DESCRIPTOR,
                   DESCRIPTOR_DEVICE << 8, 0, Data, 8) < 0) {
        return;
    }

    Maximum = Data[7] != 0 ? Data[7] : 8;
    if (UsbControl(Extension, &Device, 0x00, REQUEST_SET_ADDRESS, Address, 0, NULL, 0) < 0) {
        return;
    }

    UsbDelay(2);
    Device.Address = Address;
    Device.ControlMaximum = Maximum;
    if (UsbControl(Extension, &Device, 0x80, REQUEST_GET_DESCRIPTOR,
                   DESCRIPTOR_CONFIG << 8, 0, Data, 9) < 0) {
        return;
    }

    Total = Data[2] | (Data[3] << 8);
    Configuration = Data[5];
    if (Total > sizeof(Data)) {
        Total = sizeof(Data);
    }

    if (UsbControl(Extension, &Device, 0x80, REQUEST_GET_DESCRIPTOR,
                   DESCRIPTOR_CONFIG << 8, 0, Data, Total) < 0) {
        return;
    }

    Keyboard = Device;
    Mouse = Device;
    for (Offset = 0; Offset + 2 <= Total && Data[Offset] != 0; Offset += Data[Offset]) {
        if (Data[Offset + 1] == DESCRIPTOR_INTERFACE && Offset + 8 <= Total) {
            Function = NULL;
            if (Data[Offset + 5] != CLASS_HID || Data[Offset + 6] != SUBCLASS_BOOT) {
                continue;
            }

            if (Data[Offset + 7] == PROTOCOL_KEYBOARD && !Extension->Keyboard.Present && !Keyboard.Present) {
                Function = &Keyboard;
            } else if (Data[Offset + 7] == PROTOCOL_MOUSE && !Extension->Mouse.Present && !Mouse.Present) {
                Function = &Mouse;
            }

            if (Function != NULL) {
                Function->Interface = Data[Offset + 2];
            }

            continue;
        }

        if (Data[Offset + 1] == DESCRIPTOR_ENDPOINT && Function != NULL && !Function->Present &&
            Offset + 6 <= Total && (Data[Offset + 2] & 0x80) != 0 && (Data[Offset + 3] & 3) == 3) {
            Function->Endpoint = Data[Offset + 2] & 0xF;
            Function->EndpointMaximum = Data[Offset + 4] | (Data[Offset + 5] << 8);
            Function->Present = TRUE;
        }
    }

    if (!Keyboard.Present && !Mouse.Present) {
        return;
    }

    if (UsbControl(Extension, &Device, 0x00, REQUEST_SET_CONFIG, Configuration, 0, NULL, 0) < 0) {
        return;
    }

    if (Keyboard.Present) {
        UsbSetBootProtocol(Extension, &Keyboard);
        Extension->Keyboard = Keyboard;
    }

    if (Mouse.Present) {
        UsbSetBootProtocol(Extension, &Mouse);
        Extension->Mouse = Mouse;
    }
}

//
// Enumerate the hub on the root port: give it its address, power its ports,
// and reset each connected one to look for the keyboard and the mouse behind
// it (the LAN9514 has its ethernet controller on a port as well).
//

static BOOLEAN
UsbEnumerateHub(
    IN PDEVICE_EXTENSION Extension,
    IN ULONG Speed
    )
{
    UCHAR Data[16];
    USB_FUNCTION Hub;
    ULONG Maximum;
    ULONG Ports;
    ULONG Port;
    ULONG Status;
    ULONG Address = FIRST_DEVICE_ADDRESS;

    RtlZeroMemory(&Hub, sizeof(Hub));
    Hub.Speed = Speed;
    Hub.ControlMaximum = 8;
    if (UsbControl(Extension, &Hub, 0x80, REQUEST_GET_DESCRIPTOR,
                   DESCRIPTOR_DEVICE << 8, 0, Data, 8) < 0) {
        return FALSE;
    }

    Maximum = Data[7];
    if (UsbControl(Extension, &Hub, 0x00, REQUEST_SET_ADDRESS, HUB_ADDRESS, 0, NULL, 0) < 0) {
        return FALSE;
    }

    UsbDelay(2);
    Hub.Address = HUB_ADDRESS;
    Hub.ControlMaximum = Maximum;
    if (UsbControl(Extension, &Hub, 0x80, REQUEST_GET_DESCRIPTOR,
                   DESCRIPTOR_CONFIG << 8, 0, Data, 9) < 0 ||
        UsbControl(Extension, &Hub, 0x00, REQUEST_SET_CONFIG, Data[5], 0, NULL, 0) < 0 ||
        UsbControl(Extension, &Hub, 0xA0, REQUEST_GET_DESCRIPTOR,
                   DESCRIPTOR_HUB << 8, 0, Data, sizeof(Data)) < 0) {
        return FALSE;
    }

    Ports = Data[2];
    for (Port = 1; Port <= Ports; Port += 1) {
        UsbControl(Extension, &Hub, 0x23, REQUEST_SET_FEATURE, PORT_POWER, Port, NULL, 0);
    }

    UsbDelay(Data[5] * 2 + 10);

    for (Port = 1; Port <= Ports; Port += 1) {
        if (Extension->Keyboard.Present && Extension->Mouse.Present) {
            break;
        }

        if (UsbControl(Extension, &Hub, 0xA3, REQUEST_GET_STATUS, 0, Port, Data, 4) < 0) {
            break;
        }

        if ((Data[0] & PORT_STATUS_CONNECTION) == 0) {
            continue;
        }

        UsbControl(Extension, &Hub, 0x23, REQUEST_SET_FEATURE, PORT_RESET, Port, NULL, 0);
        UsbDelay(60);
        if (UsbControl(Extension, &Hub, 0xA3, REQUEST_GET_STATUS, 0, Port, Data, 4) < 0) {
            break;
        }

        Status = Data[0] | (Data[1] << 8);
        UsbControl(Extension, &Hub, 0x23, REQUEST_CLEAR_FEATURE, C_PORT_RESET, Port, NULL, 0);
        UsbControl(Extension, &Hub, 0x23, REQUEST_CLEAR_FEATURE, C_PORT_CONNECTION, Port, NULL, 0);

        UsbEnumerateDevice(Extension, Port,
                           (Status & PORT_STATUS_LOW_SPEED) ? SPEED_LOW :
                           (Status & PORT_STATUS_HIGH_SPEED) ? SPEED_HIGH : SPEED_FULL,
                           Address);
        Address += 1;
    }

    return Extension->Keyboard.Present || Extension->Mouse.Present;
}

//
// Bring up the host: power it, select the high speed PHY of the chip, force
// host mode, set up DMA and the FIFOs, power and reset the root port, then
// find the keyboard and the mouse behind the hub on it.
//

static BOOLEAN
UsbInitialize(
    IN PDEVICE_EXTENSION Extension
    )
{
    ULONG Port;
    ULONG Time;

    UsbPowerOn(Extension);
    if ((READ_REG(Extension, GSNPSID) & 0xFFFFF000) != 0x4F542000 || !UsbResetCore(Extension)) {
        return FALSE;
    }

    WRITE_REG(Extension, GUSBCFG, READ_REG(Extension, GUSBCFG) &
              ~(GUSBCFG_PHYSEL | GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16));
    if (!UsbResetCore(Extension)) {
        return FALSE;
    }

    WRITE_REG(Extension, GUSBCFG, (READ_REG(Extension, GUSBCFG) &
              ~(GUSBCFG_ULPI_FS_LS | GUSBCFG_ULPI_CLK_SUSP_M)) | GUSBCFG_FORCEHOSTMODE);
    for (Time = 0; (READ_REG(Extension, GINTSTS) & GINTSTS_CURMODE_HOST) == 0; Time += 1) {
        if (Time == 200) {
            return FALSE;
        }

        UsbDelay(1);
    }

    WRITE_REG(Extension, GAHBCFG, GAHBCFG_DMA_EN | GAHBCFG_HBSTLEN_INCR4);
    WRITE_REG(Extension, GINTMSK, 0);
    WRITE_REG(Extension, PCGCCTL, 0);
    WRITE_REG(Extension, HCFG, (READ_REG(Extension, HCFG) & ~7) | HCFG_FSLSPCLKSEL_30_60);
    WRITE_REG(Extension, GRXFSIZ, 516 + 16);
    WRITE_REG(Extension, GNPTXFSIZ, (0x100 << 16) | (516 + 16));
    WRITE_REG(Extension, HPTXFSIZ, (0x200 << 16) | (516 + 16 + 0x100));
    WRITE_REG(Extension, GRSTCTL, GRSTCTL_TXFNUM_ALL | GRSTCTL_TXFFLSH);
    UsbWaitBits(Extension, GRSTCTL, GRSTCTL_TXFFLSH, FALSE, TIMEOUT_MS);
    WRITE_REG(Extension, GRSTCTL, GRSTCTL_RXFFLSH);
    UsbWaitBits(Extension, GRSTCTL, GRSTCTL_RXFFLSH, FALSE, TIMEOUT_MS);

    WRITE_REG(Extension, HPRT0, (READ_REG(Extension, HPRT0) & ~HPRT0_W1C_MASK) | HPRT0_PWR);
    UsbDelay(50);
    if (!UsbWaitBits(Extension, HPRT0, HPRT0_CONNSTS, TRUE, TIMEOUT_MS)) {
        return FALSE;
    }

    WRITE_REG(Extension, HPRT0, (READ_REG(Extension, HPRT0) & ~HPRT0_W1C_MASK) | HPRT0_RST);
    UsbDelay(60);
    WRITE_REG(Extension, HPRT0, READ_REG(Extension, HPRT0) & ~(HPRT0_W1C_MASK | HPRT0_RST));
    UsbDelay(50);

    Port = READ_REG(Extension, HPRT0);
    if ((Port & HPRT0_ENA) == 0) {
        return FALSE;
    }

    return UsbEnumerateHub(Extension, (Port & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT);
}

//
// The input data queues, as i8042prt keeps them: the thread adds a record
// under the queue lock and queues the DPC, which gives the class driver the
// records up to the end of the buffer (and the rest on its next run).
//

static VOID
UsbQueueInput(
    IN PINPUT_QUEUE Queue,
    IN PVOID Input
    )
{
    KIRQL Irql;

    KeAcquireSpinLock(&Queue->Lock, &Irql);
    if (Queue->EnableCount > 0 && Queue->Count < INPUT_QUEUE_LENGTH) {
        RtlMoveMemory(Queue->DataIn, Input, Queue->RecordSize);
        Queue->DataIn += Queue->RecordSize;
        if (Queue->DataIn == Queue->DataEnd) {
            Queue->DataIn = Queue->Data;
        }

        Queue->Count += 1;
        KeInsertQueueDpc(&Queue->Dpc, NULL, NULL);
    }

    KeReleaseSpinLock(&Queue->Lock, Irql);
}

static VOID
UsbInputDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    PINPUT_QUEUE Queue = DeferredContext;
    PUCHAR Start;
    PUCHAR End;
    ULONG Consumed;

    for (;;) {
        KeAcquireSpinLockAtDpcLevel(&Queue->Lock);
        if (Queue->Count == 0 || Queue->ConnectData.ClassService == NULL) {
            KeReleaseSpinLockFromDpcLevel(&Queue->Lock);
            return;
        }

        Start = Queue->DataOut;
        End = Start + Queue->Count * Queue->RecordSize;
        if (End > Queue->DataEnd) {
            End = Queue->DataEnd;
        }

        KeReleaseSpinLockFromDpcLevel(&Queue->Lock);

        Consumed = 0;
        (*(PSERVICE_CALLBACK_ROUTINE)Queue->ConnectData.ClassService)(
            Queue->ConnectData.ClassDeviceObject, Start, End, &Consumed);

        KeAcquireSpinLockAtDpcLevel(&Queue->Lock);
        Queue->DataOut += Consumed * Queue->RecordSize;
        if (Queue->DataOut == Queue->DataEnd) {
            Queue->DataOut = Queue->Data;
        }

        Queue->Count -= Consumed;
        KeReleaseSpinLockFromDpcLevel(&Queue->Lock);
        if (Consumed == 0) {
            return;
        }
    }
}

static BOOLEAN
UsbInitializeQueue(
    IN PINPUT_QUEUE Queue,
    IN ULONG RecordSize
    )
{
    Queue->Data = ExAllocatePool(NonPagedPool, INPUT_QUEUE_LENGTH * RecordSize);
    if (Queue->Data == NULL) {
        return FALSE;
    }

    Queue->RecordSize = RecordSize;
    Queue->DataIn = Queue->DataOut = Queue->Data;
    Queue->DataEnd = Queue->Data + INPUT_QUEUE_LENGTH * RecordSize;
    KeInitializeSpinLock(&Queue->Lock);
    KeInitializeDpc(&Queue->Dpc, UsbInputDpc, Queue);
    return TRUE;
}

//
// The input of a scan code of the keyboard.
//

static VOID
UsbQueueScanCode(
    IN PDEVICE_EXTENSION Extension,
    IN USHORT Code,
    IN USHORT Flags
    )
{
    KEYBOARD_INPUT_DATA Input;

    Input.UnitId = 0;
    Input.MakeCode = Code & 0xFF;
    Input.Flags = Flags | (Code >> 8);
    Input.Reserved = 0;
    Input.ExtraInformation = 0;
    UsbQueueInput(&Extension->KeyboardQueue, &Input);
}

//
// The scan codes of a usage, and the input of its press or release: Print
// Screen is E0 37, Pause E1 1D 45.
//

static VOID
UsbQueueKey(
    IN PDEVICE_EXTENSION Extension,
    IN UCHAR Usage,
    IN USHORT Flags
    )
{
    USHORT Code;

    if (Usage < USAGE_FIRST_KEY ||
        Usage >= USAGE_FIRST_KEY + sizeof(UsageScanCode) / sizeof(UsageScanCode[0])) {
        return;
    }

    Code = UsageScanCode[Usage - USAGE_FIRST_KEY];
    if (Usage == USAGE_PAUSE) {
        UsbQueueScanCode(Extension, 0x1D, Flags | KEY_E1);
        UsbQueueScanCode(Extension, 0x45, Flags);
        return;
    }

    UsbQueueScanCode(Extension, Code, Flags);
}

static BOOLEAN
UsbReportHasKey(
    IN PUCHAR Report,
    IN UCHAR Usage
    )
{
    ULONG Index;

    for (Index = 2; Index < REPORT_LENGTH; Index += 1) {
        if (Report[Index] == Usage) {
            return TRUE;
        }
    }

    return FALSE;
}

//
// The changes of a keyboard report: the modifiers and keys released, then
// those pressed. The last key pressed is the one the thread repeats.
//

static VOID
UsbProcessReport(
    IN PDEVICE_EXTENSION Extension,
    IN PUCHAR Report
    )
{
    PUCHAR Last = Extension->Report;
    LARGE_INTEGER Now;
    ULONG Index;

    if (Report[2] == USAGE_ERROR_ROLLOVER) {
        return;
    }

    for (Index = 0; Index < 8; Index += 1) {
        if ((Last[0] & ~Report[0]) & (1 << Index)) {
            UsbQueueScanCode(Extension, ModifierScanCode[Index], KEY_BREAK);
        }
    }

    for (Index = 2; Index < REPORT_LENGTH; Index += 1) {
        if (Last[Index] >= USAGE_FIRST_KEY && !UsbReportHasKey(Report, Last[Index])) {
            UsbQueueKey(Extension, Last[Index], KEY_BREAK);
            if (Last[Index] == Extension->RepeatUsage) {
                Extension->RepeatUsage = 0;
            }
        }
    }

    for (Index = 0; Index < 8; Index += 1) {
        if ((Report[0] & ~Last[0]) & (1 << Index)) {
            UsbQueueScanCode(Extension, ModifierScanCode[Index], KEY_MAKE);
        }
    }

    KeQuerySystemTime(&Now);
    for (Index = 2; Index < REPORT_LENGTH; Index += 1) {
        if (Report[Index] >= USAGE_FIRST_KEY && !UsbReportHasKey(Last, Report[Index])) {
            UsbQueueKey(Extension, Report[Index], KEY_MAKE);
            if (Report[Index] != 0x39 && Report[Index] != 0x47 && Report[Index] != 0x53 &&
                Report[Index] != USAGE_PAUSE) {
                Extension->RepeatUsage = Report[Index];
                Extension->RepeatTime.QuadPart = Now.QuadPart +
                    10000 * (LONGLONG)Extension->Typematic.Delay;
            }
        }
    }

    RtlMoveMemory(Last, Report, REPORT_LENGTH);
}

//
// Repeat the held key at the typematic rate once the typematic delay passed.
//

static VOID
UsbRepeatKey(
    IN PDEVICE_EXTENSION Extension
    )
{
    LARGE_INTEGER Now;

    if (Extension->RepeatUsage == 0) {
        return;
    }

    KeQuerySystemTime(&Now);
    if (Now.QuadPart < Extension->RepeatTime.QuadPart) {
        return;
    }

    UsbQueueKey(Extension, Extension->RepeatUsage, KEY_MAKE);
    Extension->RepeatTime.QuadPart = Now.QuadPart +
        10000000 / (LONGLONG)Extension->Typematic.Rate;
}

//
// Set the keyboard's indicators with an output report: bit 0 num lock, bit 1
// caps lock, bit 2 scroll lock.
//

static VOID
UsbSetIndicators(
    IN PDEVICE_EXTENSION Extension
    )
{
    UCHAR Leds = 0;
    USHORT Flags = Extension->Indicators.LedFlags;

    if (Flags & KEYBOARD_NUM_LOCK_ON) {
        Leds |= 1;
    }

    if (Flags & KEYBOARD_CAPS_LOCK_ON) {
        Leds |= 2;
    }

    if (Flags & KEYBOARD_SCROLL_LOCK_ON) {
        Leds |= 4;
    }

    UsbControl(Extension, &Extension->Keyboard, 0x21, REQUEST_SET_REPORT, 0x200,
               Extension->Keyboard.Interface, &Leds, 1);
}

//
// The changes of a mouse report: the transitions of its buttons and its
// relative motion. A USB mouse reports positive motion down, as the mouse
// class driver takes it (that of the 8042 reports it up).
//

static VOID
UsbProcessMouseReport(
    IN PDEVICE_EXTENSION Extension,
    IN PUCHAR Report
    )
{
    MOUSE_INPUT_DATA Input;
    UCHAR Buttons = Report[0] & ((1 << MOUSE_BUTTONS) - 1);
    UCHAR Changed = Buttons ^ Extension->MouseButtons;
    ULONG Index;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Flags = MOUSE_MOVE_RELATIVE;
    for (Index = 0; Index < MOUSE_BUTTONS; Index += 1) {
        if (Changed & (1 << Index)) {
            Input.Buttons |= ((Buttons & (1 << Index)) ? MOUSE_BUTTON_1_DOWN : MOUSE_BUTTON_1_UP) << (2 * Index);
        }
    }

    Input.LastX = (CHAR)Report[1];
    Input.LastY = (CHAR)Report[2];
    Extension->MouseButtons = Buttons;
    if (Input.Buttons != 0 || Input.LastX != 0 || Input.LastY != 0) {
        UsbQueueInput(&Extension->MouseQueue, &Input);
    }
}

//
// Read a report from the interrupt endpoint of a function, up to a packet of
// the endpoint so that a report is one transfer.
//

static BOOLEAN
UsbReadReport(
    IN PDEVICE_EXTENSION Extension,
    IN PUSB_FUNCTION Function,
    OUT PUCHAR Report
    )
{
    ULONG Length = Function->EndpointMaximum < REPORT_LENGTH ? Function->EndpointMaximum : REPORT_LENGTH;
    ULONG Actual;

    RtlZeroMemory(Report, REPORT_LENGTH);
    return UsbTransfer(Extension, Function, Function->Endpoint, TRUE, EPTYPE_INTERRUPT,
                       Function->EndpointMaximum, &Function->Toggle, Report, Length,
                       &Actual) == XFER_DONE && Actual >= 3;
}

//
// A poll of the keyboard: the setting of its indicators, a report and the
// repeat of the held key.
//

static VOID
UsbPollKeyboard(
    IN PDEVICE_EXTENSION Extension
    )
{
    UCHAR Report[REPORT_LENGTH];

    if (Extension->IndicatorsChanged) {
        Extension->IndicatorsChanged = FALSE;
        UsbSetIndicators(Extension);
    }

    if (UsbReadReport(Extension, &Extension->Keyboard, Report)) {
        UsbProcessReport(Extension, Report);
    }

    UsbRepeatKey(Extension);
}

//
// The thread that polls the keyboard and the mouse each poll interval.
//

static VOID
UsbPollThread(
    IN PVOID Context
    )
{
    PDEVICE_EXTENSION Extension = Context;
    UCHAR Report[REPORT_LENGTH];

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
    for (;;) {
        if (Extension->Keyboard.Present) {
            UsbPollKeyboard(Extension);
        }

        if (Extension->Mouse.Present && UsbReadReport(Extension, &Extension->Mouse, Report)) {
            UsbProcessMouseReport(Extension, Report);
        }

        UsbDelay(POLL_INTERVAL_MS);
    }
}

static NTSTATUS
UsbOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
{
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

//
// The connection of a class driver to the keyboard or the mouse, and the
// disabling of its input.
//

static NTSTATUS
UsbConnect(
    IN PUSB_FUNCTION Function,
    IN PINPUT_QUEUE Queue,
    IN PIO_STACK_LOCATION Stack
    )
{
    KIRQL Irql;

    if (!Function->Present) {
        return STATUS_NO_SUCH_DEVICE;
    }

    if (Queue->ConnectData.ClassService != NULL) {
        return STATUS_SHARING_VIOLATION;
    }

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA)) {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Queue->Lock, &Irql);
    Queue->ConnectData = *(PCONNECT_DATA)Stack->Parameters.DeviceIoControl.Type3InputBuffer;
    Queue->DataIn = Queue->DataOut = Queue->Data;
    Queue->Count = 0;
    KeReleaseSpinLock(&Queue->Lock, Irql);
    return STATUS_SUCCESS;
}

static NTSTATUS
UsbDisable(
    IN PINPUT_QUEUE Queue
    )
{
    if (Queue->EnableCount == 0) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    ExInterlockedDecrementLong(&Queue->EnableCount, &Queue->Lock);
    return STATUS_SUCCESS;
}

//
// The internal device controls of the keyboard and mouse class drivers: the
// connection, enabling and disabling, the attributes, indicators and
// typematic parameters of the keyboard and the attributes of the mouse.
//

static NTSTATUS
UsbInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
{
    PDEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Size;

    Irp->IoStatus.Information = 0;
    switch (Stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_INTERNAL_KEYBOARD_CONNECT:
        Status = UsbConnect(&Extension->Keyboard, &Extension->KeyboardQueue, Stack);
        break;

    case IOCTL_INTERNAL_MOUSE_CONNECT:
        Status = UsbConnect(&Extension->Mouse, &Extension->MouseQueue, Stack);
        break;

    case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        Status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_INTERNAL_KEYBOARD_ENABLE:
        ExInterlockedIncrementLong(&Extension->KeyboardQueue.EnableCount, &Extension->KeyboardQueue.Lock);
        break;

    case IOCTL_INTERNAL_MOUSE_ENABLE:
        ExInterlockedIncrementLong(&Extension->MouseQueue.EnableCount, &Extension->MouseQueue.Lock);
        break;

    case IOCTL_INTERNAL_KEYBOARD_DISABLE:
        Status = UsbDisable(&Extension->KeyboardQueue);
        break;

    case IOCTL_INTERNAL_MOUSE_DISABLE:
        Status = UsbDisable(&Extension->MouseQueue);
        break;

    case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
        if (OutputLength < sizeof(KEYBOARD_ATTRIBUTES)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PKEYBOARD_ATTRIBUTES)Buffer = Extension->KeyboardAttributes;
            Irp->IoStatus.Information = sizeof(KEYBOARD_ATTRIBUTES);
        }

        break;

    case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
        Size = sizeof(KEYBOARD_INDICATOR_TRANSLATION) + sizeof(IndicatorList) - sizeof(INDICATOR_LIST);
        if (OutputLength < Size) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            ((PKEYBOARD_INDICATOR_TRANSLATION)Buffer)->NumberOfIndicatorKeys =
                sizeof(IndicatorList) / sizeof(IndicatorList[0]);
            RtlMoveMemory(((PKEYBOARD_INDICATOR_TRANSLATION)Buffer)->IndicatorList,
                          (PVOID)IndicatorList, sizeof(IndicatorList));
            Irp->IoStatus.Information = Size;
        }

        break;

    case IOCTL_KEYBOARD_QUERY_INDICATORS:
        if (OutputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PKEYBOARD_INDICATOR_PARAMETERS)Buffer = Extension->Indicators;
            Irp->IoStatus.Information = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
        }

        break;

    case IOCTL_KEYBOARD_SET_INDICATORS:
        if (InputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS) ||
            (((PKEYBOARD_INDICATOR_PARAMETERS)Buffer)->LedFlags &
             ~(KEYBOARD_SCROLL_LOCK_ON | KEYBOARD_NUM_LOCK_ON | KEYBOARD_CAPS_LOCK_ON)) != 0) {
            Status = STATUS_INVALID_PARAMETER;
        } else {
            Extension->Indicators = *(PKEYBOARD_INDICATOR_PARAMETERS)Buffer;
            Extension->IndicatorsChanged = TRUE;
        }

        break;

    case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
        if (OutputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer = Extension->Typematic;
            Irp->IoStatus.Information = sizeof(KEYBOARD_TYPEMATIC_PARAMETERS);
        }

        break;

    case IOCTL_KEYBOARD_SET_TYPEMATIC:
        if (InputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS) ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Rate < Extension->KeyboardAttributes.KeyRepeatMinimum.Rate ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Rate > Extension->KeyboardAttributes.KeyRepeatMaximum.Rate ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Delay < Extension->KeyboardAttributes.KeyRepeatMinimum.Delay ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Delay > Extension->KeyboardAttributes.KeyRepeatMaximum.Delay) {
            Status = STATUS_INVALID_PARAMETER;
        } else {
            Extension->Typematic = *(PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer;
        }

        break;

    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
        if (OutputLength < sizeof(MOUSE_ATTRIBUTES)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PMOUSE_ATTRIBUTES)Buffer = Extension->MouseAttributes;
            Irp->IoStatus.Information = sizeof(MOUSE_ATTRIBUTES);
        }

        break;

    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

//
// The resources of the keyboard controller in the hardware description: its
// interrupt, its registers and the mailbox.
//

typedef struct _CONFIGURATION {
    BOOLEAN Found;
    ULONG BusNumber;
    PHYSICAL_ADDRESS Memory[2];
    ULONG MemoryLength[2];
} CONFIGURATION, *PCONFIGURATION;

static NTSTATUS
UsbConfigurationCallout(
    IN PVOID Context,
    IN PUNICODE_STRING PathName,
    IN INTERFACE_TYPE BusType,
    IN ULONG BusNumber,
    IN PKEY_VALUE_FULL_INFORMATION *BusInformation,
    IN CONFIGURATION_TYPE ControllerType,
    IN ULONG ControllerNumber,
    IN PKEY_VALUE_FULL_INFORMATION *ControllerInformation,
    IN CONFIGURATION_TYPE PeripheralType,
    IN ULONG PeripheralNumber,
    IN PKEY_VALUE_FULL_INFORMATION *PeripheralInformation
    )
{
    PCONFIGURATION Configuration = Context;
    PKEY_VALUE_FULL_INFORMATION Data = ControllerInformation[IoQueryDeviceConfigurationData];
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG Index;
    ULONG Memory = 0;

    if (Configuration->Found || Data == NULL || Data->DataLength == 0) {
        return STATUS_SUCCESS;
    }

    Full = (PCM_FULL_RESOURCE_DESCRIPTOR)((PUCHAR)Data + Data->DataOffset);
    Partial = Full->PartialResourceList.PartialDescriptors;
    for (Index = 0; Index < Full->PartialResourceList.Count; Index += 1, Partial += 1) {
        if (Partial->Type == CmResourceTypeMemory && Memory < 2) {
            Configuration->Memory[Memory] = Partial->u.Memory.Start;
            Configuration->MemoryLength[Memory] = Partial->u.Memory.Length;
            Memory += 1;
        }
    }

    if (Memory == 2) {
        Configuration->Found = TRUE;
        Configuration->BusNumber = BusNumber;
    }

    return STATUS_SUCCESS;
}

static PVOID
UsbMapRegisters(
    IN ULONG BusNumber,
    IN PHYSICAL_ADDRESS Address,
    IN ULONG Length
    )
{
    PHYSICAL_ADDRESS Translated;
    ULONG AddressSpace = 0;

    if (!HalTranslateBusAddress(Internal, BusNumber, Address, &AddressSpace, &Translated)) {
        return NULL;
    }

    return MmMapIoSpace(Translated, Length, FALSE);
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine finds the keyboard controller in the hardware description,
    maps it, brings up the USB host and finds the keyboard and the mouse,
    then creates the keyboard port device, names it the pointer port as well
    if there is a mouse, records the ports in the device map for the class
    drivers and starts the thread that polls the keyboard and the mouse.

Arguments:

    DriverObject - Supplies the driver object.

    RegistryPath - Supplies the service key of the driver.

Return Value:

    The status of the initialization.

--*/

{
    CONFIGURATION Configuration;
    INTERFACE_TYPE Interface = Internal;
    CONFIGURATION_TYPE ControllerType = KeyboardController;
    CONFIGURATION_TYPE PeripheralType = KeyboardPeripheral;
    DEVICE_DESCRIPTION Description;
    PADAPTER_OBJECT Adapter;
    PHYSICAL_ADDRESS Logical;
    ULONG MapRegisters;
    UNICODE_STRING DeviceName;
    UNICODE_STRING PointerName;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_EXTENSION Extension;
    NTSTATUS Status;

    RtlZeroMemory(&Configuration, sizeof(Configuration));
    IoQueryDeviceDescription(&Interface, NULL, &ControllerType, NULL, &PeripheralType, NULL,
                             UsbConfigurationCallout, &Configuration);
    if (!Configuration.Found) {
        return STATUS_NO_SUCH_DEVICE;
    }

    RtlInitUnicodeString(&DeviceName, DD_KEYBOARD_PORT_DEVICE_NAME_U L"0");
    Status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), &DeviceName,
                            FILE_DEVICE_KEYBOARD, 0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(DEVICE_EXTENSION));
    Extension->DeviceObject = DeviceObject;

    Extension->Registers = UsbMapRegisters(Configuration.BusNumber, Configuration.Memory[0],
                                           Configuration.MemoryLength[0]);
    Extension->Mailbox = UsbMapRegisters(Configuration.BusNumber, Configuration.Memory[1],
                                         Configuration.MemoryLength[1]);

    RtlZeroMemory(&Description, sizeof(Description));
    Description.Version = DEVICE_DESCRIPTION_VERSION;
    Description.Master = TRUE;
    Description.Dma32BitAddresses = TRUE;
    Description.InterfaceType = Internal;
    Description.BusNumber = Configuration.BusNumber;
    Description.MaximumLength = COMMON_BUFFER_SIZE;
    Adapter = HalGetAdapter(&Description, &MapRegisters);
    if (Adapter != NULL) {
        Extension->Buffer = HalAllocateCommonBuffer(Adapter, COMMON_BUFFER_SIZE, &Logical, FALSE);
        Extension->BufferAddress = Logical.LowPart;
    }

    if (Extension->Registers == NULL || Extension->Mailbox == NULL || Extension->Buffer == NULL ||
        !UsbInitializeQueue(&Extension->KeyboardQueue, sizeof(KEYBOARD_INPUT_DATA)) ||
        !UsbInitializeQueue(&Extension->MouseQueue, sizeof(MOUSE_INPUT_DATA))) {
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->KeyboardAttributes.KeyboardIdentifier.Type = KEYBOARD_TYPE_ENHANCED;
    Extension->KeyboardAttributes.KeyboardMode = 1;
    Extension->KeyboardAttributes.NumberOfFunctionKeys = 12;
    Extension->KeyboardAttributes.NumberOfIndicators = sizeof(IndicatorList) / sizeof(IndicatorList[0]);
    Extension->KeyboardAttributes.NumberOfKeysTotal = 101;
    Extension->KeyboardAttributes.InputDataQueueLength = INPUT_QUEUE_LENGTH * sizeof(KEYBOARD_INPUT_DATA);
    Extension->KeyboardAttributes.KeyRepeatMinimum.Rate = 2;
    Extension->KeyboardAttributes.KeyRepeatMinimum.Delay = 250;
    Extension->KeyboardAttributes.KeyRepeatMaximum.Rate = 30;
    Extension->KeyboardAttributes.KeyRepeatMaximum.Delay = 1000;
    Extension->Typematic.Rate = 30;
    Extension->Typematic.Delay = 250;

    Extension->MouseAttributes.MouseIdentifier = MOUSE_HID_HARDWARE;
    Extension->MouseAttributes.NumberOfButtons = MOUSE_BUTTONS;
    Extension->MouseAttributes.SampleRate = 1000 / POLL_INTERVAL_MS;
    Extension->MouseAttributes.InputDataQueueLength = INPUT_QUEUE_LENGTH * sizeof(MOUSE_INPUT_DATA);

    if (!UsbInitialize(Extension)) {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = UsbOpenClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = UsbOpenClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = UsbInternalDeviceControl;

    if (Extension->Keyboard.Present) {
        RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, DD_KEYBOARD_PORT_BASE_NAME_U,
                              DeviceName.Buffer, REG_SZ, RegistryPath->Buffer,
                              RegistryPath->Length + sizeof(UNICODE_NULL));
    }

    RtlInitUnicodeString(&PointerName, DD_POINTER_PORT_DEVICE_NAME_U L"0");
    if (Extension->Mouse.Present && NT_SUCCESS(IoCreateSymbolicLink(&PointerName, &DeviceName))) {
        RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, DD_POINTER_PORT_BASE_NAME_U,
                              PointerName.Buffer, REG_SZ, RegistryPath->Buffer,
                              RegistryPath->Length + sizeof(UNICODE_NULL));
    }

    return PsCreateSystemThread(&Extension->Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                UsbPollThread, Extension);
}
