/*++

Module Name:

    bcmemmc.c

Abstract:

    This is the miniport driver for the SD card on the EMMC controller (an
    Arasan SD host controller) of the BCM2835 in the Raspberry Pi 2 and 3, to
    which the ARC firmware connects the card slot, or on the EMMC2 controller
    of the BCM2711 in the Raspberry Pi 4 (SDHCI 3.0 too), which holds the card
    slot of every BCM2711 board. The card is presented to
    the SCSI class drivers as a direct access device at target 0, as the ATAPI
    miniport presents IDE disks.

    The firmware describes the controller as the ScsiAdapter of the system,
    with the controller registers and the VideoCore mailbox as its two memory
    ranges. Blocks move by programmed I/O through the data port, one block on
    each buffer ready interrupt. The controller's base clock is asked of the
    VideoCore through the mailbox property channel; the EMMC2, 0x340000 into
    the BCM2835 peripherals (the EMMC is at 0x300000), has a clock of its own.

Environment:

    kernel mode only

--*/

#include "miniport.h"
#include "scsi.h"

//
// EMMC registers and their fields.
//

#define BLKSIZECNT          0x04
#define ARG1                0x08
#define CMDTM               0x0C
#define RESP0               0x10
#define RESP1               0x14
#define RESP2               0x18
#define RESP3               0x1C
#define DATA                0x20
#define STATUS              0x24
#define CONTROL0            0x28
#define CONTROL1            0x2C
#define INTERRUPT           0x30
#define IRPT_MASK           0x34
#define IRPT_EN             0x38

#define STATUS_CMD_INHIBIT  0x00000001
#define STATUS_DAT_INHIBIT  0x00000002

#define C0_HCTL_DWIDTH      0x00000002
#define C0_POWER_330        0x00000F00

#define C1_CLK_INTLEN       0x00000001
#define C1_CLK_STABLE       0x00000002
#define C1_CLK_EN           0x00000004
#define C1_TOUNIT_MAX       0x000E0000
#define C1_SRST_HC          0x01000000
#define C1_SRST_CMD         0x02000000
#define C1_SRST_DATA        0x04000000

#define INT_CMD_DONE        0x00000001
#define INT_DATA_DONE       0x00000002
#define INT_WRITE_RDY       0x00000010
#define INT_READ_RDY        0x00000020
#define INT_ERR             0x00008000
#define INT_ERRORS          0x017F0000

#define TM_BLKCNT_EN        0x00000002
#define TM_AUTO_CMD12       0x00000004
#define TM_DAT_DIR_READ     0x00000010
#define TM_MULTI_BLOCK      0x00000020

#define RSP_NONE            0x00000000
#define RSP_136             0x00010000
#define RSP_48              0x00020000
#define RSP_48_BUSY         0x00030000
#define CMD_CRCCHK          0x00080000
#define CMD_IXCHK           0x00100000
#define CMD_ISDATA          0x00200000

#define R1                  (RSP_48 | CMD_CRCCHK | CMD_IXCHK)
#define R1B                 (RSP_48_BUSY | CMD_CRCCHK | CMD_IXCHK)
#define R2                  (RSP_136 | CMD_CRCCHK)
#define R3                  RSP_48
#define R6                  R1
#define R7                  R1

#define CMD(n)              ((ULONG)(n) << 24)
#define APP                 0x80000000

//
// VideoCore mailbox registers and the property channel.
//

#define MBOX_READ           0x00
#define MBOX_STATUS         0x18
#define MBOX_WRITE          0x20
#define MBOX_WR_FULL        0x80000000
#define MBOX_RD_EMPTY       0x40000000
#define MBOX_PROPERTY       8
#define MBOX_SUCCESS        0x80000000
#define TAG_GET_CLOCK_RATE  0x00030002
#define TAG_GET_MAX_CLOCK_RATE 0x00030004
#define CLOCK_EMMC          1
#define CLOCK_EMMC2         12
#define EMMC2_OFFSET        0x340000
#define PROPERTY_LENGTH     8

#define SECTOR_SIZE         512
#define SECTOR_ULONGS       (SECTOR_SIZE / sizeof(ULONG))
#define IDENT_CLOCK         400000
#define TRANSFER_CLOCK      25000000
#define MAXIMUM_TRANSFER    0x10000
#define TRANSFER_PAGES      (MAXIMUM_TRANSFER / 0x1000)
#define TIMEOUT_US          1000000

typedef struct _HW_DEVICE_EXTENSION {
    PULONG Registers;
    PULONG Mailbox;
    PULONG Property;
    ULONG PropertyAddress;
    ULONG ClockId;
    ULONG BaseClock;
    ULONG WriteDelay;
    ULONG Rca;
    ULONG Sectors;
    BOOLEAN HighCapacity;
    BOOLEAN CardPresent;
    BOOLEAN Read;
    PSCSI_REQUEST_BLOCK CurrentSrb;
    PUCHAR DataBuffer;
    ULONG BlocksLeft;
    ULONG Bounce[SECTOR_ULONGS];
} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;

BOOLEAN
BcmEmmcResetBus(
    IN PVOID HwDeviceExtension,
    IN ULONG PathId
    );

static ULONG
EmmcRead(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Register
    )
{
    return ScsiPortReadRegisterUlong(DeviceExtension->Registers + Register / sizeof(ULONG));
}

//
// The controller loses a register write that follows another within two SD
// clocks, so every write but the data port's is followed by a delay.
//

static VOID
EmmcWrite(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Register,
    IN ULONG Value
    )
{
    ScsiPortWriteRegisterUlong(DeviceExtension->Registers + Register / sizeof(ULONG), Value);
    ScsiPortStallExecution(DeviceExtension->WriteDelay);
}

static BOOLEAN
EmmcWait(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Register,
    IN ULONG Mask,
    IN ULONG Value
    )
{
    ULONG Time;

    for (Time = 0; Time < TIMEOUT_US; Time += 1) {
        if ((EmmcRead(DeviceExtension, Register) & Mask) == Value) {
            return TRUE;
        }

        ScsiPortStallExecution(1);
    }

    return FALSE;
}

static BOOLEAN
MailboxWait(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Mask
    )
{
    ULONG Time;

    for (Time = 0; Time < TIMEOUT_US; Time += 1) {
        if ((ScsiPortReadRegisterUlong(DeviceExtension->Mailbox + MBOX_STATUS / sizeof(ULONG)) & Mask) == 0) {
            return TRUE;
        }

        ScsiPortStallExecution(1);
    }

    return FALSE;
}

//
// Ask the VideoCore for a rate of the EMMC clock: its rate, or its maximum rate
// (Tag). The property buffer is uncached memory the VideoCore reads and writes;
// it is filled with register writes so it is in memory before the mailbox write
// that hands it over.
//

static ULONG
EmmcGetClock(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Tag
    )
{
    PULONG Buffer = DeviceExtension->Property;
    ULONG Request[PROPERTY_LENGTH];
    ULONG Index;
    ULONG Message;

    Request[0] = PROPERTY_LENGTH * sizeof(ULONG);
    Request[1] = 0;
    Request[2] = Tag;
    Request[3] = 2 * sizeof(ULONG);
    Request[4] = sizeof(ULONG);
    Request[5] = DeviceExtension->ClockId;
    Request[6] = 0;
    Request[7] = 0;
    for (Index = 0; Index < PROPERTY_LENGTH; Index += 1) {
        ScsiPortWriteRegisterUlong(&Buffer[Index], Request[Index]);
    }

    if (!MailboxWait(DeviceExtension, MBOX_WR_FULL)) {
        return 0;
    }

    ScsiPortWriteRegisterUlong(DeviceExtension->Mailbox + MBOX_WRITE / sizeof(ULONG),
                               DeviceExtension->PropertyAddress | MBOX_PROPERTY);

    do {
        if (!MailboxWait(DeviceExtension, MBOX_RD_EMPTY)) {
            return 0;
        }

        Message = ScsiPortReadRegisterUlong(DeviceExtension->Mailbox + MBOX_READ / sizeof(ULONG));
    } while ((Message & 0xF) != MBOX_PROPERTY);

    ScsiPortWriteRegisterUlong(&Buffer[7], 0);
    if (ScsiPortReadRegisterUlong(&Buffer[1]) != MBOX_SUCCESS) {
        return 0;
    }

    return ScsiPortReadRegisterUlong(&Buffer[6]);
}

//
// Program the SD clock: the 10-bit divided clock is base / (2 * divisor).
//

static BOOLEAN
EmmcSetClock(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Hz
    )
{
    ULONG Divisor = (DeviceExtension->BaseClock + 2 * Hz - 1) / (2 * Hz);
    ULONG Control;

    if (Divisor > 0x3FF) {
        Divisor = 0x3FF;
    }

    Control = EmmcRead(DeviceExtension, CONTROL1) & ~(C1_CLK_EN | 0xFFE0);
    EmmcWrite(DeviceExtension, CONTROL1, Control);
    Control |= ((Divisor & 0xFF) << 8) | ((Divisor >> 8) << 6) | C1_CLK_INTLEN | C1_TOUNIT_MAX;
    EmmcWrite(DeviceExtension, CONTROL1, Control);
    if (!EmmcWait(DeviceExtension, CONTROL1, C1_CLK_STABLE, C1_CLK_STABLE)) {
        return FALSE;
    }

    EmmcWrite(DeviceExtension, CONTROL1, Control | C1_CLK_EN);
    DeviceExtension->WriteDelay = 2 * 1000000 / (DeviceExtension->BaseClock / (2 * Divisor)) + 1;
    return TRUE;
}

static VOID
EmmcResetLines(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Lines
    )
{
    EmmcWrite(DeviceExtension, CONTROL1, EmmcRead(DeviceExtension, CONTROL1) | Lines);
    EmmcWait(DeviceExtension, CONTROL1, Lines, 0);
}

//
// Issue a command and wait for its response. A data command's blocks move
// afterwards; an application command is prefixed with CMD55.
//

static BOOLEAN
EmmcCommand(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN ULONG Command,
    IN ULONG Argument,
    IN ULONG Blocks
    )
{
    ULONG Busy = STATUS_CMD_INHIBIT;

    if ((Command & APP) != 0 &&
        !EmmcCommand(DeviceExtension, CMD(55) | R1, DeviceExtension->Rca << 16, 0)) {
        return FALSE;
    }

    Command &= ~APP;
    if ((Command & (CMD_ISDATA | RSP_48_BUSY)) != 0) {
        Busy |= STATUS_DAT_INHIBIT;
    }

    if (!EmmcWait(DeviceExtension, STATUS, Busy, 0)) {
        return FALSE;
    }

    EmmcWrite(DeviceExtension, INTERRUPT, 0xFFFFFFFF);
    if (Blocks != 0) {
        EmmcWrite(DeviceExtension, BLKSIZECNT, (Blocks << 16) | SECTOR_SIZE);
    }

    EmmcWrite(DeviceExtension, ARG1, Argument);
    EmmcWrite(DeviceExtension, CMDTM, Command);
    if (!EmmcWait(DeviceExtension, INTERRUPT, INT_CMD_DONE | INT_ERR, INT_CMD_DONE) ||
        (EmmcRead(DeviceExtension, INTERRUPT) & INT_ERR) != 0) {
        EmmcWrite(DeviceExtension, INTERRUPT, 0xFFFFFFFF);
        EmmcResetLines(DeviceExtension, C1_SRST_CMD | C1_SRST_DATA);
        return FALSE;
    }

    EmmcWrite(DeviceExtension, INTERRUPT, INT_CMD_DONE);
    if ((Command & RSP_48_BUSY) == RSP_48_BUSY && (Command & CMD_ISDATA) == 0) {
        EmmcWait(DeviceExtension, INTERRUPT, INT_DATA_DONE, INT_DATA_DONE);
        EmmcWrite(DeviceExtension, INTERRUPT, INT_DATA_DONE);
    }

    return TRUE;
}

//
// The card's capacity in sectors from its CSD (the controller drops the CRC
// byte, so RESP0-3 hold CSD bits 127:8).
//

static ULONG
EmmcCapacity(
    IN PHW_DEVICE_EXTENSION DeviceExtension
    )
{
    ULONG Resp1 = EmmcRead(DeviceExtension, RESP1);
    ULONG Resp2 = EmmcRead(DeviceExtension, RESP2);
    ULONG Size;

    if (((EmmcRead(DeviceExtension, RESP3) >> 22) & 3) == 1) {
        return (((Resp1 >> 8) & 0x3FFFFF) + 1) * 1024;
    }

    Size = ((Resp2 & 0x3) << 10) | (Resp1 >> 22);
    return ((Size + 1) << (((Resp1 >> 7) & 0x7) + 2 + ((Resp2 >> 8) & 0xF))) / SECTOR_SIZE;
}

//
// Reset the controller and bring the card to the transfer state, as the SD
// physical layer specifies: CMD0, CMD8, ACMD41, CMD2, CMD3, CMD9, CMD7 and
// ACMD6 for the 4-bit bus. A standard capacity card is byte addressed, a high
// capacity card block addressed.
//

static BOOLEAN
EmmcInitializeCard(
    IN PHW_DEVICE_EXTENSION DeviceExtension
    )
{
    ULONG Ocr = 0;
    ULONG Index;
    BOOLEAN Version2;

    DeviceExtension->WriteDelay = 2 * 1000000 / IDENT_CLOCK + 1;
    EmmcWrite(DeviceExtension, CONTROL0, 0);
    EmmcWrite(DeviceExtension, CONTROL1, C1_SRST_HC);
    if (!EmmcWait(DeviceExtension, CONTROL1, C1_SRST_HC, 0)) {
        return FALSE;
    }

    EmmcWrite(DeviceExtension, CONTROL0, C0_POWER_330);
    if (!EmmcSetClock(DeviceExtension, IDENT_CLOCK)) {
        return FALSE;
    }

    EmmcWrite(DeviceExtension, IRPT_EN, 0);
    EmmcWrite(DeviceExtension, IRPT_MASK, 0xFFFFFFFF);
    EmmcWrite(DeviceExtension, INTERRUPT, 0xFFFFFFFF);

    DeviceExtension->Rca = 0;
    EmmcCommand(DeviceExtension, CMD(0) | RSP_NONE, 0, 0);
    Version2 = EmmcCommand(DeviceExtension, CMD(8) | R7, 0x1AA, 0) &&
               (EmmcRead(DeviceExtension, RESP0) & 0xFFF) == 0x1AA;

    for (Index = 0; Index < 1000; Index += 1) {
        if (!EmmcCommand(DeviceExtension, APP | CMD(41) | R3, (Version2 ? 0x40000000 : 0) | 0x00FF8000, 0)) {
            return FALSE;
        }

        Ocr = EmmcRead(DeviceExtension, RESP0);
        if ((Ocr & 0x80000000) != 0) {
            break;
        }

        ScsiPortStallExecution(1000);
    }

    if ((Ocr & 0x80000000) == 0) {
        return FALSE;
    }

    DeviceExtension->HighCapacity = (BOOLEAN)((Ocr & 0x40000000) != 0);
    if (!EmmcCommand(DeviceExtension, CMD(2) | R2, 0, 0) ||
        !EmmcCommand(DeviceExtension, CMD(3) | R6, 0, 0)) {
        return FALSE;
    }

    DeviceExtension->Rca = EmmcRead(DeviceExtension, RESP0) >> 16;
    if (!EmmcCommand(DeviceExtension, CMD(9) | R2, DeviceExtension->Rca << 16, 0)) {
        return FALSE;
    }

    DeviceExtension->Sectors = EmmcCapacity(DeviceExtension);
    if (!EmmcCommand(DeviceExtension, CMD(7) | R1B, DeviceExtension->Rca << 16, 0) ||
        !EmmcSetClock(DeviceExtension, TRANSFER_CLOCK)) {
        return FALSE;
    }

    if (EmmcCommand(DeviceExtension, APP | CMD(6) | R1, 2, 0)) {
        EmmcWrite(DeviceExtension, CONTROL0, EmmcRead(DeviceExtension, CONTROL0) | C0_HCTL_DWIDTH);
    }

    if (!DeviceExtension->HighCapacity &&
        !EmmcCommand(DeviceExtension, CMD(16) | R1, SECTOR_SIZE, 0)) {
        return FALSE;
    }

    return TRUE;
}

//
// Complete the current request.
//

static VOID
EmmcComplete(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN UCHAR Status
    )
{
    Srb->SrbStatus = Status;
    ScsiPortNotification(RequestComplete, DeviceExtension, Srb);
    ScsiPortNotification(NextRequest, DeviceExtension, NULL);
}

//
// Start a read or write: the blocks then move on the buffer ready interrupts.
//

static UCHAR
EmmcReadWrite(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PCDB Cdb = (PCDB)Srb->Cdb;
    ULONG Lba;
    ULONG Count;
    ULONG Command;
    BOOLEAN Read = (BOOLEAN)(Srb->Cdb[0] == SCSIOP_READ);

    Lba = ((ULONG)Cdb->CDB10.LogicalBlockByte0 << 24) | ((ULONG)Cdb->CDB10.LogicalBlockByte1 << 16) |
          ((ULONG)Cdb->CDB10.LogicalBlockByte2 << 8) | Cdb->CDB10.LogicalBlockByte3;
    Count = ((ULONG)Cdb->CDB10.TransferBlocksMsb << 8) | Cdb->CDB10.TransferBlocksLsb;
    if (Count == 0) {
        return SRB_STATUS_SUCCESS;
    }

    if (Lba + Count > DeviceExtension->Sectors || Count * SECTOR_SIZE > Srb->DataTransferLength) {
        return SRB_STATUS_ERROR;
    }

    if (Count == 1) {
        Command = Read ? (CMD(17) | R1 | CMD_ISDATA | TM_DAT_DIR_READ) : (CMD(24) | R1 | CMD_ISDATA);
    } else {
        Command = (Read ? (CMD(18) | TM_DAT_DIR_READ) : CMD(25)) |
                  R1 | CMD_ISDATA | TM_MULTI_BLOCK | TM_BLKCNT_EN | TM_AUTO_CMD12;
    }

    DeviceExtension->CurrentSrb = Srb;
    DeviceExtension->DataBuffer = Srb->DataBuffer;
    DeviceExtension->BlocksLeft = Count;
    DeviceExtension->Read = Read;
    if (!EmmcCommand(DeviceExtension, Command,
                     DeviceExtension->HighCapacity ? Lba : Lba * SECTOR_SIZE, Count)) {
        DeviceExtension->CurrentSrb = NULL;
        return SRB_STATUS_ERROR;
    }

    return SRB_STATUS_PENDING;
}

//
// Emulate the SCSI commands of a direct access device.
//

static UCHAR
EmmcExecuteScsi(
    IN PHW_DEVICE_EXTENSION DeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    INQUIRYDATA Inquiry;
    PREAD_CAPACITY_DATA Capacity;
    PMODE_PARAMETER_HEADER Header;
    ULONG Last;
    ULONG Index;

    switch (Srb->Cdb[0]) {

    case SCSIOP_INQUIRY:

        for (Index = 0; Index < sizeof(INQUIRYDATA); Index += 1) {
            ((PUCHAR)&Inquiry)[Index] = 0;
        }

        Inquiry.DeviceType = DIRECT_ACCESS_DEVICE;
        Inquiry.Versions = 2;
        Inquiry.ResponseDataFormat = 2;
        Inquiry.AdditionalLength = sizeof(INQUIRYDATA) - 5;
        ScsiPortMoveMemory(Inquiry.VendorId, "BCM2835 ", 8);
        ScsiPortMoveMemory(Inquiry.ProductId, "SD Card         ", 16);
        ScsiPortMoveMemory(Inquiry.ProductRevisionLevel, "1.0 ", 4);
        if (Srb->DataTransferLength > sizeof(INQUIRYDATA)) {
            Srb->DataTransferLength = sizeof(INQUIRYDATA);
        }

        ScsiPortMoveMemory(Srb->DataBuffer, &Inquiry, Srb->DataTransferLength);
        return SRB_STATUS_SUCCESS;

    case SCSIOP_READ_CAPACITY:

        if (Srb->DataTransferLength < sizeof(READ_CAPACITY_DATA)) {
            return SRB_STATUS_ERROR;
        }

        Capacity = Srb->DataBuffer;
        Last = DeviceExtension->Sectors - 1;
        Capacity->LogicalBlockAddress = (Last >> 24) | ((Last >> 8) & 0xFF00) |
                                        ((Last << 8) & 0xFF0000) | (Last << 24);
        Capacity->BytesPerBlock = (SECTOR_SIZE >> 8) << 16;
        return SRB_STATUS_SUCCESS;

    case SCSIOP_MODE_SENSE:

        if (Srb->DataTransferLength < sizeof(MODE_PARAMETER_HEADER)) {
            return SRB_STATUS_ERROR;
        }

        Header = Srb->DataBuffer;
        Header->ModeDataLength = sizeof(MODE_PARAMETER_HEADER) - 1;
        Header->MediumType = 0;
        Header->DeviceSpecificParameter = 0;
        Header->BlockDescriptorLength = 0;
        Srb->DataTransferLength = sizeof(MODE_PARAMETER_HEADER);
        return SRB_STATUS_SUCCESS;

    case SCSIOP_READ:
    case SCSIOP_WRITE:

        return EmmcReadWrite(DeviceExtension, Srb);

    case SCSIOP_TEST_UNIT_READY:
    case SCSIOP_VERIFY:
    case SCSIOP_START_STOP_UNIT:
    case SCSIOP_MEDIUM_REMOVAL:
    case SCSIOP_REZERO_UNIT:
    case SCSIOP_SEEK:
    case SCSIOP_RESERVE_UNIT:
    case SCSIOP_RELEASE_UNIT:

        return SRB_STATUS_SUCCESS;

    default:

        return SRB_STATUS_INVALID_REQUEST;
    }
}

BOOLEAN
BcmEmmcStartIo(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )

/*++

Routine Description:

    This routine is called from the SCSI port driver synchronized with the
    kernel to start a request.

Arguments:

    HwDeviceExtension - Supplies the device extension.

    Srb - Supplies the request.

Return Value:

    TRUE

--*/

{
    PHW_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    UCHAR Status;

    switch (Srb->Function) {

    case SRB_FUNCTION_EXECUTE_SCSI:

        if (Srb->PathId != 0 || Srb->TargetId != 0 || Srb->Lun != 0 ||
            !DeviceExtension->CardPresent) {
            Status = SRB_STATUS_SELECTION_TIMEOUT;
            break;
        }

        Status = EmmcExecuteScsi(DeviceExtension, Srb);
        break;

    case SRB_FUNCTION_ABORT_COMMAND:
    case SRB_FUNCTION_RESET_BUS:

        BcmEmmcResetBus(DeviceExtension, Srb->PathId);
        Status = SRB_STATUS_SUCCESS;
        break;

    default:

        Status = SRB_STATUS_INVALID_REQUEST;
        break;
    }

    if (Status != SRB_STATUS_PENDING) {
        EmmcComplete(DeviceExtension, Srb, Status);
    }

    return TRUE;
}

BOOLEAN
BcmEmmcInterrupt(
    IN PVOID HwDeviceExtension
    )

/*++

Routine Description:

    This is the interrupt service routine: move a block through the data port
    on a buffer ready interrupt, and complete the request when the transfer is
    done or has failed. Every status bit taken is acknowledged first: the
    interrupt is level sensitive, and the controller can signal buffer ready
    when no block is left to move; with no request, the data line is reset.

Arguments:

    HwDeviceExtension - Supplies the device extension.

Return Value:

    TRUE if the controller was interrupting.

--*/

{
    PHW_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    PSCSI_REQUEST_BLOCK Srb = DeviceExtension->CurrentSrb;
    PULONG Data = DeviceExtension->Registers + DATA / sizeof(ULONG);
    PULONG Buffer;
    ULONG Status;
    ULONG Index;

    Status = EmmcRead(DeviceExtension, INTERRUPT) &
             (INT_DATA_DONE | INT_WRITE_RDY | INT_READ_RDY | INT_ERR | INT_ERRORS);
    if (Status == 0) {
        return FALSE;
    }

    EmmcWrite(DeviceExtension, INTERRUPT, Status);
    if (Srb == NULL) {
        if ((Status & (INT_READ_RDY | INT_WRITE_RDY)) != 0) {
            EmmcResetLines(DeviceExtension, C1_SRST_DATA);
        }

        return TRUE;
    }

    if ((Status & (INT_ERR | INT_ERRORS)) != 0) {
        EmmcWrite(DeviceExtension, INTERRUPT, 0xFFFFFFFF);
        EmmcResetLines(DeviceExtension, C1_SRST_CMD | C1_SRST_DATA);
        DeviceExtension->CurrentSrb = NULL;
        EmmcComplete(DeviceExtension, Srb, SRB_STATUS_ERROR);
        return TRUE;
    }

    if ((Status & (INT_READ_RDY | INT_WRITE_RDY)) != 0 && DeviceExtension->BlocksLeft != 0) {
        Buffer = ((ULONG)DeviceExtension->DataBuffer & 3) == 0 ?
                 (PULONG)DeviceExtension->DataBuffer : DeviceExtension->Bounce;
        if (DeviceExtension->Read) {
            for (Index = 0; Index < SECTOR_ULONGS; Index += 1) {
                Buffer[Index] = ScsiPortReadRegisterUlong(Data);
            }

            if (Buffer == DeviceExtension->Bounce) {
                ScsiPortMoveMemory(DeviceExtension->DataBuffer, Buffer, SECTOR_SIZE);
            }

        } else {
            if (Buffer == DeviceExtension->Bounce) {
                ScsiPortMoveMemory(Buffer, DeviceExtension->DataBuffer, SECTOR_SIZE);
            }

            for (Index = 0; Index < SECTOR_ULONGS; Index += 1) {
                ScsiPortWriteRegisterUlong(Data, Buffer[Index]);
            }
        }

        DeviceExtension->DataBuffer += SECTOR_SIZE;
        DeviceExtension->BlocksLeft -= 1;
    }

    if ((Status & INT_DATA_DONE) != 0) {
        DeviceExtension->CurrentSrb = NULL;
        EmmcComplete(DeviceExtension, Srb,
                     DeviceExtension->BlocksLeft == 0 ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR);
    }

    return TRUE;
}

BOOLEAN
BcmEmmcResetBus(
    IN PVOID HwDeviceExtension,
    IN ULONG PathId
    )

/*++

Routine Description:

    This routine resets the command and data lines of the controller and
    completes the outstanding request.

Arguments:

    HwDeviceExtension - Supplies the device extension.

    PathId - Supplies the bus to reset.

Return Value:

    TRUE

--*/

{
    PHW_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;

    EmmcResetLines(DeviceExtension, C1_SRST_CMD | C1_SRST_DATA);
    EmmcWrite(DeviceExtension, INTERRUPT, 0xFFFFFFFF);
    DeviceExtension->CurrentSrb = NULL;
    ScsiPortCompleteRequest(DeviceExtension, (UCHAR)PathId, (UCHAR)-1, (UCHAR)-1,
                            SRB_STATUS_BUS_RESET);
    ScsiPortNotification(NextRequest, DeviceExtension, NULL);
    return TRUE;
}

BOOLEAN
BcmEmmcInitialize(
    IN PVOID HwDeviceExtension
    )

/*++

Routine Description:

    This routine enables the interrupts of the controller: the end of a
    transfer, buffer ready, and the errors.

Arguments:

    HwDeviceExtension - Supplies the device extension.

Return Value:

    TRUE

--*/

{
    PHW_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;

    EmmcWrite(DeviceExtension, INTERRUPT, 0xFFFFFFFF);
    EmmcWrite(DeviceExtension, IRPT_EN, INT_DATA_DONE | INT_WRITE_RDY | INT_READ_RDY | INT_ERRORS);
    return TRUE;
}

ULONG
BcmEmmcFindAdapter(
    IN PVOID HwDeviceExtension,
    IN PVOID Context,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    )

/*++

Routine Description:

    This routine maps the controller and the mailbox the firmware described,
    asks the VideoCore for the base clock and brings the card to the transfer
    state.

Arguments:

    HwDeviceExtension - Supplies the device extension.

    Context - Not used.

    BusInformation - Not used.

    ArgumentString - Not used.

    ConfigInfo - Supplies the configuration information.

    Again - Returns FALSE: there is one controller.

Return Value:

    SP_RETURN_FOUND, SP_RETURN_NOT_FOUND or SP_RETURN_ERROR.

--*/

{
    PHW_DEVICE_EXTENSION DeviceExtension = HwDeviceExtension;
    PACCESS_RANGE Range = &(*ConfigInfo->AccessRanges)[0];
    ULONG Length;

    *Again = FALSE;
    if (Range[0].RangeLength == 0 || Range[1].RangeLength == 0) {
        return SP_RETURN_NOT_FOUND;
    }

    DeviceExtension->Registers = ScsiPortGetDeviceBase(DeviceExtension,
                                                       ConfigInfo->AdapterInterfaceType,
                                                       ConfigInfo->SystemIoBusNumber,
                                                       Range[0].RangeStart,
                                                       Range[0].RangeLength,
                                                       (BOOLEAN)!Range[0].RangeInMemory);
    DeviceExtension->Mailbox = ScsiPortGetDeviceBase(DeviceExtension,
                                                     ConfigInfo->AdapterInterfaceType,
                                                     ConfigInfo->SystemIoBusNumber,
                                                     Range[1].RangeStart,
                                                     Range[1].RangeLength,
                                                     (BOOLEAN)!Range[1].RangeInMemory);
    if (DeviceExtension->Registers == NULL || DeviceExtension->Mailbox == NULL) {
        return SP_RETURN_ERROR;
    }

    //
    // The VideoCore masters the property buffer.
    //

    ConfigInfo->Master = TRUE;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumTransferLength = MAXIMUM_TRANSFER;
    ConfigInfo->NumberOfPhysicalBreaks = TRANSFER_PAGES;
    ConfigInfo->InterruptMode = LevelSensitive;
    if (ConfigInfo->InitiatorBusId[0] == (CCHAR)SP_UNINITIALIZED_VALUE) {
        ConfigInfo->InitiatorBusId[0] = 7;
    }

    DeviceExtension->Property = ScsiPortGetUncachedExtension(DeviceExtension, ConfigInfo,
                                                             PROPERTY_LENGTH * sizeof(ULONG));
    if (DeviceExtension->Property == NULL) {
        return SP_RETURN_ERROR;
    }

    DeviceExtension->PropertyAddress = ScsiPortConvertPhysicalAddressToUlong(
        ScsiPortGetPhysicalAddress(DeviceExtension, NULL, DeviceExtension->Property, &Length));
    DeviceExtension->ClockId = CLOCK_EMMC;
    if ((ScsiPortConvertPhysicalAddressToUlong(Range[0].RangeStart) & 0xFFFFFF) == EMMC2_OFFSET) {
        DeviceExtension->ClockId = CLOCK_EMMC2;
    }

    DeviceExtension->BaseClock = EmmcGetClock(DeviceExtension, TAG_GET_CLOCK_RATE);
    if (DeviceExtension->BaseClock == 0) {
        DeviceExtension->BaseClock = EmmcGetClock(DeviceExtension, TAG_GET_MAX_CLOCK_RATE);
    }

    if (DeviceExtension->BaseClock == 0) {
        return SP_RETURN_ERROR;
    }

    DeviceExtension->CardPresent = EmmcInitializeCard(DeviceExtension);
    return SP_RETURN_FOUND;
}

ULONG
DriverEntry(
    IN PVOID DriverObject,
    IN PVOID Argument2
    )

/*++

Routine Description:

    Installable driver initialization entry point for system.

Arguments:

    DriverObject - Supplies the driver object.

    Argument2 - Supplies the registry path.

Return Value:

    Status from ScsiPortInitialize()

--*/

{
    HW_INITIALIZATION_DATA HwInitializationData;
    ULONG Index;

    for (Index = 0; Index < sizeof(HW_INITIALIZATION_DATA); Index += 1) {
        ((PUCHAR)&HwInitializationData)[Index] = 0;
    }

    HwInitializationData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);
    HwInitializationData.AdapterInterfaceType = Internal;
    HwInitializationData.HwInitialize = BcmEmmcInitialize;
    HwInitializationData.HwStartIo = BcmEmmcStartIo;
    HwInitializationData.HwInterrupt = BcmEmmcInterrupt;
    HwInitializationData.HwFindAdapter = BcmEmmcFindAdapter;
    HwInitializationData.HwResetBus = BcmEmmcResetBus;
    HwInitializationData.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    HwInitializationData.NumberOfAccessRanges = 2;
    HwInitializationData.MapBuffers = TRUE;

    return ScsiPortInitialize(DriverObject, Argument2, &HwInitializationData, NULL);
}
