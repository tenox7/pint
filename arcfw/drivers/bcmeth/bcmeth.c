/*++

Module Name:

    bcmeth.c

Abstract:

    This is the NDIS 3.0 miniport driver of the Ethernet controller of the
    Raspberry Pi: the Ethernet function of the LAN9514 (Pi 2 and 3 Model B)
    or of the LAN7515 (Pi 3 Model B+) behind the USB host, or a CDC Ethernet
    function in its place (chip.c). The firmware describes the controller as
    the network controller of the internal bus, with the interrupt of the
    USB host and the station address of the board, as Jazz describes its
    SONIC; the USB host port driver (bcmusb.sys) found the function.

    The frames move on two pipes, the bulk endpoints of the function on host
    channels of their own, through buffers in shared memory: the receive
    pipe waits for a transfer, which holds a frame or more, and the transmit
    pipe sends the frames of a ring of slots, one transfer each (and a zero
    length packet after one that fills its last packet). The halts of the
    channels interrupt; the interrupt handler indicates the frames received
    and starts the next transfers. A frame is indicated whole, from cached
    memory. Transmits are copied into the ring and complete at once.

    The control transfers to the function cannot run at DISPATCH_LEVEL, so a
    thread sets the receive filter, clears the halt of a stalled endpoint and
    looks at the link; the timer completes a set request and restarts a
    stalled pipe for it.

Environment:

    kernel mode only

--*/

#include "bcmeth.h"

NDIS_PHYSICAL_ADDRESS HighestAcceptableMax = NDIS_PHYSICAL_ADDRESS_CONST(-1, -1);

#define TRANSMIT_FRAME_SIZE         (TRANSMIT_SLOT_SIZE + 8)
#define FRAME_BUFFER_SIZE           (ETH_MAXIMUM_FRAME + 8)

static const NDIS_OID BcmethSupportedOids[] = {
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_PROTOCOL_OPTIONS,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_RCV_CRC_ERROR,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS
};

static const UCHAR Lan9500Description[] = "SMSC LAN9500 USB Ethernet Controller";
static const UCHAR Lan7800Description[] = "Microchip LAN7800 USB Gigabit Ethernet Controller";
static const UCHAR CdcDescription[] = "USB CDC Ethernet Controller";

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

//
// The shared memory is uncached and mapped as a device, where the processor
// must not access unaligned: a frame moves in and out of it in longwords.
//

static VOID
MoveFromShared(
    OUT PUCHAR Destination,
    IN PUCHAR Source,
    IN ULONG Length
    )
{
    volatile ULONG *Word = (volatile ULONG *)((ULONG)Source & ~3);
    ULONG Skip = (ULONG)Source & 3;
    ULONG Value = *Word++ >> (Skip * 8);
    ULONG Left = 4 - Skip;

    while (Length != 0) {
        if (Left == 0) {
            Value = *Word++;
            Left = 4;
        }

        *Destination++ = (UCHAR)Value;
        Value >>= 8;
        Left -= 1;
        Length -= 1;
    }
}

static VOID
MoveToShared(
    OUT PUCHAR Destination,
    IN PUCHAR Source,
    IN ULONG Length
    )
{
    volatile ULONG *Word = (volatile ULONG *)Destination;
    ULONG Value;
    ULONG Index;

    while (Length != 0) {
        Value = 0;
        for (Index = 0; Index < 4 && Length != 0; Index += 1, Length -= 1) {
            Value |= (ULONG)*Source++ << (Index * 8);
        }

        *Word++ = Value;
    }
}

//
// The hardware description of the controller the firmware gives: the
// interrupt of the USB host and the station address of the board.
//

typedef struct _BCMETH_HARDWARE {
    BOOLEAN Found;
    BOOLEAN AddressValid;
    ULONG InterruptVector;
    ULONG InterruptLevel;
    UCHAR EthernetAddress[ETH_LENGTH_OF_ADDRESS];
} BCMETH_HARDWARE, *PBCMETH_HARDWARE;

static NTSTATUS
BcmethSaveHardware(
    IN PWSTR ValueName,
    IN ULONG ValueType,
    IN PVOID ValueData,
    IN ULONG ValueLength,
    IN PVOID Context,
    IN PVOID EntryContext
    )
{
    PBCMETH_HARDWARE Hardware = Context;
    PCM_PARTIAL_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    PCM_BCMETH_DEVICE_DATA Data;
    ULONG Index;

    if ((ValueType != REG_BINARY && ValueType != REG_FULL_RESOURCE_DESCRIPTOR) ||
        ValueLength < sizeof(CM_FULL_RESOURCE_DESCRIPTOR)) {
        return STATUS_SUCCESS;
    }

    List = &((PCM_FULL_RESOURCE_DESCRIPTOR)ValueData)->PartialResourceList;
    for (Index = 0; Index < List->Count; Index += 1) {
        Partial = &List->PartialDescriptors[Index];
        if (Partial->Type == CmResourceTypeInterrupt) {
            Hardware->InterruptVector = Partial->u.Interrupt.Vector;
            Hardware->InterruptLevel = Partial->u.Interrupt.Level;
            Hardware->Found = TRUE;
        } else if (Partial->Type == CmResourceTypeDeviceSpecific && Index == List->Count - 1 &&
                   Partial->u.DeviceSpecificData.DataSize >= sizeof(CM_BCMETH_DEVICE_DATA)) {
            Data = (PCM_BCMETH_DEVICE_DATA)(Partial + 1);
            NdisMoveMemory(Hardware->EthernetAddress, Data->EthernetAddress, ETH_LENGTH_OF_ADDRESS);
            Hardware->AddressValid = (Data->EthernetAddress[0] | Data->EthernetAddress[1] |
                                      Data->EthernetAddress[2] | Data->EthernetAddress[3] |
                                      Data->EthernetAddress[4] | Data->EthernetAddress[5]) != 0;
        }
    }

    return STATUS_SUCCESS;
}

static BOOLEAN
BcmethReadHardware(
    IN UINT MultifunctionAdapter,
    IN UINT Controller,
    OUT PBCMETH_HARDWARE Hardware
    )
{
    WCHAR Path[] = L"\\Registry\\Machine\\Hardware\\Description\\System\\MultifunctionAdapter\\#\\NetworkController\\#";
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    PWCHAR Mark;

    Mark = wcschr(Path, L'#');
    *Mark = (WCHAR)(L'0' + MultifunctionAdapter);
    Mark = wcschr(Mark, L'#');
    *Mark = (WCHAR)(L'0' + Controller);

    NdisZeroMemory(QueryTable, sizeof(QueryTable));
    NdisZeroMemory(Hardware, sizeof(BCMETH_HARDWARE));
    QueryTable[0].QueryRoutine = BcmethSaveHardware;
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_REQUIRED;
    QueryTable[0].Name = L"Configuration Data";
    QueryTable[0].DefaultType = REG_NONE;
    return NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, Path, QueryTable, Hardware, NULL)) &&
           Hardware->Found;
}

//
// The pipes.
//

static VOID
BcmethStartReceive(
    IN PBCMETH_ADAPTER Adapter
    )
{
    UsbFunctionStartPipe(&Adapter->ReceivePipe, Adapter->ReceivePhysical.LowPart + Adapter->ReceiveDone,
                         Adapter->ReceiveLength - Adapter->ReceiveDone);
}

static VOID
BcmethStartTransmit(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Address = Adapter->TransmitPhysical.LowPart + Adapter->TransmitHead * TRANSMIT_SLOT_SIZE;

    if (Adapter->TransmitStopped || (!Adapter->TransmitZero && Adapter->TransmitCount == 0)) {
        return;
    }

    Adapter->TransmitBusy = TRUE;
    if (Adapter->TransmitZero) {
        UsbFunctionStartPipe(&Adapter->TransmitPipe, Address, 0);
        return;
    }

    UsbFunctionStartPipe(&Adapter->TransmitPipe, Address + Adapter->TransmitDone,
                         Adapter->TransmitLength[Adapter->TransmitHead] - Adapter->TransmitDone);
}

static VOID
BcmethQueueWork(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Work
    )
{
    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Work |= Work;
    NdisReleaseSpinLock(&Adapter->Lock);
    KeSetEvent(&Adapter->WorkEvent, 0, FALSE);
}

//
// A receive transfer the pipe halted at: its frames are indicated, or the
// rest of it waited for (a NAK ended it early), and the next started. A
// stalled endpoint waits for the thread.
//

static VOID
BcmethReceive(
    IN PBCMETH_ADAPTER Adapter
    )
{
    LONG Result = UsbFunctionPipeResult(&Adapter->ReceivePipe);
    BOOLEAN Indicated = FALSE;
    BOOLEAN Error;
    ULONG Length;
    ULONG Offset = 0;
    ULONG Next;
    ULONG FrameOffset;
    ULONG FrameLength;

    if (Result == XFER_STALL) {
        Adapter->ReceiveStopped = TRUE;
        Adapter->ReceiveDone = 0;
        BcmethQueueWork(Adapter, WORK_RECEIVE_STALL);
        return;
    }

    if (Result == XFER_NAK) {
        Adapter->ReceiveDone += Adapter->ReceivePipe.Moved;
        if (Adapter->ReceiveDone < Adapter->ReceiveLength) {
            BcmethStartReceive(Adapter);
            return;
        }

        Result = 0;
    }

    if (Result == XFER_ERROR) {
        Adapter->ReceiveError += 1;
        Adapter->ReceiveDone = 0;
        BcmethStartReceive(Adapter);
        return;
    }

    Length = Adapter->ReceiveDone + (ULONG)Result;
    Adapter->ReceiveDone = 0;
    while ((Next = ChipNextFrame(Adapter, Adapter->ReceiveBuffer, Offset, Length, &FrameOffset,
                                 &FrameLength, &Error)) != 0) {
        Offset = Next;
        if (Error || FrameLength < ETH_HEADER_SIZE || FrameLength > ETH_MAXIMUM_FRAME) {
            Adapter->ReceiveError += 1;
            continue;
        }

        MoveFromShared(Adapter->Frame, Adapter->ReceiveBuffer + FrameOffset, FrameLength);
        Adapter->FrameLength = FrameLength;
        Adapter->ReceiveOk += 1;
        NdisMEthIndicateReceive(Adapter->MiniportAdapterHandle, (NDIS_HANDLE)Adapter, Adapter->Frame,
                                ETH_HEADER_SIZE, Adapter->Frame + ETH_HEADER_SIZE,
                                FrameLength - ETH_HEADER_SIZE, FrameLength - ETH_HEADER_SIZE);
        Indicated = TRUE;
    }

    if (Indicated) {
        NdisMEthIndicateReceiveComplete(Adapter->MiniportAdapterHandle);
    }

    if (!Adapter->Halting) {
        BcmethStartReceive(Adapter);
    }
}

//
// A transmit transfer the pipe halted at: its slot is freed (the frame
// was completed when it was copied), a zero length packet owed after it,
// and the next started.
//

static VOID
BcmethTransmitDone(
    IN PBCMETH_ADAPTER Adapter
    )
{
    LONG Result = UsbFunctionPipeResult(&Adapter->TransmitPipe);
    ULONG Length = Adapter->TransmitLength[Adapter->TransmitHead];

    Adapter->TransmitBusy = FALSE;
    if (Result == XFER_STALL) {
        Adapter->TransmitStopped = TRUE;
        Adapter->TransmitDone = 0;
        BcmethQueueWork(Adapter, WORK_TRANSMIT_STALL);
        return;
    }

    if (Result == XFER_NAK && !Adapter->TransmitZero) {
        Adapter->TransmitDone += Adapter->TransmitPipe.Moved;
        BcmethStartTransmit(Adapter);
        return;
    }

    if (Adapter->TransmitZero) {
        Adapter->TransmitZero = FALSE;
    } else {
        if (Result < 0) {
            Adapter->TransmitError += 1;
        } else {
            Adapter->TransmitOk += 1;
            Adapter->TransmitZero = (Length % Adapter->TransmitPipe.Maximum) == 0;
        }

        Adapter->TransmitDone = 0;
        Adapter->TransmitHead = (Adapter->TransmitHead + 1) % TRANSMIT_SLOTS;
        Adapter->TransmitCount -= 1;
        if (Adapter->ResourcesNeeded) {
            Adapter->ResourcesNeeded = FALSE;
            NdisMSendResourcesAvailable(Adapter->MiniportAdapterHandle);
        }
    }

    BcmethStartTransmit(Adapter);
}

//
// The interrupt of the USB host: the halt of a channel of a pipe.
//

static VOID
BcmethIsr(
    OUT PBOOLEAN InterruptRecognized,
    OUT PBOOLEAN QueueMiniportHandleInterrupt,
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;
    BOOLEAN Recognized = FALSE;

    if (UsbFunctionPipeHalted(&Adapter->ReceivePipe)) {
        Adapter->ReceiveHalted = TRUE;
        Recognized = TRUE;
    }

    if (UsbFunctionPipeHalted(&Adapter->TransmitPipe)) {
        Adapter->TransmitHalted = TRUE;
        Recognized = TRUE;
    }

    *InterruptRecognized = Recognized;
    *QueueMiniportHandleInterrupt = Recognized;
}

static VOID
BcmethHandleInterrupt(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;

    if (Adapter->Resetting) {
        Adapter->ReceiveHalted = FALSE;
        Adapter->TransmitHalted = FALSE;
        return;
    }

    if (Adapter->ReceiveHalted) {
        Adapter->ReceiveHalted = FALSE;
        BcmethReceive(Adapter);
    }

    if (Adapter->TransmitHalted) {
        Adapter->TransmitHalted = FALSE;
        BcmethTransmitDone(Adapter);
    }
}

static VOID
BcmethDisableInterrupt(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;

    UsbFunctionPipeInterrupt(&Adapter->ReceivePipe, FALSE);
    UsbFunctionPipeInterrupt(&Adapter->TransmitPipe, FALSE);
}

static VOID
BcmethEnableInterrupt(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;

    UsbFunctionPipeInterrupt(&Adapter->ReceivePipe, TRUE);
    UsbFunctionPipeInterrupt(&Adapter->TransmitPipe, TRUE);
}

//
// The timer, which the thread sets: the completion of a set request, and
// the restart of the pipes whose halts it cleared.
//

static VOID
BcmethTimer(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3
    )
{
    PBCMETH_ADAPTER Adapter = FunctionContext;

    if (Adapter->ResetDone) {
        Adapter->ResetDone = FALSE;
        Adapter->Resetting = FALSE;
        Adapter->TransmitHead = Adapter->TransmitTail = Adapter->TransmitCount = 0;
        Adapter->TransmitBusy = Adapter->TransmitZero = FALSE;
        Adapter->TransmitDone = Adapter->ReceiveDone = 0;
        Adapter->ReceiveRestart = Adapter->TransmitRestart = TRUE;
        NdisMResetComplete(Adapter->MiniportAdapterHandle, Adapter->ResetStatus, FALSE);
    }

    if (Adapter->RequestPending) {
        Adapter->RequestPending = FALSE;
        NdisMSetInformationComplete(Adapter->MiniportAdapterHandle, Adapter->RequestStatus);
    }

    if (Adapter->ReceiveRestart) {
        Adapter->ReceiveRestart = FALSE;
        Adapter->ReceiveStopped = FALSE;
        BcmethStartReceive(Adapter);
    }

    if (Adapter->TransmitRestart) {
        Adapter->TransmitRestart = FALSE;
        Adapter->TransmitStopped = FALSE;
        BcmethStartTransmit(Adapter);
    }
}

//
// The thread: the work the miniport queued, and the link each second.
//

static VOID
BcmethThread(
    IN PVOID Context
    )
{
    PBCMETH_ADAPTER Adapter = Context;
    LARGE_INTEGER Interval;
    BOOLEAN Timer;
    ULONG Work;

    Interval.QuadPart = -10000 * (LONGLONG)LINK_INTERVAL_MS;
    while (!Adapter->Halting) {
        KeWaitForSingleObject(&Adapter->WorkEvent, Executive, KernelMode, FALSE, &Interval);
        if (Adapter->Halting) {
            break;
        }

        NdisAcquireSpinLock(&Adapter->Lock);
        Work = Adapter->Work;
        Adapter->Work = 0;
        NdisReleaseSpinLock(&Adapter->Lock);

        Timer = FALSE;
        if (Work & WORK_RESET) {
            UsbFunctionClosePipe(&Adapter->ReceivePipe);
            UsbFunctionClosePipe(&Adapter->TransmitPipe);
            ChipStop(Adapter);
            Adapter->ResetStatus = ChipStart(Adapter);
            if (!UsbFunctionOpenPipe(Adapter->Device, Adapter->BulkIn, TRUE, Adapter->BulkInMaximum,
                                     &Adapter->ReceivePipe) ||
                !UsbFunctionOpenPipe(Adapter->Device, Adapter->BulkOut, FALSE, Adapter->BulkOutMaximum,
                                     &Adapter->TransmitPipe)) {
                Adapter->ResetStatus = NDIS_STATUS_HARD_ERRORS;
            }

            Adapter->ResetDone = TRUE;
            Timer = TRUE;
        }

        if (Work & WORK_FILTER) {
            ChipSetFilter(Adapter);
            Adapter->RequestStatus = NDIS_STATUS_SUCCESS;
            Adapter->RequestPending = TRUE;
            Timer = TRUE;
        }

        if (Work & WORK_RECEIVE_STALL) {
            ChipClearHalt(Adapter, &Adapter->ReceivePipe);
            Adapter->ReceiveRestart = TRUE;
            Timer = TRUE;
        }

        if (Work & WORK_TRANSMIT_STALL) {
            ChipClearHalt(Adapter, &Adapter->TransmitPipe);
            Adapter->TransmitRestart = TRUE;
            Timer = TRUE;
        }

        if (Timer) {
            NdisMSetTimer(&Adapter->RequestTimer, 0);
        }

        ChipCheckLink(Adapter);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

//
// The resources of the adapter.
//

static VOID
BcmethFreeAdapter(
    IN PBCMETH_ADAPTER Adapter
    )
{
    if (Adapter->ReceiveBuffer != NULL) {
        NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, RECEIVE_BUFFER_SIZE, FALSE,
                              Adapter->ReceiveBuffer, Adapter->ReceivePhysical);
    }

    if (Adapter->TransmitBuffer != NULL) {
        NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, TRANSMIT_SLOTS * TRANSMIT_SLOT_SIZE, FALSE,
                              Adapter->TransmitBuffer, Adapter->TransmitPhysical);
    }

    if (Adapter->FrameBuffer != NULL) {
        NdisFreeMemory(Adapter->FrameBuffer, FRAME_BUFFER_SIZE, 0);
    }

    if (Adapter->TransmitFrame != NULL) {
        NdisFreeMemory(Adapter->TransmitFrame, TRANSMIT_FRAME_SIZE, 0);
    }

    NdisFreeSpinLock(&Adapter->Lock);
    NdisFreeMemory(Adapter, sizeof(BCMETH_ADAPTER), 0);
}

//
// Log why the adapter did not start: the step (1 the firmware describes no
// controller, 2 none among the USB functions, 3 no station address, 4 the
// resources, 5 the start of the controller, its pipes or interrupt), the
// status, the controller and its device, and its last register access.
//

static VOID
BcmethLogFailure(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Step,
    IN NDIS_STATUS Status
    )
{
    NdisWriteErrorLogEntry(Adapter->MiniportAdapterHandle,
                           Step <= 2 ? NDIS_ERROR_CODE_ADAPTER_NOT_FOUND : NDIS_ERROR_CODE_HARDWARE_FAILURE, 7,
                           Step, Status, Adapter->Chip, Adapter->Device, Adapter->LastRegister, Adapter->LastValue,
                           Adapter->LastResult);
}

static NDIS_STATUS
BcmethInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE WrapperConfigurationContext
    )

/*++

Routine Description:

    This routine finds the controller the firmware describes among the
    functions of the USB host, starts it with the address of the board (or
    the one configured), opens its pipes, connects the interrupt and starts
    the first receive and the thread.

Arguments:

    OpenErrorStatus - Not used.

    SelectedMediumIndex - Receives the index of 802.3.

    MediumArray, MediumArraySize - Supply the media the protocols take.

    MiniportAdapterHandle - Supplies the handle of the adapter.

    WrapperConfigurationContext - Supplies the configuration of the adapter.

Return Value:

    NDIS_STATUS_SUCCESS, or why the adapter could not be initialized.

--*/

{
    NDIS_STRING MultifunctionAdapterString = NDIS_STRING_CONST("MultifunctionAdapter");
    NDIS_STRING NetworkControllerString = NDIS_STRING_CONST("NetworkController");
    PNDIS_CONFIGURATION_PARAMETER Value;
    PBCMETH_ADAPTER Adapter;
    BCMETH_HARDWARE Hardware;
    NDIS_HANDLE ConfigurationHandle;
    PUCHAR NetworkAddress;
    UINT NetworkAddressLength;
    UINT MultifunctionAdapter = 0;
    UINT Controller = 0;
    BOOLEAN AddressConfigured = FALSE;
    NDIS_STATUS Status;
    UINT Index;
    HANDLE Thread;

    for (Index = 0; Index < MediumArraySize && MediumArray[Index] != NdisMedium802_3; Index += 1) {
    }

    if (Index == MediumArraySize) {
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    }

    *SelectedMediumIndex = Index;
    if (NdisAllocateMemory((PVOID *)&Adapter, sizeof(BCMETH_ADAPTER), 0, HighestAcceptableMax) !=
        NDIS_STATUS_SUCCESS) {
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(Adapter, sizeof(BCMETH_ADAPTER));
    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    NdisAllocateSpinLock(&Adapter->Lock);
    KeInitializeEvent(&Adapter->WorkEvent, SynchronizationEvent, FALSE);

    NdisOpenConfiguration(&Status, &ConfigurationHandle, WrapperConfigurationContext);
    if (Status != NDIS_STATUS_SUCCESS) {
        BcmethFreeAdapter(Adapter);
        return Status;
    }

    NdisReadConfiguration(&Status, &Value, ConfigurationHandle, &MultifunctionAdapterString,
                          NdisParameterInteger);
    if (Status == NDIS_STATUS_SUCCESS) {
        MultifunctionAdapter = Value->ParameterData.IntegerData;
    }

    NdisReadConfiguration(&Status, &Value, ConfigurationHandle, &NetworkControllerString, NdisParameterInteger);
    if (Status == NDIS_STATUS_SUCCESS) {
        Controller = Value->ParameterData.IntegerData;
    }

    NdisReadNetworkAddress(&Status, (PVOID *)&NetworkAddress, &NetworkAddressLength, ConfigurationHandle);
    if (Status == NDIS_STATUS_SUCCESS && NetworkAddressLength == ETH_LENGTH_OF_ADDRESS &&
        (NetworkAddress[0] | NetworkAddress[1] | NetworkAddress[2] | NetworkAddress[3] |
         NetworkAddress[4] | NetworkAddress[5]) != 0 && (NetworkAddress[0] & 1) == 0) {
        NdisMoveMemory(Adapter->CurrentAddress, NetworkAddress, ETH_LENGTH_OF_ADDRESS);
        AddressConfigured = TRUE;
    }

    NdisCloseConfiguration(ConfigurationHandle);
    if (!BcmethReadHardware(MultifunctionAdapter, Controller, &Hardware)) {
        BcmethLogFailure(Adapter, 1, NDIS_STATUS_ADAPTER_NOT_FOUND);
        BcmethFreeAdapter(Adapter);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    Status = ChipFind(Adapter);
    if (Status != NDIS_STATUS_SUCCESS) {
        BcmethLogFailure(Adapter, 2, Status);
        BcmethFreeAdapter(Adapter);
        return Status;
    }

    if (Adapter->Chip != CHIP_CDC) {
        if (!Hardware.AddressValid) {
            BcmethLogFailure(Adapter, 3, NDIS_STATUS_ADAPTER_NOT_FOUND);
            BcmethFreeAdapter(Adapter);
            return NDIS_STATUS_ADAPTER_NOT_FOUND;
        }

        NdisMoveMemory(Adapter->PermanentAddress, Hardware.EthernetAddress, ETH_LENGTH_OF_ADDRESS);
    }

    if (!AddressConfigured) {
        NdisMoveMemory(Adapter->CurrentAddress, Adapter->PermanentAddress, ETH_LENGTH_OF_ADDRESS);
    }

    NdisMSetAttributes(MiniportAdapterHandle, (NDIS_HANDLE)Adapter, TRUE, NdisInterfaceInternal);
    Status = NdisMAllocateMapRegisters(MiniportAdapterHandle, 0, TRUE, 1, TRANSMIT_SLOT_SIZE);
    if (Status != NDIS_STATUS_SUCCESS) {
        BcmethLogFailure(Adapter, 4, Status);
        BcmethFreeAdapter(Adapter);
        return Status;
    }

    NdisMAllocateSharedMemory(MiniportAdapterHandle, RECEIVE_BUFFER_SIZE, FALSE,
                              (PVOID *)&Adapter->ReceiveBuffer, &Adapter->ReceivePhysical);
    NdisMAllocateSharedMemory(MiniportAdapterHandle, TRANSMIT_SLOTS * TRANSMIT_SLOT_SIZE, FALSE,
                              (PVOID *)&Adapter->TransmitBuffer, &Adapter->TransmitPhysical);
    NdisAllocateMemory((PVOID *)&Adapter->FrameBuffer, FRAME_BUFFER_SIZE, 0, HighestAcceptableMax);
    NdisAllocateMemory((PVOID *)&Adapter->TransmitFrame, TRANSMIT_FRAME_SIZE, 0, HighestAcceptableMax);
    if (Adapter->ReceiveBuffer == NULL || Adapter->TransmitBuffer == NULL || Adapter->FrameBuffer == NULL ||
        Adapter->TransmitFrame == NULL) {
        BcmethLogFailure(Adapter, 4, NDIS_STATUS_RESOURCES);
        NdisMFreeMapRegisters(MiniportAdapterHandle);
        BcmethFreeAdapter(Adapter);
        return NDIS_STATUS_RESOURCES;
    }

    //
    // The IP header of an indicated frame falls on a longword.
    //

    Adapter->Frame = Adapter->FrameBuffer + 2;
    Adapter->PacketFilter = 0;
    NdisMInitializeTimer(&Adapter->RequestTimer, MiniportAdapterHandle, BcmethTimer, Adapter);

    Status = ChipStart(Adapter);
    if (Status == NDIS_STATUS_SUCCESS &&
        (!UsbFunctionOpenPipe(Adapter->Device, Adapter->BulkIn, TRUE, Adapter->BulkInMaximum,
                              &Adapter->ReceivePipe) ||
         !UsbFunctionOpenPipe(Adapter->Device, Adapter->BulkOut, FALSE, Adapter->BulkOutMaximum,
                              &Adapter->TransmitPipe))) {
        Status = NDIS_STATUS_RESOURCES;
    }

    if (Status == NDIS_STATUS_SUCCESS) {
        Status = NdisMRegisterInterrupt(&Adapter->Interrupt, MiniportAdapterHandle, Hardware.InterruptVector,
                                        Hardware.InterruptLevel, TRUE, TRUE, NdisInterruptLevelSensitive);
    }

    if (Status != NDIS_STATUS_SUCCESS) {
        BcmethLogFailure(Adapter, 5, Status);
        if (Adapter->ReceivePipe.Channel != 0) {
            UsbFunctionClosePipe(&Adapter->ReceivePipe);
        }

        if (Adapter->TransmitPipe.Channel != 0) {
            UsbFunctionClosePipe(&Adapter->TransmitPipe);
        }

        ChipStop(Adapter);
        NdisMFreeMapRegisters(MiniportAdapterHandle);
        BcmethFreeAdapter(Adapter);
        return Status;
    }

    ChipCheckLink(Adapter);
    BcmethStartReceive(Adapter);

    //
    // The handle is of the process this runs in; halt can run in any (winlogon at
    // shutdown), so keep the thread object.
    //

    if (NT_SUCCESS(PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, BcmethThread, Adapter))) {
        ObReferenceObjectByHandle(Thread, THREAD_ALL_ACCESS, NULL, KernelMode, &Adapter->Thread, NULL);
        ZwClose(Thread);
    }

    return NDIS_STATUS_SUCCESS;
}

static VOID
BcmethHalt(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;

    Adapter->Halting = TRUE;
    KeSetEvent(&Adapter->WorkEvent, 0, FALSE);
    if (Adapter->Thread != NULL) {
        KeWaitForSingleObject(Adapter->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->Thread);
    }

    NdisMDeregisterInterrupt(&Adapter->Interrupt);
    UsbFunctionClosePipe(&Adapter->ReceivePipe);
    UsbFunctionClosePipe(&Adapter->TransmitPipe);
    ChipStop(Adapter);
    NdisMFreeMapRegisters(Adapter->MiniportAdapterHandle);
    BcmethFreeAdapter(Adapter);
}

//
// A reset: the thread stops the controller and starts it again with fresh
// pipes, and the timer drops the transmits in the ring and starts them.
//

static NDIS_STATUS
BcmethReset(
    OUT PBOOLEAN AddressingReset,
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;

    *AddressingReset = FALSE;
    Adapter->Resetting = TRUE;
    BcmethQueueWork(Adapter, WORK_RESET);
    return NDIS_STATUS_PENDING;
}

//
// The transmitter hangs if a transfer in the ring has not completed over
// two checks (four seconds).
//

static BOOLEAN
BcmethCheckForHang(
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;
    ULONG Transmits = Adapter->TransmitOk + Adapter->TransmitError;

    if (Adapter->Resetting || !Adapter->TransmitBusy || Transmits != Adapter->LastTransmits) {
        Adapter->LastTransmits = Transmits;
        Adapter->HangChecks = 0;
        return FALSE;
    }

    Adapter->HangChecks += 1;
    return Adapter->HangChecks >= 2;
}

//
// A transmit: the frame is copied behind the controller's header into the
// next slot of the ring (padded to the minimum frame) and completes.
//

static NDIS_STATUS
BcmethSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_PACKET Packet,
    IN UINT Flags
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;
    PNDIS_BUFFER Buffer;
    PVOID Data;
    UINT Length;
    UINT Total;
    UINT Header;
    UINT Copied = 0;
    UINT Frame;

    if (Adapter->Resetting) {
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }

    if (Adapter->TransmitCount == TRANSMIT_SLOTS) {
        Adapter->ResourcesNeeded = TRUE;
        return NDIS_STATUS_RESOURCES;
    }

    NdisQueryPacket(Packet, NULL, NULL, &Buffer, &Total);
    if (Total < ETH_HEADER_SIZE || Total > ETH_MAXIMUM_FRAME) {
        Adapter->TransmitError += 1;
        return NDIS_STATUS_FAILURE;
    }

    Frame = Total < ETH_MINIMUM_FRAME ? ETH_MINIMUM_FRAME : Total;
    Header = ChipFrameHeader(Adapter, Adapter->TransmitFrame, Frame);
    while (Buffer != NULL) {
        NdisQueryBuffer(Buffer, &Data, &Length);
        NdisMoveMemory(Adapter->TransmitFrame + Header + Copied, Data, Length);
        Copied += Length;
        NdisGetNextBuffer(Buffer, &Buffer);
    }

    NdisZeroMemory(Adapter->TransmitFrame + Header + Copied, Frame - Copied);
    MoveToShared(Adapter->TransmitBuffer + Adapter->TransmitTail * TRANSMIT_SLOT_SIZE, Adapter->TransmitFrame,
                 Header + Frame);
    Adapter->TransmitLength[Adapter->TransmitTail] = Header + Frame;
    Adapter->TransmitTail = (Adapter->TransmitTail + 1) % TRANSMIT_SLOTS;
    Adapter->TransmitCount += 1;
    if (!Adapter->TransmitBusy) {
        BcmethStartTransmit(Adapter);
    }

    return NDIS_STATUS_SUCCESS;
}

//
// The rest of a frame indicated, copied into a packet.
//

static NDIS_STATUS
BcmethTransferData(
    OUT PNDIS_PACKET Packet,
    OUT PUINT BytesTransferred,
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HANDLE MiniportReceiveContext,
    IN UINT ByteOffset,
    IN UINT BytesToTransfer
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;
    PUCHAR Source = Adapter->Frame + ETH_HEADER_SIZE + ByteOffset;
    PNDIS_BUFFER Buffer;
    PVOID Data;
    UINT Length;
    UINT Left;
    UINT Moved = 0;

    if (ETH_HEADER_SIZE + ByteOffset >= Adapter->FrameLength) {
        *BytesTransferred = 0;
        return NDIS_STATUS_SUCCESS;
    }

    Left = Adapter->FrameLength - ETH_HEADER_SIZE - ByteOffset;
    if (BytesToTransfer < Left) {
        Left = BytesToTransfer;
    }

    NdisQueryPacket(Packet, NULL, NULL, &Buffer, NULL);
    while (Buffer != NULL && Left != 0) {
        NdisQueryBuffer(Buffer, &Data, &Length);
        if (Length > Left) {
            Length = Left;
        }

        NdisMoveMemory(Data, Source + Moved, Length);
        Moved += Length;
        Left -= Length;
        NdisGetNextBuffer(Buffer, &Buffer);
    }

    *BytesTransferred = Moved;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
BcmethQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;
    PVOID Source;
    ULONG SourceLength = sizeof(ULONG);
    ULONG Value = 0;
    USHORT Version;
    UCHAR VendorId[4];

    Source = &Value;
    switch (Oid) {
    case OID_GEN_SUPPORTED_LIST:
        Source = (PVOID)BcmethSupportedOids;
        SourceLength = sizeof(BcmethSupportedOids);
        break;

    case OID_GEN_HARDWARE_STATUS:
        Value = NdisHardwareStatusReady;
        break;

    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        Value = NdisMedium802_3;
        break;

    case OID_GEN_MAXIMUM_LOOKAHEAD:
    case OID_GEN_CURRENT_LOOKAHEAD:
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        Value = ETH_MAXIMUM_FRAME - ETH_HEADER_SIZE;
        break;

    case OID_GEN_MAXIMUM_TOTAL_SIZE:
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        Value = ETH_MAXIMUM_FRAME;
        break;

    case OID_GEN_MAC_OPTIONS:
        Value = NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                NDIS_MAC_OPTION_RECEIVE_SERIALIZED | NDIS_MAC_OPTION_NO_LOOPBACK;
        break;

    case OID_GEN_LINK_SPEED:
        Value = Adapter->LinkSpeed != 0 ? Adapter->LinkSpeed : 100000;
        break;

    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        Value = TRANSMIT_SLOTS * TRANSMIT_SLOT_SIZE;
        break;

    case OID_GEN_RECEIVE_BUFFER_SPACE:
        Value = RECEIVE_BUFFER_SIZE;
        break;

    case OID_GEN_VENDOR_ID:
        NdisMoveMemory(VendorId, Adapter->PermanentAddress, 3);
        VendorId[3] = 0;
        Source = VendorId;
        SourceLength = sizeof(VendorId);
        break;

    case OID_GEN_VENDOR_DESCRIPTION:
        Source = (PVOID)(Adapter->Chip == CHIP_LAN9500 ? Lan9500Description :
                         Adapter->Chip == CHIP_LAN7800 ? Lan7800Description : CdcDescription);
        SourceLength = Adapter->Chip == CHIP_LAN9500 ? sizeof(Lan9500Description) :
                       Adapter->Chip == CHIP_LAN7800 ? sizeof(Lan7800Description) : sizeof(CdcDescription);
        break;

    case OID_GEN_DRIVER_VERSION:
        Version = (BCMETH_NDIS_MAJOR_VERSION << 8) | BCMETH_NDIS_MINOR_VERSION;
        Source = &Version;
        SourceLength = sizeof(USHORT);
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        Value = Adapter->PacketFilter;
        break;

    case OID_GEN_XMIT_OK:
        Value = Adapter->TransmitOk;
        break;

    case OID_GEN_RCV_OK:
        Value = Adapter->ReceiveOk;
        break;

    case OID_GEN_XMIT_ERROR:
        Value = Adapter->TransmitError;
        break;

    case OID_GEN_RCV_ERROR:
        Value = Adapter->ReceiveError;
        break;

    case OID_GEN_RCV_NO_BUFFER:
        Value = Adapter->ReceiveNoBuffer;
        break;

    case OID_GEN_RCV_CRC_ERROR:
        Value = Adapter->ReceiveCrcError;
        break;

    case OID_802_3_PERMANENT_ADDRESS:
        Source = Adapter->PermanentAddress;
        SourceLength = ETH_LENGTH_OF_ADDRESS;
        break;

    case OID_802_3_CURRENT_ADDRESS:
        Source = Adapter->CurrentAddress;
        SourceLength = ETH_LENGTH_OF_ADDRESS;
        break;

    case OID_802_3_MULTICAST_LIST:
        Source = Adapter->Multicast;
        SourceLength = Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS;
        break;

    case OID_802_3_MAXIMUM_LIST_SIZE:
        Value = BCMETH_MULTICAST_LIST;
        break;

    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
        Value = 0;
        break;

    default:
        *BytesWritten = 0;
        return NDIS_STATUS_INVALID_OID;
    }

    if (SourceLength > InformationBufferLength) {
        *BytesNeeded = SourceLength;
        return NDIS_STATUS_INVALID_LENGTH;
    }

    NdisMoveMemory(InformationBuffer, Source, SourceLength);
    *BytesWritten = SourceLength;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
BcmethSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded
    )
{
    PBCMETH_ADAPTER Adapter = MiniportAdapterContext;
    ULONG Filter;

    switch (Oid) {
    case OID_802_3_MULTICAST_LIST:
        if (InformationBufferLength % ETH_LENGTH_OF_ADDRESS != 0 ||
            InformationBufferLength > BCMETH_MULTICAST_LIST * ETH_LENGTH_OF_ADDRESS) {
            *BytesNeeded = ETH_LENGTH_OF_ADDRESS;
            return NDIS_STATUS_INVALID_LENGTH;
        }

        NdisMoveMemory(Adapter->Multicast, InformationBuffer, InformationBufferLength);
        Adapter->MulticastCount = InformationBufferLength / ETH_LENGTH_OF_ADDRESS;
        *BytesRead = InformationBufferLength;
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        if (InformationBufferLength != sizeof(ULONG)) {
            *BytesNeeded = sizeof(ULONG);
            return NDIS_STATUS_INVALID_LENGTH;
        }

        NdisMoveMemory(&Filter, InformationBuffer, sizeof(ULONG));
        *BytesRead = sizeof(ULONG);
        if (Filter & (NDIS_PACKET_TYPE_SOURCE_ROUTING | NDIS_PACKET_TYPE_SMT | NDIS_PACKET_TYPE_MAC_FRAME |
                      NDIS_PACKET_TYPE_FUNCTIONAL | NDIS_PACKET_TYPE_ALL_FUNCTIONAL | NDIS_PACKET_TYPE_GROUP)) {
            return NDIS_STATUS_NOT_SUPPORTED;
        }

        Adapter->PacketFilter = Filter;
        break;

    case OID_GEN_CURRENT_LOOKAHEAD:
    case OID_GEN_PROTOCOL_OPTIONS:
        *BytesRead = InformationBufferLength;
        return NDIS_STATUS_SUCCESS;

    default:
        return NDIS_STATUS_INVALID_OID;
    }

    BcmethQueueWork(Adapter, WORK_FILTER);
    return NDIS_STATUS_PENDING;
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine registers the miniport with the wrapper, which initializes
    each adapter of its service.

Arguments:

    DriverObject - Supplies the driver object.

    RegistryPath - Supplies the service key of the driver.

Return Value:

    The status of the registration.

--*/

{
    NDIS_MINIPORT_CHARACTERISTICS Characteristics;
    NDIS_HANDLE WrapperHandle;
    NDIS_STATUS Status;

    NdisMInitializeWrapper(&WrapperHandle, DriverObject, RegistryPath, NULL);
    NdisZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.MajorNdisVersion = BCMETH_NDIS_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = BCMETH_NDIS_MINOR_VERSION;
    Characteristics.CheckForHangHandler = BcmethCheckForHang;
    Characteristics.DisableInterruptHandler = BcmethDisableInterrupt;
    Characteristics.EnableInterruptHandler = BcmethEnableInterrupt;
    Characteristics.HaltHandler = BcmethHalt;
    Characteristics.HandleInterruptHandler = BcmethHandleInterrupt;
    Characteristics.InitializeHandler = BcmethInitialize;
    Characteristics.ISRHandler = BcmethIsr;
    Characteristics.QueryInformationHandler = BcmethQueryInformation;
    Characteristics.ReconfigureHandler = NULL;
    Characteristics.ResetHandler = BcmethReset;
    Characteristics.SendHandler = BcmethSend;
    Characteristics.SetInformationHandler = BcmethSetInformation;
    Characteristics.TransferDataHandler = BcmethTransferData;

    Status = NdisMRegisterMiniport(WrapperHandle, &Characteristics, sizeof(Characteristics));
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisTerminateWrapper(WrapperHandle, NULL);
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;
}
