/*++

Module Name:

    jxusage.c

Abstract:

    This module reports the resources the Raspberry Pi 2 HAL uses to the I/O
    system, so they are listed in the registry and no driver claims them: the
    interval and profile clock interrupts and the device registers of the
    system timer, interrupt controller, serial port and power manager.

Environment:

    Kernel mode only.

--*/

#include "halp.h"

#define HALP_INTERRUPTS 2
#define HALP_MEMORY_RANGES 4

typedef struct _HALP_RESOURCE_LIST {
    CM_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR More[HALP_INTERRUPTS + HALP_MEMORY_RANGES - 1];
} HALP_RESOURCE_LIST;

static struct {
    ULONG Irq;
    KIRQL Irql;
} HalpInterrupts[HALP_INTERRUPTS] = {
    { HAL_CLOCK_IRQ, CLOCK2_LEVEL },
    { HAL_PROFILE_IRQ, PROFILE_LEVEL },
};

static struct {
    ULONG Base;
    ULONG Length;
} HalpMemoryRanges[HALP_MEMORY_RANGES] = {
    { 0x3F003000, 0x20 },
    { 0x3F00B200, 0x28 },
    { 0x3F100000, 0x28 },
    { 0x3F201000, 0x90 },
};

static VOID
HalpBuildResourceList (
    OUT HALP_RESOURCE_LIST *Resources,
    IN BOOLEAN Translated
    )
{
    PCM_FULL_RESOURCE_DESCRIPTOR Full = &Resources->List.List[0];
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = Full->PartialResourceList.PartialDescriptors;
    ULONG Index;

    RtlZeroMemory(Resources, sizeof(HALP_RESOURCE_LIST));
    Resources->List.Count = 1;
    Full->InterfaceType = Internal;
    Full->BusNumber = 0;
    Full->PartialResourceList.Version = 1;
    Full->PartialResourceList.Revision = 1;
    Full->PartialResourceList.Count = HALP_INTERRUPTS + HALP_MEMORY_RANGES;

    for (Index = 0; Index < HALP_INTERRUPTS; Index += 1, Partial += 1) {
        Partial->Type = CmResourceTypeInterrupt;
        Partial->ShareDisposition = CmResourceShareDriverExclusive;
        Partial->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        Partial->u.Interrupt.Affinity = 1;
        if (Translated) {
            Partial->u.Interrupt.Level = HalpInterrupts[Index].Irql;
            Partial->u.Interrupt.Vector = DEVICE_VECTORS + HalpInterrupts[Index].Irq;
        } else {
            Partial->u.Interrupt.Level = HalpInterrupts[Index].Irql;
            Partial->u.Interrupt.Vector = HalpInterrupts[Index].Irq;
        }
    }

    for (Index = 0; Index < HALP_MEMORY_RANGES; Index += 1, Partial += 1) {
        Partial->Type = CmResourceTypeMemory;
        Partial->ShareDisposition = CmResourceShareDriverExclusive;
        Partial->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Partial->u.Memory.Start.LowPart = HalpMemoryRanges[Index].Base;
        Partial->u.Memory.Length = HalpMemoryRanges[Index].Length;
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

    RtlInitUnicodeString(&HalName, L"Raspberry Pi 2");
    IoReportHalResourceUsage(&HalName,
                             &Raw->List,
                             &Translated->List,
                             sizeof(HALP_RESOURCE_LIST));

    ExFreePool(Raw);
}
