/*++

Module Name:

    machdep.h

Abstract:

    The platform definitions the PCI bus support (pcisup.c) takes from the
    HAL, those of the PCI Express root complex of the BCM2711 (pcibus.c): bus
    0 holds the root complex alone, the bus has no I/O space, and its memory
    is the HAL's window from HAL_PCI_MEMORY_BUS.

--*/

#ifndef _MACHDEP_
#define _MACHDEP_

#define PCI_MAX_LOCAL_DEVICE            0
#define PCI_MAX_IO_ADDRESS              0
#define PCI_MIN_DENSE_MEMORY_ADDRESS    HAL_PCI_MEMORY_BUS
#define PCI_MAX_DENSE_MEMORY_ADDRESS    (HAL_PCI_MEMORY_BUS + HAL_PCI_MEMORY_LENGTH - 1)
#define PCI_MAX_SPARSE_MEMORY_ADDRESS   PCI_MAX_DENSE_MEMORY_ADDRESS
#define PCI_MAX_INTERRUPT_VECTOR        HAL_MAXIMUM_GIC_VECTOR

UCHAR
READ_CONFIG_UCHAR (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    );

USHORT
READ_CONFIG_USHORT (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    );

ULONG
READ_CONFIG_ULONG (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationType
    );

VOID
WRITE_CONFIG_UCHAR (
    IN PVOID ConfigurationAddress,
    IN UCHAR ConfigurationData,
    IN ULONG ConfigurationType
    );

VOID
WRITE_CONFIG_USHORT (
    IN PVOID ConfigurationAddress,
    IN USHORT ConfigurationData,
    IN ULONG ConfigurationType
    );

VOID
WRITE_CONFIG_ULONG (
    IN PVOID ConfigurationAddress,
    IN ULONG ConfigurationData,
    IN ULONG ConfigurationType
    );

#endif // _MACHDEP_
