/*++

Module Name:

    init.c

Abstract:

    This module initializes the ARC firmware of the Raspberry Pi 2: the system
    parameter block at its fixed address, the firmware services and the I/O
    devices, and opens standard in and out (the FwInitialize of the Jazz
    selftest and FW\ALPHA\JNFSINIT.C).

--*/

#include "fwp.h"
#include "fwstring.h"

PVOID GlobalFirmwareVectors[MaximumRoutine];
ULONG BlDcacheFillSize;

VOID
FwSystemBlockInitialize (
    VOID
    )

/*++

Routine Description:

    This routine initializes the system parameter block, and the vendor
    routines.

--*/

{
    ULONG Ctr;

    Ctr = _MoveFromCoprocessor(15, 0, 0, 0, 1);
    BlDcacheFillSize = 4 << ((Ctr >> 16) & 0xf);

    RtlZeroMemory(SYSTEM_BLOCK, sizeof(SYSTEM_PARAMETER_BLOCK));
    RtlZeroMemory(GlobalFirmwareVectors, sizeof(GlobalFirmwareVectors));
    SYSTEM_BLOCK->Signature = 0x53435241;
    SYSTEM_BLOCK->Length = sizeof(SYSTEM_PARAMETER_BLOCK);
    SYSTEM_BLOCK->Version = ARC_VERSION;
    SYSTEM_BLOCK->Revision = ARC_REVISION;
    SYSTEM_BLOCK->FirmwareVectorLength = sizeof(GlobalFirmwareVectors);
    SYSTEM_BLOCK->FirmwareVector = GlobalFirmwareVectors;
    FwVendorInitialize();
}

static VOID
FwOpenConsole (
    VOID
    )

/*++

Routine Description:

     This opens the console input and output devices the environment names.

--*/

{
    ULONG Fid;
    PCHAR ConsoleName;

    if ((ConsoleName = FwGetEnvironmentVariable("CONSOLEIN")) == NULL) {
        ConsoleName = "multi(0)serial(0)";
    }

    if (FwOpen(ConsoleName, ArcOpenReadOnly, &Fid) != ESUCCESS) {
        FwPrint(FW_CONSOLE_IN_ERROR_MSG);
    } else if (Fid != ARC_CONSOLE_INPUT) {
        FwPrint(FW_CONSOLE_IN_ERROR2_MSG);
    }

    if ((ConsoleName = FwGetEnvironmentVariable("CONSOLEOUT")) == NULL) {
        ConsoleName = "multi(0)serial(0)";
    }

    if (FwOpen(ConsoleName, ArcOpenWriteOnly, &Fid) != ESUCCESS) {
        FwPrint(FW_CONSOLE_OUT_ERROR_MSG);
    } else if (Fid != ARC_CONSOLE_OUTPUT) {
        FwPrint(FW_CONSOLE_OUT_ERROR2_MSG);
    }
}

VOID
FwInitialize (
    VOID
    )

/*++

Routine Description:

    This routine initializes the firmware services and the I/O devices, and
    opens standard in and out.

--*/

{
    FwInitializeMemory();
    FwTimeInitialize();
    FwSystemIdInitialize();
    FwLoadInitialize();
    FwTerminationInitialize();
    FwConfigurationInitialize();
    FwIoInitialize1();
    FwIoInitialize2();
    FwEnvironmentInitialize();
    FwOpenConsole();
    FwConsoleInitialized = TRUE;
}
