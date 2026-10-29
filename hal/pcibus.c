/*++

Module Name:

    pcibus.c

Abstract:

    This module is the platform part of the PCI bus support of the HAL
    (pcisup.c): the PCI Express root complex of the BCM2711. The firmware
    brings up the link, numbers the buses, assigns the memory and the
    interrupt lines of the devices, and describes each bus as a
    MultiFunctionAdapter "PCI" of the configuration tree, from which the HAL
    makes the handler of the bus (HalpInitializePCIBus).

    Bus 0 holds the root complex alone, a PCI to PCI bridge whose
    configuration space is its own registers (type 0 cycles). The buses
    behind it are reached through its configuration index and data window
    (type 1 cycles); the link of its root port carries one device, number 0.

Environment:

    Kernel mode only.

--*/

#include "halp.h"
#include "pci.h"
#include "pcip.h"
#include "machdep.h"
#include <string.h>

#define PCIE_CONFIG_DATA        0x8000
#define PCIE_CONFIG_INDEX       0x9000
#define PCIE_LINK_BUS           1

BOOLEAN HalpPCIPresent;

static PUCHAR HalpPCIRegisters;

PCI_CONFIGURATION_TYPES
HalpPCIConfigCycleType (
    IN PBUS_HANDLER BusHandler
    )
{
    return BusHandler->BusNumber == 0 ? PciConfigType0 : PciConfigType1;
}

VOID
HalpPCIConfigAddr (
    IN PBUS_HANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot,
    PPCI_CFG_CYCLE_BITS pPciAddr
    )

/*++

Routine Description:

    This function forms the configuration address of a slot: the index of the
    root complex (the bus in bits 20 to 27, the device in bits 15 to 19 and
    the function in bits 12 to 14) with the cycle type in bits 0 and 1, to
    which the accesses add the offset of the register.

--*/

{
    pPciAddr->u.AsULONG = (BusHandler->BusNumber << 20) | (Slot.u.bits.DeviceNumber << 15) |
                          (Slot.u.bits.FunctionNumber << 12) |
                          (HalpPCIConfigCycleType(BusHandler) & 3);
}

static PUCHAR
HalpPCIConfigRegister (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    )

/*++

Routine Description:

    This function gives the register of a configuration access: on bus 0 that
    of the root complex itself, on the others the data window, once the index
    of the root complex selects the slot. A device other than 0 on the link
    has none.

--*/

{
    ULONG Address = (ULONG)ConfigurationAddress;

    if (ConfigurationType == PciConfigType0) {
        return HalpPCIRegisters + (Address & 0xFFF);
    }

    if (((Address >> 20) & 0xFF) == PCIE_LINK_BUS && ((Address >> 15) & 0x1F) != 0) {
        return NULL;
    }

    WRITE_REGISTER_ULONG((PULONG)(HalpPCIRegisters + PCIE_CONFIG_INDEX), Address & ~0xFFF);
    return HalpPCIRegisters + PCIE_CONFIG_DATA + (Address & 0xFFF);
}

UCHAR
READ_CONFIG_UCHAR (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    )
{
    PUCHAR Register = HalpPCIConfigRegister(ConfigurationAddress, ConfigurationType);

    return Register == NULL ? 0xFF : READ_REGISTER_UCHAR(Register);
}

USHORT
READ_CONFIG_USHORT (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    )
{
    PUCHAR Register = HalpPCIConfigRegister(ConfigurationAddress, ConfigurationType);

    return Register == NULL ? 0xFFFF : READ_REGISTER_USHORT((PUSHORT)Register);
}

ULONG
READ_CONFIG_ULONG (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    )
{
    PUCHAR Register = HalpPCIConfigRegister(ConfigurationAddress, ConfigurationType);

    return Register == NULL ? 0xFFFFFFFF : READ_REGISTER_ULONG((PULONG)Register);
}

VOID
WRITE_CONFIG_UCHAR (
    IN PVOID ConfigurationAddress,
    IN UCHAR ConfigurationData,
    IN ULONG ConfigurationType
    )
{
    PUCHAR Register = HalpPCIConfigRegister(ConfigurationAddress, ConfigurationType);

    if (Register != NULL) {
        WRITE_REGISTER_UCHAR(Register, ConfigurationData);
    }
}

VOID
WRITE_CONFIG_USHORT (
    IN PVOID ConfigurationAddress,
    IN USHORT ConfigurationData,
    IN ULONG ConfigurationType
    )
{
    PUCHAR Register = HalpPCIConfigRegister(ConfigurationAddress, ConfigurationType);

    if (Register != NULL) {
        WRITE_REGISTER_USHORT((PUSHORT)Register, ConfigurationData);
    }
}

VOID
WRITE_CONFIG_ULONG (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationData,
    IN ULONG ConfigurationType
    )
{
    PUCHAR Register = HalpPCIConfigRegister(ConfigurationAddress, ConfigurationType);

    if (Register != NULL) {
        WRITE_REGISTER_ULONG((PULONG)Register, ConfigurationData);
    }
}

VOID
HalpInitializePCIExpress (
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

/*++

Routine Description:

    This function brings up the PCI buses if the firmware describes them, as
    adapters of the system: it maps the registers of the root complex, then
    the PCI bus state is initialized (HalpInitializePCIBus), which registers
    a bus handler for each bus. The adapters of the system are siblings, from
    which KeFindConfigurationNextEntry does not resume, so they are walked.

Arguments:

    LoaderBlock - Supplies a pointer to the loader parameter block.

Return Value:

    None.

--*/

{
    PCONFIGURATION_COMPONENT_DATA Adapter;
    PHYSICAL_ADDRESS Physical;

    if (!HalpBcm2711) {
        return;
    }

    for (Adapter = LoaderBlock->ConfigurationRoot->Child; Adapter != NULL; Adapter = Adapter->Sibling) {
        if (Adapter->ComponentEntry.Class == AdapterClass &&
            Adapter->ComponentEntry.Type == MultiFunctionAdapter &&
            Adapter->ComponentEntry.Identifier != NULL &&
            strcmp(Adapter->ComponentEntry.Identifier, "PCI") == 0) {
            break;
        }
    }

    if (Adapter == NULL) {
        return;
    }

    Physical.HighPart = 0;
    Physical.LowPart = HAL_PCIE_PHYSICAL;
    HalpPCIRegisters = MmMapIoSpace(Physical, HAL_PCIE_LENGTH, FALSE);
    if (HalpPCIRegisters == NULL) {
        return;
    }

    HalpPCIPresent = TRUE;
    HalpInitializePCIBus(LoaderBlock);
}
