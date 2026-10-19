/*++

Module Name:

    bcmeth.h

Abstract:

    The definitions of the Ethernet miniport driver of the Raspberry Pi: the
    adapter, and the routines of its controllers (chip.c), USB functions that
    the USB host port driver (bcmusb.sys) found.

--*/

#ifndef _BCMETH_
#define _BCMETH_

#include <ndis.h>
#include "bcmusb.h"
#include "bcmethhw.h"

#define BCMETH_NDIS_MAJOR_VERSION   3
#define BCMETH_NDIS_MINOR_VERSION   0

#define ETH_HEADER_SIZE             14
#define ETH_MAXIMUM_FRAME           1514
#define ETH_MINIMUM_FRAME           60
#define BCMETH_MULTICAST_LIST       32

//
// The controllers, and the receive buffer of each: a transfer of whole
// packets, which a controller fills with one frame and its header, or more.
//

#define CHIP_LAN9500                1
#define CHIP_LAN7800                2
#define CHIP_CDC                    3

#define RECEIVE_BUFFER_SIZE         2048
#define TRANSMIT_SLOTS              16
#define TRANSMIT_SLOT_SIZE          1536

//
// The work of the thread, which runs the control transfers the miniport
// cannot run at DISPATCH_LEVEL: a new packet filter or multicast list, a
// stalled pipe, a reset of the controller, and the look at the link.
//

#define WORK_FILTER                 0x0001
#define WORK_RECEIVE_STALL          0x0002
#define WORK_TRANSMIT_STALL         0x0004
#define WORK_RESET                  0x0008

#define LINK_INTERVAL_MS            1000

typedef struct _BCMETH_ADAPTER {
    NDIS_HANDLE MiniportAdapterHandle;
    NDIS_MINIPORT_INTERRUPT Interrupt;
    NDIS_MINIPORT_TIMER RequestTimer;
    NDIS_SPIN_LOCK Lock;

    //
    // The function and its controller: the device, the bulk endpoints, the
    // data interface and its setting of a CDC function, the receive
    // transfer length, the headers of the frames in the transfers and the
    // MAC control register of a LAN9500.
    //

    ULONG Chip;
    ULONG Device;
    UCHAR Configuration;
    UCHAR CommInterface;
    UCHAR DataInterface;
    UCHAR DataSetting;
    UCHAR BulkIn;
    UCHAR BulkOut;
    USHORT BulkInMaximum;
    USHORT BulkOutMaximum;
    ULONG ReceiveLength;
    ULONG ReceiveHeader;
    ULONG TransmitHeader;
    ULONG MacControl;

    UCHAR PermanentAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentAddress[ETH_LENGTH_OF_ADDRESS];

    //
    // The pipes, their buffers in shared memory (uncached), and the frame a
    // receive is indicated from, in cached memory.
    //

    USB_PIPE ReceivePipe;
    USB_PIPE TransmitPipe;
    PUCHAR ReceiveBuffer;
    NDIS_PHYSICAL_ADDRESS ReceivePhysical;
    PUCHAR TransmitBuffer;
    NDIS_PHYSICAL_ADDRESS TransmitPhysical;
    PUCHAR Frame;
    ULONG FrameLength;

    //
    // The receive in progress (what it moved before a NAK ended it early),
    // and the ring of transmit slots: the next to send and to fill, the
    // count filled, the length of each, and a zero length packet owed.
    //

    ULONG ReceiveDone;
    BOOLEAN ReceiveHalted;
    BOOLEAN TransmitHalted;
    BOOLEAN ReceiveStopped;
    BOOLEAN TransmitStopped;
    BOOLEAN TransmitBusy;
    BOOLEAN TransmitZero;
    BOOLEAN ResourcesNeeded;
    ULONG TransmitHead;
    ULONG TransmitTail;
    ULONG TransmitCount;
    ULONG TransmitLength[TRANSMIT_SLOTS];

    //
    // The receive filter, its multicast list and the request that set
    // them, which the thread completes.
    //

    ULONG PacketFilter;
    ULONG MulticastCount;
    UCHAR Multicast[BCMETH_MULTICAST_LIST][ETH_LENGTH_OF_ADDRESS];
    BOOLEAN RequestPending;
    NDIS_STATUS RequestStatus;

    //
    // The thread and its work, and the link.
    //

    PVOID Thread;
    KEVENT WorkEvent;
    ULONG Work;
    BOOLEAN Halting;
    BOOLEAN LinkUp;
    BOOLEAN FullDuplex;
    ULONG LinkSpeed;

    //
    // Restarts of stalled pipes, which the thread asks the timer for once it
    // cleared their halts, and what a transmit moved before a NAK.
    //

    BOOLEAN ReceiveRestart;
    BOOLEAN TransmitRestart;
    BOOLEAN Resetting;
    BOOLEAN ResetDone;
    NDIS_STATUS ResetStatus;
    ULONG HangChecks;
    ULONG LastTransmits;
    ULONG TransmitDone;
    PUCHAR TransmitFrame;
    PUCHAR FrameBuffer;

    //
    // The statistics.
    //

    ULONG TransmitOk;
    ULONG ReceiveOk;
    ULONG TransmitError;
    ULONG ReceiveError;
    ULONG ReceiveNoBuffer;
    ULONG ReceiveCrcError;

    //
    // The last register access of the controller (its index, the value read
    // or written and the result of the transfer), which the error log entry
    // of an adapter that failed to start names.
    //

    ULONG LastRegister;
    ULONG LastValue;
    LONG LastResult;
} BCMETH_ADAPTER, *PBCMETH_ADAPTER;

//
// The controllers (chip.c).
//

NDIS_STATUS
ChipFind(
    IN PBCMETH_ADAPTER Adapter
    );

NDIS_STATUS
ChipStart(
    IN PBCMETH_ADAPTER Adapter
    );

VOID
ChipStop(
    IN PBCMETH_ADAPTER Adapter
    );

VOID
ChipSetFilter(
    IN PBCMETH_ADAPTER Adapter
    );

VOID
ChipCheckLink(
    IN PBCMETH_ADAPTER Adapter
    );

VOID
ChipClearHalt(
    IN PBCMETH_ADAPTER Adapter,
    IN PUSB_PIPE Pipe
    );

ULONG
ChipFrameHeader(
    IN PBCMETH_ADAPTER Adapter,
    IN PUCHAR Header,
    IN ULONG Length
    );

ULONG
ChipNextFrame(
    IN PBCMETH_ADAPTER Adapter,
    IN PUCHAR Buffer,
    IN ULONG Offset,
    IN ULONG Length,
    OUT PULONG FrameOffset,
    OUT PULONG FrameLength,
    OUT PBOOLEAN Error
    );

extern NDIS_PHYSICAL_ADDRESS HighestAcceptableMax;

#endif // _BCMETH_
