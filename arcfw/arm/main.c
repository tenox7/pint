/*++

Module Name:

    main.c

Abstract:

    This module is the entry of the ARC firmware of the Raspberry Pi 2 from
    the reset stub (the FW\MIPS\FWENTRY.C and the selftest): it turns on the
    MMU, initializes the system parameter block, the serial port and the
    video, and runs the boot manager.

--*/

#include "fwp.h"

VOID
FwSystemBlockInitialize (
    VOID
    );

VOID
cmain (
    VOID
    )
{
    FwMmuInitialize();
    FwSystemBlockInitialize();
    SerialBootSetup(SERIAL_PORT0_BASE);
    SerialOutput = TRUE;
    DisplayOutput = DisplayBootInitialize() == ESUCCESS;
    FwPrint("\r\nARC Firmware for the Raspberry Pi 2\r\n");
    FwBootSystem();
}
