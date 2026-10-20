/*++

Module Name:

    xhci.h

Abstract:

    The routines of the xHCI (xhci.c) the USB host (usbhost.c) runs its
    transfers and root ports with when its host is one.

--*/

#ifndef _XHCI_
#define _XHCI_

//
// The features, status and changes of a hub port, which those of a root
// port of an xHCI have the form of, with a status bit of its own for a
// super speed device.
//

#define PORT_ENABLE             1
#define PORT_RESET              4
#define PORT_POWER              8
#define C_PORT_CONNECTION       16
#define PORT_STATUS_CONNECTION  0x0001
#define PORT_STATUS_ENABLE      0x0002
#define PORT_STATUS_RESET       0x0010
#define PORT_STATUS_POWER       0x0100
#define PORT_STATUS_LOW_SPEED   0x0200
#define PORT_STATUS_HIGH_SPEED  0x0400
#define PORT_STATUS_SUPER_SPEED 0x8000
#define PORT_CHANGE_CONNECTION  0x0001
#define PORT_CHANGE_ENABLE      0x0002
#define PORT_CHANGE_OVER_CURRENT 0x0008
#define PORT_CHANGE_RESET       0x0010

BOOLEAN
XhciInitialize(
    IN PUSB_HOST Host
    );

BOOLEAN
XhciPortStatus(
    IN PUSB_HOST Host,
    IN ULONG Port,
    OUT PULONG Status,
    OUT PULONG Change
    );

VOID
XhciPortFeature(
    IN PUSB_HOST Host,
    IN ULONG Port,
    IN BOOLEAN Set,
    IN ULONG Feature
    );

BOOLEAN
XhciAddressDevice(
    IN PUSB_HOST Host,
    IN ULONG Index
    );

BOOLEAN
XhciSetControlMaximum(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device
    );

BOOLEAN
XhciConfigureEndpoint(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint,
    IN ULONG Maximum,
    IN ULONG Interval
    );

BOOLEAN
XhciConfigureHub(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Hub,
    IN ULONG ThinkTime
    );

VOID
XhciRemoveDevice(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device
    );

LONG
XhciControl(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN PUCHAR Setup,
    IN OUT PVOID Data,
    IN ULONG Length
    );

LONG
XhciInterrupt(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint,
    OUT PUCHAR Buffer,
    IN ULONG Length
    );

#endif // _XHCI_
