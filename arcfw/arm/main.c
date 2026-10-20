/*++

Module Name:

    main.c

Abstract:

    This module is the entry of the ARC firmware of the Raspberry Pi 2 from
    the reset stub (the FW\MIPS\FWENTRY.C and the selftest): it sets up the
    exception vectors, turns on the MMU, initializes the system parameter
    block, the serial port and the video, from when on it takes the
    asynchronous aborts (the bus errors of writes), and runs the boot manager.

--*/

#include "fwp.h"
#include "fwstring.h"

VOID
FwSystemBlockInitialize (
    VOID
    );

VOID
FwExceptionReport (
    IN ULONG Vector,
    IN ULONG ReturnAddress,
    IN ULONG Psr
    )

/*++

Routine Description:

    This routine displays an exception the firmware does not handle, taken
    through its vectors (FwExceptionVectors), and stops the processor.

Arguments:

    Vector - The number of the vector.

    ReturnAddress - The link register of the exception.

    Psr - The program status of the code that took it.

--*/

{
    static PCHAR Names[] = {"reset", "undefined instruction", "supervisor call", "prefetch abort",
                            "data abort", "reserved", "interrupt", "fast interrupt"};
    static BOOLEAN Reported;

    if (!Reported) {
        Reported = TRUE;
        FwPrint("\r\nFirmware exception: %s, lr %08lx, psr %08lx, dfsr %08lx at %08lx, ifsr %08lx at %08lx\r\n",
                Names[Vector], ReturnAddress, Psr,
                _MoveFromCoprocessor(15, 0, 5, 0, 0), _MoveFromCoprocessor(15, 0, 6, 0, 0),
                _MoveFromCoprocessor(15, 0, 5, 0, 1), _MoveFromCoprocessor(15, 0, 6, 0, 2));
    }

    for (;;) {
        __wfi();
    }
}

VOID
FwEnableAsynchronousAborts (
    VOID
    );

VOID
cmain (
    VOID
    )
{
    _MoveToCoprocessor((ULONG)FwExceptionVectors & ~1, 15, 0, 12, 0, 0);
    __isb(_ARM_BARRIER_SY);
    FwSocInitialize();
    FwMmuInitialize();
    FwSystemBlockInitialize();
    SerialBootSetup(SERIAL_PORT0_BASE);
    SerialOutput = TRUE;
    DisplayOutput = DisplayBootInitialize() == ESUCCESS;
    FwPrint("\r\n%s\r\n", FW_TITLE_MSG);
    FwEnableAsynchronousAborts();
    FwBootSystem();
}
