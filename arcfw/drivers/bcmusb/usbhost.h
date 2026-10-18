/*++

Module Name:

    usbhost.h

Abstract:

    The USB host of the BCM2835, shared by the firmware's keyboard and the
    keyboard and pointer port driver: the Synopsys DesignWare OTG core, the
    hubs and devices behind its root port, and the keyboards and pointers of
    the HID class among them.

    The user of the host supplies the stall and sleep routines, a free
    running clock and the counts of it in a millisecond, the registers and a
    buffer the core can reach with its bus address.

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
// A device behind the root port: its address is its index plus one. A full
// or low speed device behind a high speed hub is reached through the
// transaction translator of that hub's port.
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
    UCHAR ControlMaximum;
    UCHAR Class;
    UCHAR Stage;
    UCHAR Kinds;
    USHORT Vendor;
    USHORT Product;
    ULONG Interrupt;

    //
    // A hub: its ports and its status change endpoint.
    //

    UCHAR Ports;
    UCHAR HubEndpoint;
    UCHAR HubToggle;
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
// The host: the frame interval it set for the speed of the root port, in
// clocks of the PHY, with the one the core came out of reset with and the
// frames counted in a millisecond with each (which the boot menu shows).
//

typedef struct _USB_HOST {
    PUCHAR Registers;
    PUCHAR Buffer;
    ULONG BufferAddress;
    ULONG ClockRate;
    UCHAR RootSpeed;
    ULONG Interrupt;
    ULONG FrameInterval;
    ULONG ResetFrameInterval;
    UCHAR ResetFrames;
    UCHAR Frames;
    USB_DEVICE Devices[USB_MAXIMUM_DEVICES];
    USB_HID Hids[USB_MAXIMUM_HIDS];
    UCHAR Descriptor[USB_DESCRIPTOR_SIZE];
} USB_HOST, *PUSB_HOST;

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

#endif // _USBHOST_
