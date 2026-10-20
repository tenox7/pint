/*++

Module Name:

    bcmusb.h

Abstract:

    The interface of the USB host port driver (bcmusb.sys) to the drivers of
    the USB functions it leaves unconfigured, such as the Ethernet controller
    on the board: the functions, control transfers to them, which the port
    driver serializes with its own, and pipes, the bulk endpoints of a
    function on channels of their own that interrupt the function driver.

    The function, control and open and close routines are called at
    PASSIVE_LEVEL; the pipe routines at any level, a pipe by one caller at a
    time.

--*/

#ifndef _BCMUSB_
#define _BCMUSB_

#include "usbhost.h"

//
// A function: its device (the index the other routines take), its vendor
// and product, the class of its device descriptor and its speed.
//

typedef struct _USB_FUNCTION {
    ULONG Device;
    USHORT Vendor;
    USHORT Product;
    UCHAR Class;
    UCHAR Speed;
} USB_FUNCTION, *PUSB_FUNCTION;

BOOLEAN
UsbGetFunction(
    IN ULONG Index,
    OUT PUSB_FUNCTION Function
    );

LONG
UsbFunctionControl(
    IN ULONG Device,
    IN ULONG RequestType,
    IN ULONG Request,
    IN ULONG Value,
    IN ULONG Interface,
    IN OUT PVOID Data,
    IN ULONG Length
    );

BOOLEAN
UsbFunctionOpenPipe(
    IN ULONG Device,
    IN ULONG Endpoint,
    IN BOOLEAN In,
    IN ULONG Maximum,
    OUT PUSB_PIPE Pipe
    );

VOID
UsbFunctionClosePipe(
    IN PUSB_PIPE Pipe
    );

VOID
UsbFunctionStartPipe(
    IN PUSB_PIPE Pipe,
    IN ULONG Address,
    IN ULONG Length
    );

VOID
UsbFunctionPipeInterrupt(
    IN PUSB_PIPE Pipe,
    IN BOOLEAN Enable
    );

BOOLEAN
UsbFunctionPipeHalted(
    IN PUSB_PIPE Pipe
    );

LONG
UsbFunctionPipeResult(
    IN PUSB_PIPE Pipe
    );

#endif // _BCMUSB_
