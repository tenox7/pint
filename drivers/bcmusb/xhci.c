/*++

Module Name:

    xhci.c

Abstract:

    This module implements the eXtensible Host Controller of the USB host
    (usbhost.c): on the Raspberry Pi 4 Model B and 400 the VIA VL805 behind
    the PCI Express link of the BCM2711, the host of their USB-A ports.

    The controller runs the transfers itself, from rings of transfer request
    blocks (TRBs) in memory, a ring for each endpoint in use, and reports
    their completions on the event ring, which is polled. The commands that
    give a device a slot and its address, and that configure its endpoints,
    go on the command ring. A low or full speed device behind a high speed
    hub reaches the transaction translator of the hub through its slot
    context; the controller keeps the data toggles and repeats the
    transactions a device NAKs. A transfer that does not complete in time is
    stopped, an endpoint that halted on an error is reset, and the transfers
    left on its ring are skipped.

    An interrupt endpoint, once read, has a transfer queued at all times: a
    read takes the report of the one that completed and queues the next,
    else it finds none, as a NAK.

    The root ports look like the ports of a hub to the host: their status
    has the form of a hub port's, and the features of a hub port (reset,
    enable, power) are set and cleared on them.

    The memory of the controller (XHCI_MEMORY_SIZE, page aligned) holds the
    structures below. The VL805 reads up to four TRBs past the end of a ring
    and may use them later without reading them again (Linux: TRB overfetch):
    64 bytes after each ring that fetches TRBs belong to no ring.

        0x00000     the device context base address array (slots 0 - 16)
        0x00100     the event ring segment table (one segment)
        0x00140     the scratchpad buffer array
        0x00400     the command ring
        0x00540     the event ring
        0x00980     the input context
        0x01200     the transfer rings
        0x04400     the buffers of the interrupt endpoints
        0x05000     the device contexts, one per slot
        0x10000     the scratchpad buffers, a page each

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

#define MEMORY_DCBAA            0x00000
#define MEMORY_ERST             0x00100
#define MEMORY_SCRATCHPAD_ARRAY 0x00140
#define MEMORY_COMMAND          0x00400
#define MEMORY_EVENT            0x00540
#define MEMORY_INPUT            0x00980
#define MEMORY_RINGS            0x01200
#define MEMORY_DATA             (MEMORY_RINGS + XHCI_RINGS * RING_STRIDE)
#define MEMORY_CONTEXTS         0x05000
#define MEMORY_SCRATCHPADS      0x10000
#define RING_DATA_SIZE          64
#define RING_STRIDE             (XHCI_RING_TRBS * 16 + OVERFETCH)
#define OVERFETCH               64
#define PAGE                    0x1000

//
// Capability, operational and runtime registers, and their fields.
//

#define CAPLENGTH               0x00
#define HCSPARAMS1              0x04
#define HCSPARAMS2              0x08
#define HCCPARAMS1              0x10
#define DBOFF                   0x14
#define RTSOFF                  0x18

#define USBCMD                  0x00
#define USBSTS                  0x04
#define PAGESIZE                0x08
#define DNCTRL                  0x14
#define CRCR                    0x18
#define DCBAAP                  0x30
#define CONFIG                  0x38
#define PORTSC(Port)            (0x400 + ((Port) - 1) * 0x10)

#define IMAN                    0x20
#define ERSTSZ                  0x28
#define ERSTBA                  0x30
#define ERDP                    0x38

#define HCCPARAMS1_CSZ          0x00000004
#define HCCPARAMS1_PPC          0x00000008
#define USBCMD_RUN              0x00000001
#define USBCMD_RESET            0x00000002
#define USBSTS_HALTED           0x00000001
#define USBSTS_NOT_READY        0x00000800
#define PAGESIZE_4K             0x00000001
#define CRCR_CYCLE              0x00000001
#define CRCR_ABORT              0x00000004
#define IMAN_PENDING            0x00000001
#define ERDP_BUSY               0x00000008

#define PORTSC_CCS              0x00000001
#define PORTSC_PED              0x00000002
#define PORTSC_PR               0x00000010
#define PORTSC_PP               0x00000200
#define PORTSC_SPEED_SHIFT      10
#define PORTSC_SPEED_MASK       0x00003C00
#define PORTSC_CSC              0x00020000
#define PORTSC_PEC              0x00040000
#define PORTSC_WRC              0x00080000
#define PORTSC_OCC              0x00100000
#define PORTSC_PRC              0x00200000
#define PORTSC_CHANGES          0x00FE0000
#define PORTSC_KEEP             0x0E00C3E0

#define PORT_SPEED_LOW          2
#define PORT_SPEED_HIGH         3
#define PORT_SPEED_SUPER        4

#define XECP_LEGACY             1
#define LEGACY_BIOS_OWNED       0x00010000
#define LEGACY_OS_OWNED         0x01000000
#define LEGACY_SMI_STATUS       0xE0000000

//
// Transfer request blocks, and the completion codes of their events.
//

#define TRB_CYCLE               0x00000001
#define TRB_TOGGLE              0x00000002
#define TRB_ISP                 0x00000004
#define TRB_CHAIN               0x00000010
#define TRB_IOC                 0x00000020
#define TRB_IDT                 0x00000040
#define TRB_DIR_IN              0x00010000
#define TRB_TRT_OUT             0x00020000
#define TRB_TRT_IN              0x00030000
#define TRB_TYPE(Type)          ((ULONG)(Type) << 10)
#define TRB_ENDPOINT(Index)     ((ULONG)(Index) << 16)
#define TRB_SLOT(Slot)          ((ULONG)(Slot) << 24)

#define TYPE_NORMAL             1
#define TYPE_SETUP              2
#define TYPE_DATA               3
#define TYPE_STATUS             4
#define TYPE_LINK               6
#define TYPE_ENABLE_SLOT        9
#define TYPE_DISABLE_SLOT       10
#define TYPE_ADDRESS_DEVICE     11
#define TYPE_CONFIGURE_ENDPOINT 12
#define TYPE_EVALUATE_CONTEXT   13
#define TYPE_RESET_ENDPOINT     14
#define TYPE_STOP_ENDPOINT      15
#define TYPE_SET_DEQUEUE        16
#define TYPE_TRANSFER_EVENT     32
#define TYPE_COMMAND_COMPLETION 33

#define CODE_TIMEOUT            0
#define CODE_SUCCESS            1
#define CODE_STALL              6
#define CODE_SHORT_PACKET       13

//
// Slot and endpoint contexts.
//

#define SLOT_ENTRIES_SHIFT      27
#define SLOT_SPEED_SHIFT        20
#define SLOT_HUB                0x04000000
#define SLOT_MTT                0x02000000
#define SLOT_PORTS_SHIFT        24
#define SLOT_ROOT_PORT_SHIFT    16
#define SLOT_TT_PORT_SHIFT      8
#define SLOT_THINK_TIME_SHIFT   16
#define SLOT_THINK_TIME_MASK    0x00030000

#define ENDPOINT_STATE_MASK     7
#define ENDPOINT_RUNNING        1
#define ENDPOINT_HALTED         2
#define ENDPOINT_INTERVAL_SHIFT 16
#define ENDPOINT_ERRORS         (3 << 1)
#define ENDPOINT_TYPE_SHIFT     3
#define ENDPOINT_CONTROL        4
#define ENDPOINT_INTERRUPT_IN   7
#define ENDPOINT_MAXIMUM_SHIFT  16

#define CONTROL_ENDPOINT        1

#define COMMAND_TIMEOUT_MS      2000
#define ABORT_TIMEOUT_MS        100
#define CONTROL_TIMEOUT_MS      1000
#define HALT_TIMEOUT_MS         100
#define RESET_TIMEOUT_MS        1000
#define CONNECT_TIMEOUT_MS      1000
#define POWER_ON_MS             100

//
// The speeds of the slot context (and of the root ports), by SPEED_.
//

static const UCHAR XhciSpeeds[] = {3, 1, 2, 4};

static BOOLEAN
XhciWaitRegister(
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

//
// The contexts: of the device context of a slot, its slot context (0) or
// the context of an endpoint by its index (1 is the control endpoint); of
// the input context, the input control context (0), the slot context (1)
// or an endpoint's (its index plus one).
//

static volatile ULONG *
XhciDeviceContext(
    IN PUSB_HOST Host,
    IN ULONG Slot,
    IN ULONG Index
    )
{
    return (volatile ULONG *)(Host->Memory + MEMORY_CONTEXTS + ((Slot - 1) * 32 + Index) * Host->Xhc.ContextSize);
}

static PULONG
XhciInputContext(
    IN PUSB_HOST Host,
    IN ULONG Index
    )
{
    return (PULONG)(Host->Memory + MEMORY_INPUT + Index * Host->Xhc.ContextSize);
}

static VOID
XhciClearInput(
    IN PUSB_HOST Host
    )
{
    RtlZeroMemory(Host->Memory + MEMORY_INPUT, 33 * Host->Xhc.ContextSize);
}

//
// The rings: that of an endpoint of a slot, a free one given to one (cleared,
// with its first TRB to fill and the cycle bit 1), and the TRBs put on one. A
// TRB is handed to the controller by its cycle bit, written last; the first
// TRB of a transfer of several is held back (its cycle bit is the wrong one)
// until the others are on the ring. The last TRB of the ring links back to
// its first and toggles the cycle bit.
//

static PXHCI_RING
XhciFindRing(
    IN PUSB_HOST Host,
    IN ULONG Slot,
    IN ULONG Endpoint
    )
{
    ULONG Index;

    for (Index = 0; Index < XHCI_RINGS; Index += 1) {
        if (Host->Xhc.Rings[Index].Slot == Slot && Host->Xhc.Rings[Index].Endpoint == Endpoint) {
            return &Host->Xhc.Rings[Index];
        }
    }

    return NULL;
}

static VOID
XhciClearRing(
    IN PXHCI_RING Ring,
    IN ULONG Trbs
    )
{
    ULONG Index;

    for (Index = 0; Index < Trbs * 4; Index += 1) {
        Ring->Trbs[Index] = 0;
    }

    Ring->Enqueue = 0;
    Ring->Cycle = 1;
    Ring->Pending = FALSE;
    Ring->Done = FALSE;
}

static PXHCI_RING
XhciAllocateRing(
    IN PUSB_HOST Host,
    IN ULONG Slot,
    IN ULONG Endpoint,
    IN ULONG Maximum
    )
{
    PXHCI_RING Ring = XhciFindRing(Host, Slot, Endpoint);

    if (Ring == NULL) {
        Ring = XhciFindRing(Host, 0, 0);
    }

    if (Ring == NULL) {
        return NULL;
    }

    XhciClearRing(Ring, XHCI_RING_TRBS);
    Ring->Slot = (UCHAR)Slot;
    Ring->Endpoint = (UCHAR)Endpoint;
    Ring->Maximum = (USHORT)Maximum;
    return Ring;
}

static ULONG
XhciQueue(
    IN PXHCI_RING Ring,
    IN ULONG Parameter0,
    IN ULONG Parameter1,
    IN ULONG Status,
    IN ULONG Control,
    IN BOOLEAN Hold
    )
{
    volatile ULONG *Trb = Ring->Trbs + Ring->Enqueue * 4;
    ULONG Address = Ring->Address + Ring->Enqueue * 16;

    Trb[0] = Parameter0;
    Trb[1] = Parameter1;
    Trb[2] = Status;
    MEMORY_BARRIER();
    Trb[3] = Control | (Hold ? Ring->Cycle ^ 1 : Ring->Cycle);
    Ring->Enqueue += 1;
    if (Ring->Enqueue == XHCI_RING_TRBS - 1) {
        Trb = Ring->Trbs + Ring->Enqueue * 4;
        Trb[0] = Ring->Address;
        Trb[1] = 0;
        Trb[2] = 0;
        MEMORY_BARRIER();
        Trb[3] = TRB_TYPE(TYPE_LINK) | TRB_TOGGLE | (Control & TRB_CHAIN) | Ring->Cycle;
        Ring->Enqueue = 0;
        Ring->Cycle ^= 1;
    }

    return Address;
}

static VOID
XhciRelease(
    IN PXHCI_RING Ring,
    IN ULONG Address
    )
{
    volatile ULONG *Trb = Ring->Trbs + (Address - Ring->Address) / 4;

    MEMORY_BARRIER();
    Trb[3] ^= TRB_CYCLE;
}

static VOID
XhciDoorbell(
    IN PUSB_HOST Host,
    IN ULONG Slot,
    IN ULONG Target
    )
{
    MEMORY_BARRIER();
    WRITE_REG(Host, Host->Xhc.Doorbells + Slot * 4, Target);
}

//
// The events: a command completes, or a transfer of an endpoint (a short
// packet of the data stage of a control transfer comes before the end of
// the transfer, an error ends it at any TRB). The controller is told the
// events read.
//

static VOID
XhciTransferEvent(
    IN PUSB_HOST Host,
    IN ULONG Control,
    IN ULONG Pointer,
    IN ULONG Status
    )
{
    PXHCI_RING Ring = XhciFindRing(Host, Control >> 24, (Control >> 16) & 0x1F);
    UCHAR Code = (UCHAR)(Status >> 24);

    if (Ring == NULL || !Ring->Pending || Ring->Done) {
        return;
    }

    if (Pointer == Ring->DataTrb && Code == CODE_SHORT_PACKET) {
        Ring->DataCode = Code;
        Ring->DataResidual = Status & 0xFFFFFF;
        return;
    }

    if (Pointer != Ring->LastTrb && (Code == CODE_SUCCESS || Code == CODE_SHORT_PACKET)) {
        return;
    }

    Ring->Code = Code;
    Ring->Residual = Status & 0xFFFFFF;
    Ring->Done = TRUE;
}

static VOID
XhciEvents(
    IN PUSB_HOST Host
    )
{
    PXHCI_CONTROLLER Xhc = &Host->Xhc;
    PXHCI_RING Event = &Xhc->Event;
    volatile ULONG *Trb;
    ULONG Control;
    BOOLEAN Read = FALSE;

    for (;;) {
        Trb = Event->Trbs + Event->Enqueue * 4;
        Control = Trb[3];
        if ((Control & TRB_CYCLE) != Event->Cycle) {
            break;
        }

        MEMORY_BARRIER();
        switch ((Control >> 10) & 0x3F) {
        case TYPE_COMMAND_COMPLETION:
            if (Trb[0] == Xhc->CommandTrb) {
                Xhc->CommandCode = (UCHAR)(Trb[2] >> 24);
                Xhc->CommandSlot = (UCHAR)(Control >> 24);
                Xhc->CommandDone = TRUE;
            }

            break;

        case TYPE_TRANSFER_EVENT:
            XhciTransferEvent(Host, Control, Trb[0], Trb[2]);
            break;
        }

        Event->Enqueue += 1;
        if (Event->Enqueue == XHCI_EVENT_TRBS) {
            Event->Enqueue = 0;
            Event->Cycle ^= 1;
        }

        Read = TRUE;
    }

    if (Read) {
        WRITE_REG(Host, Xhc->Runtime + ERDP, (Event->Address + Event->Enqueue * 16) | ERDP_BUSY);
        WRITE_REG(Host, Xhc->Runtime + ERDP + 4, 0);
    }
}

//
// Poll the events until a flag is set or a deadline of the clock passes: a
// short spin covers a quick completion, then the thread sleeps between
// looks.
//

static BOOLEAN
XhciWait(
    IN PUSB_HOST Host,
    IN PBOOLEAN Flag,
    IN ULONG Limit
    )
{
    ULONG Start = UsbClock();
    ULONG Now;

    for (;;) {
        XhciEvents(Host);
        if (*Flag) {
            return TRUE;
        }

        Now = UsbClock();
        if ((LONG)(Now - Limit) >= 0) {
            return FALSE;
        }

        if (Now - Start >= Host->ClockRate) {
            UsbSleep(1);
        }
    }
}

//
// Run a command and wait for its completion: a command still running at the
// deadline is aborted. A command that fails leaves its type and completion
// code for the list of the devices. Returns the completion code.
//

static UCHAR
XhciCommand(
    IN PUSB_HOST Host,
    IN ULONG Parameter,
    IN ULONG Control
    )
{
    PXHCI_CONTROLLER Xhc = &Host->Xhc;

    Xhc->CommandDone = FALSE;
    Xhc->CommandSlot = 0;
    Xhc->CommandTrb = XhciQueue(&Xhc->Command, Parameter, 0, 0, Control, FALSE);
    XhciDoorbell(Host, 0, 0);
    if (!XhciWait(Host, &Xhc->CommandDone, UsbClock() + COMMAND_TIMEOUT_MS * Host->ClockRate)) {
        WRITE_REG(Host, Xhc->Operational + CRCR, CRCR_ABORT);
        WRITE_REG(Host, Xhc->Operational + CRCR + 4, 0);
        XhciWait(Host, &Xhc->CommandDone, UsbClock() + ABORT_TIMEOUT_MS * Host->ClockRate);
        Xhc->CommandCode = CODE_TIMEOUT;
    }

    if (Xhc->CommandCode != CODE_SUCCESS) {
        Host->Interrupt = (((Control >> 10) & 0x3F) << 8) | Xhc->CommandCode;
    }

    return Xhc->CommandCode;
}

//
// Take the transfers left on the ring of an endpoint off it: a halted
// endpoint is reset, a running one stopped, then the next TRB to fill is
// made the next the controller runs.
//

static VOID
XhciRecover(
    IN PUSB_HOST Host,
    IN PXHCI_RING Ring
    )
{
    ULONG Target = TRB_ENDPOINT(Ring->Endpoint) | TRB_SLOT(Ring->Slot);
    ULONG State = XhciDeviceContext(Host, Ring->Slot, Ring->Endpoint)[0] & ENDPOINT_STATE_MASK;

    if (State == ENDPOINT_HALTED) {
        XhciCommand(Host, 0, TRB_TYPE(TYPE_RESET_ENDPOINT) | Target);
    } else if (State == ENDPOINT_RUNNING) {
        XhciCommand(Host, 0, TRB_TYPE(TYPE_STOP_ENDPOINT) | Target);
    }

    XhciCommand(Host, (Ring->Address + Ring->Enqueue * 16) | Ring->Cycle, TRB_TYPE(TYPE_SET_DEQUEUE) | Target);
    Ring->Pending = FALSE;
    Ring->Done = FALSE;
}

//
// Wait for the transfer queued on a ring to complete, up to a deadline:
// one that did not, or failed, is taken off the ring. Returns its completion
// code, which is left for the list of the devices.
//

static UCHAR
XhciComplete(
    IN PUSB_HOST Host,
    IN PXHCI_RING Ring,
    IN ULONG Limit
    )
{
    UCHAR Code = CODE_TIMEOUT;

    if (XhciWait(Host, &Ring->Done, Limit)) {
        Code = Ring->Code;
    }

    Host->Interrupt = Code;
    if (Code != CODE_SUCCESS && Code != CODE_SHORT_PACKET) {
        XhciRecover(Host, Ring);
    }

    Ring->Pending = FALSE;
    return Code;
}

//
// The route of a device from its root port, through the hubs: the ports at
// each tier in the route string, the root port, and the slot and port of
// the high speed hub whose transaction translator a low or full speed
// device is reached through.
//

static ULONG
XhciRoute(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    OUT PULONG RootPort,
    OUT PULONG TtSlot,
    OUT PULONG TtPort
    )
{
    PUSB_DEVICE Child = Device;
    PUSB_DEVICE Hub;
    ULONG Route = 0;

    *TtSlot = 0;
    *TtPort = 0;
    while (Child->Parent != USB_ROOT) {
        Hub = &Host->Devices[Child->Parent];
        Route |= (ULONG)(Child->Port < 15 ? Child->Port : 15) << (Hub->Depth * 4);
        if (*TtSlot == 0 && Hub->Speed == SPEED_HIGH &&
            (Device->Speed == SPEED_FULL || Device->Speed == SPEED_LOW)) {
            *TtSlot = Hub->Slot;
            *TtPort = Child->Port;
        }

        Child = Hub;
    }

    *RootPort = Child->Port;
    return Route;
}

//
// The input context of a slot to change: its slot context (from the device
// context, with the contexts of the endpoints added counted) and the
// contexts to add.
//

static PULONG
XhciInputSlot(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint
    )
{
    volatile ULONG *Output = XhciDeviceContext(Host, Device->Slot, 0);
    PULONG Slot;
    ULONG Index;

    XhciClearInput(Host);
    XhciInputContext(Host, 0)[1] = 1 | (Endpoint != 0 ? 1 << Endpoint : 0);
    Slot = XhciInputContext(Host, 1);
    for (Index = 0; Index < 3; Index += 1) {
        Slot[Index] = Output[Index];
    }

    if ((Slot[0] >> SLOT_ENTRIES_SHIFT) < Endpoint) {
        Slot[0] = (Slot[0] & ~(0x1F << SLOT_ENTRIES_SHIFT)) | (Endpoint << SLOT_ENTRIES_SHIFT);
    }

    return Slot;
}

BOOLEAN
XhciInitialize(
    IN PUSB_HOST Host
    )

/*++

Routine Description:

    This routine takes the controller from its firmware if it has one, resets
    it, gives it its memory (the device context base address array, the
    scratchpad buffers, the command ring and the event ring), and runs it.
    The root ports are powered if the controller switches their power, and
    the devices on them are given time to connect. Interrupts stay off: the
    events are polled.

Arguments:

    Host - Supplies the host, with the registers and memory of the
        controller.

Return Value:

    TRUE if the controller runs, else FALSE, with the step it failed at in
    Host->Interrupt.

--*/

{
    PXHCI_CONTROLLER Xhc = &Host->Xhc;
    volatile ULONG *Dcbaa = (volatile ULONG *)(Host->Memory + MEMORY_DCBAA);
    volatile ULONG *Table = (volatile ULONG *)(Host->Memory + MEMORY_ERST);
    volatile ULONG *Array = (volatile ULONG *)(Host->Memory + MEMORY_SCRATCHPAD_ARRAY);
    ULONG Capability = READ_REG(Host, CAPLENGTH);
    ULONG Parameters;
    ULONG Scratchpads;
    ULONG Offset;
    ULONG Index;
    ULONG Start;
    PXHCI_RING Ring;

    RtlZeroMemory(Xhc, sizeof(XHCI_CONTROLLER));
    Host->Interrupt = 1;
    if (Capability == 0xFFFFFFFF || (Capability & 0xFF) == 0) {
        return FALSE;
    }

    Xhc->Version = Capability >> 16;
    Xhc->Operational = Capability & 0xFF;
    Xhc->Runtime = READ_REG(Host, RTSOFF) & ~0x1F;
    Xhc->Doorbells = READ_REG(Host, DBOFF) & ~3;
    Parameters = READ_REG(Host, HCSPARAMS1);
    Xhc->Ports = (UCHAR)(Parameters >> 24);
    Xhc->Slots = (UCHAR)((Parameters & 0xFF) < USB_MAXIMUM_DEVICES ? Parameters & 0xFF : USB_MAXIMUM_DEVICES);
    Parameters = READ_REG(Host, HCSPARAMS2);
    Scratchpads = (((Parameters >> 21) & 0x1F) << 5) | (Parameters >> 27);
    Xhc->Scratchpads = (UCHAR)(Scratchpads < 255 ? Scratchpads : 255);
    Parameters = READ_REG(Host, HCCPARAMS1);
    Xhc->ContextSize = (Parameters & HCCPARAMS1_CSZ) ? 64 : 32;
    Host->Interrupt = 2;
    if (Xhc->Ports == 0 || Xhc->Slots == 0 || Scratchpads > XHCI_SCRATCHPADS ||
        (READ_REG(Host, Xhc->Operational + PAGESIZE) & PAGESIZE_4K) == 0) {
        return FALSE;
    }

    //
    // Take the controller from the firmware that owns it, and its SMIs.
    //

    for (Offset = (Parameters >> 16) * 4; Offset != 0; ) {
        Capability = READ_REG(Host, Offset);
        if ((Capability & 0xFF) == XECP_LEGACY && (Capability & LEGACY_BIOS_OWNED)) {
            WRITE_REG(Host, Offset, Capability | LEGACY_OS_OWNED);
            XhciWaitRegister(Host, Offset, LEGACY_BIOS_OWNED, FALSE, RESET_TIMEOUT_MS);
            WRITE_REG(Host, Offset + 4, LEGACY_SMI_STATUS);
        }

        Offset = ((Capability >> 8) & 0xFF) != 0 ? Offset + ((Capability >> 8) & 0xFF) * 4 : 0;
    }

    Host->Interrupt = 3;
    WRITE_REG(Host, Xhc->Operational + USBCMD, READ_REG(Host, Xhc->Operational + USBCMD) & ~USBCMD_RUN);
    if (!XhciWaitRegister(Host, Xhc->Operational + USBSTS, USBSTS_HALTED, TRUE, HALT_TIMEOUT_MS)) {
        return FALSE;
    }

    Host->Interrupt = 4;
    WRITE_REG(Host, Xhc->Operational + USBCMD, USBCMD_RESET);
    UsbSleep(1);
    if (!XhciWaitRegister(Host, Xhc->Operational + USBCMD, USBCMD_RESET, FALSE, RESET_TIMEOUT_MS) ||
        !XhciWaitRegister(Host, Xhc->Operational + USBSTS, USBSTS_NOT_READY, FALSE, RESET_TIMEOUT_MS)) {
        return FALSE;
    }

    RtlZeroMemory(Host->Memory, MEMORY_SCRATCHPADS + Scratchpads * PAGE);
    Xhc->Command.Trbs = (volatile ULONG *)(Host->Memory + MEMORY_COMMAND);
    Xhc->Command.Address = Host->MemoryAddress + MEMORY_COMMAND;
    Xhc->Command.Cycle = 1;
    Xhc->Event.Trbs = (volatile ULONG *)(Host->Memory + MEMORY_EVENT);
    Xhc->Event.Address = Host->MemoryAddress + MEMORY_EVENT;
    Xhc->Event.Cycle = 1;
    for (Index = 0; Index < XHCI_RINGS; Index += 1) {
        Ring = &Xhc->Rings[Index];
        Ring->Trbs = (volatile ULONG *)(Host->Memory + MEMORY_RINGS + Index * RING_STRIDE);
        Ring->Address = Host->MemoryAddress + MEMORY_RINGS + Index * RING_STRIDE;
        Ring->Data = Host->Memory + MEMORY_DATA + Index * RING_DATA_SIZE;
        Ring->DataAddress = Host->MemoryAddress + MEMORY_DATA + Index * RING_DATA_SIZE;
    }

    for (Index = 0; Index < Scratchpads; Index += 1) {
        Array[Index * 2] = Host->MemoryAddress + MEMORY_SCRATCHPADS + Index * PAGE;
    }

    if (Scratchpads != 0) {
        Dcbaa[0] = Host->MemoryAddress + MEMORY_SCRATCHPAD_ARRAY;
    }

    Table[0] = Host->MemoryAddress + MEMORY_EVENT;
    Table[2] = XHCI_EVENT_TRBS;
    MEMORY_BARRIER();
    WRITE_REG(Host, Xhc->Operational + CONFIG, (READ_REG(Host, Xhc->Operational + CONFIG) & ~0xFF) | Xhc->Slots);
    WRITE_REG(Host, Xhc->Operational + DCBAAP, Host->MemoryAddress + MEMORY_DCBAA);
    WRITE_REG(Host, Xhc->Operational + DCBAAP + 4, 0);
    WRITE_REG(Host, Xhc->Operational + CRCR, Xhc->Command.Address | CRCR_CYCLE);
    WRITE_REG(Host, Xhc->Operational + CRCR + 4, 0);
    WRITE_REG(Host, Xhc->Operational + DNCTRL, 0);
    WRITE_REG(Host, Xhc->Runtime + ERSTSZ, (READ_REG(Host, Xhc->Runtime + ERSTSZ) & ~0xFFFF) | 1);
    WRITE_REG(Host, Xhc->Runtime + ERDP, Xhc->Event.Address);
    WRITE_REG(Host, Xhc->Runtime + ERDP + 4, 0);
    WRITE_REG(Host, Xhc->Runtime + ERSTBA, Host->MemoryAddress + MEMORY_ERST);
    WRITE_REG(Host, Xhc->Runtime + ERSTBA + 4, 0);
    WRITE_REG(Host, Xhc->Runtime + IMAN, IMAN_PENDING);

    Host->Interrupt = 5;
    WRITE_REG(Host, Xhc->Operational + USBCMD, USBCMD_RUN);
    if (!XhciWaitRegister(Host, Xhc->Operational + USBSTS, USBSTS_HALTED, FALSE, HALT_TIMEOUT_MS)) {
        return FALSE;
    }

    if (Parameters & HCCPARAMS1_PPC) {
        for (Index = 1; Index <= Xhc->Ports; Index += 1) {
            XhciPortFeature(Host, Index, TRUE, PORT_POWER);
        }
    }

    UsbSleep(POWER_ON_MS);
    Start = UsbClock();
    while (UsbClock() - Start < CONNECT_TIMEOUT_MS * Host->ClockRate) {
        for (Index = 1; Index <= Xhc->Ports; Index += 1) {
            if (READ_REG(Host, Xhc->Operational + PORTSC(Index)) & PORTSC_CCS) {
                break;
            }
        }

        if (Index <= Xhc->Ports) {
            break;
        }

        UsbSleep(10);
    }

    Host->Interrupt = 0;
    return TRUE;
}

BOOLEAN
XhciPortStatus(
    IN PUSB_HOST Host,
    IN ULONG Port,
    OUT PULONG Status,
    OUT PULONG Change
    )

/*++

Routine Description:

    This routine gives the status of a root port, in the form of a hub
    port's, and acknowledges its changes.

Arguments:

    Host - Supplies the host.

    Port - Supplies the number of the root port.

    Status - Receives the status of the port.

    Change - Receives its changes.

Return Value:

    TRUE if the port is there, else FALSE.

--*/

{
    ULONG Offset = Host->Xhc.Operational + PORTSC(Port);
    ULONG Register;
    ULONG Speed;

    if (Port == 0 || Port > Host->Xhc.Ports) {
        return FALSE;
    }

    Register = READ_REG(Host, Offset);
    if (Register == 0xFFFFFFFF) {
        return FALSE;
    }

    Speed = (Register & PORTSC_SPEED_MASK) >> PORTSC_SPEED_SHIFT;
    *Status = ((Register & PORTSC_CCS) ? PORT_STATUS_CONNECTION : 0) |
              ((Register & PORTSC_PED) ? PORT_STATUS_ENABLE : 0) |
              ((Register & PORTSC_PR) ? PORT_STATUS_RESET : 0) |
              ((Register & PORTSC_PP) ? PORT_STATUS_POWER : 0) |
              (Speed == PORT_SPEED_LOW ? PORT_STATUS_LOW_SPEED : 0) |
              (Speed == PORT_SPEED_HIGH ? PORT_STATUS_HIGH_SPEED : 0) |
              (Speed >= PORT_SPEED_SUPER ? PORT_STATUS_SUPER_SPEED : 0);
    *Change = ((Register & PORTSC_CSC) ? PORT_CHANGE_CONNECTION : 0) |
              ((Register & PORTSC_PEC) ? PORT_CHANGE_ENABLE : 0) |
              ((Register & PORTSC_OCC) ? PORT_CHANGE_OVER_CURRENT : 0) |
              ((Register & (PORTSC_PRC | PORTSC_WRC)) ? PORT_CHANGE_RESET : 0);
    if (Register & PORTSC_CHANGES) {
        WRITE_REG(Host, Offset, (Register & PORTSC_KEEP) | (Register & PORTSC_CHANGES));
    }

    return TRUE;
}

VOID
XhciPortFeature(
    IN PUSB_HOST Host,
    IN ULONG Port,
    IN BOOLEAN Set,
    IN ULONG Feature
    )

/*++

Routine Description:

    This routine sets or clears a feature of a root port, as of a hub port:
    its reset (set), its enable (clear) or its power. The changes are
    cleared when the status is read.

Arguments:

    Host - Supplies the host.

    Port - Supplies the number of the root port.

    Set - Supplies TRUE to set the feature, FALSE to clear it.

    Feature - Supplies the feature, PORT_RESET, PORT_ENABLE or PORT_POWER.

Return Value:

    None.

--*/

{
    ULONG Offset = Host->Xhc.Operational + PORTSC(Port);
    ULONG Register;

    if (Port == 0 || Port > Host->Xhc.Ports) {
        return;
    }

    Register = READ_REG(Host, Offset) & PORTSC_KEEP;
    if (Feature == PORT_RESET && Set) {
        WRITE_REG(Host, Offset, Register | PORTSC_PR);
    } else if (Feature == PORT_ENABLE && !Set) {
        WRITE_REG(Host, Offset, Register | PORTSC_PED);
    } else if (Feature == PORT_POWER) {
        WRITE_REG(Host, Offset, Set ? Register | PORTSC_PP : Register & ~PORTSC_PP);
    }
}

BOOLEAN
XhciAddressDevice(
    IN PUSB_HOST Host,
    IN ULONG Index
    )

/*++

Routine Description:

    This routine gives the device a port was reset for a slot and its
    address: its control endpoint gets a ring and the largest packet of its
    speed (that of a full speed device is set right once its device
    descriptor is read), and its slot context its route, speed, root port
    and transaction translator.

Arguments:

    Host - Supplies the host.

    Index - Supplies the index of the device.

Return Value:

    TRUE if the device took its address, else FALSE.

--*/

{
    PXHCI_CONTROLLER Xhc = &Host->Xhc;
    PUSB_DEVICE Device = &Host->Devices[Index];
    volatile ULONG *Dcbaa = (volatile ULONG *)(Host->Memory + MEMORY_DCBAA);
    volatile ULONG *Context;
    PULONG Input;
    PXHCI_RING Ring;
    ULONG Slot;
    ULONG Route;
    ULONG RootPort;
    ULONG TtSlot;
    ULONG TtPort;
    ULONG Offset;

    if (XhciCommand(Host, 0, TRB_TYPE(TYPE_ENABLE_SLOT)) != CODE_SUCCESS) {
        return FALSE;
    }

    Slot = Xhc->CommandSlot;
    if (Slot == 0 || Slot > Xhc->Slots) {
        return FALSE;
    }

    Device->Slot = (UCHAR)Slot;
    Ring = XhciAllocateRing(Host, Slot, CONTROL_ENDPOINT, 0);
    if (Ring == NULL) {
        return FALSE;
    }

    Context = XhciDeviceContext(Host, Slot, 0);
    for (Offset = 0; Offset < 32 * Xhc->ContextSize / 4; Offset += 1) {
        Context[Offset] = 0;
    }

    Dcbaa[Slot * 2] = Host->MemoryAddress + MEMORY_CONTEXTS + (Slot - 1) * 32 * Xhc->ContextSize;
    Dcbaa[Slot * 2 + 1] = 0;
    Device->ControlMaximum = Device->Speed == SPEED_LOW ? 8 : Device->Speed == SPEED_SUPER ? 512 : 64;
    Route = XhciRoute(Host, Device, &RootPort, &TtSlot, &TtPort);
    XhciClearInput(Host);
    XhciInputContext(Host, 0)[1] = 1 | (1 << CONTROL_ENDPOINT);
    Input = XhciInputContext(Host, 1);
    Input[0] = Route | ((ULONG)XhciSpeeds[Device->Speed] << SLOT_SPEED_SHIFT) | (CONTROL_ENDPOINT << SLOT_ENTRIES_SHIFT);
    Input[1] = RootPort << SLOT_ROOT_PORT_SHIFT;
    Input[2] = TtSlot | (TtPort << SLOT_TT_PORT_SHIFT);
    Input = XhciInputContext(Host, 1 + CONTROL_ENDPOINT);
    Input[1] = ENDPOINT_ERRORS | (ENDPOINT_CONTROL << ENDPOINT_TYPE_SHIFT) |
               ((ULONG)Device->ControlMaximum << ENDPOINT_MAXIMUM_SHIFT);
    Input[2] = Ring->Address | Ring->Cycle;
    Input[4] = 8;
    if (XhciCommand(Host, Host->MemoryAddress + MEMORY_INPUT, TRB_TYPE(TYPE_ADDRESS_DEVICE) | TRB_SLOT(Slot)) !=
        CODE_SUCCESS) {
        return FALSE;
    }

    Device->Address = (UCHAR)XhciDeviceContext(Host, Slot, 0)[3];
    return TRUE;
}

BOOLEAN
XhciSetControlMaximum(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device
    )

/*++

Routine Description:

    This routine gives the control endpoint of a device the largest packet
    its device descriptor gives, if the controller has another.

Arguments:

    Host - Supplies the host.

    Device - Supplies the device.

Return Value:

    TRUE if the endpoint has the size, else FALSE.

--*/

{
    volatile ULONG *Output = XhciDeviceContext(Host, Device->Slot, CONTROL_ENDPOINT);
    PULONG Input;
    ULONG Index;

    if ((Output[1] >> ENDPOINT_MAXIMUM_SHIFT) == Device->ControlMaximum) {
        return TRUE;
    }

    XhciClearInput(Host);
    XhciInputContext(Host, 0)[1] = 1 << CONTROL_ENDPOINT;
    Input = XhciInputContext(Host, 1 + CONTROL_ENDPOINT);
    for (Index = 0; Index < 5; Index += 1) {
        Input[Index] = Output[Index];
    }

    Input[1] = (Input[1] & 0xFFFF) | ((ULONG)Device->ControlMaximum << ENDPOINT_MAXIMUM_SHIFT);
    return XhciCommand(Host, Host->MemoryAddress + MEMORY_INPUT,
                       TRB_TYPE(TYPE_EVALUATE_CONTEXT) | TRB_SLOT(Device->Slot)) == CODE_SUCCESS;
}

BOOLEAN
XhciConfigureEndpoint(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint,
    IN ULONG Maximum,
    IN ULONG Interval
    )

/*++

Routine Description:

    This routine adds an interrupt IN endpoint of a device to its slot, with
    a ring: it is polled at its interval, in frames at low and full speed,
    else as a power of two of microframes, given in (micro)frames of 2 to
    the power of the Interval field.

Arguments:

    Host - Supplies the host.

    Device - Supplies the device.

    Endpoint - Supplies the endpoint number.

    Maximum - Supplies its maximum packet size.

    Interval - Supplies its bInterval.

Return Value:

    TRUE if the endpoint was configured, else FALSE.

--*/

{
    ULONG Index = Endpoint * 2 + 1;
    PXHCI_RING Ring;
    PULONG Input;
    ULONG Exponent;

    if (Device->Slot == 0 || Maximum == 0) {
        return FALSE;
    }

    Ring = XhciAllocateRing(Host, Device->Slot, Index, Maximum);
    if (Ring == NULL) {
        return FALSE;
    }

    if (Device->Speed == SPEED_HIGH || Device->Speed == SPEED_SUPER) {
        Exponent = (Interval < 1 ? 1 : Interval > 16 ? 16 : Interval) - 1;
    } else {
        for (Exponent = 3; Exponent < 10 && (1UL << (Exponent + 1)) <= Interval * 8; Exponent += 1) {
        }
    }

    XhciInputSlot(Host, Device, Index);
    Input = XhciInputContext(Host, 1 + Index);
    Input[0] = Exponent << ENDPOINT_INTERVAL_SHIFT;
    Input[1] = ENDPOINT_ERRORS | (ENDPOINT_INTERRUPT_IN << ENDPOINT_TYPE_SHIFT) | (Maximum << ENDPOINT_MAXIMUM_SHIFT);
    Input[2] = Ring->Address | Ring->Cycle;
    Input[4] = Maximum | (Maximum << 16);
    return XhciCommand(Host, Host->MemoryAddress + MEMORY_INPUT,
                       TRB_TYPE(TYPE_CONFIGURE_ENDPOINT) | TRB_SLOT(Device->Slot)) == CODE_SUCCESS;
}

BOOLEAN
XhciConfigureHub(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Hub,
    IN ULONG ThinkTime
    )

/*++

Routine Description:

    This routine tells the controller a device is a hub (with one
    transaction translator), with its ports and, at high speed, the think
    time of its translator. A controller older than xHCI 1.0 takes the hub
    flag only, by evaluating the slot context.

Arguments:

    Host - Supplies the host.

    Hub - Supplies the hub.

    ThinkTime - Supplies the think time of its transaction translator, as
        in its hub descriptor.

Return Value:

    TRUE if the controller took it, else FALSE.

--*/

{
    PULONG Slot = XhciInputSlot(Host, Hub, 0);

    Slot[0] = (Slot[0] & ~SLOT_MTT) | SLOT_HUB;
    if (Host->Xhc.Version < 0x100) {
        return XhciCommand(Host, Host->MemoryAddress + MEMORY_INPUT,
                           TRB_TYPE(TYPE_EVALUATE_CONTEXT) | TRB_SLOT(Hub->Slot)) == CODE_SUCCESS;
    }

    Slot[1] = (Slot[1] & ~(0xFF << SLOT_PORTS_SHIFT)) | ((ULONG)Hub->Ports << SLOT_PORTS_SHIFT);
    if (Hub->Speed == SPEED_HIGH) {
        Slot[2] = (Slot[2] & ~SLOT_THINK_TIME_MASK) | ((ThinkTime & 3) << SLOT_THINK_TIME_SHIFT);
    }

    return XhciCommand(Host, Host->MemoryAddress + MEMORY_INPUT,
                       TRB_TYPE(TYPE_CONFIGURE_ENDPOINT) | TRB_SLOT(Hub->Slot)) == CODE_SUCCESS;
}

VOID
XhciRemoveDevice(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device
    )

/*++

Routine Description:

    This routine frees the slot of a device that went, and its rings.

Arguments:

    Host - Supplies the host.

    Device - Supplies the device.

Return Value:

    None.

--*/

{
    volatile ULONG *Dcbaa = (volatile ULONG *)(Host->Memory + MEMORY_DCBAA);
    PXHCI_RING Ring;

    if (Device->Slot == 0) {
        return;
    }

    XhciCommand(Host, 0, TRB_TYPE(TYPE_DISABLE_SLOT) | TRB_SLOT(Device->Slot));
    Dcbaa[Device->Slot * 2] = 0;
    for (Ring = Host->Xhc.Rings; Ring < Host->Xhc.Rings + XHCI_RINGS; Ring += 1) {
        if (Ring->Slot == Device->Slot) {
            Ring->Slot = 0;
            Ring->Endpoint = 0;
            Ring->Pending = FALSE;
        }
    }

    Device->Slot = 0;
}

LONG
XhciControl(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN PUCHAR Setup,
    IN OUT PVOID Data,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine runs a control transfer on the control endpoint of a
    device: the setup stage, the data stage through the buffer of the host
    if there is data, and the status stage.

Arguments:

    Host - Supplies the host.

    Device - Supplies the device.

    Setup - Supplies the setup packet.

    Data - Supplies the data, or receives it.

    Length - Supplies the length of the data.

Return Value:

    The length of the data moved, or XFER_STALL or XFER_ERROR.

--*/

{
    PXHCI_RING Ring = XhciFindRing(Host, Device->Slot, CONTROL_ENDPOINT);
    BOOLEAN In = (Setup[0] & 0x80) != 0;
    ULONG Transfer = 0;
    ULONG Actual;
    ULONG First;
    UCHAR Code;

    if (Device->Slot == 0 || Ring == NULL || Length > USB_BUFFER_SIZE) {
        return XFER_ERROR;
    }

    if (Length != 0 && !In) {
        RtlMoveMemory(Host->Buffer, Data, Length);
    }

    if (Length != 0 && Host->Xhc.Version >= 0x100) {
        Transfer = In ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    Ring->Pending = TRUE;
    Ring->Done = FALSE;
    Ring->DataTrb = 0;
    Ring->DataCode = CODE_SUCCESS;
    Ring->DataResidual = 0;
    First = XhciQueue(Ring, Setup[0] | (Setup[1] << 8) | (Setup[2] << 16) | ((ULONG)Setup[3] << 24),
                      Setup[4] | (Setup[5] << 8) | (Setup[6] << 16) | ((ULONG)Setup[7] << 24),
                      8, TRB_TYPE(TYPE_SETUP) | TRB_IDT | Transfer, TRUE);
    if (Length != 0) {
        Ring->DataTrb = XhciQueue(Ring, Host->BufferAddress, 0, Length,
                                  TRB_TYPE(TYPE_DATA) | (In ? TRB_DIR_IN | TRB_ISP : 0), FALSE);
    }

    Ring->LastTrb = XhciQueue(Ring, 0, 0, 0, TRB_TYPE(TYPE_STATUS) | TRB_IOC | (Length != 0 && In ? 0 : TRB_DIR_IN),
                              FALSE);
    XhciRelease(Ring, First);
    XhciDoorbell(Host, Device->Slot, CONTROL_ENDPOINT);
    Code = XhciComplete(Host, Ring, UsbClock() + CONTROL_TIMEOUT_MS * Host->ClockRate);
    if (Code != CODE_SUCCESS && Code != CODE_SHORT_PACKET) {
        return Code == CODE_STALL ? XFER_STALL : XFER_ERROR;
    }

    Actual = Length - (Ring->DataCode == CODE_SHORT_PACKET ? Ring->DataResidual : 0);
    if (In && Actual != 0) {
        RtlMoveMemory(Data, Host->Buffer, Actual);
    }

    return (LONG)Actual;
}

//
// Queue a read of a packet on the ring of an interrupt endpoint, to its
// buffer.
//

static VOID
XhciQueueRead(
    IN PUSB_HOST Host,
    IN PXHCI_RING Ring
    )
{
    ULONG Length = Ring->Maximum < RING_DATA_SIZE ? Ring->Maximum : RING_DATA_SIZE;

    Ring->Pending = TRUE;
    Ring->Done = FALSE;
    Ring->DataTrb = 0;
    Ring->LastTrb = XhciQueue(Ring, Ring->DataAddress, 0, Length, TRB_TYPE(TYPE_NORMAL) | TRB_ISP | TRB_IOC, FALSE);
    XhciDoorbell(Host, Ring->Slot, Ring->Endpoint);
}

LONG
XhciInterrupt(
    IN PUSB_HOST Host,
    IN PUSB_DEVICE Device,
    IN ULONG Endpoint,
    OUT PUCHAR Buffer,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine reads an interrupt IN endpoint of a device: the packet of
    the read queued on its ring if that completed (and the next is queued),
    else nothing. An endpoint that failed is taken back from the error.

Arguments:

    Host - Supplies the host.

    Device - Supplies the device.

    Endpoint - Supplies the endpoint number.

    Buffer - Receives the packet.

    Length - Supplies the length of the buffer.

Return Value:

    The length of the packet, or XFER_NAK if none came, XFER_STALL or
    XFER_ERROR.

--*/

{
    PXHCI_RING Ring = XhciFindRing(Host, Device->Slot, Endpoint * 2 + 1);
    ULONG Count;
    UCHAR Code;

    if (Device->Slot == 0 || Ring == NULL) {
        return XFER_ERROR;
    }

    if (!Ring->Pending) {
        XhciQueueRead(Host, Ring);
    }

    XhciEvents(Host);
    if (!Ring->Done) {
        return XFER_NAK;
    }

    Code = Ring->Code;
    Ring->Pending = FALSE;
    Host->Interrupt = Code;
    if (Code != CODE_SUCCESS && Code != CODE_SHORT_PACKET) {
        XhciRecover(Host, Ring);
        return Code == CODE_STALL ? XFER_STALL : XFER_ERROR;
    }

    Count = (Ring->Maximum < RING_DATA_SIZE ? Ring->Maximum : RING_DATA_SIZE) - Ring->Residual;
    if (Count > Length) {
        Count = Length;
    }

    RtlMoveMemory(Buffer, Ring->Data, Count);
    XhciQueueRead(Host, Ring);
    return (LONG)Count;
}
