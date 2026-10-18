/*++

Module Name:

    mailbox.c

Abstract:

    This module implements the VideoCore mailbox property interface of the
    BCM2835 (channel 8), through which the ARM asks the VideoCore for the
    frame buffer, peripheral power, clock rates, its memory, the board
    revision and serial number and its firmware revision. A message is a 16
    byte aligned tag buffer whose address the ARM sends through the uncached
    VideoCore alias of memory; the VideoCore answers in place.

--*/

#include "fwp.h"

#define MAILBOX_BASE            (PERIPHERAL_BASE + 0xB880)
#define MAILBOX_READ            (*(volatile ULONG *)(MAILBOX_BASE + 0x00))
#define MAILBOX_STATUS          (*(volatile ULONG *)(MAILBOX_BASE + 0x18))
#define MAILBOX_WRITE           (*(volatile ULONG *)(MAILBOX_BASE + 0x20))

#define MAILBOX_FULL            0x80000000
#define MAILBOX_EMPTY           0x40000000
#define MAILBOX_PROPERTY        8
#define MAILBOX_CHANNEL_MASK    0xF
#define MAILBOX_SUCCESS         0x80000000

#define BUS_UNCACHED_ALIAS      0xC0000000

#define TAG_GET_FIRMWARE_REVISION 0x00000001
#define TAG_GET_BOARD_REVISION  0x00010002
#define TAG_GET_BOARD_MAC       0x00010003
#define TAG_GET_BOARD_SERIAL    0x00010004
#define TAG_GET_ARM_MEMORY      0x00010005
#define TAG_SET_POWER_STATE     0x00028001
#define TAG_GET_CLOCK_RATE      0x00030002
#define TAG_GET_MAX_CLOCK_RATE  0x00030004

#define POWER_STATE_ON          1
#define POWER_STATE_WAIT        2

ULONG
MailboxPhysicalToBus (
    IN ULONG PhysicalAddress
    )
{
    return BUS_UNCACHED_ALIAS | PhysicalAddress;
}

ULONG
MailboxBusToPhysical (
    IN ULONG BusAddress
    )
{
    return BusAddress & ~BUS_UNCACHED_ALIAS;
}

ULONG
MailboxPropertyCall (
    IN OUT volatile ULONG *Buffer
    )

/*++

Routine Description:

    This routine sends a property message to the VideoCore and waits for its
    answer. The message is in memory before the VideoCore is given it, and the
    answer is read after it is back: the processor may reorder the buffer, which
    is ordinary memory to it, with the mailbox registers.

Arguments:

    Buffer - Supplies the 16 byte aligned message.

Return Value:

    0 if the VideoCore processed the message successfully, 1 otherwise.

--*/

{
    ULONG Message = (MailboxPhysicalToBus((ULONG)Buffer & ~KSEG0_BASE) &
                     ~MAILBOX_CHANNEL_MASK) | MAILBOX_PROPERTY;

    while ((MAILBOX_STATUS & MAILBOX_FULL) != 0) {
    }

    __dsb(_ARM_BARRIER_SY);
    MAILBOX_WRITE = Message;
    do {
        while ((MAILBOX_STATUS & MAILBOX_EMPTY) != 0) {
        }
    } while ((MAILBOX_READ & MAILBOX_CHANNEL_MASK) != MAILBOX_PROPERTY);

    __dsb(_ARM_BARRIER_SY);
    return Buffer[1] == MAILBOX_SUCCESS ? 0 : 1;
}

static ULONG
MailboxTag (
    IN ULONG Tag,
    IN ULONG RequestLength,
    IN ULONG Value0,
    IN ULONG Value1,
    OUT PULONG Answer
    )
{
    static __declspec(align(16)) volatile ULONG Buffer[8];

    Buffer[0] = sizeof(Buffer);
    Buffer[1] = 0;
    Buffer[2] = Tag;
    Buffer[3] = 8;
    Buffer[4] = RequestLength;
    Buffer[5] = Value0;
    Buffer[6] = Value1;
    Buffer[7] = 0;
    if (MailboxPropertyCall(Buffer) != 0) {
        return 1;
    }

    Answer[0] = Buffer[5];
    Answer[1] = Buffer[6];
    return 0;
}

ULONG
MailboxSetPowerState (
    IN ULONG DeviceId,
    IN BOOLEAN On
    )

/*++

Routine Description:

    This routine sets the power of a peripheral and waits for it to settle.

Return Value:

    0 on success, 1 otherwise.

--*/

{
    ULONG Answer[2];

    return MailboxTag(TAG_SET_POWER_STATE, 8, DeviceId,
                      On ? POWER_STATE_ON | POWER_STATE_WAIT : POWER_STATE_WAIT,
                      Answer);
}

ULONG
MailboxGetClockRate (
    IN ULONG ClockId
    )

/*++

Routine Description:

    This routine returns the rate of a clock in hertz, or its maximum rate if
    the VideoCore reports none, or 0.

--*/

{
    ULONG Answer[2];

    if (MailboxTag(TAG_GET_CLOCK_RATE, 4, ClockId, 0, Answer) == 0 && Answer[1] != 0) {
        return Answer[1];
    }

    return MailboxTag(TAG_GET_MAX_CLOCK_RATE, 4, ClockId, 0, Answer) ? 0 : Answer[1];
}

ULONG
MailboxGetArmMemory (
    VOID
    )

/*++

Routine Description:

    This routine returns the size of the memory of the ARM, which starts at
    0, or 0.

--*/

{
    ULONG Answer[2];

    return MailboxTag(TAG_GET_ARM_MEMORY, 0, 0, 0, Answer) ? 0 : Answer[1];
}

ULONG
MailboxGetBoardSerial (
    OUT PULONG High
    )

/*++

Routine Description:

    This routine returns the board serial number, low part, and its high
    part in High; 0 if unknown.

--*/

{
    ULONG Answer[2];

    if (MailboxTag(TAG_GET_BOARD_SERIAL, 0, 0, 0, Answer) != 0) {
        Answer[0] = Answer[1] = 0;
    }

    *High = Answer[1];
    return Answer[0];
}

ULONG
MailboxGetBoardRevision (
    VOID
    )

/*++

Routine Description:

    This routine returns the board revision code (the model, its revision,
    memory size, processor and maker); 0 if unknown.

--*/

{
    ULONG Answer[2];

    return MailboxTag(TAG_GET_BOARD_REVISION, 0, 0, 0, Answer) ? 0 : Answer[0];
}

ULONG
MailboxGetFirmwareRevision (
    VOID
    )

/*++

Routine Description:

    This routine returns the revision of the VideoCore firmware (the time it
    was built, in seconds since 1970); 0 if unknown.

--*/

{
    ULONG Answer[2];

    return MailboxTag(TAG_GET_FIRMWARE_REVISION, 0, 0, 0, Answer) ? 0 : Answer[0];
}

BOOLEAN
MailboxGetBoardMacAddress (
    OUT PUCHAR Address
    )

/*++

Routine Description:

    This routine returns the Ethernet address of the board.

Return Value:

    TRUE if the VideoCore knows one, else FALSE.

--*/

{
    ULONG Answer[2];
    ULONG Index;

    if (MailboxTag(TAG_GET_BOARD_MAC, 0, 0, 0, Answer) != 0) {
        return FALSE;
    }

    for (Index = 0; Index < 6; Index += 1) {
        Address[Index] = (UCHAR)(Answer[Index / 4] >> ((Index % 4) * 8));
    }

    return (Address[0] | Address[1] | Address[2] | Address[3] | Address[4] | Address[5]) != 0;
}
