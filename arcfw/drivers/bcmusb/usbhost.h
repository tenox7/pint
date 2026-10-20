/*++

Module Name:

    usbhost.h

Abstract:

    The USB host shared by the firmware's keyboard and the keyboard and
    pointer port driver: the Synopsys DesignWare OTG core of the BCM2835, or
    an xHCI (xhci.c), the hubs and devices behind its root ports, and the
    keyboards and pointers of the HID class among them.

    The user of the host supplies the stall and sleep routines, a free
    running clock and the counts of it in a millisecond, the registers and a
    buffer the host can reach with its bus address, and for an xHCI its
    memory (XHCI_MEMORY_SIZE, page aligned) with its bus address.

--*/

#ifndef _USBHOST_
#define _USBHOST_

#define USB_MAXIMUM_DEVICES     16
#define USB_MAXIMUM_HIDS        8
#define USB_MAXIMUM_DEPTH       5
#define USB_BUFFER_SIZE         1024
#define USB_DESCRIPTOR_SIZE     512
#define USB_REPORT_LENGTH       64
#define USB_KEY_BYTES           32
#define USB_ROOT                0xFF
#define USB_NONE                0xFF

#define SPEED_HIGH              0
#define SPEED_FULL              1
#define SPEED_LOW               2
#define SPEED_SUPER             3

//
// The kinds of input a HID interface gives.
//

#define HID_KEYBOARD            1
#define HID_POINTER             2

//
// The stages of the enumeration of a device, the one it failed at is kept.
//

#define USB_STAGE_READY         0
#define USB_STAGE_DESCRIPTOR    1
#define USB_STAGE_ADDRESS       2
#define USB_STAGE_DEVICE        3
#define USB_STAGE_CONFIGURATION 4
#define USB_STAGE_SET           5
#define USB_STAGE_HUB           6
#define USB_STAGE_RESET         7

//
// The results of a transfer that failed, else its length.
//

#define XFER_NAK                (-1)
#define XFER_STALL              (-2)
#define XFER_ERROR              (-3)

//
// A device behind a root port: on the core its address is its index plus
// one, and a full or low speed device behind a high speed hub is reached
// through the transaction translator of that hub's port; on an xHCI it has
// a device slot, and the controller gives it its address.
//

typedef struct _USB_DEVICE {
    BOOLEAN Present;
    UCHAR Address;
    UCHAR Speed;
    UCHAR Parent;
    UCHAR Port;
    UCHAR Depth;
    UCHAR TtAddress;
    UCHAR TtPort;
    UCHAR Slot;
    UCHAR Class;
    UCHAR Stage;
    UCHAR Kinds;
    USHORT ControlMaximum;
    USHORT Vendor;
    USHORT Product;
    ULONG Interrupt;

    //
    // A hub: its ports and its status change endpoint.
    //

    UCHAR Ports;
    UCHAR HubEndpoint;
    UCHAR HubToggle;
    UCHAR HubInterval;
    USHORT HubMaximum;
} USB_DEVICE, *PUSB_DEVICE;

//
// A field of a report: its bit offset after the report identifier, its size
// and count, its logical range and the usage of its first value.
//

typedef struct _HID_FIELD {
    USHORT Offset;
    UCHAR Size;
    UCHAR Count;
    LONG Minimum;
    LONG Maximum;
    ULONG Usage;
} HID_FIELD, *PHID_FIELD;

//
// An interface of the HID class: its interrupt endpoint, the delay of the
// next read after errors, the counts of its reads (which the boot menu
// shows), and the layout of its keyboard and pointer reports, those of the
// boot protocol or parsed from its report descriptor.
//

typedef struct _USB_HID {
    BOOLEAN Present;
    UCHAR Device;
    UCHAR Interface;
    UCHAR Endpoint;
    USHORT EndpointMaximum;
    UCHAR Toggle;
    UCHAR Kinds;
    BOOLEAN Boot;
    BOOLEAN ReportIds;
    UCHAR Generation;
    UCHAR RetryDelay;
    ULONG RetryTime;
    USHORT Reports;
    USHORT Naks;
    USHORT Failures;
    USHORT Late;
    ULONG LastInterrupt;

    UCHAR KeyboardId;
    HID_FIELD Modifiers;
    HID_FIELD KeyBitmap;
    HID_FIELD KeyArray;
    UCHAR LedId;
    UCHAR LedLength;
    USHORT LedOffset;

    UCHAR PointerId;
    BOOLEAN Absolute;
    HID_FIELD Buttons;
    HID_FIELD X;
    HID_FIELD Y;
} USB_HID, *PUSB_HID;

//
// The state of a pointer from a report: its buttons, and its motion or (an
// absolute pointer) its position scaled to 0 - 0xFFFF.
//

typedef struct _USB_POINTER {
    ULONG Buttons;
    LONG X;
    LONG Y;
    BOOLEAN Absolute;
} USB_POINTER, *PUSB_POINTER;

//
// The xHCI: a ring of transfer request blocks, of the command or event ring
// or of an endpoint of a device slot, with the TRB it fills next (the event
// ring: reads next) and its cycle bit. The transfer an endpoint has queued
// (one at a time) ends at its last TRB, after the data stage TRB of a
// control transfer, whose short packet is reported first; an interrupt
// endpoint's data goes to a buffer of its own.
//

#define XHCI_RING_TRBS          16
#define XHCI_EVENT_TRBS         64
#define XHCI_RINGS              (2 * USB_MAXIMUM_DEVICES + USB_MAXIMUM_HIDS)
#define XHCI_SCRATCHPADS        32
#define XHCI_MEMORY_SIZE        (0x10000 + XHCI_SCRATCHPADS * 0x1000)

typedef struct _XHCI_RING {
    volatile ULONG *Trbs;
    ULONG Address;
    PUCHAR Data;
    ULONG DataAddress;
    USHORT Maximum;
    UCHAR Slot;
    UCHAR Endpoint;
    UCHAR Enqueue;
    UCHAR Cycle;
    BOOLEAN Pending;
    BOOLEAN Done;
    UCHAR Code;
    UCHAR DataCode;
    ULONG Residual;
    ULONG DataResidual;
    ULONG DataTrb;
    ULONG LastTrb;
} XHCI_RING, *PXHCI_RING;

//
// The xHCI: the offsets of its register sets, its root ports (a bit for
// each USB 3 port), its device slots, the size of its contexts, and the
// completion of the last command.
//

typedef struct _XHCI_CONTROLLER {
    ULONG Version;
    ULONG Operational;
    ULONG Runtime;
    ULONG Doorbells;
    ULONG ContextSize;
    ULONG Usb3Ports;
    UCHAR Ports;
    UCHAR Slots;
    UCHAR Scratchpads;
    BOOLEAN CommandDone;
    UCHAR CommandCode;
    UCHAR CommandSlot;
    ULONG CommandTrb;
    XHCI_RING Command;
    XHCI_RING Event;
    XHCI_RING Rings[XHCI_RINGS];
} XHCI_CONTROLLER, *PXHCI_CONTROLLER;

//
// The host. The core: the frame interval it set for the speed of the root
// port, in clocks of the PHY, with the one the core came out of reset with
// and the frames counted in a millisecond with each (which the boot menu
// shows). An xHCI: its memory and the state of its rings.
//

typedef struct _USB_HOST {
    PUCHAR Registers;
    PUCHAR Buffer;
    ULONG BufferAddress;
    ULONG ClockRate;
    BOOLEAN Xhci;
    PUCHAR Memory;
    ULONG MemoryAddress;
    XHCI_CONTROLLER Xhc;
    UCHAR RootSpeed;
    ULONG Interrupt;
    ULONG FrameInterval;
    ULONG ResetFrameInterval;
    UCHAR ResetFrames;
    UCHAR Frames;
    UCHAR Channels;
    USB_DEVICE Devices[USB_MAXIMUM_DEVICES];
    USB_HID Hids[USB_MAXIMUM_HIDS];
    UCHAR Descriptor[USB_DESCRIPTOR_SIZE];
} USB_HOST, *PUSB_HOST;

//
// A pipe: a bulk endpoint of a device on a channel of the core of its own,
// with the toggle of its next transfer, the packets and length of the one
// started, and the interrupts and transfer size it halted with.
//

typedef struct _USB_PIPE {
    UCHAR Device;
    UCHAR Channel;
    UCHAR Endpoint;
    UCHAR Pid;
    BOOLEAN In;
    USHORT Maximum;
    ULONG Character;
    ULONG Packets;
    ULONG Length;
    ULONG Moved;
    ULONG Interrupt;
    ULONG Transfer;
} USB_PIPE, *PUSB_PIPE;

//
// The routines of the user of the host.
//

VOID
UsbStall(
    IN ULONG Microseconds
    );

VOID
UsbSleep(
    IN ULONG Milliseconds
    );

ULONG
UsbClock(
    VOID
    );

//
// The host.
//

BOOLEAN
UsbHostInitialize(
    IN PUSB_HOST Host
    );

VOID
UsbHostScan(
    IN PUSB_HOST Host
    );

LONG
UsbHidRead(
    IN PUSB_HOST Host,
    IN PUSB_HID Hid,
    OUT PUCHAR Report
    );

BOOLEAN
UsbHidKeys(
    IN PUSB_HID Hid,
    IN PUCHAR Report,
    IN ULONG Length,
    OUT PUCHAR Keys
    );

BOOLEAN
UsbHidPointer(
    IN PUSB_HID Hid,
    IN PUCHAR Report,
    IN ULONG Length,
    OUT PUSB_POINTER Pointer
    );

VOID
UsbHidSetIndicators(
    IN PUSB_HOST Host,
    IN PUSB_HID Hid,
    IN ULONG Leds
    );

//
// The function drivers of the devices the host leaves unconfigured.
//

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
    );

BOOLEAN
UsbOpenPipe(
    IN PUSB_HOST Host,
    IN ULONG Index,
    IN ULONG Endpoint,
    IN BOOLEAN In,
    IN ULONG Maximum,
    OUT PUSB_PIPE Pipe
    );

VOID
UsbClosePipe(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe
    );

VOID
UsbStartPipe(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe,
    IN ULONG Address,
    IN ULONG Length
    );

VOID
UsbPipeInterrupt(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe,
    IN BOOLEAN Enable
    );

BOOLEAN
UsbPipeHalted(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe
    );

LONG
UsbPipeResult(
    IN PUSB_HOST Host,
    IN PUSB_PIPE Pipe
    );

#endif // _USBHOST_
