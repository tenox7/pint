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

volatile ULONG FwProcessorsReleased;

VOID
FwStartProcessors (
    VOID
    )

/*++

Routine Description:

    This routine releases the secondary processors into the firmware, once the
    translation table and the restart blocks are built: those QEMU started at
    the entry wait for the release, those the Raspberry Pi firmware holds
    enter at the address their mailbox 3 receives.

--*/

{
    ULONG Number;

    FwProcessorsReleased = TRUE;
    __dsb(_ARM_BARRIER_SY);
    for (Number = 1; Number < ARC_PROCESSORS; Number += 1) {
        LOCAL_MAILBOX3_SET(Number) = (ULONG)FwStart & ~3;
    }

    __dsb(_ARM_BARRIER_SY);
    __sev();
}

VOID
FwSecondaryStartup (
    IN ULONG Number
    )

/*++

Routine Description:

    This routine is entered from the reset stub on a secondary processor, on
    its own stack with the MMU off: it takes the firmware's vectors, waits for
    the boot processor to release it, turns on the MMU and waits for its start
    in the firmware (FwProcessorSystemBoot).

Arguments:

    Number - Supplies the number of the processor.

--*/

{
    _MoveToCoprocessor((ULONG)FwExceptionVectors & ~1, 15, 0, 12, 0, 0);
    __isb(_ARM_BARRIER_SY);
    while (FwProcessorsReleased == FALSE) {
        __wfe();
    }

    LOCAL_MAILBOX3_CLEAR(Number) = 0xFFFFFFFF;

    FwMmuInitializeProcessor();
    FwProcessorSystemBoot(Number);
}

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
    FwStartProcessors();
    SerialBootSetup(SERIAL_PORT0_BASE);
    SerialOutput = TRUE;
    DisplayOutput = DisplayBootInitialize() == ESUCCESS;
    FwPrint("\r\n%s\r\n", FW_TITLE_MSG);
    FwEnableAsynchronousAborts();
    FwBootSystem();
}
