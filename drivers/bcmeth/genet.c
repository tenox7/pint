/*++

Module Name:

    genet.c

Abstract:

    The GENET of the BCM2711, the Ethernet controller of the Raspberry Pi 4:
    a UniMAC with a DMA engine of its own, whose descriptors are in its
    register space, and a BCM54213PE PHY on its MDIO bus, joined through
    RGMII (the PHY delays the receive clock, the GENET the transmit clock).

    The frames move on the default rings of its receive and transmit DMA.
    Each descriptor has a buffer of its own in cached shared memory, which
    the controller does not snoop: the data cache is flushed for a buffer
    before the controller fills or sends it and before a frame received in
    it is read, as for the SONIC's receive buffers. A frame received is
    indicated from its buffer, which then goes back to the controller; a
    transmit is copied into the next buffer and completes at once, and its
    descriptor is reclaimed once the consumer index of the controller has
    passed it. Both rings interrupt for each frame. The interrupt is the
    controller's own; the wrapper masks it while the handler runs.

    The PHY is reset and autonegotiates; the thread of the miniport looks at
    its link each second and sets the speed, duplex and pause of the MAC.
    The UniMAC registers and the MDIO bus are shared with that thread under
    the lock of the adapter; the rings belong to the miniport.

Environment:

    kernel mode only

--*/

#include "bcmeth.h"

//
// The registers: the system block, the RGMII block, the interrupt
// controllers, the receive buffer, the UniMAC with its MDIO bus and
// destination filter, and the descriptors and default rings (16) of the
// receive and transmit DMA, which follow the 256 descriptors of each.
//

#define SYS_REV_CTRL                0x0000
#define SYS_PORT_CTRL               0x0004
#define SYS_RBUF_FLUSH_CTRL         0x0008
#define EXT_RGMII_OOB_CTRL          0x008C
#define INTRL2_0                    0x0200
#define INTRL2_1                    0x0240
#define RBUF_CTRL                   0x0300
#define RBUF_TBUF_SIZE_CTRL         0x03B4
#define UMAC_CMD                    0x0808
#define UMAC_MAC0                   0x080C
#define UMAC_MAC1                   0x0810
#define UMAC_MAX_FRAME_LEN          0x0814
#define UMAC_TX_FLUSH               0x0B34
#define UMAC_MIB_CTRL               0x0D80
#define UMAC_MDIO_CMD               0x0E14
#define UMAC_MDF_CTRL               0x0E50
#define UMAC_MDF_ADDR               0x0E54

#define RDMA_DESCRIPTORS            0x2000
#define TDMA_DESCRIPTORS            0x4000
#define DESCRIPTOR_SIZE             12
#define DESCRIPTOR_LENGTH_STATUS    0x00
#define DESCRIPTOR_ADDRESS_LO       0x04
#define DESCRIPTOR_ADDRESS_HI       0x08
#define DMA_RING_SIZE               0x40
#define DEFAULT_RING                16

#define RDMA_RING                   (RDMA_DESCRIPTORS + 256 * DESCRIPTOR_SIZE + DEFAULT_RING * DMA_RING_SIZE)
#define RDMA_WRITE_PTR              (RDMA_RING + 0x00)
#define RDMA_PROD_INDEX             (RDMA_RING + 0x08)
#define RDMA_CONS_INDEX             (RDMA_RING + 0x0C)
#define RDMA_RING_BUF_SIZE          (RDMA_RING + 0x10)
#define RDMA_START_ADDR             (RDMA_RING + 0x14)
#define RDMA_END_ADDR               (RDMA_RING + 0x1C)
#define RDMA_MBUF_DONE_THRESH       (RDMA_RING + 0x24)
#define RDMA_XON_XOFF_THRESH        (RDMA_RING + 0x28)
#define RDMA_READ_PTR               (RDMA_RING + 0x2C)
#define RDMA_RING_CFG               (RDMA_RING + DMA_RING_SIZE + 0x00)
#define RDMA_CTRL                   (RDMA_RING + DMA_RING_SIZE + 0x04)
#define RDMA_STATUS                 (RDMA_RING + DMA_RING_SIZE + 0x08)
#define RDMA_SCB_BURST_SIZE         (RDMA_RING + DMA_RING_SIZE + 0x0C)

#define TDMA_RING                   (TDMA_DESCRIPTORS + 256 * DESCRIPTOR_SIZE + DEFAULT_RING * DMA_RING_SIZE)
#define TDMA_READ_PTR               (TDMA_RING + 0x00)
#define TDMA_CONS_INDEX             (TDMA_RING + 0x08)
#define TDMA_PROD_INDEX             (TDMA_RING + 0x0C)
#define TDMA_RING_BUF_SIZE          (TDMA_RING + 0x10)
#define TDMA_START_ADDR             (TDMA_RING + 0x14)
#define TDMA_END_ADDR               (TDMA_RING + 0x1C)
#define TDMA_MBUF_DONE_THRESH       (TDMA_RING + 0x24)
#define TDMA_FLOW_PERIOD            (TDMA_RING + 0x28)
#define TDMA_WRITE_PTR              (TDMA_RING + 0x2C)
#define TDMA_RING_CFG               (TDMA_RING + DMA_RING_SIZE + 0x00)
#define TDMA_CTRL                   (TDMA_RING + DMA_RING_SIZE + 0x04)
#define TDMA_STATUS                 (TDMA_RING + DMA_RING_SIZE + 0x08)
#define TDMA_SCB_BURST_SIZE         (TDMA_RING + DMA_RING_SIZE + 0x0C)

#define INTRL2_CPU_STAT             0x00
#define INTRL2_CPU_CLEAR            0x08
#define INTRL2_CPU_MASK_STATUS      0x0C
#define INTRL2_CPU_MASK_SET         0x10
#define INTRL2_CPU_MASK_CLEAR       0x14

//
// The bits.
//

#define GENET_MAJOR_REVISION        6
#define PORT_MODE_EXT_GPHY          3
#define RBUF_FLUSH_UMAC_RESET       0x00000002
#define RGMII_LINK                  0x00000010
#define OOB_DISABLE                 0x00000020
#define RGMII_MODE_EN               0x00000040
#define ID_MODE_DIS                 0x00010000
#define RBUF_64B_EN                 0x00000001
#define RBUF_ALIGN_2B               0x00000002

#define CMD_TX_EN                   0x00000001
#define CMD_RX_EN                   0x00000002
#define CMD_SPEED_10                0x00000000
#define CMD_SPEED_100               0x00000004
#define CMD_SPEED_1000              0x00000008
#define CMD_SPEED_MASK              0x0000000C
#define CMD_PROMISC                 0x00000010
#define CMD_RX_PAUSE_IGNORE         0x00000100
#define CMD_HD_EN                   0x00000400
#define CMD_SW_RESET                0x00002000
#define CMD_LCL_LOOP_EN             0x00008000
#define CMD_TX_PAUSE_IGNORE         0x10000000
#define CMD_LINK_MASK               (CMD_SPEED_MASK | CMD_HD_EN | CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE)

#define MIB_RESET_RX                0x00000001
#define MIB_RESET_RUNT              0x00000002
#define MIB_RESET_TX                0x00000004

#define MDIO_START_BUSY             0x20000000
#define MDIO_READ_FAIL              0x10000000
#define MDIO_RD                     0x08000000
#define MDIO_WR                     0x04000000
#define MDIO_PHY_SHIFT              21
#define MDIO_REGISTER_SHIFT         16
#define MDIO_TIMEOUT_US             2000
#define MDF_FILTERS                 17

#define IRQ_RXDMA_DONE              0x00002000
#define IRQ_TXDMA_DONE              0x00010000
#define GENET_INTERRUPTS            (IRQ_RXDMA_DONE | IRQ_TXDMA_DONE)

#define DMA_EN                      0x00000001
#define DMA_RING_EN                 (1 << (DEFAULT_RING + 1))
#define DMA_DISABLED                0x00000001
#define DMA_TIMEOUT_US              5000
#define DMA_BURST_LENGTH            8
#define DMA_RING_SIZE_SHIFT         16
#define DMA_LENGTH_SHIFT            16
#define DMA_LENGTH_MASK             0x0FFF
#define DMA_OWN                     0x00008000
#define DMA_EOP                     0x00004000
#define DMA_SOP                     0x00002000
#define DMA_TX_QTAG                 (0x3F << 7)
#define DMA_TX_APPEND_CRC           0x00000040
#define DMA_RX_CRC_ERROR            0x00000002
#define DMA_RX_ERRORS               0x0000001F
#define DMA_XOFF_THRESHOLD          5

//
// The frames: the receive buffer holds two bytes before the frame (the IP
// header falls on a longword), the longest frame a tagged one with the
// Broadcom tag and the CRC, padded.
//

#define RECEIVE_PAD                 2
#define GENET_MAXIMUM_FRAME         1536

//
// The PHY: the MII registers and the Broadcom auxiliary control and shadow
// registers through which the BCM54213PE (of the BCM54210E family) sets its
// clock delays and LEDs.
//

#define MII_BMCR                    0
#define MII_BMSR                    1
#define MII_PHYSID1                 2
#define MII_PHYSID2                 3
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
#define ADVERTISE_PAUSE             0x0400
#define ADVERTISE_1000HALF          0x0100
#define ADVERTISE_1000FULL          0x0200
#define LPA_1000HALF                0x0400
#define LPA_1000FULL                0x0800
#define PHY_RESET_TIMEOUT_MS        500

#define PHY_ID_BCM54210E            0x600D84A0
#define PHY_ID_MODEL_MASK           0xFFFFFFF0
#define BCM_AUXCTL                  0x18
#define BCM_AUXCTL_MISC             0x0007
#define BCM_AUXCTL_READ_MISC        0x7007
#define BCM_AUXCTL_MISC_RGMII_SKEW  0x0100
#define BCM_AUXCTL_MISC_WREN        0x8000
#define BCM_SHADOW                  0x1C
#define BCM_SHADOW_WRITE            0x8000
#define BCM_SHADOW_SHIFT            10
#define BCM_SHADOW_DATA             0x03FF
#define BCM_SHADOW_CLOCK            0x03
#define BCM_SHADOW_CLOCK_GTXCLK     0x0200
#define BCM_SHADOW_LEDS1            0x0D
#define BCM_LEDS1_ACTIVITY_SPEED    0x0013

#define GENET_AREA_SIZE             (GENET_AREA_BUFFERS * GENET_BUFFER_SIZE)

static const UCHAR Broadcast[ETH_LENGTH_OF_ADDRESS] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static ULONG
GenetRead(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Offset
    )
{
    ULONG Value;

    NdisReadRegisterUlong((PULONG)(Adapter->Registers + Offset), &Value);
    return Value;
}

static VOID
GenetWrite(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Offset,
    IN ULONG Value
    )
{
    NdisWriteRegisterUlong((PULONG)(Adapter->Registers + Offset), Value);
}

static VOID
GenetSleep(
    IN ULONG Milliseconds
    )
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000 * (LONGLONG)Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

//
// The buffer of a descriptor: its virtual address in its area, and its
// address on the bus.
//

static PUCHAR
GenetBuffer(
    IN PUCHAR *Areas,
    IN NDIS_PHYSICAL_ADDRESS *AreaPhysical,
    IN ULONG Index,
    OUT PULONG Physical
    )
{
    ULONG Area = Index / GENET_AREA_BUFFERS;
    ULONG Offset = (Index % GENET_AREA_BUFFERS) * GENET_BUFFER_SIZE;

    *Physical = NdisGetPhysicalAddressLow(AreaPhysical[Area]) + Offset;
    return Areas[Area] + Offset;
}

//
// The PHY, on the MDIO bus of the UniMAC; a transaction takes about 25
// microseconds.
//

static BOOLEAN
GenetMdio(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Command,
    OUT PULONG Result
    )
{
    ULONG Waited;
    BOOLEAN Done = FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    GenetWrite(Adapter, UMAC_MDIO_CMD, Command);
    GenetWrite(Adapter, UMAC_MDIO_CMD, Command | MDIO_START_BUSY);
    for (Waited = 0; Waited < MDIO_TIMEOUT_US; Waited += 1) {
        *Result = GenetRead(Adapter, UMAC_MDIO_CMD);
        if ((*Result & MDIO_START_BUSY) == 0) {
            Done = TRUE;
            break;
        }

        NdisStallExecution(1);
    }

    NdisReleaseSpinLock(&Adapter->Lock);
    Adapter->LastRegister = UMAC_MDIO_CMD;
    Adapter->LastValue = *Result;
    return Done;
}

static BOOLEAN
GenetPhyRead(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Register,
    OUT PUSHORT Value
    )
{
    ULONG Result;

    if (!GenetMdio(Adapter, MDIO_RD | (Adapter->PhyAddress << MDIO_PHY_SHIFT) | (Register << MDIO_REGISTER_SHIFT),
                   &Result) ||
        (Result & MDIO_READ_FAIL) != 0) {
        return FALSE;
    }

    *Value = (USHORT)Result;
    return TRUE;
}

static BOOLEAN
GenetPhyWrite(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Register,
    IN USHORT Value
    )
{
    ULONG Result;

    return GenetMdio(Adapter, MDIO_WR | (Adapter->PhyAddress << MDIO_PHY_SHIFT) | (Register << MDIO_REGISTER_SHIFT) |
                     Value, &Result);
}

//
// Find the PHY: at address 1 on the Pi 4 Model B, 0 on the Compute Module 4,
// else anywhere on the bus.
//

static BOOLEAN
GenetFindPhy(
    IN PBCMETH_ADAPTER Adapter
    )
{
    USHORT Id1;
    USHORT Id2;
    ULONG Index;

    for (Index = 0; Index < 32; Index += 1) {
        Adapter->PhyAddress = Index == 0 ? 1 : Index == 1 ? 0 : Index;
        if (GenetPhyRead(Adapter, MII_PHYSID1, &Id1) && GenetPhyRead(Adapter, MII_PHYSID2, &Id2) &&
            (Id1 | Id2) != 0 && (Id1 & Id2) != 0xFFFF) {
            Adapter->PhyId = ((ULONG)Id1 << 16) | Id2;
            return TRUE;
        }
    }

    return FALSE;
}

//
// Reset the PHY, set its RGMII clock delays (the receive clock delayed, the
// transmit clock not: the GENET delays it) and its LEDs (activity and link
// speed), advertise what the MAC does and start the autonegotiation.
//

static BOOLEAN
GenetStartPhy(
    IN PBCMETH_ADAPTER Adapter
    )
{
    USHORT Value;
    ULONG Waited;

    if (!GenetPhyWrite(Adapter, MII_BMCR, BMCR_RESET)) {
        return FALSE;
    }

    for (Waited = 0; Waited < PHY_RESET_TIMEOUT_MS; Waited += 10) {
        if (GenetPhyRead(Adapter, MII_BMCR, &Value) && (Value & BMCR_RESET) == 0) {
            break;
        }

        GenetSleep(10);
    }

    if ((Adapter->PhyId & PHY_ID_MODEL_MASK) == PHY_ID_BCM54210E) {
        if (!GenetPhyWrite(Adapter, BCM_AUXCTL, BCM_AUXCTL_READ_MISC) ||
            !GenetPhyRead(Adapter, BCM_AUXCTL, &Value) ||
            !GenetPhyWrite(Adapter, BCM_AUXCTL, (USHORT)(Value | BCM_AUXCTL_MISC_WREN | BCM_AUXCTL_MISC_RGMII_SKEW |
                                                          BCM_AUXCTL_MISC)) ||
            !GenetPhyWrite(Adapter, BCM_SHADOW, BCM_SHADOW_CLOCK << BCM_SHADOW_SHIFT) ||
            !GenetPhyRead(Adapter, BCM_SHADOW, &Value) ||
            !GenetPhyWrite(Adapter, BCM_SHADOW, (USHORT)(BCM_SHADOW_WRITE | (BCM_SHADOW_CLOCK << BCM_SHADOW_SHIFT) |
                                                          (Value & BCM_SHADOW_DATA & ~BCM_SHADOW_CLOCK_GTXCLK))) ||
            !GenetPhyWrite(Adapter, BCM_SHADOW, BCM_SHADOW_WRITE | (BCM_SHADOW_LEDS1 << BCM_SHADOW_SHIFT) |
                           BCM_LEDS1_ACTIVITY_SPEED)) {
            return FALSE;
        }
    }

    return GenetPhyWrite(Adapter, MII_ADVERTISE, ADVERTISE_CSMA | ADVERTISE_10HALF | ADVERTISE_10FULL |
                         ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE) &&
           GenetPhyWrite(Adapter, MII_CTRL1000, ADVERTISE_1000HALF | ADVERTISE_1000FULL) &&
           GenetPhyWrite(Adapter, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

//
// The UniMAC command bits of the link: its speed, duplex and pause.
//

static ULONG
GenetLinkCommand(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Command;

    Command = Adapter->LinkSpeed == 10000000 ? CMD_SPEED_1000 :
              Adapter->LinkSpeed == 1000000 ? CMD_SPEED_100 : CMD_SPEED_10;
    if (!Adapter->FullDuplex) {
        Command |= CMD_HD_EN;
    }

    if (!Adapter->Pause) {
        Command |= CMD_RX_PAUSE_IGNORE | CMD_TX_PAUSE_IGNORE;
    }

    return Command;
}

//
// The receive filter, with the lock held: the destination filter takes the
// broadcast address, the station address and a multicast list of up to 15;
// promiscuous mode takes all multicast too (the UniMAC has no all-multicast
// mode), and the wrapper filters what reaches each binding. Without a
// multicast list the UniMAC takes what is sent to its address or broadcast.
//

static VOID
GenetSetMdf(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG Filter,
    IN PUCHAR Address
    )
{
    GenetWrite(Adapter, UMAC_MDF_ADDR + Filter * 8, (Address[0] << 8) | Address[1]);
    GenetWrite(Adapter, UMAC_MDF_ADDR + Filter * 8 + 4,
               (Address[2] << 24) | (Address[3] << 16) | (Address[4] << 8) | Address[5]);
}

static VOID
GenetSetFilterLocked(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Filter = Adapter->PacketFilter;
    BOOLEAN Multicast = (Filter & NDIS_PACKET_TYPE_MULTICAST) && Adapter->MulticastCount != 0;
    ULONG Command = GenetRead(Adapter, UMAC_CMD) & ~CMD_PROMISC;
    ULONG Count = 0;
    ULONG Index;

    if ((Filter & (NDIS_PACKET_TYPE_PROMISCUOUS | NDIS_PACKET_TYPE_ALL_MULTICAST)) ||
        (Multicast && Adapter->MulticastCount > MDF_FILTERS - 2)) {
        Command |= CMD_PROMISC;
    } else if (Multicast) {
        GenetSetMdf(Adapter, Count++, (PUCHAR)Broadcast);
        GenetSetMdf(Adapter, Count++, Adapter->CurrentAddress);
        for (Index = 0; Index < Adapter->MulticastCount; Index += 1) {
            GenetSetMdf(Adapter, Count++, Adapter->Multicast[Index]);
        }
    }

    GenetWrite(Adapter, UMAC_MDF_CTRL, Count == 0 ? 0 : ((1 << Count) - 1) << (MDF_FILTERS - Count));
    GenetWrite(Adapter, UMAC_CMD, Command);
}

//
// Stop the controller: its interrupts, the MAC, then the DMA, waiting for
// each engine to stop, and the transmit FIFO.
//

static VOID
GenetStop(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Waited;

    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);
    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_CLEAR, 0xFFFFFFFF);
    NdisAcquireSpinLock(&Adapter->Lock);
    GenetWrite(Adapter, UMAC_CMD, GenetRead(Adapter, UMAC_CMD) & ~(CMD_TX_EN | CMD_RX_EN));
    NdisReleaseSpinLock(&Adapter->Lock);

    GenetWrite(Adapter, TDMA_CTRL, GenetRead(Adapter, TDMA_CTRL) & ~(DMA_RING_EN | DMA_EN));
    for (Waited = 0; Waited < DMA_TIMEOUT_US && (GenetRead(Adapter, TDMA_STATUS) & DMA_DISABLED) == 0;
         Waited += 1) {
        NdisStallExecution(1);
    }

    GenetWrite(Adapter, RDMA_CTRL, GenetRead(Adapter, RDMA_CTRL) & ~(DMA_RING_EN | DMA_EN));
    for (Waited = 0; Waited < DMA_TIMEOUT_US && (GenetRead(Adapter, RDMA_STATUS) & DMA_DISABLED) == 0;
         Waited += 1) {
        NdisStallExecution(1);
    }

    GenetWrite(Adapter, UMAC_TX_FLUSH, 1);
    NdisStallExecution(10);
    GenetWrite(Adapter, UMAC_TX_FLUSH, 0);
}

//
// Start the controller with its interrupts masked: reset the UniMAC (in
// local loopback, for a steady receive clock), set its frame length, the
// alignment of the receive buffer and the station address, set up both
// rings with their buffers and start the DMA, then set the RGMII block and
// the MAC for the link the PHY has, the receive filter, and start the MAC.
// The DMA engines count frames in their producer and consumer indexes,
// which a reset does not clear: the rings start from the indexes as they
// are, at their first descriptors.
//

static VOID
GenetStart(
    IN PBCMETH_ADAPTER Adapter
    )
{
    PUCHAR Address = Adapter->CurrentAddress;
    ULONG Physical;
    ULONG Index;
    ULONG Value;

    NdisAcquireSpinLock(&Adapter->Lock);
    Value = GenetRead(Adapter, SYS_RBUF_FLUSH_CTRL);
    GenetWrite(Adapter, SYS_RBUF_FLUSH_CTRL, Value | RBUF_FLUSH_UMAC_RESET);
    NdisStallExecution(10);
    GenetWrite(Adapter, SYS_RBUF_FLUSH_CTRL, Value & ~RBUF_FLUSH_UMAC_RESET);
    NdisStallExecution(10);
    GenetWrite(Adapter, SYS_RBUF_FLUSH_CTRL, 0);
    NdisStallExecution(10);
    GenetWrite(Adapter, UMAC_CMD, 0);
    GenetWrite(Adapter, UMAC_CMD, CMD_SW_RESET | CMD_LCL_LOOP_EN);
    NdisStallExecution(2);
    GenetWrite(Adapter, UMAC_CMD, 0);
    GenetWrite(Adapter, UMAC_MIB_CTRL, MIB_RESET_RX | MIB_RESET_TX | MIB_RESET_RUNT);
    GenetWrite(Adapter, UMAC_MIB_CTRL, 0);
    GenetWrite(Adapter, UMAC_MAX_FRAME_LEN, GENET_MAXIMUM_FRAME);
    GenetWrite(Adapter, RBUF_CTRL, (GenetRead(Adapter, RBUF_CTRL) | RBUF_ALIGN_2B) & ~RBUF_64B_EN);
    GenetWrite(Adapter, RBUF_TBUF_SIZE_CTRL, 1);
    GenetWrite(Adapter, UMAC_MAC0, (Address[0] << 24) | (Address[1] << 16) | (Address[2] << 8) | Address[3]);
    GenetWrite(Adapter, UMAC_MAC1, (Address[4] << 8) | Address[5]);
    NdisReleaseSpinLock(&Adapter->Lock);

    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);
    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_CLEAR, 0xFFFFFFFF);
    GenetWrite(Adapter, INTRL2_1 + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);
    GenetWrite(Adapter, INTRL2_1 + INTRL2_CPU_CLEAR, 0xFFFFFFFF);
    GenetWrite(Adapter, TDMA_CTRL, GenetRead(Adapter, TDMA_CTRL) & ~(DMA_RING_EN | DMA_EN));
    GenetWrite(Adapter, RDMA_CTRL, GenetRead(Adapter, RDMA_CTRL) & ~(DMA_RING_EN | DMA_EN));
    GenetWrite(Adapter, UMAC_TX_FLUSH, 1);
    NdisStallExecution(10);
    GenetWrite(Adapter, UMAC_TX_FLUSH, 0);

    GenetWrite(Adapter, RDMA_SCB_BURST_SIZE, DMA_BURST_LENGTH);
    GenetWrite(Adapter, RDMA_START_ADDR, 0);
    GenetWrite(Adapter, RDMA_READ_PTR, 0);
    GenetWrite(Adapter, RDMA_WRITE_PTR, 0);
    GenetWrite(Adapter, RDMA_END_ADDR, GENET_RECEIVE_BUFFERS * DESCRIPTOR_SIZE / 4 - 1);
    Adapter->ReceiveConsumer = GenetRead(Adapter, RDMA_PROD_INDEX) & 0xFFFF;
    Adapter->ReceiveIndex = 0;
    GenetWrite(Adapter, RDMA_CONS_INDEX, Adapter->ReceiveConsumer);
    GenetWrite(Adapter, RDMA_RING_BUF_SIZE, (GENET_RECEIVE_BUFFERS << DMA_RING_SIZE_SHIFT) | GENET_BUFFER_SIZE);
    GenetWrite(Adapter, RDMA_XON_XOFF_THRESH, (DMA_XOFF_THRESHOLD << 16) | (GENET_RECEIVE_BUFFERS >> 4));
    GenetWrite(Adapter, RDMA_MBUF_DONE_THRESH, 1);
    for (Index = 0; Index < GENET_RECEIVE_BUFFERS; Index += 1) {
        GenetBuffer(Adapter->ReceiveArea, Adapter->ReceiveAreaPhysical, Index, &Physical);
        GenetWrite(Adapter, RDMA_DESCRIPTORS + Index * DESCRIPTOR_SIZE + DESCRIPTOR_ADDRESS_LO, Physical);
        GenetWrite(Adapter, RDMA_DESCRIPTORS + Index * DESCRIPTOR_SIZE + DESCRIPTOR_ADDRESS_HI, 0);
        GenetWrite(Adapter, RDMA_DESCRIPTORS + Index * DESCRIPTOR_SIZE + DESCRIPTOR_LENGTH_STATUS,
                   (GENET_BUFFER_SIZE << DMA_LENGTH_SHIFT) | DMA_OWN);
    }

    GenetWrite(Adapter, RDMA_RING_CFG, 1 << DEFAULT_RING);

    GenetWrite(Adapter, TDMA_SCB_BURST_SIZE, DMA_BURST_LENGTH);
    GenetWrite(Adapter, TDMA_START_ADDR, 0);
    GenetWrite(Adapter, TDMA_READ_PTR, 0);
    GenetWrite(Adapter, TDMA_WRITE_PTR, 0);
    GenetWrite(Adapter, TDMA_END_ADDR, GENET_TRANSMIT_BUFFERS * DESCRIPTOR_SIZE / 4 - 1);
    Adapter->TransmitConsumer = GenetRead(Adapter, TDMA_CONS_INDEX) & 0xFFFF;
    Adapter->TransmitProducer = Adapter->TransmitConsumer;
    Adapter->TransmitIndex = 0;
    Adapter->TransmitCount = 0;
    GenetWrite(Adapter, TDMA_PROD_INDEX, Adapter->TransmitProducer);
    GenetWrite(Adapter, TDMA_MBUF_DONE_THRESH, 1);
    GenetWrite(Adapter, TDMA_FLOW_PERIOD, 0);
    GenetWrite(Adapter, TDMA_RING_BUF_SIZE, (GENET_TRANSMIT_BUFFERS << DMA_RING_SIZE_SHIFT) | GENET_BUFFER_SIZE);
    GenetWrite(Adapter, TDMA_RING_CFG, 1 << DEFAULT_RING);

    GenetWrite(Adapter, TDMA_CTRL, DMA_RING_EN | DMA_EN);
    GenetWrite(Adapter, RDMA_CTRL, GenetRead(Adapter, RDMA_CTRL) | DMA_RING_EN | DMA_EN);

    NdisAcquireSpinLock(&Adapter->Lock);
    Value = GenetRead(Adapter, EXT_RGMII_OOB_CTRL) & ~(OOB_DISABLE | ID_MODE_DIS | RGMII_LINK);
    GenetWrite(Adapter, EXT_RGMII_OOB_CTRL, Value | RGMII_MODE_EN | (Adapter->LinkUp ? RGMII_LINK : 0));
    GenetWrite(Adapter, UMAC_CMD, GenetLinkCommand(Adapter));
    GenetSetFilterLocked(Adapter);
    GenetWrite(Adapter, UMAC_CMD, GenetRead(Adapter, UMAC_CMD) | CMD_TX_EN | CMD_RX_EN);
    NdisReleaseSpinLock(&Adapter->Lock);
}

//
// The buffers: the areas of shared memory, and an NDIS buffer for each
// buffer, through which the data cache is flushed. The cache is flushed for
// each receive buffer before the controller first fills it.
//

static VOID
GenetFreeBuffers(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Index;

    for (Index = 0; Index < GENET_RECEIVE_BUFFERS; Index += 1) {
        if (Adapter->ReceiveFlush[Index] != NULL) {
            NdisFreeBuffer(Adapter->ReceiveFlush[Index]);
        }
    }

    for (Index = 0; Index < GENET_TRANSMIT_BUFFERS; Index += 1) {
        if (Adapter->TransmitFlush[Index] != NULL) {
            NdisFreeBuffer(Adapter->TransmitFlush[Index]);
        }
    }

    if (Adapter->BufferPool != NULL) {
        NdisFreeBufferPool(Adapter->BufferPool);
    }

    for (Index = 0; Index < GENET_RECEIVE_AREAS; Index += 1) {
        if (Adapter->ReceiveArea[Index] != NULL) {
            NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, GENET_AREA_SIZE, TRUE, Adapter->ReceiveArea[Index],
                                  Adapter->ReceiveAreaPhysical[Index]);
        }
    }

    for (Index = 0; Index < GENET_TRANSMIT_AREAS; Index += 1) {
        if (Adapter->TransmitArea[Index] != NULL) {
            NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, GENET_AREA_SIZE, TRUE,
                                  Adapter->TransmitArea[Index], Adapter->TransmitAreaPhysical[Index]);
        }
    }
}

static BOOLEAN
GenetAllocateBuffers(
    IN PBCMETH_ADAPTER Adapter
    )
{
    NDIS_STATUS Status;
    PUCHAR Buffer;
    ULONG Physical;
    ULONG Index;

    NdisAllocateBufferPool(&Status, &Adapter->BufferPool, GENET_RECEIVE_BUFFERS + GENET_TRANSMIT_BUFFERS);
    if (Status != NDIS_STATUS_SUCCESS) {
        Adapter->BufferPool = NULL;
        return FALSE;
    }

    for (Index = 0; Index < GENET_RECEIVE_AREAS; Index += 1) {
        NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, GENET_AREA_SIZE, TRUE,
                                  (PVOID *)&Adapter->ReceiveArea[Index], &Adapter->ReceiveAreaPhysical[Index]);
        if (Adapter->ReceiveArea[Index] == NULL ||
            NdisGetPhysicalAddressHigh(Adapter->ReceiveAreaPhysical[Index]) != 0) {
            return FALSE;
        }
    }

    for (Index = 0; Index < GENET_TRANSMIT_AREAS; Index += 1) {
        NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, GENET_AREA_SIZE, TRUE,
                                  (PVOID *)&Adapter->TransmitArea[Index], &Adapter->TransmitAreaPhysical[Index]);
        if (Adapter->TransmitArea[Index] == NULL ||
            NdisGetPhysicalAddressHigh(Adapter->TransmitAreaPhysical[Index]) != 0) {
            return FALSE;
        }
    }

    for (Index = 0; Index < GENET_RECEIVE_BUFFERS; Index += 1) {
        Buffer = GenetBuffer(Adapter->ReceiveArea, Adapter->ReceiveAreaPhysical, Index, &Physical);
        NdisAllocateBuffer(&Status, &Adapter->ReceiveFlush[Index], Adapter->BufferPool, Buffer, GENET_BUFFER_SIZE);
        if (Status != NDIS_STATUS_SUCCESS) {
            Adapter->ReceiveFlush[Index] = NULL;
            return FALSE;
        }

        NdisFlushBuffer(Adapter->ReceiveFlush[Index], FALSE);
    }

    for (Index = 0; Index < GENET_TRANSMIT_BUFFERS; Index += 1) {
        Buffer = GenetBuffer(Adapter->TransmitArea, Adapter->TransmitAreaPhysical, Index, &Physical);
        NdisAllocateBuffer(&Status, &Adapter->TransmitFlush[Index], Adapter->BufferPool, Buffer, GENET_BUFFER_SIZE);
        if (Status != NDIS_STATUS_SUCCESS) {
            Adapter->TransmitFlush[Index] = NULL;
            return FALSE;
        }
    }

    return TRUE;
}

static VOID
GenetRelease(
    IN PBCMETH_ADAPTER Adapter
    )
{
    GenetFreeBuffers(Adapter);
    if (Adapter->Registers != NULL) {
        NdisMUnmapIoSpace(Adapter->MiniportAdapterHandle, Adapter->Registers, Adapter->RegistersLength);
    }

    if (Adapter->MapRegisters) {
        NdisMFreeMapRegisters(Adapter->MiniportAdapterHandle);
    }
}

NDIS_STATUS
GenetInitialize(
    IN PBCMETH_ADAPTER Adapter,
    IN ULONG InterruptVector,
    IN ULONG InterruptLevel,
    OUT PULONG Step
    )

/*++

Routine Description:

    This routine maps the registers of the GENET the firmware describes,
    allocates its buffers on the bus of its DMA channel, finds and starts its
    PHY (with the MAC held in reset), starts the controller, connects its
    interrupt and unmasks it.

Arguments:

    Adapter - Supplies the adapter, with the registers, the DMA channel and
        the current address of the controller.

    InterruptVector, InterruptLevel - Supply the interrupt of the controller.

    Step - Receives the step that failed: 2 the controller or its PHY did not
        answer, 4 the resources, 5 the start of the PHY or the interrupt.

Return Value:

    NDIS_STATUS_SUCCESS, or why the controller could not be started.

--*/

{
    NDIS_PHYSICAL_ADDRESS Physical;
    NDIS_STATUS Status;
    ULONG Revision;

    *Step = 4;
    NdisMSetAttributes(Adapter->MiniportAdapterHandle, (NDIS_HANDLE)Adapter, TRUE, NdisInterfaceInternal);
    Status = NdisMAllocateMapRegisters(Adapter->MiniportAdapterHandle, Adapter->DmaChannel, TRUE, 1,
                                       GENET_BUFFER_SIZE);
    if (Status != NDIS_STATUS_SUCCESS) {
        return Status;
    }

    Adapter->MapRegisters = TRUE;
    NdisSetPhysicalAddressHigh(Physical, 0);
    NdisSetPhysicalAddressLow(Physical, Adapter->RegistersPhysical);
    Status = NdisMMapIoSpace((PVOID *)&Adapter->Registers, Adapter->MiniportAdapterHandle, Physical,
                             Adapter->RegistersLength);
    if (Status != NDIS_STATUS_SUCCESS) {
        Adapter->Registers = NULL;
        GenetRelease(Adapter);
        return Status;
    }

    if (!GenetAllocateBuffers(Adapter)) {
        GenetRelease(Adapter);
        return NDIS_STATUS_RESOURCES;
    }

    *Step = 2;
    Revision = GenetRead(Adapter, SYS_REV_CTRL);
    Adapter->LastRegister = SYS_REV_CTRL;
    Adapter->LastValue = Revision;
    if (((Revision >> 24) & 0xF) != GENET_MAJOR_REVISION) {
        GenetRelease(Adapter);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);
    GenetWrite(Adapter, INTRL2_1 + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);
    GenetWrite(Adapter, SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);
    GenetWrite(Adapter, SYS_RBUF_FLUSH_CTRL, 0);
    NdisStallExecution(10);
    GenetWrite(Adapter, UMAC_CMD, 0);
    GenetWrite(Adapter, UMAC_CMD, CMD_SW_RESET | CMD_LCL_LOOP_EN);
    if (!GenetFindPhy(Adapter)) {
        GenetRelease(Adapter);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    *Step = 5;
    Adapter->Device = Adapter->PhyAddress;
    if (!GenetStartPhy(Adapter)) {
        GenetRelease(Adapter);
        return NDIS_STATUS_HARD_ERRORS;
    }

    //
    // Until the link is up the MAC runs at a gigabit, full duplex.
    //

    Adapter->LinkUp = FALSE;
    Adapter->LinkSpeed = 10000000;
    Adapter->FullDuplex = TRUE;
    Adapter->Pause = FALSE;
    GenetStart(Adapter);
    Status = NdisMRegisterInterrupt(&Adapter->Interrupt, Adapter->MiniportAdapterHandle, InterruptVector,
                                    InterruptLevel, FALSE, FALSE, NdisInterruptLevelSensitive);
    if (Status != NDIS_STATUS_SUCCESS) {
        GenetStop(Adapter);
        GenetRelease(Adapter);
        return Status;
    }

    GenetEnableInterrupt(Adapter);
    return NDIS_STATUS_SUCCESS;
}

VOID
GenetHalt(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine stops the controller, disconnects its interrupt and frees
    its resources.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    GenetStop(Adapter);
    NdisMDeregisterInterrupt(&Adapter->Interrupt);
    GenetRelease(Adapter);
}

NDIS_STATUS
GenetReset(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine resets the controller: it stops it and starts it again,
    with empty rings (the transmits in the ring completed when they were
    copied) and the link as it was.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    NDIS_STATUS_SUCCESS.

--*/

{
    GenetStop(Adapter);
    GenetStart(Adapter);
    Adapter->ResourcesNeeded = FALSE;
    GenetEnableInterrupt(Adapter);
    return NDIS_STATUS_SUCCESS;
}

//
// The transmits the controller has sent: from the last consumer index to
// its consumer index now.
//

static VOID
GenetReclaimTransmits(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Consumed = GenetRead(Adapter, TDMA_CONS_INDEX) & 0xFFFF;
    ULONG Done = (Consumed - Adapter->TransmitConsumer) & 0xFFFF;

    if (Done > Adapter->TransmitCount) {
        Done = Adapter->TransmitCount;
    }

    Adapter->TransmitConsumer = (Adapter->TransmitConsumer + Done) & 0xFFFF;
    Adapter->TransmitCount -= Done;
    Adapter->TransmitOk += Done;
}

BOOLEAN
GenetCheckForHang(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine looks at the transmitter: it hangs if, with the link up,
    transmits wait in the ring and none was sent over two checks (four
    seconds).

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    TRUE if the controller must be reset.

--*/

{
    ULONG Transmits = Adapter->TransmitOk;

    if (!Adapter->LinkUp || Adapter->TransmitCount == 0 || Transmits != Adapter->LastTransmits) {
        Adapter->LastTransmits = Transmits;
        Adapter->HangChecks = 0;
        return FALSE;
    }

    Adapter->HangChecks += 1;
    return Adapter->HangChecks >= 2;
}

VOID
GenetIsr(
    OUT PBOOLEAN InterruptRecognized,
    OUT PBOOLEAN QueueMiniportHandleInterrupt,
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine is the interrupt service routine, which the wrapper calls
    only while the adapter initializes or halts (it masks the interrupt
    itself otherwise). It clears the interrupts of the rings: the frames
    they announce wait in the rings for the next interrupt.

Arguments:

    InterruptRecognized - Receives whether the controller interrupted.

    QueueMiniportHandleInterrupt - Receives whether to run the handler.

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    ULONG Status = GenetRead(Adapter, INTRL2_0 + INTRL2_CPU_STAT) &
                   ~GenetRead(Adapter, INTRL2_0 + INTRL2_CPU_MASK_STATUS) & GENET_INTERRUPTS;

    if (Status != 0) {
        GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_CLEAR, Status);
    }

    *InterruptRecognized = Status != 0;
    *QueueMiniportHandleInterrupt = Status != 0;
}

VOID
GenetDisableInterrupt(
    IN PBCMETH_ADAPTER Adapter
    )
{
    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_MASK_SET, GENET_INTERRUPTS);
}

VOID
GenetEnableInterrupt(
    IN PBCMETH_ADAPTER Adapter
    )
{
    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_MASK_CLEAR, GENET_INTERRUPTS);
}

//
// The frames received: from the consumer index of the ring to the producer
// index of the controller, each indicated from its buffer (with the frame
// whole in the lookahead), which then goes back to the controller.
//

static VOID
GenetReceive(
    IN PBCMETH_ADAPTER Adapter
    )
{
    ULONG Produced = GenetRead(Adapter, RDMA_PROD_INDEX) & 0xFFFF;
    BOOLEAN Indicated = FALSE;
    ULONG LengthStatus;
    ULONG Length;
    ULONG Physical;
    ULONG Index;
    PUCHAR Buffer;

    while (Adapter->ReceiveConsumer != Produced) {
        Index = Adapter->ReceiveIndex;
        LengthStatus = GenetRead(Adapter, RDMA_DESCRIPTORS + Index * DESCRIPTOR_SIZE + DESCRIPTOR_LENGTH_STATUS);
        Length = (LengthStatus >> DMA_LENGTH_SHIFT) & DMA_LENGTH_MASK;
        if ((LengthStatus & (DMA_SOP | DMA_EOP)) != (DMA_SOP | DMA_EOP) || (LengthStatus & DMA_RX_ERRORS) != 0 ||
            Length < RECEIVE_PAD + ETH_HEADER_SIZE || Length > RECEIVE_PAD + ETH_MAXIMUM_FRAME) {
            Adapter->ReceiveError += 1;
            if (LengthStatus & DMA_RX_CRC_ERROR) {
                Adapter->ReceiveCrcError += 1;
            }
        } else {
            Buffer = GenetBuffer(Adapter->ReceiveArea, Adapter->ReceiveAreaPhysical, Index, &Physical);
            NdisFlushBuffer(Adapter->ReceiveFlush[Index], FALSE);
            Adapter->Frame = Buffer + RECEIVE_PAD;
            Adapter->FrameLength = Length - RECEIVE_PAD;
            Adapter->ReceiveOk += 1;
            NdisMEthIndicateReceive(Adapter->MiniportAdapterHandle, (NDIS_HANDLE)Adapter, Adapter->Frame,
                                    ETH_HEADER_SIZE, Adapter->Frame + ETH_HEADER_SIZE,
                                    Adapter->FrameLength - ETH_HEADER_SIZE, Adapter->FrameLength - ETH_HEADER_SIZE);
            Indicated = TRUE;
        }

        Adapter->ReceiveIndex = (Index + 1) % GENET_RECEIVE_BUFFERS;
        Adapter->ReceiveConsumer = (Adapter->ReceiveConsumer + 1) & 0xFFFF;
        GenetWrite(Adapter, RDMA_CONS_INDEX, Adapter->ReceiveConsumer);
    }

    if (Indicated) {
        NdisMEthIndicateReceiveComplete(Adapter->MiniportAdapterHandle);
    }
}

VOID
GenetHandleInterrupt(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine is the interrupt handler, which runs with the interrupt
    masked: it clears the interrupts of the rings first, so that a frame
    that arrives after the producer index was read interrupts again, then
    indicates the frames received and reclaims the transmits sent, telling
    the wrapper when there is room for a transmit again.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    GenetWrite(Adapter, INTRL2_0 + INTRL2_CPU_CLEAR, GENET_INTERRUPTS);
    GenetReceive(Adapter);
    GenetReclaimTransmits(Adapter);
    if (Adapter->ResourcesNeeded && Adapter->TransmitCount < GENET_TRANSMIT_BUFFERS) {
        Adapter->ResourcesNeeded = FALSE;
        NdisMSendResourcesAvailable(Adapter->MiniportAdapterHandle);
    }
}

NDIS_STATUS
GenetSend(
    IN PBCMETH_ADAPTER Adapter,
    IN PNDIS_PACKET Packet
    )

/*++

Routine Description:

    This routine copies a frame into the buffer of the next transmit
    descriptor (padded to the minimum frame), flushes the data cache for it
    and hands the descriptor to the controller, which appends the CRC.

Arguments:

    Adapter - Supplies the adapter.

    Packet - Supplies the frame.

Return Value:

    NDIS_STATUS_SUCCESS, NDIS_STATUS_RESOURCES when the ring is full, or
    NDIS_STATUS_FAILURE for a frame of a wrong length.

--*/

{
    ULONG Index;
    ULONG Physical;
    ULONG Descriptor;
    PNDIS_BUFFER Buffer;
    PUCHAR Slot;
    PVOID Data;
    UINT Length;
    UINT Total;
    UINT Copied = 0;
    UINT Frame;

    GenetReclaimTransmits(Adapter);
    if (Adapter->TransmitCount == GENET_TRANSMIT_BUFFERS) {
        Adapter->ResourcesNeeded = TRUE;
        return NDIS_STATUS_RESOURCES;
    }

    NdisQueryPacket(Packet, NULL, NULL, &Buffer, &Total);
    if (Total < ETH_HEADER_SIZE || Total > ETH_MAXIMUM_FRAME) {
        Adapter->TransmitError += 1;
        return NDIS_STATUS_FAILURE;
    }

    Index = Adapter->TransmitIndex;
    Slot = GenetBuffer(Adapter->TransmitArea, Adapter->TransmitAreaPhysical, Index, &Physical);
    while (Buffer != NULL) {
        NdisQueryBuffer(Buffer, &Data, &Length);
        NdisMoveMemory(Slot + Copied, Data, Length);
        Copied += Length;
        NdisGetNextBuffer(Buffer, &Buffer);
    }

    Frame = Total < ETH_MINIMUM_FRAME ? ETH_MINIMUM_FRAME : Total;
    NdisZeroMemory(Slot + Copied, Frame - Copied);
    NdisFlushBuffer(Adapter->TransmitFlush[Index], TRUE);

    Descriptor = TDMA_DESCRIPTORS + Index * DESCRIPTOR_SIZE;
    GenetWrite(Adapter, Descriptor + DESCRIPTOR_ADDRESS_LO, Physical);
    GenetWrite(Adapter, Descriptor + DESCRIPTOR_ADDRESS_HI, 0);
    GenetWrite(Adapter, Descriptor + DESCRIPTOR_LENGTH_STATUS,
               (Frame << DMA_LENGTH_SHIFT) | DMA_TX_QTAG | DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP);
    Adapter->TransmitIndex = (Index + 1) % GENET_TRANSMIT_BUFFERS;
    Adapter->TransmitProducer = (Adapter->TransmitProducer + 1) & 0xFFFF;
    Adapter->TransmitCount += 1;
    GenetWrite(Adapter, TDMA_PROD_INDEX, Adapter->TransmitProducer);
    return NDIS_STATUS_SUCCESS;
}

VOID
GenetSetFilter(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine sets the receive filter of the controller from the packet
    filter and the multicast list.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    NdisAcquireSpinLock(&Adapter->Lock);
    GenetSetFilterLocked(Adapter);
    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
GenetCheckLink(
    IN PBCMETH_ADAPTER Adapter
    )

/*++

Routine Description:

    This routine looks at the link of the PHY: up or down, and the speed,
    duplex and pause the autonegotiation chose, which the MAC and the RGMII
    block are set to when they change.

Arguments:

    Adapter - Supplies the adapter.

Return Value:

    None.

--*/

{
    USHORT Status;
    USHORT Advertise;
    USHORT Partner;
    USHORT Control1000;
    USHORT Status1000;
    USHORT Common;
    ULONG Speed;
    BOOLEAN FullDuplex;
    BOOLEAN Pause;

    if (!GenetPhyRead(Adapter, MII_BMSR, &Status) || !GenetPhyRead(Adapter, MII_BMSR, &Status)) {
        return;
    }

    if ((Status & BMSR_LINK) == 0) {
        if (Adapter->LinkUp) {
            Adapter->LinkUp = FALSE;
            NdisAcquireSpinLock(&Adapter->Lock);
            GenetWrite(Adapter, EXT_RGMII_OOB_CTRL, GenetRead(Adapter, EXT_RGMII_OOB_CTRL) & ~RGMII_LINK);
            NdisReleaseSpinLock(&Adapter->Lock);
        }

        return;
    }

    if (!GenetPhyRead(Adapter, MII_ADVERTISE, &Advertise) || !GenetPhyRead(Adapter, MII_LPA, &Partner) ||
        !GenetPhyRead(Adapter, MII_CTRL1000, &Control1000) || !GenetPhyRead(Adapter, MII_STAT1000, &Status1000)) {
        return;
    }

    Common = Advertise & Partner;
    if ((Control1000 & ADVERTISE_1000FULL) && (Status1000 & LPA_1000FULL)) {
        Speed = 10000000;
        FullDuplex = TRUE;
    } else if ((Control1000 & ADVERTISE_1000HALF) && (Status1000 & LPA_1000HALF)) {
        Speed = 10000000;
        FullDuplex = FALSE;
    } else if (Common & (ADVERTISE_100FULL | ADVERTISE_100HALF)) {
        Speed = 1000000;
        FullDuplex = (Common & ADVERTISE_100FULL) != 0;
    } else {
        Speed = 100000;
        FullDuplex = (Common & ADVERTISE_10FULL) != 0;
    }

    Pause = FullDuplex && (Common & ADVERTISE_PAUSE) != 0;
    if (Adapter->LinkUp && Speed == Adapter->LinkSpeed && FullDuplex == Adapter->FullDuplex &&
        Pause == Adapter->Pause) {
        return;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkSpeed = Speed;
    Adapter->FullDuplex = FullDuplex;
    Adapter->Pause = Pause;
    Adapter->LinkUp = TRUE;
    GenetWrite(Adapter, UMAC_CMD, (GenetRead(Adapter, UMAC_CMD) & ~CMD_LINK_MASK) | GenetLinkCommand(Adapter));
    GenetWrite(Adapter, EXT_RGMII_OOB_CTRL, GenetRead(Adapter, EXT_RGMII_OOB_CTRL) | RGMII_LINK);
    NdisReleaseSpinLock(&Adapter->Lock);
}
