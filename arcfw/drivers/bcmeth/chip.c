/*++

Module Name:

    chip.c

Abstract:

    The Ethernet controllers of the miniport, USB functions of the host:

    The LAN9500 (the Ethernet function of the LAN9512 and LAN9514 on the Pi
    2 and 3 Model B) and the LAN7800 (that of the LAN7515 on the Pi 3 Model
    B+) are programmed through vendor requests that read and write their
    registers, their PHYs through the MII registers. A transmit is a frame
    behind the controller's two command words; a receive transfer holds a
    frame behind its status words, aligned so that the IP header of the
    frame falls on a longword. The VideoCore holds the station address of
    the board, which the firmware describes; the controllers have no EEPROM.

    A CDC Ethernet function (an emulator's) moves bare frames on the bulk
    endpoints of the second setting of its data interface, and names its
    station address in a string descriptor.

    These routines run at PASSIVE_LEVEL, but for the frame header routines.

--*/

#include "bcmeth.h"

#define VENDOR_WRITE_REGISTER       0xA0
#define VENDOR_READ_REGISTER        0xA1

//
// LAN9500 registers.
//

#define L95_ID_REV                  0x000
#define L95_INT_STS                 0x008
#define L95_TX_CFG                  0x010
#define L95_HW_CFG                  0x014
#define L95_PM_CTRL                 0x020
#define L95_LED_GPIO_CFG            0x024
#define L95_AFC_CFG                 0x02C
#define L95_BURST_CAP               0x038
#define L95_BULK_IN_DLY             0x06C
#define L95_MAC_CR                  0x100
#define L95_ADDRH                   0x104
#define L95_ADDRL                   0x108
#define L95_HASHH                   0x10C
#define L95_HASHL                   0x110
#define L95_MII_ADDR                0x114
#define L95_MII_DATA                0x118
#define L95_FLOW                    0x11C
#define L95_VLAN1                   0x120
#define L95_COE_CR                  0x130

#define L95_TX_CFG_ON               0x00000004
#define L95_HW_CFG_BIR              0x00001000
#define L95_HW_CFG_RXDOFF           0x00000600
#define L95_HW_CFG_RXDOFF_2         0x00000400
#define L95_HW_CFG_MEF              0x00000020
#define L95_HW_CFG_LRST             0x00000008
#define L95_HW_CFG_BCE              0x00000002
#define L95_PM_CTRL_PHY_RST         0x00000010
#define L95_LED_SPD                 0x01000000
#define L95_LED_LNK                 0x00100000
#define L95_LED_FDX                 0x00010000
#define L95_AFC_CFG_DEFAULT         0x00F830A1
#define L95_BULK_IN_DELAY           0x00002000
#define L95_MAC_CR_FDPX             0x00100000
#define L95_MAC_CR_MCPAS            0x00080000
#define L95_MAC_CR_PRMS             0x00040000
#define L95_MAC_CR_HPFILT           0x00002000
#define L95_MAC_CR_BCAST            0x00000800
#define L95_MAC_CR_TXEN             0x00000008
#define L95_MAC_CR_RXEN             0x00000004
#define L95_COE_TX                  0x00010000
#define L95_COE_RX                  0x00000001

#define L95_TX_CMD_A_FIRST_SEG      0x00002000
#define L95_TX_CMD_A_LAST_SEG       0x00001000
#define L95_RX_STS_FL_MASK          0x3FFF0000
#define L95_RX_STS_FL_SHIFT         16
#define L95_RX_STS_ES               0x00008000
#define L95_RX_STS_CRC              0x00000002

#define L95_PHY_INT_SRC             29
#define L95_PHY_INT_MASK            30
#define L95_PHY_INT_DEFAULT         0x0050

//
// LAN7800 registers.
//

#define L78_ID_REV                  0x000
#define L78_INT_STS                 0x00C
#define L78_HW_CFG                  0x010
#define L78_PMT_CTL                 0x014
#define L78_USB_CFG0                0x080
#define L78_BURST_CAP               0x090
#define L78_BULK_IN_DLY             0x094
#define L78_RFE_CTL                 0x0B0
#define L78_FCT_RX_CTL              0x0C0
#define L78_FCT_TX_CTL              0x0C4
#define L78_FCT_RX_FIFO_END         0x0C8
#define L78_FCT_TX_FIFO_END         0x0CC
#define L78_FCT_FLOW                0x0D0
#define L78_MAC_CR                  0x100
#define L78_MAC_RX                  0x104
#define L78_MAC_TX                  0x108
#define L78_FLOW                    0x10C
#define L78_RX_ADDRH                0x118
#define L78_RX_ADDRL                0x11C
#define L78_MII_ACC                 0x120
#define L78_MII_DATA                0x124
#define L78_MAF_HI0                 0x400
#define L78_MAF_LO0                 0x404

#define L78_HW_CFG_LED1_EN          0x00200000
#define L78_HW_CFG_LED0_EN          0x00100000
#define L78_HW_CFG_LRST             0x00000002
#define L78_PMT_CTL_READY           0x00000080
#define L78_PMT_CTL_PHY_RST         0x00000010
#define L78_USB_CFG0_BIR            0x00000040
#define L78_BULK_IN_DELAY           0x00000800
#define L78_FIFO_END                ((12 * 1024 - 512) / 512)
#define L78_RFE_CTL_BCAST_EN        0x00000400
#define L78_RFE_CTL_MCAST_EN        0x00000200
#define L78_RFE_CTL_UCAST_EN        0x00000100
#define L78_RFE_CTL_DA_PERFECT      0x00000002
#define L78_FCT_CTL_EN              0x80000000
#define L78_MAC_CR_ADP              0x00002000
#define L78_MAC_CR_AUTO_DUPLEX      0x00001000
#define L78_MAC_CR_AUTO_SPEED       0x00000800
#define L78_MAC_RX_MAX_SIZE         (1544 << 16)
#define L78_MAC_RX_FCS_STRIP        0x00000010
#define L78_MAC_RX_RXEN             0x00000001
#define L78_MAC_TX_TXEN             0x00000001
#define L78_MAF_HI_VALID            0x80000000

#define L78_TX_CMD_A_FCS            0x00400000
#define L78_TX_CMD_A_LEN_MASK       0x000FFFFF
#define L78_RX_CMD_A_RXE            0x00040000
#define L78_RX_CMD_A_FCS            0x00010000
#define L78_RX_CMD_A_LEN_MASK       0x00003FFF
#define L78_RX_HEADER               10

//
// The MII access register (MII_ADDR, MII_ACC) of both, and the PHY.
//

#define MII_BUSY                    0x00000001
#define MII_WRITE                   0x00000002
#define MII_PHY_SHIFT               11
#define MII_REGISTER_SHIFT          6
#define PHY_ADDRESS                 1

#define MII_BMCR                    0
#define MII_BMSR                    1
#define MII_ADVERTISE               4
#define MII_LPA                     5
#define MII_CTRL1000                9
#define MII_STAT1000                10

#define BMCR_RESET                  0x8000
#define BMCR_ANENABLE               0x1000
#define BMCR_ANRESTART              0x0200
#define BMSR_LINK                   0x0004
#define ADVERTISE_CSMA              0x0001
#define ADVERTISE_10HALF            0x0020
#define ADVERTISE_10FULL            0x0040
#define ADVERTISE_100HALF           0x0080
#define ADVERTISE_100FULL           0x0100
#define ADVERTISE_PAUSE             0x0C00
#define ADVERTISE_1000FULL          0x0200
#define LPA_1000FULL                0x0800

//
// The USB standard and CDC requests and descriptors.
//

#define REQUEST_CLEAR_FEATURE       0x01
#define REQUEST_GET_DESCRIPTOR      0x06
#define REQUEST_SET_CONFIGURATION   0x09
#define REQUEST_SET_INTERFACE       0x0B
#define CDC_SET_ETHERNET_FILTER     0x43
#define DESCRIPTOR_DEVICE           0x01
#define DESCRIPTOR_CONFIGURATION    0x02
#define DESCRIPTOR_STRING           0x03
#define DESCRIPTOR_INTERFACE        0x04
#define DESCRIPTOR_ENDPOINT         0x05
#define DESCRIPTOR_CS_INTERFACE     0x24
#define CDC_ETHERNET_FUNCTIONAL     0x0F
#define CLASS_CDC_DATA              0x0A
#define SUBCLASS_ECM                0x06
#define ENDPOINT_HALT               0
#define LANGUAGE_US                 0x0409

#define CDC_FILTER_PROMISCUOUS      0x0001
#define CDC_FILTER_ALL_MULTICAST    0x0002
#define CDC_FILTER_DIRECTED         0x0004
#define CDC_FILTER_BROADCAST        0x0008
#define CDC_FILTER_MULTICAST        0x0010

#define DESCRIPTOR_BUFFER_SIZE      512
#define RESET_TIMEOUT_MS            1000

static VOID
ChipSleep(
    IN ULONG Milliseconds
    )
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000 * (LONGLONG)Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static BOOLEAN
ReadRegister(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Index,
    OUT PULONG Value
    )
{
    ULONG Data = 0;

    if (UsbFunctionControl(Adapter->Device, 0xC0, VENDOR_READ_REGISTER, 0, Index, &Data, 4) != 4) {
        return FALSE;
    }

    *Value = Data;
    return TRUE;
}

static BOOLEAN
WriteRegister(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Index,
    IN ULONG Value
    )
{
    return UsbFunctionControl(Adapter->Device, 0x40, VENDOR_WRITE_REGISTER, 0, Index, &Value, 4) == 4;
}

static BOOLEAN
ChangeRegister(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Index,
    IN ULONG Clear,
    IN ULONG Set
    )
{
    ULONG Value;

    return ReadRegister(Adapter, Index, &Value) && WriteRegister(Adapter, Index, (Value & ~Clear) | Set);
}

//
// Wait up to a second for the bits of the mask in a register to be set or
// clear.
//

static BOOLEAN
WaitRegister(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Index,
    IN ULONG Mask,
    IN BOOLEAN Set
    )
{
    ULONG Waited;
    ULONG Value;

    for (Waited = 0; Waited < RESET_TIMEOUT_MS; Waited += 10) {
        if (!ReadRegister(Adapter, Index, &Value)) {
            return FALSE;
        }

        if ((Value & Mask) == (Set ? Mask : 0)) {
            return TRUE;
        }

        ChipSleep(10);
    }

    return FALSE;
}

//
// The PHY, through the MII registers of the controller.
//

static ULONG
MiiAccess(
    IN PBCMETH_ADAPTER Adapter
    )
{
    return Adapter->Chip == CHIP_LAN9500 ? L95_MII_ADDR : L78_MII_ACC;
}

static BOOLEAN
MiiRead(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Register,
    OUT PUSHORT Value
    )
{
    ULONG Access = MiiAccess(Adapter);
    ULONG Data;

    if (!WaitRegister(Adapter, Access, MII_BUSY, FALSE) ||
        !WriteRegister(Adapter, Access, (PHY_ADDRESS << MII_PHY_SHIFT) |
                       (Register << MII_REGISTER_SHIFT) | MII_BUSY) ||
        !WaitRegister(Adapter, Access, MII_BUSY, FALSE) ||
        !ReadRegister(Adapter, Access + 4, &Data)) {
        return FALSE;
    }

    *Value = (USHORT)Data;
    return TRUE;
}

static BOOLEAN
MiiWrite(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Register,
    IN USHORT Value
    )
{
    ULONG Access = MiiAccess(Adapter);

    return WaitRegister(Adapter, Access, MII_BUSY, FALSE) &&
           WriteRegister(Adapter, Access + 4, Value) &&
           WriteRegister(Adapter, Access, (PHY_ADDRESS << MII_PHY_SHIFT) |
                         (Register << MII_REGISTER_SHIFT) | MII_WRITE | MII_BUSY) &&
           WaitRegister(Adapter, Access, MII_BUSY, FALSE);
}

//
// Reset the PHY, advertise what the controller does and start the
// autonegotiation.
//

static BOOLEAN
PhyStart(
    IN PBCMETH_ADAPTER Adapter
    )
{
    USHORT Control;
    ULONG Waited;

    if (!MiiWrite(Adapter, MII_BMCR, BMCR_RESET)) {
        return FALSE;
    }

    for (Waited = 0; Waited < RESET_TIMEOUT_MS; Waited += 10) {
        if (MiiRead(Adapter, MII_BMCR, &Control) && (Control & BMCR_RESET) == 0) {
            break;
        }

        ChipSleep(10);
    }

    if (!MiiWrite(Adapter, MII_ADVERTISE, ADVERTISE_CSMA | ADVERTISE_10HALF | ADVERTISE_10FULL |
                  ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE)) {
        return FALSE;
    }

    if (Adapter->Chip == CHIP_LAN7800) {
        MiiWrite(Adapter, MII_CTRL1000, ADVERTISE_1000FULL);
    } else {
        MiiRead(Adapter, L95_PHY_INT_SRC, &Control);
        MiiWrite(Adapter, L95_PHY_INT_MASK, L95_PHY_INT_DEFAULT);
    }

    return MiiWrite(Adapter, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

//
// The descriptors: a configuration of the function (its interface and bulk
// endpoints, or those of the CDC Ethernet model), and a string.
//

static BOOLEAN
ParseConfiguration(
    IN PBCMETH_ADAPTER Adapter,
    IN PUCHAR Data,
    IN ULONG Length,
    OUT PUCHAR AddressString
    )
{
    BOOLEAN Ecm = FALSE;
    BOOLEAN InData = FALSE;
    UCHAR In = 0;
    UCHAR Out = 0;
    USHORT InMaximum = 0;
    USHORT OutMaximum = 0;
    ULONG Offset;
    PUCHAR Item;

    *AddressString = 0;
    for (Offset = 0; Offset + 2 <= Length && Data[Offset] >= 2 && Offset + Data[Offset] <= Length;
         Offset += Data[Offset]) {
        Item = Data + Offset;
        switch (Item[1]) {
        case DESCRIPTOR_INTERFACE:
            if (Item[0] < 9 || (In != 0 && Out != 0)) {
                break;
            }

            In = Out = 0;
            InData = FALSE;
            if (Item[5] == USB_CLASS_COMMUNICATION && Item[6] == SUBCLASS_ECM) {
                Ecm = TRUE;
                Adapter->CommInterface = Item[2];
            } else if (Item[5] == CLASS_CDC_DATA) {
                InData = TRUE;
                Adapter->DataInterface = Item[2];
                Adapter->DataSetting = Item[3];
            }

            break;

        case DESCRIPTOR_CS_INTERFACE:
            if (Item[0] >= 4 && Item[2] == CDC_ETHERNET_FUNCTIONAL) {
                *AddressString = Item[3];
            }

            break;

        case DESCRIPTOR_ENDPOINT:
            if (Item[0] < 7 || (Item[3] & 3) != 2 || (Adapter->Chip == CHIP_CDC && !InData)) {
                break;
            }

            if ((Item[2] & 0x80) && In == 0) {
                In = Item[2] & 0xF;
                InMaximum = (Item[4] | (Item[5] << 8)) & 0x7FF;
            } else if ((Item[2] & 0x80) == 0 && Out == 0) {
                Out = Item[2] & 0xF;
                OutMaximum = (Item[4] | (Item[5] << 8)) & 0x7FF;
            }

            break;
        }
    }

    if (In == 0 || Out == 0 || (Adapter->Chip == CHIP_CDC && !Ecm)) {
        return FALSE;
    }

    Adapter->BulkIn = In;
    Adapter->BulkOut = Out;
    Adapter->BulkInMaximum = InMaximum;
    Adapter->BulkOutMaximum = OutMaximum;
    return TRUE;
}

static UCHAR
HexDigit(
    IN USHORT Character
    )
{
    if (Character >= '0' && Character <= '9') {
        return (UCHAR)(Character - '0');
    }

    if (Character >= 'A' && Character <= 'F') {
        return (UCHAR)(Character - 'A' + 10);
    }

    if (Character >= 'a' && Character <= 'f') {
        return (UCHAR)(Character - 'a' + 10);
    }

    return 0xFF;
}

static BOOLEAN
ReadAddressString(
    IN PBCMETH_ADAPTER Adapter,
    IN UCHAR String,
    OUT PUCHAR Address
    )
{
    UCHAR Data[2 + 24];
    UCHAR High;
    UCHAR Low;
    ULONG Index;

    if (String == 0 ||
        UsbFunctionControl(Adapter->Device, 0x80, REQUEST_GET_DESCRIPTOR, (DESCRIPTOR_STRING << 8) | String,
                           LANGUAGE_US, Data, sizeof(Data)) < (LONG)sizeof(Data)) {
        return FALSE;
    }

    for (Index = 0; Index < ETH_LENGTH_OF_ADDRESS; Index += 1) {
        High = HexDigit(Data[2 + Index * 4] | (Data[3 + Index * 4] << 8));
        Low = HexDigit(Data[4 + Index * 4] | (Data[5 + Index * 4] << 8));
        if (High > 15 || Low > 15) {
            return FALSE;
        }

        Address[Index] = (UCHAR)((High << 4) | Low);
    }

    return TRUE;
}

NDIS_STATUS
ChipFind(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine finds the controller among the functions of the USB host,
    and reads its configuration: the bulk endpoints, and those of a CDC
    function with its station address.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    NDIS_STATUS_SUCCESS, or NDIS_STATUS_ADAPTER_NOT_FOUND.

--*/

{
    USB_FUNCTION Function;
    UCHAR Device[18];
    PUCHAR Data;
    ULONG Index;
    ULONG Configuration;
    ULONG Total;
    UCHAR AddressString;
    LONG Length;
    NDIS_STATUS Status = NDIS_STATUS_ADAPTER_NOT_FOUND;

    for (Index = 0; UsbGetFunction(Index, &Function); Index += 1) {
        if (Function.Vendor == USB_VENDOR_SMSC && Function.Product == USB_PRODUCT_LAN9500) {
            Adapter->Chip = CHIP_LAN9500;
        } else if (Function.Vendor == USB_VENDOR_SMSC && Function.Product == USB_PRODUCT_LAN7800) {
            Adapter->Chip = CHIP_LAN7800;
        } else if (Function.Class == USB_CLASS_COMMUNICATION) {
            Adapter->Chip = CHIP_CDC;
        } else {
            continue;
        }

        Adapter->Device = Function.Device;
        break;
    }

    if (Adapter->Chip == 0 ||
        NdisAllocateMemory((PVOID *)&Data, DESCRIPTOR_BUFFER_SIZE, 0, HighestAcceptableMax) != NDIS_STATUS_SUCCESS) {
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    if (UsbFunctionControl(Adapter->Device, 0x80, REQUEST_GET_DESCRIPTOR, DESCRIPTOR_DEVICE << 8, 0,
                           Device, sizeof(Device)) < (LONG)sizeof(Device)) {
        Device[17] = 0;
    }

    for (Configuration = 0; Configuration < Device[17]; Configuration += 1) {
        if (UsbFunctionControl(Adapter->Device, 0x80, REQUEST_GET_DESCRIPTOR,
                               (DESCRIPTOR_CONFIGURATION << 8) | Configuration, 0, Data, 9) < 9) {
            continue;
        }

        Total = Data[2] | (Data[3] << 8);
        Length = UsbFunctionControl(Adapter->Device, 0x80, REQUEST_GET_DESCRIPTOR,
                                    (DESCRIPTOR_CONFIGURATION << 8) | Configuration, 0, Data,
                                    Total < DESCRIPTOR_BUFFER_SIZE ? Total : DESCRIPTOR_BUFFER_SIZE);
        if (Length < 9 || !ParseConfiguration(Adapter, Data, Length, &AddressString)) {
            continue;
        }

        Adapter->Configuration = Data[5];
        if (Adapter->Chip == CHIP_CDC && !ReadAddressString(Adapter, AddressString, Adapter->PermanentAddress)) {
            continue;
        }

        Status = NDIS_STATUS_SUCCESS;
        break;
    }

    NdisFreeMemory(Data, DESCRIPTOR_BUFFER_SIZE, 0);
    return Status;
}

static BOOLEAN
Lan9500Start(
    IN PBCMETH_ADAPTER Adapter
    )
{
    PUCHAR Address = Adapter->CurrentAddress;

    if (!WriteRegister(Adapter, L95_HW_CFG, L95_HW_CFG_LRST) ||
        !WaitRegister(Adapter, L95_HW_CFG, L95_HW_CFG_LRST, FALSE) ||
        !WriteRegister(Adapter, L95_PM_CTRL, L95_PM_CTRL_PHY_RST) ||
        !WaitRegister(Adapter, L95_PM_CTRL, L95_PM_CTRL_PHY_RST, FALSE)) {
        return FALSE;
    }

    WriteRegister(Adapter, L95_ADDRL, Address[0] | (Address[1] << 8) | (Address[2] << 16) | (Address[3] << 24));
    WriteRegister(Adapter, L95_ADDRH, Address[4] | (Address[5] << 8));
    WriteRegister(Adapter, L95_BURST_CAP, 0);
    WriteRegister(Adapter, L95_BULK_IN_DLY, L95_BULK_IN_DELAY);
    ChangeRegister(Adapter, L95_HW_CFG, L95_HW_CFG_BIR | L95_HW_CFG_MEF | L95_HW_CFG_BCE | L95_HW_CFG_RXDOFF,
                   L95_HW_CFG_RXDOFF_2);
    WriteRegister(Adapter, L95_INT_STS, 0xFFFFFFFF);
    WriteRegister(Adapter, L95_LED_GPIO_CFG, L95_LED_SPD | L95_LED_LNK | L95_LED_FDX);
    WriteRegister(Adapter, L95_FLOW, 0);
    WriteRegister(Adapter, L95_AFC_CFG, L95_AFC_CFG_DEFAULT);
    WriteRegister(Adapter, L95_VLAN1, 0x8100);
    ChangeRegister(Adapter, L95_COE_CR, L95_COE_TX | L95_COE_RX, 0);
    if (!ReadRegister(Adapter, L95_MAC_CR, &Adapter->MacControl) || !PhyStart(Adapter)) {
        return FALSE;
    }

    Adapter->MacControl |= L95_MAC_CR_TXEN;
    WriteRegister(Adapter, L95_MAC_CR, Adapter->MacControl);
    WriteRegister(Adapter, L95_TX_CFG, L95_TX_CFG_ON);
    Adapter->MacControl |= L95_MAC_CR_RXEN;
    return WriteRegister(Adapter, L95_MAC_CR, Adapter->MacControl);
}

static BOOLEAN
Lan7800Start(
    IN PBCMETH_ADAPTER Adapter
    )
{
    PUCHAR Address = Adapter->CurrentAddress;
    ULONG Low = Address[0] | (Address[1] << 8) | (Address[2] << 16) | (Address[3] << 24);
    ULONG High = Address[4] | (Address[5] << 8);

    if (!WriteRegister(Adapter, L78_HW_CFG, L78_HW_CFG_LRST) ||
        !WaitRegister(Adapter, L78_HW_CFG, L78_HW_CFG_LRST, FALSE) ||
        !ChangeRegister(Adapter, L78_PMT_CTL, 0, L78_PMT_CTL_PHY_RST) ||
        !WaitRegister(Adapter, L78_PMT_CTL, L78_PMT_CTL_PHY_RST, FALSE) ||
        !WaitRegister(Adapter, L78_PMT_CTL, L78_PMT_CTL_READY, TRUE)) {
        return FALSE;
    }

    ChangeRegister(Adapter, L78_USB_CFG0, L78_USB_CFG0_BIR, 0);
    WriteRegister(Adapter, L78_RX_ADDRL, Low);
    WriteRegister(Adapter, L78_RX_ADDRH, High);
    WriteRegister(Adapter, L78_MAF_LO0, Low);
    WriteRegister(Adapter, L78_MAF_HI0, High | L78_MAF_HI_VALID);
    WriteRegister(Adapter, L78_BURST_CAP, 0);
    WriteRegister(Adapter, L78_BULK_IN_DLY, L78_BULK_IN_DELAY);
    WriteRegister(Adapter, L78_INT_STS, 0xFFFFFFFF);
    WriteRegister(Adapter, L78_FCT_RX_FIFO_END, L78_FIFO_END);
    WriteRegister(Adapter, L78_FCT_TX_FIFO_END, L78_FIFO_END);
    WriteRegister(Adapter, L78_FLOW, 0);
    WriteRegister(Adapter, L78_FCT_FLOW, 0);
    WriteRegister(Adapter, L78_RFE_CTL, L78_RFE_CTL_BCAST_EN | L78_RFE_CTL_DA_PERFECT);
    ChangeRegister(Adapter, L78_HW_CFG, 0, L78_HW_CFG_LED0_EN | L78_HW_CFG_LED1_EN);
    if (!PhyStart(Adapter)) {
        return FALSE;
    }

    ChangeRegister(Adapter, L78_MAC_CR, 0, L78_MAC_CR_AUTO_DUPLEX | L78_MAC_CR_AUTO_SPEED | L78_MAC_CR_ADP);
    WriteRegister(Adapter, L78_MAC_TX, L78_MAC_TX_TXEN);
    WriteRegister(Adapter, L78_FCT_TX_CTL, L78_FCT_CTL_EN);
    WriteRegister(Adapter, L78_MAC_RX, L78_MAC_RX_MAX_SIZE | L78_MAC_RX_FCS_STRIP | L78_MAC_RX_RXEN);
    return WriteRegister(Adapter, L78_FCT_RX_CTL, L78_FCT_CTL_EN);
}

NDIS_STATUS
ChipStart(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine configures the function and starts the controller with
    the current address: its MAC, its PHY and its receive filter.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    NDIS_STATUS_SUCCESS, or NDIS_STATUS_HARD_ERRORS.

--*/

{
    BOOLEAN Started;

    if (UsbFunctionControl(Adapter->Device, 0x00, REQUEST_SET_CONFIGURATION, Adapter->Configuration, 0,
                           NULL, 0) < 0) {
        return NDIS_STATUS_HARD_ERRORS;
    }

    switch (Adapter->Chip) {
    case CHIP_LAN9500:
        Adapter->ReceiveLength = RECEIVE_BUFFER_SIZE;
        Adapter->TransmitHeader = 8;
        Started = Lan9500Start(Adapter);
        break;

    case CHIP_LAN7800:
        Adapter->ReceiveLength = RECEIVE_BUFFER_SIZE;
        Adapter->TransmitHeader = 8;
        Started = Lan7800Start(Adapter);
        break;

    default:
        Adapter->ReceiveLength = ETH_MAXIMUM_FRAME;
        Adapter->TransmitHeader = 0;
        Adapter->LinkUp = TRUE;
        Adapter->FullDuplex = TRUE;
        Adapter->LinkSpeed = 1000000;
        Started = UsbFunctionControl(Adapter->Device, 0x01, REQUEST_SET_INTERFACE, Adapter->DataSetting,
                                     Adapter->DataInterface, NULL, 0) >= 0;
        break;
    }

    if (!Started) {
        return NDIS_STATUS_HARD_ERRORS;
    }

    ChipSetFilter(Adapter);
    return NDIS_STATUS_SUCCESS;
}

VOID
ChipStop(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine stops the receiver and transmitter of the controller.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    switch (Adapter->Chip) {
    case CHIP_LAN9500:
        Adapter->MacControl &= ~(L95_MAC_CR_TXEN | L95_MAC_CR_RXEN);
        WriteRegister(Adapter, L95_MAC_CR, Adapter->MacControl);
        WriteRegister(Adapter, L95_TX_CFG, 0);
        break;

    case CHIP_LAN7800:
        WriteRegister(Adapter, L78_MAC_RX, 0);
        WriteRegister(Adapter, L78_MAC_TX, 0);
        WriteRegister(Adapter, L78_FCT_RX_CTL, 0);
        WriteRegister(Adapter, L78_FCT_TX_CTL, 0);
        break;

    default:
        UsbFunctionControl(Adapter->Device, 0x01, REQUEST_SET_INTERFACE, 0, Adapter->DataInterface, NULL, 0);
        break;
    }
}

//
// The bit of the multicast hash table of the LAN9500 an address selects:
// the high six bits of its Ethernet CRC.
//

static ULONG
HashBit(
    IN PUCHAR Address
    )
{
    ULONG Crc = 0xFFFFFFFF;
    ULONG Index;
    ULONG Bit;
    UCHAR Byte;

    for (Index = 0; Index < ETH_LENGTH_OF_ADDRESS; Index += 1) {
        Byte = Address[Index];
        for (Bit = 0; Bit < 8; Bit += 1, Byte >>= 1) {
            Crc = ((Crc >> 31) ^ (Byte & 1)) ? (Crc << 1) ^ 0x04C11DB7 : Crc << 1;
        }
    }

    return Crc >> 26;
}

VOID
ChipSetFilter(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine sets the receive filter of the controller from the packet
    filter and the multicast list: the LAN9500 hashes the multicast list,
    the LAN7800 passes all multicast once any is asked for, and the wrapper
    filters what reaches each binding.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    ULONG Filter = Adapter->PacketFilter;
    BOOLEAN Multicast = (Filter & NDIS_PACKET_TYPE_ALL_MULTICAST) ||
                        ((Filter & NDIS_PACKET_TYPE_MULTICAST) && Adapter->MulticastCount != 0);
    ULONG Hash[2];
    ULONG Bit;
    ULONG Index;
    ULONG Control;
    USHORT CdcFilter;

    switch (Adapter->Chip) {
    case CHIP_LAN9500:
        Hash[0] = Hash[1] = 0;
        for (Index = 0; Index < Adapter->MulticastCount; Index += 1) {
            Bit = HashBit(Adapter->Multicast[Index]);
            Hash[Bit >> 5] |= 1 << (Bit & 31);
        }

        Control = Adapter->MacControl & ~(L95_MAC_CR_PRMS | L95_MAC_CR_MCPAS | L95_MAC_CR_HPFILT | L95_MAC_CR_BCAST);
        if (Filter & NDIS_PACKET_TYPE_PROMISCUOUS) {
            Control |= L95_MAC_CR_PRMS;
        }

        if (Filter & NDIS_PACKET_TYPE_ALL_MULTICAST) {
            Control |= L95_MAC_CR_MCPAS;
        } else if (Multicast) {
            Control |= L95_MAC_CR_HPFILT;
        }

        if ((Filter & (NDIS_PACKET_TYPE_BROADCAST | NDIS_PACKET_TYPE_PROMISCUOUS)) == 0) {
            Control |= L95_MAC_CR_BCAST;
        }

        WriteRegister(Adapter, L95_HASHL, Hash[0]);
        WriteRegister(Adapter, L95_HASHH, Hash[1]);
        Adapter->MacControl = Control;
        WriteRegister(Adapter, L95_MAC_CR, Control);
        break;

    case CHIP_LAN7800:
        Control = L78_RFE_CTL_DA_PERFECT;
        if (Filter & (NDIS_PACKET_TYPE_BROADCAST | NDIS_PACKET_TYPE_PROMISCUOUS)) {
            Control |= L78_RFE_CTL_BCAST_EN;
        }

        if (Multicast || (Filter & NDIS_PACKET_TYPE_PROMISCUOUS)) {
            Control |= L78_RFE_CTL_MCAST_EN;
        }

        if (Filter & NDIS_PACKET_TYPE_PROMISCUOUS) {
            Control |= L78_RFE_CTL_UCAST_EN;
        }

        WriteRegister(Adapter, L78_RFE_CTL, Control);
        break;

    default:
        CdcFilter = CDC_FILTER_DIRECTED;
        if (Filter & NDIS_PACKET_TYPE_BROADCAST) {
            CdcFilter |= CDC_FILTER_BROADCAST;
        }

        if (Filter & NDIS_PACKET_TYPE_ALL_MULTICAST) {
            CdcFilter |= CDC_FILTER_ALL_MULTICAST;
        } else if (Multicast) {
            CdcFilter |= CDC_FILTER_ALL_MULTICAST | CDC_FILTER_MULTICAST;
        }

        if (Filter & NDIS_PACKET_TYPE_PROMISCUOUS) {
            CdcFilter |= CDC_FILTER_PROMISCUOUS;
        }

        UsbFunctionControl(Adapter->Device, 0x21, CDC_SET_ETHERNET_FILTER, CdcFilter, Adapter->CommInterface,
                           NULL, 0);
        break;
    }
}

VOID
ChipCheckLink(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine looks at the link of the PHY: up or down, and the speed
    and duplex the autonegotiation chose, which the MAC of the LAN9500 is
    set to (that of the LAN7800 follows the PHY itself).

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    USHORT Status;
    USHORT Advertise;
    USHORT Partner;
    USHORT Control1000 = 0;
    USHORT Status1000 = 0;
    USHORT Common;
    BOOLEAN FullDuplex;

    if (Adapter->Chip == CHIP_CDC) {
        return;
    }

    if (!MiiRead(Adapter, MII_BMSR, &Status) || !MiiRead(Adapter, MII_BMSR, &Status)) {
        return;
    }

    Adapter->LinkUp = (Status & BMSR_LINK) != 0;
    if (!Adapter->LinkUp || !MiiRead(Adapter, MII_ADVERTISE, &Advertise) || !MiiRead(Adapter, MII_LPA, &Partner)) {
        return;
    }

    if (Adapter->Chip == CHIP_LAN7800) {
        MiiRead(Adapter, MII_CTRL1000, &Control1000);
        MiiRead(Adapter, MII_STAT1000, &Status1000);
    }

    Common = Advertise & Partner;
    if ((Control1000 & ADVERTISE_1000FULL) && (Status1000 & LPA_1000FULL)) {
        Adapter->LinkSpeed = 10000000;
        FullDuplex = TRUE;
    } else if (Common & (ADVERTISE_100FULL | ADVERTISE_100HALF)) {
        Adapter->LinkSpeed = 1000000;
        FullDuplex = (Common & ADVERTISE_100FULL) != 0;
    } else {
        Adapter->LinkSpeed = 100000;
        FullDuplex = (Common & ADVERTISE_10FULL) != 0;
    }

    if (Adapter->Chip == CHIP_LAN9500 && FullDuplex != Adapter->FullDuplex) {
        Adapter->MacControl = FullDuplex ? Adapter->MacControl | L95_MAC_CR_FDPX :
                                           Adapter->MacControl & ~L95_MAC_CR_FDPX;
        WriteRegister(Adapter, L95_MAC_CR, Adapter->MacControl);
    }

    Adapter->FullDuplex = FullDuplex;
}

VOID
ChipClearHalt(
    IN PBCMETH_ADAPTER Adapter,
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine clears the halt of the endpoint of a stalled pipe.

Arguments:

    Adapter - Supplies the adapter.

    Pipe - Supplies the pipe.

Return Value:

    None.

--*/

{
    UsbFunctionControl(Adapter->Device, 0x02, REQUEST_CLEAR_FEATURE, ENDPOINT_HALT,
                       Pipe->Endpoint | (Pipe->In ? 0x80 : 0), NULL, 0);
}

ULONG
ChipFrameHeader(
    IN PBCMETH_ADAPTER Adapter,
    IN PUCHAR Header,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine makes the command words of the controller that go before
    a frame it transmits.

Arguments:

    Adapter - Supplies the adapter.

    Header - Receives the command words.

    Length - Supplies the length of the frame.

Return Value:

    The length of the header.

--*/

{
    ULONG Words[2];

    switch (Adapter->Chip) {
    case CHIP_LAN9500:
        Words[0] = Length | L95_TX_CMD_A_FIRST_SEG | L95_TX_CMD_A_LAST_SEG;
        Words[1] = Length;
        break;

    case CHIP_LAN7800:
        Words[0] = (Length & L78_TX_CMD_A_LEN_MASK) | L78_TX_CMD_A_FCS;
        Words[1] = 0;
        break;

    default:
        return 0;
    }

    NdisMoveMemory(Header, Words, sizeof(Words));
    return sizeof(Words);
}

ULONG
ChipNextFrame(
    IN PBCMETH_ADAPTER Adapter,
    IN PUCHAR Buffer,
    IN ULONG Offset,
    IN ULONG Length,
    OUT PULONG FrameOffset,
    OUT PULONG FrameLength,
    OUT PBOOLEAN Error
    )

/*++

Routine Description:

    This routine finds the next frame of a receive transfer: behind the
    status word of a LAN9500 and its two bytes of offset, or the commands of
    a LAN7800, each next frame on a longword; a CDC transfer is one bare
    frame. The status words are read as longwords from the shared memory.

Arguments:

    Adapter - Supplies the adapter.

    Buffer - Supplies the receive buffer.

    Offset - Supplies the offset of the frame (with its header).

    Length - Supplies the length of the transfer.

    FrameOffset - Receives the offset of the frame itself.

    FrameLength - Receives the length of the frame, without its CRC.

    Error - Receives TRUE if the controller found the frame bad.

Return Value:

    The offset of the next frame, or 0 if there is none.

--*/

{
    ULONG Status;
    ULONG Size;
    ULONG Start;

    *Error = FALSE;
    switch (Adapter->Chip) {
    case CHIP_LAN9500:
        if (Offset + 4 > Length) {
            return 0;
        }

        Status = *(volatile ULONG *)(Buffer + Offset);
        Size = (Status & L95_RX_STS_FL_MASK) >> L95_RX_STS_FL_SHIFT;
        Start = Offset + 4 + 2;
        if (Size < 4 || Start + Size > Length) {
            return 0;
        }

        *Error = (Status & L95_RX_STS_ES) != 0;
        if (Status & L95_RX_STS_CRC) {
            Adapter->ReceiveCrcError += 1;
        }

        *FrameOffset = Start;
        *FrameLength = Size - 4;
        return (Start + Size + 3) & ~3;

    case CHIP_LAN7800:
        if (Offset + L78_RX_HEADER > Length) {
            return 0;
        }

        Status = *(volatile ULONG *)(Buffer + Offset);
        Size = Status & L78_RX_CMD_A_LEN_MASK;
        Start = Offset + L78_RX_HEADER;
        if (Start + Size > Length) {
            return 0;
        }

        *Error = (Status & L78_RX_CMD_A_RXE) != 0;
        if (*Error && (Status & L78_RX_CMD_A_FCS)) {
            Adapter->ReceiveCrcError += 1;
        }

        *FrameOffset = Start;
        *FrameLength = Size;
        return (Start + Size + 3) & ~3;

    default:
        if (Offset != 0 || Length == 0) {
            return 0;
        }

        *FrameOffset = 0;
        *FrameLength = Length;
        return Length;
    }
}
