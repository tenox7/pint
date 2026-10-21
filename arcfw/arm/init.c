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
FwRestartBlockInitialize (
    VOID
    );

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
    FwRestartBlockInitialize();
    FwVendorInitialize();
}

VOID
FwRestartBlockInitialize (
    VOID
    )

/*++

Routine Description:

    This routine builds the restart blocks after the system parameter block,
    one per processor, each in its own cache line: the first is the boot
    master's, processor 0, which is ready, running and started (the Jazz
    selftest); a secondary processor sets ready in its block when it waits in
    the firmware to be started.

--*/

{
    PRESTART_BLOCK RestartBlock;
    ULONG Number;
    ULONG Size = (sizeof(RESTART_BLOCK) + 63) & ~63;
    BOOT_STATUS BootStatus;

    RestartBlock = (PRESTART_BLOCK)(((ULONG)SYSTEM_BLOCK + sizeof(SYSTEM_PARAMETER_BLOCK) + 63) & ~63);
    SYSTEM_BLOCK->RestartBlock = RestartBlock;
    for (Number = 0; Number < ARC_PROCESSORS; Number += 1) {
        RtlZeroMemory(RestartBlock, sizeof(RESTART_BLOCK));
        RestartBlock->Signature = ARC_RESTART_BLOCK_SIGNATURE;
        RestartBlock->Length = sizeof(RESTART_BLOCK);
        RestartBlock->Version = ARC_VERSION;
        RestartBlock->Revision = ARC_REVISION;
        RestartBlock->BootMasterId = 0;
        RestartBlock->ProcessorId = Number;
        RestartBlock->SaveAreaLength = sizeof(ARM_RESTART_STATE);
        if (Number == 0) {
            *(PULONG)&BootStatus = 0;
            BootStatus.ProcessorReady = 1;
            BootStatus.ProcessorRunning = 1;
            BootStatus.ProcessorStart = 1;
            BootStatus.BootStarted = 1;
            RestartBlock->BootStatus = BootStatus;
        }

        if (Number + 1 < ARC_PROCESSORS) {
            RestartBlock->NextRestartBlock = (PRESTART_BLOCK)((PUCHAR)RestartBlock + Size);
        }

        RestartBlock = (PRESTART_BLOCK)((PUCHAR)RestartBlock + Size);
    }
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
