/*++

Module Name:

    pcie.c

Abstract:

    This module brings up the PCI Express root complex of the BCM2711 and
    the device on its link: on the Raspberry Pi 4 Model B and 400 the VIA
    VL805, the USB 3 host of their USB-A ports, an xHCI.

    The root complex is reset and its windows set up: inbound, the first
    4 GB of the bus are the memory of the processor at the same addresses,
    for the DMA of the device (the wrapper of the root complex reaches the
    first 3 GB); outbound, 64 MB of the processor at 0x6_0000_0000 are the
    bus addresses from 0xF8000000, which the firmware maps at 0xF8000000.
    The link is trained, then the bridge of the root complex is given the
    bus behind it and the outbound window, and the device its memory space
    there, and its interrupt line. The VideoCore is told the host was reset,
    and loads its firmware into it if it has none. The bus is then added to
    the configuration tree, for the HAL.

    The configuration space of the root complex is its registers; that of
    the device behind it is reached through an index and a data window. A
    BCM2711 without the root complex (QEMU's) is found by a probe of its
    reset register, the one that answers while the bridge is held in reset
    (the others answer with a bus error then).

--*/

#include "fwp.h"

#define PCIE_BASE               0xFD500000
#define PCIE(Offset)            (*(volatile ULONG *)(PCIE_BASE + (Offset)))

#define RC_CFG_VENDOR_SPECIFIC_REG1     0x0188
#define RC_CFG_PRIV1_ID_VAL3            0x043C
#define RC_CFG_PRIV1_LINK_CAPABILITY    0x04DC
#define RC_DL_MDIO_ADDR                 0x1100
#define RC_DL_MDIO_WR_DATA              0x1104
#define RC_DL_MDIO_RD_DATA              0x1108
#define MISC_CTRL                       0x4008
#define MISC_MEM_WIN0_LO                0x400C
#define MISC_MEM_WIN0_HI                0x4010
#define MISC_RC_BAR1_CONFIG_LO          0x402C
#define MISC_RC_BAR2_CONFIG_LO          0x4034
#define MISC_RC_BAR2_CONFIG_HI          0x4038
#define MISC_RC_BAR3_CONFIG_LO          0x403C
#define MISC_PCIE_STATUS                0x4068
#define MISC_MEM_WIN0_BASE_LIMIT        0x4070
#define MISC_MEM_WIN0_BASE_HI           0x4080
#define MISC_MEM_WIN0_LIMIT_HI          0x4084
#define MISC_HARD_PCIE_HARD_DEBUG       0x4204
#define MSI_INTR2_CLR                   0x4508
#define MSI_INTR2_MASK_SET              0x4510
#define EXT_CFG_DATA                    0x8000
#define EXT_CFG_INDEX                   0x9000
#define RGR1_SW_INIT_1                  0x9210

#define SW_INIT_PERST                   0x00000001
#define SW_INIT_INIT                    0x00000002
#define HARD_DEBUG_SERDES_IDDQ          0x08000000
#define MISC_CTRL_SCB_ACCESS_EN         0x00001000
#define MISC_CTRL_CFG_READ_UR_MODE      0x00002000
#define MISC_CTRL_MAX_BURST_MASK        0x00300000
#define MISC_CTRL_SCB0_SIZE_SHIFT       27
#define MISC_CTRL_SCB0_SIZE_MASK        0xF8000000
#define BAR_SIZE_MASK                   0x0000001F
#define STATUS_PHYLINKUP                0x00000010
#define STATUS_DL_ACTIVE                0x00000020
#define STATUS_PORT_RC                  0x00000080
#define ENDIAN_MODE_BAR2_MASK           0x0000000C
#define LINK_CAPABILITY_ASPM            0x00000C00
#define CLASS_CODE_MASK                 0x00FFFFFF
#define CLASS_BRIDGE_PCI                0x00060400
#define CLASS_USB_XHCI                  0x000C0330

//
// The inbound window: 4 GB (its size field: log2 of the size less 15), and
// the outbound window, in megabytes of the processor, and its bus address.
//

#define INBOUND_SIZE                    17
#define OUTBOUND_BASE_MB                0x6000
#define OUTBOUND_LIMIT_MB               (OUTBOUND_BASE_MB + 63)
#define OUTBOUND_BUS                    0xF8000000

//
// The spread spectrum clock of the link, through the MDIO bus of its PHY.
//

#define MDIO_READ                       1
#define MDIO_WRITE                      0
#define MDIO_DONE                       0x80000000
#define MDIO_SET_ADDRESS                0x1F
#define MDIO_SSC_REGISTERS              0x1100
#define MDIO_SSC_CONTROL                0x02
#define MDIO_SSC_STATUS                 0x01
#define SSC_CONTROL_OVERRIDE            0xC000
#define SSC_STATUS_LOCKED               0x0C00

//
// The configuration space: the bridge's, and the device's; the VL805 keeps
// the version of its firmware at 0x50.
//

#define PCI_ID                          0x00
#define PCI_COMMAND                     0x04
#define PCI_CLASS                       0x08
#define PCI_BAR0                        0x10
#define PCI_BAR1                        0x14
#define PCI_BUS_NUMBERS                 0x18
#define PCI_IO_WINDOW                   0x1C
#define PCI_MEMORY_WINDOW               0x20
#define PCI_PREFETCH_WINDOW             0x24
#define PCI_INTERRUPT                   0x3C
#define COMMAND_MEMORY                  0x0002
#define COMMAND_MASTER                  0x0004
#define VL805_ID                        0x34831106
#define VL805_FIRMWARE_VERSION          0x50
#define VL805_ADDRESS                   (1 << 20)
#define VL805_LOAD_MS                   100

//
// The interrupt of the device, INTA of the link: the GIC interrupt ID it
// reaches, which the device's interrupt line holds for the HAL.
//

#define PCIE_INTA_VECTOR                175

static ULONG
PcieConfigRead (
    IN ULONG Bus,
    IN ULONG Offset
    )
{
    if (Bus == 0) {
        return PCIE(Offset);
    }

    PCIE(EXT_CFG_INDEX) = Bus << 20;
    return PCIE(EXT_CFG_DATA + Offset);
}

static VOID
PcieConfigWrite (
    IN ULONG Bus,
    IN ULONG Offset,
    IN ULONG Value
    )
{
    if (Bus == 0) {
        PCIE(Offset) = Value;
        return;
    }

    PCIE(EXT_CFG_INDEX) = Bus << 20;
    PCIE(EXT_CFG_DATA + Offset) = Value;
}

static BOOLEAN
PcieMdio (
    IN ULONG Command,
    IN ULONG Register,
    IN OUT PULONG Value
    )
{
    ULONG Start = FwGetMicroseconds();
    ULONG Data;

    PCIE(RC_DL_MDIO_ADDR) = (Command << 20) | Register;
    PCIE(RC_DL_MDIO_ADDR);
    if (Command == MDIO_WRITE) {
        PCIE(RC_DL_MDIO_WR_DATA) = MDIO_DONE | *Value;
    }

    for (;;) {
        Data = PCIE(Command == MDIO_WRITE ? RC_DL_MDIO_WR_DATA : RC_DL_MDIO_RD_DATA);
        if (Command == MDIO_WRITE ? (Data & MDIO_DONE) == 0 : (Data & MDIO_DONE) != 0) {
            break;
        }

        if (FwGetMicroseconds() - Start >= 100) {
            return FALSE;
        }
    }

    if (Command == MDIO_READ) {
        *Value = Data & ~MDIO_DONE;
    }

    return TRUE;
}

static BOOLEAN
PcieSetSpreadSpectrum (
    VOID
    )
{
    ULONG Value = MDIO_SSC_REGISTERS;

    if (!PcieMdio(MDIO_WRITE, MDIO_SET_ADDRESS, &Value) || !PcieMdio(MDIO_READ, MDIO_SSC_CONTROL, &Value)) {
        return FALSE;
    }

    Value |= SSC_CONTROL_OVERRIDE;
    if (!PcieMdio(MDIO_WRITE, MDIO_SSC_CONTROL, &Value)) {
        return FALSE;
    }

    FwStallExecution(1000);
    return PcieMdio(MDIO_READ, MDIO_SSC_STATUS, &Value) && (Value & SSC_STATUS_LOCKED) == SSC_STATUS_LOCKED;
}

BOOLEAN
PcieInitialize (
    OUT PULONG Registers
    )

/*++

Routine Description:

    This routine brings up the root complex and the link, and the USB 3 host
    behind it.

Arguments:

    Registers - Receives the address of the registers of the host.

Return Value:

    TRUE if an xHCI is on the link, else FALSE.

--*/

{
    BOOLEAN SpreadSpectrum;
    ULONG Status;
    ULONG Class;
    ULONG Version;
    ULONG Start;
    ULONG Id;

    if (!FwProbeRead(PCIE_BASE + RGR1_SW_INIT_1, &Status)) {
        FwPrint("PCIe: no root complex\r\n");
        return FALSE;
    }

    PCIE(RGR1_SW_INIT_1) |= SW_INIT_INIT | SW_INIT_PERST;
    FwStallExecution(100);
    PCIE(RGR1_SW_INIT_1) &= ~SW_INIT_INIT;
    PCIE(MISC_HARD_PCIE_HARD_DEBUG) &= ~HARD_DEBUG_SERDES_IDDQ;
    FwStallExecution(100);

    PCIE(MISC_CTRL) = (PCIE(MISC_CTRL) & ~MISC_CTRL_MAX_BURST_MASK) |
                      MISC_CTRL_SCB_ACCESS_EN | MISC_CTRL_CFG_READ_UR_MODE;
    PCIE(MISC_RC_BAR2_CONFIG_LO) = INBOUND_SIZE;
    PCIE(MISC_RC_BAR2_CONFIG_HI) = 0;
    PCIE(MISC_CTRL) = (PCIE(MISC_CTRL) & ~MISC_CTRL_SCB0_SIZE_MASK) | (INBOUND_SIZE << MISC_CTRL_SCB0_SIZE_SHIFT);
    PCIE(MISC_RC_BAR1_CONFIG_LO) &= ~BAR_SIZE_MASK;
    PCIE(MISC_RC_BAR3_CONFIG_LO) &= ~BAR_SIZE_MASK;
    PCIE(MSI_INTR2_MASK_SET) = 0xFFFFFFFF;
    PCIE(MSI_INTR2_CLR) = 0xFFFFFFFF;

    PCIE(RGR1_SW_INIT_1) &= ~SW_INIT_PERST;
    FwStallExecution(100 * 1000);
    Start = FwGetMicroseconds();
    do {
        Status = PCIE(MISC_PCIE_STATUS);
        if ((Status & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) == (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) {
            break;
        }

        FwStallExecution(5000);
    } while (FwGetMicroseconds() - Start < 100 * 1000);

    if ((Status & (STATUS_PHYLINKUP | STATUS_DL_ACTIVE)) != (STATUS_PHYLINKUP | STATUS_DL_ACTIVE) ||
        (Status & STATUS_PORT_RC) == 0) {
        FwPrint("PCIe: no link (%08lx)\r\n", Status);
        return FALSE;
    }

    PCIE(MISC_MEM_WIN0_LO) = OUTBOUND_BUS;
    PCIE(MISC_MEM_WIN0_HI) = 0;
    PCIE(MISC_MEM_WIN0_BASE_LIMIT) = ((OUTBOUND_LIMIT_MB & 0xFFF) << 20) | ((OUTBOUND_BASE_MB & 0xFFF) << 4);
    PCIE(MISC_MEM_WIN0_BASE_HI) = OUTBOUND_BASE_MB >> 12;
    PCIE(MISC_MEM_WIN0_LIMIT_HI) = OUTBOUND_LIMIT_MB >> 12;
    PCIE(RC_CFG_PRIV1_ID_VAL3) = (PCIE(RC_CFG_PRIV1_ID_VAL3) & ~CLASS_CODE_MASK) | CLASS_BRIDGE_PCI;
    SpreadSpectrum = PcieSetSpreadSpectrum();
    PCIE(RC_CFG_VENDOR_SPECIFIC_REG1) &= ~ENDIAN_MODE_BAR2_MASK;
    PCIE(RC_CFG_PRIV1_LINK_CAPABILITY) &= ~LINK_CAPABILITY_ASPM;

    //
    // The bridge: bus 1 behind it, the outbound window as its memory window
    // (no I/O or prefetchable window), and memory and bus master enabled.
    //

    PcieConfigWrite(0, PCI_BUS_NUMBERS, 0x00010100);
    PcieConfigWrite(0, PCI_IO_WINDOW, 0x000000F0);
    PcieConfigWrite(0, PCI_MEMORY_WINDOW, (OUTBOUND_BUS >> 16) | OUTBOUND_BUS);
    PcieConfigWrite(0, PCI_PREFETCH_WINDOW, 0x0000FFF0);
    PcieConfigWrite(0, PCI_COMMAND, PcieConfigRead(0, PCI_COMMAND) | COMMAND_MEMORY | COMMAND_MASTER);

    Id = PcieConfigRead(1, PCI_ID);
    Class = PcieConfigRead(1, PCI_CLASS) >> 8;
    if (Class != CLASS_USB_XHCI) {
        FwPrint("PCIe: %04lx:%04lx class %06lx, not a USB 3 host\r\n", Id & 0xFFFF, Id >> 16, Class);
        return FALSE;
    }

    PcieConfigWrite(1, PCI_BAR0, OUTBOUND_BUS);
    PcieConfigWrite(1, PCI_BAR1, 0);
    PcieConfigWrite(1, PCI_COMMAND, PcieConfigRead(1, PCI_COMMAND) | COMMAND_MEMORY);
    PcieConfigWrite(1, PCI_INTERRUPT, (PcieConfigRead(1, PCI_INTERRUPT) & ~0xFF) | PCIE_INTA_VECTOR);
    Version = PcieConfigRead(1, VL805_FIRMWARE_VERSION);
    if (Id == VL805_ID && Version == 0) {
        MailboxNotifyXhciReset(VL805_ADDRESS);
        Start = FwGetMicroseconds();
        do {
            FwStallExecution(1000);
            Version = PcieConfigRead(1, VL805_FIRMWARE_VERSION);
        } while (Version == 0 && FwGetMicroseconds() - Start < VL805_LOAD_MS * 1000);
    }

    PcieConfigWrite(1, PCI_COMMAND, PcieConfigRead(1, PCI_COMMAND) | COMMAND_MASTER);
    FwPrint("USB 3: %04lx:%04lx on PCIe%s, firmware %08lx\r\n", Id & 0xFFFF, Id >> 16,
            SpreadSpectrum ? "" : " (no SSC)", Version);
    FwAddPciBus();
    *Registers = OUTBOUND_BUS;
    return TRUE;
}
