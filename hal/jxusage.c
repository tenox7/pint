/*++

Module Name:

    jxusage.c

Abstract:

    This module reports the resources the Raspberry Pi HAL uses to the I/O
    system, so they are listed in the registry and no driver claims them: the
    interval and profile clock interrupts and the device registers of the
    system timer, interrupt controller, serial port and power manager, on the
    BCM2836 and BCM2837 or on the BCM2711 (whose interrupt controller is the
    GIC-400, and whose interval clock is the generic virtual timer), and
    those of the PCI Express root complex of a BCM2711 whose PCI bus is up.

Environment:

    Kernel mode only.

--*/

#include "halp.h"

#define HALP_INTERRUPTS 2
#define HALP_MAXIMUM_MEMORY_RANGES 6

typedef struct _HALP_RESOURCE_LIST {
    CM_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR More[HALP_INTERRUPTS + HALP_MAXIMUM_MEMORY_RANGES - 1];
} HALP_RESOURCE_LIST;

typedef struct _HALP_INTERRUPT {
    ULONG Irq;
    KIRQL Irql;
} HALP_INTERRUPT;

typedef struct _HALP_MEMORY_RANGE {
    ULONG Base;
    ULONG Length;
} HALP_MEMORY_RANGE;

static HALP_INTERRUPT HalpBcm2836Interrupts[HALP_INTERRUPTS] = {
    { HAL_CLOCK_CHANNEL, CLOCK2_LEVEL },
    { HAL_PROFILE_CHANNEL, PROFILE_LEVEL },
};

static HALP_MEMORY_RANGE HalpBcm2836MemoryRanges[] = {
    { 0x3F003000, 0x20 },
    { 0x3F00B200, 0x28 },
    { 0x3F100000, 0x28 },
    { 0x3F201000, 0x90 },
};

static HALP_INTERRUPT HalpBcm2711Interrupts[HALP_INTERRUPTS] = {
    { HAL_VIRTUAL_TIMER_ID, CLOCK2_LEVEL },
    { HAL_VIDEOCORE_ID(HAL_PROFILE_CHANNEL), PROFILE_LEVEL },
};

static HALP_MEMORY_RANGE HalpBcm2711MemoryRanges[] = {
    { 0xFE003000, 0x20 },
    { 0xFF841000, 0x1000 },
    { 0xFF842000, 0x2000 },
    { 0xFE100000, 0x28 },
    { 0xFE201000, 0x90 },
    { HAL_PCIE_PHYSICAL, HAL_PCIE_LENGTH },
};

static VOID
HalpBuildResourceList (
    OUT HALP_RESOURCE_LIST *Resources,
    IN BOOLEAN Translated
    )
{
    PCM_FULL_RESOURCE_DESCRIPTOR Full = &Resources->List.List[0];
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = Full->PartialResourceList.PartialDescriptors;
    HALP_INTERRUPT *Interrupts = HalpBcm2836Interrupts;
    HALP_MEMORY_RANGE *MemoryRanges = HalpBcm2836MemoryRanges;
    ULONG MemoryRangeCount = sizeof(HalpBcm2836MemoryRanges) / sizeof(HALP_MEMORY_RANGE);
    ULONG Index;

    if (HalpBcm2711) {
        Interrupts = HalpBcm2711Interrupts;
        MemoryRanges = HalpBcm2711MemoryRanges;
        MemoryRangeCount = sizeof(HalpBcm2711MemoryRanges) / sizeof(HALP_MEMORY_RANGE) - (HalpPCIPresent ? 0 : 1);
    }

    RtlZeroMemory(Resources, sizeof(HALP_RESOURCE_LIST));
    Resources->List.Count = 1;
    Full->InterfaceType = Internal;
    Full->BusNumber = 0;
    Full->PartialResourceList.Version = 1;
    Full->PartialResourceList.Revision = 1;
    Full->PartialResourceList.Count = HALP_INTERRUPTS + MemoryRangeCount;

    for (Index = 0; Index < HALP_INTERRUPTS; Index += 1, Partial += 1) {
        Partial->Type = CmResourceTypeInterrupt;
        Partial->ShareDisposition = CmResourceShareDriverExclusive;
        Partial->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        Partial->u.Interrupt.Affinity = 1;
        if (Translated) {
            Partial->u.Interrupt.Level = Interrupts[Index].Irql;
            Partial->u.Interrupt.Vector = DEVICE_VECTORS + Interrupts[Index].Irq;
        } else {
            Partial->u.Interrupt.Level = Interrupts[Index].Irql;
            Partial->u.Interrupt.Vector = Interrupts[Index].Irq;
        }
    }

    for (Index = 0; Index < MemoryRangeCount; Index += 1, Partial += 1) {
        Partial->Type = CmResourceTypeMemory;
        Partial->ShareDisposition = CmResourceShareDriverExclusive;
        Partial->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Partial->u.Memory.Start.LowPart = MemoryRanges[Index].Base;
        Partial->u.Memory.Length = MemoryRanges[Index].Length;
    }
}

VOID
HalReportResourceUsage (
    VOID
    )

/*++

Routine Description:

    This routine reports the resources the HAL uses to the I/O system.

Arguments:

    None.

Return Value:

    None.

--*/

{
    UNICODE_STRING HalName;
    HALP_RESOURCE_LIST *Raw;
    HALP_RESOURCE_LIST *Translated;

    Raw = ExAllocatePool(PagedPool, 2 * sizeof(HALP_RESOURCE_LIST));
    if (Raw == NULL) {
        return;
    }

    Translated = Raw + 1;
    HalpBuildResourceList(Raw, FALSE);
    HalpBuildResourceList(Translated, TRUE);

    RtlInitUnicodeString(&HalName, L"Raspberry Pi");
    IoReportHalResourceUsage(&HalName,
                             &Raw->List,
                             &Translated->List,
                             sizeof(HALP_RESOURCE_LIST));

    ExFreePool(Raw);
}
