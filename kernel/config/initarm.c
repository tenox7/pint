/*++

Module Name:

    initarm.c

Abstract:

    This module builds the ARM specific entries of the hardware tree of the
    registry. The ARC firmware keeps the dates and version strings of the
    system and video firmware in the configuration data of the system
    component (ARM_SYSTEM_FIRMWARE_DATA); they go into the system key under the
    names a PC's BIOS strings have (CONFIG\I386\INIT386.C).

Environment:

    Kernel mode.

--*/

#include "cmp.h"

#define TITLE_INDEX_VALUE 0

//
// Enter a date (REG_SZ) or version strings (REG_MULTI_SZ, Strings holding zero
// terminated strings, the last followed by an empty one) of the firmware.
//

VOID
CmpSetFirmwareValue (
    IN HANDLE KeyHandle,
    IN PWSTR Name,
    IN ULONG Type,
    IN PCHAR Strings,
    IN ULONG Size
    )
{
    UNICODE_STRING ValueName;
    UNICODE_STRING ValueData;
    ANSI_STRING AnsiString;
    PWCHAR Buffer;
    ULONG Length = 0;
    PCHAR String = Strings;

    if (Strings[0] == '\0') {
        return;
    }

    Buffer = ExAllocatePool(PagedPool, (Size + 1) * sizeof(WCHAR));
    if (Buffer == NULL) {
        return;
    }

    while (String < Strings + Size && *String != '\0') {
        RtlInitAnsiString(&AnsiString, String);
        if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(&ValueData, &AnsiString, TRUE))) {
            break;
        }
        RtlMoveMemory((PCHAR)Buffer + Length, ValueData.Buffer, ValueData.Length);
        Length += ValueData.Length + sizeof(UNICODE_NULL);
        *(PWCHAR)((PCHAR)Buffer + Length - sizeof(UNICODE_NULL)) = UNICODE_NULL;
        RtlFreeUnicodeString(&ValueData);
        String += AnsiString.Length + 1;
        if (Type == REG_SZ) {
            break;
        }
    }

    if (Type == REG_MULTI_SZ) {
        *(PWCHAR)((PCHAR)Buffer + Length) = UNICODE_NULL;
        Length += sizeof(UNICODE_NULL);
    }

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, TITLE_INDEX_VALUE, Type, Buffer, Length);
    ExFreePool(Buffer);
}

NTSTATUS
CmpInitializeMachineDependentConfiguration (
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

/*++

Routine Description:

    This routine creates the ARM specific entries in the registry: the system
    and video firmware dates and versions of the system component.

Arguments:

    LoaderBlock - supplies a pointer to the LoaderBlock passed in from the
                  OS Loader.

Returns:

    NTSTATUS code for sucess or reason of failure.

--*/

{
    PCONFIGURATION_COMPONENT_DATA SystemEntry = LoaderBlock->ConfigurationRoot;
    PCM_PARTIAL_RESOURCE_LIST List;
    PARM_SYSTEM_FIRMWARE_DATA Firmware;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if (SystemEntry == NULL || SystemEntry->ConfigurationData == NULL ||
        SystemEntry->ComponentEntry.ConfigurationDataLength <
            sizeof(CM_PARTIAL_RESOURCE_LIST) + sizeof(ARM_SYSTEM_FIRMWARE_DATA)) {
        return STATUS_SUCCESS;
    }

    List = (PCM_PARTIAL_RESOURCE_LIST)SystemEntry->ConfigurationData;
    if (List->Count != 1 ||
        List->PartialDescriptors[0].Type != CmResourceTypeDeviceSpecific ||
        List->PartialDescriptors[0].u.DeviceSpecificData.DataSize < sizeof(ARM_SYSTEM_FIRMWARE_DATA)) {
        return STATUS_SUCCESS;
    }
    Firmware = (PARM_SYSTEM_FIRMWARE_DATA)(List + 1);

    InitializeObjectAttributes(&ObjectAttributes,
                               &CmRegistryMachineHardwareDescriptionSystemName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    Status = NtOpenKey(&KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    CmpSetFirmwareValue(KeyHandle, L"SystemBiosDate", REG_SZ,
                        Firmware->SystemFirmwareDate, sizeof(Firmware->SystemFirmwareDate));
    CmpSetFirmwareValue(KeyHandle, L"SystemBiosVersion", REG_MULTI_SZ,
                        Firmware->SystemFirmwareVersion, sizeof(Firmware->SystemFirmwareVersion));
    CmpSetFirmwareValue(KeyHandle, L"VideoBiosDate", REG_SZ,
                        Firmware->VideoFirmwareDate, sizeof(Firmware->VideoFirmwareDate));
    CmpSetFirmwareValue(KeyHandle, L"VideoBiosVersion", REG_MULTI_SZ,
                        Firmware->VideoFirmwareVersion, sizeof(Firmware->VideoFirmwareVersion));

    NtClose(KeyHandle);
    return STATUS_SUCCESS;
}
