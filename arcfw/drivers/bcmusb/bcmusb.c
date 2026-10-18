/*++

Module Name:

    bcmusb.c

Abstract:

    This is the keyboard and pointer port driver of the Raspberry Pi 2. Its
    keyboards and pointers are USB devices of the HID class behind the root
    port of the USB host of the BCM2835 (usbhost.c), a Synopsys DesignWare
    OTG core: the board's LAN9514 hub (or that of the emulator), and the
    hubs, keyboards, mice and KVMs plugged into its ports. The firmware
    describes the host as the keyboard and the pointer controller of the
    internal bus, with its registers, its interrupt and the VideoCore mailbox
    through which it is powered.

    The driver powers and brings up the host, which finds the devices, then a
    system thread polls the interrupt endpoints of the keyboards and pointers,
    and the hubs, so that devices can come and go. The keys a keyboard report
    presses and releases become the scan codes (set 1) of an enhanced
    keyboard, and a pointer report its button transitions and its motion (or
    its position, if it is absolute), queued for the keyboard and mouse class
    drivers as i8042prt queues them; as i8042prt, the driver has one device,
    the keyboard port, also named the pointer port. A USB keyboard reports
    only changes, so the thread repeats a held key at the typematic rate, and
    it sets the keyboards' indicators.

Environment:

    kernel mode only

--*/

#include "ntddk.h"
#include "ntddkbd.h"
#include "ntddmou.h"
#include "kbdmou.h"
#include "bcmusb.h"

//
// VideoCore mailbox registers, the property channel and the power of the USB
// host.
//

#define MBOX_READ               0x00
#define MBOX_STATUS             0x18
#define MBOX_WRITE              0x20
#define MBOX_WR_FULL            0x80000000
#define MBOX_RD_EMPTY           0x40000000
#define MBOX_PROPERTY           8
#define TAG_SET_POWER_STATE     0x00028001
#define POWER_DEVICE_USB        3
#define POWER_ON_WAIT           3
#define PROPERTY_LENGTH         8

//
// The common buffer: the buffer of the host, then the mailbox property
// buffer.
//

#define PROPERTY_OFFSET         USB_BUFFER_SIZE
#define COMMON_BUFFER_SIZE      PAGE_SIZE

#define TIMEOUT_US              1000000
#define POLL_INTERVAL_MS        8
#define SCAN_INTERVAL_MS        256
#define INPUT_QUEUE_LENGTH      100

#define KEYBOARD_TYPE_ENHANCED  4

//
// The mouse: the identifier of a mouse of the HID class (the value later NT
// gives it), and its buttons, the first three of a report (left, right and
// middle, NT's buttons 1 to 3).
//

#define MOUSE_HID_HARDWARE      0x0080
#define MOUSE_BUTTONS           3

//
// The usages of the keyboard page: the first key, the modifiers (left
// control, shift, alt and windows, right control, shift, alt and windows)
// and the keys that do not repeat.
//

#define USAGE_FIRST_KEY         0x04
#define USAGE_FIRST_MODIFIER    0xE0
#define USAGE_LAST_MODIFIER     0xE7
#define USAGE_CAPS_LOCK         0x39
#define USAGE_SCROLL_LOCK       0x47
#define USAGE_PAUSE             0x48
#define USAGE_NUM_LOCK          0x53

#define KEY_DOWN(Keys, Usage)   (((Keys)[(Usage) / 8] >> ((Usage) & 7)) & 1)

//
// The input data queue of a class driver connection, as i8042prt keeps one
// for the keyboard and one for the mouse: records of the class's input data,
// added by the thread under the queue lock and given to the class driver by
// the DPC.
//

typedef struct _INPUT_QUEUE {
    CONNECT_DATA ConnectData;
    LONG EnableCount;
    KSPIN_LOCK Lock;
    KDPC Dpc;
    ULONG RecordSize;
    PUCHAR Data;
    PUCHAR DataIn;
    PUCHAR DataOut;
    PUCHAR DataEnd;
    ULONG Count;
} INPUT_QUEUE, *PINPUT_QUEUE;

typedef struct _DEVICE_EXTENSION {
    PDEVICE_OBJECT DeviceObject;
    PULONG Mailbox;
    USB_HOST Host;
    INPUT_QUEUE KeyboardQueue;
    INPUT_QUEUE MouseQueue;

    //
    // Of each HID interface of the host: the generation of its device, the
    // keys and buttons it holds down and its last absolute position; and the
    // keys and buttons all of them hold down, the input the class drivers
    // were given (a key held on two keyboards is down once).
    //

    UCHAR Generation[USB_MAXIMUM_HIDS];
    UCHAR Keys[USB_MAXIMUM_HIDS][USB_KEY_BYTES];
    ULONG Buttons[USB_MAXIMUM_HIDS];
    LONG X[USB_MAXIMUM_HIDS];
    LONG Y[USB_MAXIMUM_HIDS];
    UCHAR KeysDown[USB_KEY_BYTES];
    ULONG ButtonsDown;

    //
    // The attributes, indicators (changed under the keyboard queue lock) and
    // typematic parameters of the keyboard, and the key the thread repeats.
    //

    KEYBOARD_ATTRIBUTES KeyboardAttributes;
    KEYBOARD_INDICATOR_PARAMETERS Indicators;
    KEYBOARD_TYPEMATIC_PARAMETERS Typematic;
    BOOLEAN IndicatorsChanged;
    UCHAR RepeatUsage;
    LARGE_INTEGER RepeatTime;

    MOUSE_ATTRIBUTES MouseAttributes;
    HANDLE Thread;

    //
    // The lock of channel 0 and the devices, which the thread and the
    // function drivers take.
    //

    KMUTEX HostLock;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

//
// The host of the function drivers, once it is up.
//

static PDEVICE_EXTENSION FunctionHost;

static const INDICATOR_LIST IndicatorList[] = {
    {0x3A, KEYBOARD_CAPS_LOCK_ON},
    {0x45, KEYBOARD_NUM_LOCK_ON},
    {0x46, KEYBOARD_SCROLL_LOCK_ON}};

//
// The scan code (set 1) of each usage of the keyboard page, from usage 4:
// KEY_E0 above the code marks the keys of the enhanced keyboard that send an
// E0 prefix.
//

#define E0(Code)                ((KEY_E0 << 8) | (Code))

static const USHORT UsageScanCode[] = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,             // 04 a - h
    0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,             // 0C i - p
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,             // 14 q - x
    0x15, 0x2C, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,             // 1C y z 1 - 6
    0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F,             // 24 7 - 0 enter esc bs tab
    0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27,             // 2C space - = [ ] \ # ;
    0x28, 0x29, 0x33, 0x34, 0x35, 0x3A, 0x3B, 0x3C,             // 34 ' ` , . / caps f1 f2
    0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,             // 3C f3 - f10
    0x57, 0x58, E0(0x37), 0x46, 0x1D, E0(0x52), E0(0x47), E0(0x49), // 44 f11 f12 prtsc scroll pause ins home pgup
    E0(0x53), E0(0x4F), E0(0x51), E0(0x4D), E0(0x4B), E0(0x50), E0(0x48), 0x45, // 4C del end pgdn right left down up num
    E0(0x35), 0x37, 0x4A, 0x4E, E0(0x1C), 0x4F, 0x50, 0x51,     // 54 kp/ kp* kp- kp+ kpenter kp1 - kp3
    0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53,             // 5C kp4 - kp9 kp0 kp.
    0x56, E0(0x5D)};                                            // 64 \ (non-US) application

static const USHORT ModifierScanCode[8] = {
    0x1D, 0x2A, 0x38, E0(0x5B), E0(0x1D), 0x36, E0(0x38), E0(0x5C)};

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

//
// The routines the host is given: a stall, a sleep of the thread and a
// microsecond clock from the performance counter.
//

VOID
UsbStall(
    IN ULONG Microseconds
    )
{
    KeStallExecutionProcessor(Microseconds);
}

VOID
UsbSleep(
    IN ULONG Milliseconds
    )
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000 * (LONGLONG)Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

ULONG
UsbClock(
    VOID
    )
{
    return KeQueryPerformanceCounter(NULL).LowPart;
}

//
// Power the USB host through the VideoCore mailbox. The property buffer is in
// the common buffer, which the VideoCore reads and writes.
//

static BOOLEAN
UsbPowerOn(
    IN PDEVICE_EXTENSION Extension
    )
{
    PULONG Property = (PULONG)(Extension->Host.Buffer + PROPERTY_OFFSET);
    PULONG Mailbox = Extension->Mailbox;
    ULONG Time;
    ULONG Message;

    Property[0] = PROPERTY_LENGTH * sizeof(ULONG);
    Property[1] = 0;
    Property[2] = TAG_SET_POWER_STATE;
    Property[3] = 2 * sizeof(ULONG);
    Property[4] = 2 * sizeof(ULONG);
    Property[5] = POWER_DEVICE_USB;
    Property[6] = POWER_ON_WAIT;
    Property[7] = 0;

    for (Time = 0; READ_REGISTER_ULONG(Mailbox + MBOX_STATUS / sizeof(ULONG)) & MBOX_WR_FULL; Time += 1) {
        if (Time == TIMEOUT_US) {
            return FALSE;
        }

        KeStallExecutionProcessor(1);
    }

    WRITE_REGISTER_ULONG(Mailbox + MBOX_WRITE / sizeof(ULONG),
                         (Extension->Host.BufferAddress + PROPERTY_OFFSET) | MBOX_PROPERTY);

    do {
        for (Time = 0; READ_REGISTER_ULONG(Mailbox + MBOX_STATUS / sizeof(ULONG)) & MBOX_RD_EMPTY; Time += 1) {
            if (Time == TIMEOUT_US) {
                return FALSE;
            }

            KeStallExecutionProcessor(1);
        }

        Message = READ_REGISTER_ULONG(Mailbox + MBOX_READ / sizeof(ULONG));
    } while ((Message & 0xF) != MBOX_PROPERTY);

    return TRUE;
}

//
// The input data queues, as i8042prt keeps them: the thread adds a record
// under the queue lock and queues the DPC, which gives the class driver the
// records up to the end of the buffer (and the rest on its next run).
//

static VOID
UsbQueueInput(
    IN PINPUT_QUEUE Queue,
    IN PVOID Input
    )
{
    KIRQL Irql;

    KeAcquireSpinLock(&Queue->Lock, &Irql);
    if (Queue->EnableCount > 0 && Queue->Count < INPUT_QUEUE_LENGTH) {
        RtlMoveMemory(Queue->DataIn, Input, Queue->RecordSize);
        Queue->DataIn += Queue->RecordSize;
        if (Queue->DataIn == Queue->DataEnd) {
            Queue->DataIn = Queue->Data;
        }

        Queue->Count += 1;
        KeInsertQueueDpc(&Queue->Dpc, NULL, NULL);
    }

    KeReleaseSpinLock(&Queue->Lock, Irql);
}

static VOID
UsbInputDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    PINPUT_QUEUE Queue = DeferredContext;
    PUCHAR Start;
    PUCHAR End;
    ULONG Consumed;

    for (;;) {
        KeAcquireSpinLockAtDpcLevel(&Queue->Lock);
        if (Queue->Count == 0 || Queue->ConnectData.ClassService == NULL) {
            KeReleaseSpinLockFromDpcLevel(&Queue->Lock);
            return;
        }

        Start = Queue->DataOut;
        End = Start + Queue->Count * Queue->RecordSize;
        if (End > Queue->DataEnd) {
            End = Queue->DataEnd;
        }

        KeReleaseSpinLockFromDpcLevel(&Queue->Lock);

        Consumed = 0;
        (*(PSERVICE_CALLBACK_ROUTINE)Queue->ConnectData.ClassService)(
            Queue->ConnectData.ClassDeviceObject, Start, End, &Consumed);

        KeAcquireSpinLockAtDpcLevel(&Queue->Lock);
        Queue->DataOut += Consumed * Queue->RecordSize;
        if (Queue->DataOut == Queue->DataEnd) {
            Queue->DataOut = Queue->Data;
        }

        Queue->Count -= Consumed;
        KeReleaseSpinLockFromDpcLevel(&Queue->Lock);
        if (Consumed == 0) {
            return;
        }
    }
}

static BOOLEAN
UsbInitializeQueue(
    IN PINPUT_QUEUE Queue,
    IN ULONG RecordSize
    )
{
    Queue->Data = ExAllocatePool(NonPagedPool, INPUT_QUEUE_LENGTH * RecordSize);
    if (Queue->Data == NULL) {
        return FALSE;
    }

    Queue->RecordSize = RecordSize;
    Queue->DataIn = Queue->DataOut = Queue->Data;
    Queue->DataEnd = Queue->Data + INPUT_QUEUE_LENGTH * RecordSize;
    KeInitializeSpinLock(&Queue->Lock);
    KeInitializeDpc(&Queue->Dpc, UsbInputDpc, Queue);
    return TRUE;
}


//
// The input of a scan code of the keyboard.
//

static VOID
UsbQueueScanCode(
    IN PDEVICE_EXTENSION Extension,
    IN USHORT Code,
    IN USHORT Flags
    )
{
    KEYBOARD_INPUT_DATA Input;

    Input.UnitId = 0;
    Input.MakeCode = Code & 0xFF;
    Input.Flags = Flags | (Code >> 8);
    Input.Reserved = 0;
    Input.ExtraInformation = 0;
    UsbQueueInput(&Extension->KeyboardQueue, &Input);
}

//
// The scan codes of a usage, and the input of its press or release: Print
// Screen is E0 37, Pause E1 1D 45.
//

static VOID
UsbQueueKey(
    IN PDEVICE_EXTENSION Extension,
    IN ULONG Usage,
    IN USHORT Flags
    )
{
    if (Usage >= USAGE_FIRST_MODIFIER && Usage <= USAGE_LAST_MODIFIER) {
        UsbQueueScanCode(Extension, ModifierScanCode[Usage - USAGE_FIRST_MODIFIER], Flags);
        return;
    }

    if (Usage < USAGE_FIRST_KEY ||
        Usage >= USAGE_FIRST_KEY + sizeof(UsageScanCode) / sizeof(UsageScanCode[0])) {
        return;
    }

    if (Usage == USAGE_PAUSE) {
        UsbQueueScanCode(Extension, 0x1D, Flags | KEY_E1);
        UsbQueueScanCode(Extension, 0x45, Flags);
        return;
    }

    UsbQueueScanCode(Extension, UsageScanCode[Usage - USAGE_FIRST_KEY], Flags);
}

//
// The changes of the keys down: the modifiers and keys released, then those
// pressed. The last key pressed is the one the thread repeats.
//

static VOID
UsbProcessKeys(
    IN PDEVICE_EXTENSION Extension,
    IN OUT PUCHAR Last,
    IN PUCHAR Keys
    )
{
    LARGE_INTEGER Now;
    ULONG Usage;

    for (Usage = USAGE_FIRST_MODIFIER; Usage <= USAGE_LAST_MODIFIER; Usage += 1) {
        if (KEY_DOWN(Last, Usage) && !KEY_DOWN(Keys, Usage)) {
            UsbQueueKey(Extension, Usage, KEY_BREAK);
        }
    }

    for (Usage = USAGE_FIRST_KEY; Usage < USAGE_FIRST_MODIFIER; Usage += 1) {
        if (KEY_DOWN(Last, Usage) && !KEY_DOWN(Keys, Usage)) {
            UsbQueueKey(Extension, Usage, KEY_BREAK);
            if (Usage == Extension->RepeatUsage) {
                Extension->RepeatUsage = 0;
            }
        }
    }

    for (Usage = USAGE_FIRST_MODIFIER; Usage <= USAGE_LAST_MODIFIER; Usage += 1) {
        if (!KEY_DOWN(Last, Usage) && KEY_DOWN(Keys, Usage)) {
            UsbQueueKey(Extension, Usage, KEY_MAKE);
        }
    }

    KeQuerySystemTime(&Now);
    for (Usage = USAGE_FIRST_KEY; Usage < USAGE_FIRST_MODIFIER; Usage += 1) {
        if (KEY_DOWN(Last, Usage) || !KEY_DOWN(Keys, Usage)) {
            continue;
        }

        UsbQueueKey(Extension, Usage, KEY_MAKE);
        if (Usage != USAGE_CAPS_LOCK && Usage != USAGE_SCROLL_LOCK && Usage != USAGE_NUM_LOCK &&
            Usage != USAGE_PAUSE) {
            Extension->RepeatUsage = (UCHAR)Usage;
            Extension->RepeatTime.QuadPart = Now.QuadPart + 10000 * (LONGLONG)Extension->Typematic.Delay;
        }
    }

    RtlMoveMemory(Last, Keys, USB_KEY_BYTES);
}

//
// Repeat the held key at the typematic rate once the typematic delay passed.
//

static VOID
UsbRepeatKey(
    IN PDEVICE_EXTENSION Extension
    )
{
    LARGE_INTEGER Now;

    if (Extension->RepeatUsage == 0) {
        return;
    }

    KeQuerySystemTime(&Now);
    if (Now.QuadPart < Extension->RepeatTime.QuadPart) {
        return;
    }

    UsbQueueKey(Extension, Extension->RepeatUsage, KEY_MAKE);
    Extension->RepeatTime.QuadPart = Now.QuadPart + 10000000 / (LONGLONG)Extension->Typematic.Rate;
}

//
// The indicators of the keyboards, for their output reports: bit 0 num lock,
// bit 1 caps lock, bit 2 scroll lock.
//

static ULONG
UsbIndicators(
    IN PDEVICE_EXTENSION Extension,
    OUT PBOOLEAN Changed
    )
{
    KIRQL Irql;
    USHORT Flags;

    KeAcquireSpinLock(&Extension->KeyboardQueue.Lock, &Irql);
    Flags = Extension->Indicators.LedFlags;
    *Changed = Extension->IndicatorsChanged;
    Extension->IndicatorsChanged = FALSE;
    KeReleaseSpinLock(&Extension->KeyboardQueue.Lock, Irql);
    return ((Flags & KEYBOARD_NUM_LOCK_ON) ? 1 : 0) |
           ((Flags & KEYBOARD_CAPS_LOCK_ON) ? 2 : 0) |
           ((Flags & KEYBOARD_SCROLL_LOCK_ON) ? 4 : 0);
}

//
// The changes of a pointer: the transitions of the buttons all the pointers
// hold down, and its motion or, if it is absolute, its new position. A USB
// pointer reports positive motion down, as the mouse class driver takes it
// (that of the 8042 reports it up).
//

static VOID
UsbProcessPointer(
    IN PDEVICE_EXTENSION Extension,
    IN ULONG Index,
    IN PUSB_POINTER Pointer
    )
{
    MOUSE_INPUT_DATA Input;
    ULONG Buttons = 0;
    ULONG Changed;
    ULONG Button;

    Extension->Buttons[Index] = Pointer->Buttons & ((1 << MOUSE_BUTTONS) - 1);
    for (Button = 0; Button < USB_MAXIMUM_HIDS; Button += 1) {
        Buttons |= Extension->Buttons[Button];
    }

    Changed = Buttons ^ Extension->ButtonsDown;
    Extension->ButtonsDown = Buttons;
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Flags = Pointer->Absolute ? MOUSE_MOVE_ABSOLUTE : MOUSE_MOVE_RELATIVE;
    for (Button = 0; Button < MOUSE_BUTTONS; Button += 1) {
        if (Changed & (1 << Button)) {
            Input.Buttons |= ((Buttons & (1 << Button)) ? MOUSE_BUTTON_1_DOWN : MOUSE_BUTTON_1_UP) << (2 * Button);
        }
    }

    Input.LastX = Pointer->X;
    Input.LastY = Pointer->Y;
    if (Pointer->Absolute) {
        if (Input.Buttons == 0 && Pointer->X == Extension->X[Index] && Pointer->Y == Extension->Y[Index]) {
            return;
        }

        Extension->X[Index] = Pointer->X;
        Extension->Y[Index] = Pointer->Y;
    } else if (Input.Buttons == 0 && Input.LastX == 0 && Input.LastY == 0) {
        return;
    }

    UsbQueueInput(&Extension->MouseQueue, &Input);
}

//
// A HID interface that went, or whose slot a new device took: it holds no
// keys or buttons down any more.
//

static VOID
UsbReleaseInterface(
    IN PDEVICE_EXTENSION Extension,
    IN ULONG Index
    )
{
    USB_POINTER Pointer;

    RtlZeroMemory(Extension->Keys[Index], USB_KEY_BYTES);
    if (Extension->Buttons[Index] != 0) {
        RtlZeroMemory(&Pointer, sizeof(Pointer));
        UsbProcessPointer(Extension, Index, &Pointer);
    }

    Extension->X[Index] = -1;
    Extension->Y[Index] = -1;
}

//
// The keys all the keyboards hold down, and their changes.
//

static VOID
UsbProcessKeyboards(
    IN PDEVICE_EXTENSION Extension
    )
{
    UCHAR Keys[USB_KEY_BYTES];
    ULONG Index;
    ULONG Byte;

    RtlZeroMemory(Keys, sizeof(Keys));
    for (Index = 0; Index < USB_MAXIMUM_HIDS; Index += 1) {
        for (Byte = 0; Byte < USB_KEY_BYTES; Byte += 1) {
            Keys[Byte] |= Extension->Keys[Index][Byte];
        }
    }

    UsbProcessKeys(Extension, Extension->KeysDown, Keys);
}

//
// The thread: each poll interval it reads a report from each keyboard and
// pointer, sets the indicators of the keyboards when they change (and those
// of a new one), repeats the held key, and now and then looks for devices
// that came or went.
//

static VOID
UsbPollThread(
    IN PVOID Context
    )
{
    PDEVICE_EXTENSION Extension = Context;
    PUSB_HOST Host = &Extension->Host;
    UCHAR Report[USB_REPORT_LENGTH];
    UCHAR Keys[USB_KEY_BYTES];
    USB_POINTER Pointer;
    PUSB_HID Hid;
    ULONG Scan = UsbClock();
    ULONG Leds;
    ULONG Index;
    BOOLEAN Changed;
    LONG Length;

    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
    for (;;) {
        KeWaitForSingleObject(&Extension->HostLock, Executive, KernelMode, FALSE, NULL);
        Leds = UsbIndicators(Extension, &Changed);
        for (Index = 0; Index < USB_MAXIMUM_HIDS; Index += 1) {
            Hid = &Host->Hids[Index];
            if (!Hid->Present || Hid->Generation != Extension->Generation[Index]) {
                UsbReleaseInterface(Extension, Index);
                Extension->Generation[Index] = Hid->Present ? Hid->Generation : 0;
                if (!Hid->Present) {
                    continue;
                }

                UsbHidSetIndicators(Host, Hid, Leds);
            } else if (Changed) {
                UsbHidSetIndicators(Host, Hid, Leds);
            }

            Length = UsbHidRead(Host, Hid, Report);
            if (Length <= 0) {
                continue;
            }

            if ((Hid->Kinds & HID_KEYBOARD) && UsbHidKeys(Hid, Report, Length, Keys)) {
                RtlMoveMemory(Extension->Keys[Index], Keys, USB_KEY_BYTES);
            }

            if ((Hid->Kinds & HID_POINTER) && UsbHidPointer(Hid, Report, Length, &Pointer)) {
                UsbProcessPointer(Extension, Index, &Pointer);
            }
        }

        UsbProcessKeyboards(Extension);
        UsbRepeatKey(Extension);
        if (UsbClock() - Scan >= SCAN_INTERVAL_MS * Host->ClockRate) {
            UsbHostScan(Host);
            Scan = UsbClock();
        }

        KeReleaseMutex(&Extension->HostLock, FALSE);
        UsbSleep(POLL_INTERVAL_MS);
    }
}

static NTSTATUS
UsbOpenClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
{
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

//
// The connection of a class driver to the keyboard or the mouse, and the
// disabling of its input.
//

static NTSTATUS
UsbConnect(
    IN PINPUT_QUEUE Queue,
    IN PIO_STACK_LOCATION Stack
    )
{
    KIRQL Irql;

    if (Queue->ConnectData.ClassService != NULL) {
        return STATUS_SHARING_VIOLATION;
    }

    if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA)) {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Queue->Lock, &Irql);
    Queue->ConnectData = *(PCONNECT_DATA)Stack->Parameters.DeviceIoControl.Type3InputBuffer;
    Queue->DataIn = Queue->DataOut = Queue->Data;
    Queue->Count = 0;
    KeReleaseSpinLock(&Queue->Lock, Irql);
    return STATUS_SUCCESS;
}

static NTSTATUS
UsbDisable(
    IN PINPUT_QUEUE Queue
    )
{
    if (Queue->EnableCount == 0) {
        return STATUS_DEVICE_DATA_ERROR;
    }

    ExInterlockedDecrementLong(&Queue->EnableCount, &Queue->Lock);
    return STATUS_SUCCESS;
}

//
// The internal device controls of the keyboard and mouse class drivers: the
// connection, enabling and disabling, the attributes, indicators and
// typematic parameters of the keyboard and the attributes of the mouse.
//

static NTSTATUS
UsbInternalDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
{
    PDEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Size;
    KIRQL Irql;

    Irp->IoStatus.Information = 0;
    switch (Stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_INTERNAL_KEYBOARD_CONNECT:
        Status = UsbConnect(&Extension->KeyboardQueue, Stack);
        break;

    case IOCTL_INTERNAL_MOUSE_CONNECT:
        Status = UsbConnect(&Extension->MouseQueue, Stack);
        break;

    case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        Status = STATUS_NOT_IMPLEMENTED;
        break;

    case IOCTL_INTERNAL_KEYBOARD_ENABLE:
        ExInterlockedIncrementLong(&Extension->KeyboardQueue.EnableCount, &Extension->KeyboardQueue.Lock);
        break;

    case IOCTL_INTERNAL_MOUSE_ENABLE:
        ExInterlockedIncrementLong(&Extension->MouseQueue.EnableCount, &Extension->MouseQueue.Lock);
        break;

    case IOCTL_INTERNAL_KEYBOARD_DISABLE:
        Status = UsbDisable(&Extension->KeyboardQueue);
        break;

    case IOCTL_INTERNAL_MOUSE_DISABLE:
        Status = UsbDisable(&Extension->MouseQueue);
        break;

    case IOCTL_KEYBOARD_QUERY_ATTRIBUTES:
        if (OutputLength < sizeof(KEYBOARD_ATTRIBUTES)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PKEYBOARD_ATTRIBUTES)Buffer = Extension->KeyboardAttributes;
            Irp->IoStatus.Information = sizeof(KEYBOARD_ATTRIBUTES);
        }

        break;

    case IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION:
        Size = sizeof(KEYBOARD_INDICATOR_TRANSLATION) + sizeof(IndicatorList) - sizeof(INDICATOR_LIST);
        if (OutputLength < Size) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            ((PKEYBOARD_INDICATOR_TRANSLATION)Buffer)->NumberOfIndicatorKeys =
                sizeof(IndicatorList) / sizeof(IndicatorList[0]);
            RtlMoveMemory(((PKEYBOARD_INDICATOR_TRANSLATION)Buffer)->IndicatorList,
                          (PVOID)IndicatorList, sizeof(IndicatorList));
            Irp->IoStatus.Information = Size;
        }

        break;

    case IOCTL_KEYBOARD_QUERY_INDICATORS:
        if (OutputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PKEYBOARD_INDICATOR_PARAMETERS)Buffer = Extension->Indicators;
            Irp->IoStatus.Information = sizeof(KEYBOARD_INDICATOR_PARAMETERS);
        }

        break;

    case IOCTL_KEYBOARD_SET_INDICATORS:
        if (InputLength < sizeof(KEYBOARD_INDICATOR_PARAMETERS) ||
            (((PKEYBOARD_INDICATOR_PARAMETERS)Buffer)->LedFlags &
             ~(KEYBOARD_SCROLL_LOCK_ON | KEYBOARD_NUM_LOCK_ON | KEYBOARD_CAPS_LOCK_ON)) != 0) {
            Status = STATUS_INVALID_PARAMETER;
        } else {
            KeAcquireSpinLock(&Extension->KeyboardQueue.Lock, &Irql);
            Extension->Indicators = *(PKEYBOARD_INDICATOR_PARAMETERS)Buffer;
            Extension->IndicatorsChanged = TRUE;
            KeReleaseSpinLock(&Extension->KeyboardQueue.Lock, Irql);
        }

        break;

    case IOCTL_KEYBOARD_QUERY_TYPEMATIC:
        if (OutputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer = Extension->Typematic;
            Irp->IoStatus.Information = sizeof(KEYBOARD_TYPEMATIC_PARAMETERS);
        }

        break;

    case IOCTL_KEYBOARD_SET_TYPEMATIC:
        if (InputLength < sizeof(KEYBOARD_TYPEMATIC_PARAMETERS) ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Rate < Extension->KeyboardAttributes.KeyRepeatMinimum.Rate ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Rate > Extension->KeyboardAttributes.KeyRepeatMaximum.Rate ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Delay < Extension->KeyboardAttributes.KeyRepeatMinimum.Delay ||
            ((PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer)->Delay > Extension->KeyboardAttributes.KeyRepeatMaximum.Delay) {
            Status = STATUS_INVALID_PARAMETER;
        } else {
            Extension->Typematic = *(PKEYBOARD_TYPEMATIC_PARAMETERS)Buffer;
        }

        break;

    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
        if (OutputLength < sizeof(MOUSE_ATTRIBUTES)) {
            Status = STATUS_BUFFER_TOO_SMALL;
        } else {
            *(PMOUSE_ATTRIBUTES)Buffer = Extension->MouseAttributes;
            Irp->IoStatus.Information = sizeof(MOUSE_ATTRIBUTES);
        }

        break;

    default:
        Status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

//
// The resources of the keyboard controller in the hardware description: its
// interrupt, its registers and the mailbox.
//

typedef struct _CONFIGURATION {
    BOOLEAN Found;
    ULONG BusNumber;
    PHYSICAL_ADDRESS Memory[2];
    ULONG MemoryLength[2];
} CONFIGURATION, *PCONFIGURATION;

static NTSTATUS
UsbConfigurationCallout(
    IN PVOID Context,
    IN PUNICODE_STRING PathName,
    IN INTERFACE_TYPE BusType,
    IN ULONG BusNumber,
    IN PKEY_VALUE_FULL_INFORMATION *BusInformation,
    IN CONFIGURATION_TYPE ControllerType,
    IN ULONG ControllerNumber,
    IN PKEY_VALUE_FULL_INFORMATION *ControllerInformation,
    IN CONFIGURATION_TYPE PeripheralType,
    IN ULONG PeripheralNumber,
    IN PKEY_VALUE_FULL_INFORMATION *PeripheralInformation
    )
{
    PCONFIGURATION Configuration = Context;
    PKEY_VALUE_FULL_INFORMATION Data = ControllerInformation[IoQueryDeviceConfigurationData];
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG Index;
    ULONG Memory = 0;

    if (Configuration->Found || Data == NULL || Data->DataLength == 0) {
        return STATUS_SUCCESS;
    }

    Full = (PCM_FULL_RESOURCE_DESCRIPTOR)((PUCHAR)Data + Data->DataOffset);
    Partial = Full->PartialResourceList.PartialDescriptors;
    for (Index = 0; Index < Full->PartialResourceList.Count; Index += 1, Partial += 1) {
        if (Partial->Type == CmResourceTypeMemory && Memory < 2) {
            Configuration->Memory[Memory] = Partial->u.Memory.Start;
            Configuration->MemoryLength[Memory] = Partial->u.Memory.Length;
            Memory += 1;
        }
    }

    if (Memory == 2) {
        Configuration->Found = TRUE;
        Configuration->BusNumber = BusNumber;
    }

    return STATUS_SUCCESS;
}

static PVOID
UsbMapRegisters(
    IN ULONG BusNumber,
    IN PHYSICAL_ADDRESS Address,
    IN ULONG Length
    )
{
    PHYSICAL_ADDRESS Translated;
    ULONG AddressSpace = 0;

    if (!HalTranslateBusAddress(Internal, BusNumber, Address, &AddressSpace, &Translated)) {
        return NULL;
    }

    return MmMapIoSpace(Translated, Length, FALSE);
}

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )

/*++

Routine Description:

    This routine finds the keyboard controller in the hardware description,
    maps it, powers and brings up the USB host, which finds the keyboards
    and pointers, then creates the keyboard port device, names it the pointer
    port as well, records the ports in the device map for the class drivers
    and starts the thread that polls the devices.

Arguments:

    DriverObject - Supplies the driver object.

    RegistryPath - Supplies the service key of the driver.

Return Value:

    The status of the initialization.

--*/

{
    CONFIGURATION Configuration;
    INTERFACE_TYPE Interface = Internal;
    CONFIGURATION_TYPE ControllerType = KeyboardController;
    CONFIGURATION_TYPE PeripheralType = KeyboardPeripheral;
    DEVICE_DESCRIPTION Description;
    PADAPTER_OBJECT Adapter;
    PHYSICAL_ADDRESS Logical;
    LARGE_INTEGER Frequency;
    ULONG MapRegisters;
    UNICODE_STRING DeviceName;
    UNICODE_STRING PointerName;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_EXTENSION Extension;
    NTSTATUS Status;

    RtlZeroMemory(&Configuration, sizeof(Configuration));
    IoQueryDeviceDescription(&Interface, NULL, &ControllerType, NULL, &PeripheralType, NULL,
                             UsbConfigurationCallout, &Configuration);
    if (!Configuration.Found) {
        return STATUS_NO_SUCH_DEVICE;
    }

    RtlInitUnicodeString(&DeviceName, DD_KEYBOARD_PORT_DEVICE_NAME_U L"0");
    Status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), &DeviceName,
                            FILE_DEVICE_KEYBOARD, 0, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(DEVICE_EXTENSION));
    Extension->DeviceObject = DeviceObject;

    Extension->Host.Registers = UsbMapRegisters(Configuration.BusNumber, Configuration.Memory[0],
                                                Configuration.MemoryLength[0]);
    Extension->Mailbox = UsbMapRegisters(Configuration.BusNumber, Configuration.Memory[1],
                                         Configuration.MemoryLength[1]);

    RtlZeroMemory(&Description, sizeof(Description));
    Description.Version = DEVICE_DESCRIPTION_VERSION;
    Description.Master = TRUE;
    Description.Dma32BitAddresses = TRUE;
    Description.InterfaceType = Internal;
    Description.BusNumber = Configuration.BusNumber;
    Description.MaximumLength = COMMON_BUFFER_SIZE;
    Adapter = HalGetAdapter(&Description, &MapRegisters);
    if (Adapter != NULL) {
        Extension->Host.Buffer = HalAllocateCommonBuffer(Adapter, COMMON_BUFFER_SIZE, &Logical, FALSE);
        Extension->Host.BufferAddress = Logical.LowPart;
    }

    if (Extension->Host.Registers == NULL || Extension->Mailbox == NULL || Extension->Host.Buffer == NULL ||
        !UsbInitializeQueue(&Extension->KeyboardQueue, sizeof(KEYBOARD_INPUT_DATA)) ||
        !UsbInitializeQueue(&Extension->MouseQueue, sizeof(MOUSE_INPUT_DATA))) {
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->KeyboardAttributes.KeyboardIdentifier.Type = KEYBOARD_TYPE_ENHANCED;
    Extension->KeyboardAttributes.KeyboardMode = 1;
    Extension->KeyboardAttributes.NumberOfFunctionKeys = 12;
    Extension->KeyboardAttributes.NumberOfIndicators = sizeof(IndicatorList) / sizeof(IndicatorList[0]);
    Extension->KeyboardAttributes.NumberOfKeysTotal = 101;
    Extension->KeyboardAttributes.InputDataQueueLength = INPUT_QUEUE_LENGTH * sizeof(KEYBOARD_INPUT_DATA);
    Extension->KeyboardAttributes.KeyRepeatMinimum.Rate = 2;
    Extension->KeyboardAttributes.KeyRepeatMinimum.Delay = 250;
    Extension->KeyboardAttributes.KeyRepeatMaximum.Rate = 30;
    Extension->KeyboardAttributes.KeyRepeatMaximum.Delay = 1000;
    Extension->Typematic.Rate = 30;
    Extension->Typematic.Delay = 250;

    Extension->MouseAttributes.MouseIdentifier = MOUSE_HID_HARDWARE;
    Extension->MouseAttributes.NumberOfButtons = MOUSE_BUTTONS;
    Extension->MouseAttributes.SampleRate = 1000 / POLL_INTERVAL_MS;
    Extension->MouseAttributes.InputDataQueueLength = INPUT_QUEUE_LENGTH * sizeof(MOUSE_INPUT_DATA);

    KeQueryPerformanceCounter(&Frequency);
    Extension->Host.ClockRate = Frequency.LowPart / 1000;
    KeInitializeMutex(&Extension->HostLock, 0);
    UsbPowerOn(Extension);
    if (!UsbHostInitialize(&Extension->Host)) {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    FunctionHost = Extension;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = UsbOpenClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = UsbOpenClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = UsbInternalDeviceControl;

    RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, DD_KEYBOARD_PORT_BASE_NAME_U,
                          DeviceName.Buffer, REG_SZ, RegistryPath->Buffer,
                          RegistryPath->Length + sizeof(UNICODE_NULL));

    RtlInitUnicodeString(&PointerName, DD_POINTER_PORT_DEVICE_NAME_U L"0");
    if (NT_SUCCESS(IoCreateSymbolicLink(&PointerName, &DeviceName))) {
        RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, DD_POINTER_PORT_BASE_NAME_U,
                              PointerName.Buffer, REG_SZ, RegistryPath->Buffer,
                              RegistryPath->Length + sizeof(UNICODE_NULL));
    }

    return PsCreateSystemThread(&Extension->Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                UsbPollThread, Extension);
}

//
// The interface of the function drivers (bcmusb.h).
//

BOOLEAN
UsbGetFunction(
    IN ULONG Index,
    OUT PUSB_FUNCTION Function
    )

/*++

Routine Description:

    This routine gives a function: a device the host found and left
    unconfigured, one that is not a hub and has no keyboards or pointers.

Arguments:

    Index - Supplies the number of the function, from 0.

    Function - Receives the function.

Return Value:

    TRUE if there is such a function, else FALSE.

--*/

{
    PUSB_DEVICE Device;
    ULONG Slot;
    BOOLEAN Found = FALSE;

    if (FunctionHost == NULL) {
        return FALSE;
    }

    KeWaitForSingleObject(&FunctionHost->HostLock, Executive, KernelMode, FALSE, NULL);
    for (Slot = 0; Slot < USB_MAXIMUM_DEVICES; Slot += 1) {
        Device = &FunctionHost->Host.Devices[Slot];
        if (!Device->Present || Device->Stage != USB_STAGE_READY || Device->Ports != 0 || Device->Kinds != 0) {
            continue;
        }

        if (Index != 0) {
            Index -= 1;
            continue;
        }

        Function->Device = Slot;
        Function->Vendor = Device->Vendor;
        Function->Product = Device->Product;
        Function->Class = Device->Class;
        Function->Speed = Device->Speed;
        Found = TRUE;
        break;
    }

    KeReleaseMutex(&FunctionHost->HostLock, FALSE);
    return Found;
}

LONG
UsbFunctionControl(
    IN ULONG Device,
    IN ULONG RequestType,
    IN ULONG Request,
    IN ULONG Value,
    IN ULONG Interface,
    IN OUT PVOID Data,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine runs a control transfer to a function.

Arguments:

    Device - Supplies the device of the function.

    RequestType, Request, Value, Interface, Length - Supply the setup packet.

    Data - Supplies the data of the transfer, or receives it.

Return Value:

    The length of the data, or a negative XFER_ status.

--*/

{
    LONG Status;

    if (FunctionHost == NULL) {
        return XFER_ERROR;
    }

    KeWaitForSingleObject(&FunctionHost->HostLock, Executive, KernelMode, FALSE, NULL);
    Status = UsbDeviceControl(&FunctionHost->Host, Device, RequestType, Request, Value, Interface,
                              Data, Length);
    KeReleaseMutex(&FunctionHost->HostLock, FALSE);
    return Status;
}

BOOLEAN
UsbFunctionOpenPipe(
    IN ULONG Device,
    IN ULONG Endpoint,
    IN BOOLEAN In,
    IN ULONG Maximum,
    OUT PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine opens a pipe to a bulk endpoint of a function.

Arguments:

    Device - Supplies the device of the function.

    Endpoint - Supplies the endpoint number.

    In - Supplies TRUE for an IN endpoint.

    Maximum - Supplies the maximum packet size of the endpoint.

    Pipe - Receives the pipe.

Return Value:

    TRUE if the pipe was opened, else FALSE.

--*/

{
    BOOLEAN Opened;

    if (FunctionHost == NULL) {
        return FALSE;
    }

    KeWaitForSingleObject(&FunctionHost->HostLock, Executive, KernelMode, FALSE, NULL);
    Opened = UsbOpenPipe(&FunctionHost->Host, Device, Endpoint, In, Maximum, Pipe);
    KeReleaseMutex(&FunctionHost->HostLock, FALSE);
    return Opened;
}

VOID
UsbFunctionClosePipe(
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine closes a pipe.

Arguments:

    Pipe - Supplies the pipe.

Return Value:

    None.

--*/

{
    KeWaitForSingleObject(&FunctionHost->HostLock, Executive, KernelMode, FALSE, NULL);
    UsbClosePipe(&FunctionHost->Host, Pipe);
    KeReleaseMutex(&FunctionHost->HostLock, FALSE);
}

VOID
UsbFunctionStartPipe(
    IN PUSB_PIPE Pipe,
    IN ULONG Address,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine starts a transfer on a pipe (UsbStartPipe).

Arguments:

    Pipe - Supplies the pipe.

    Address - Supplies the bus address of the buffer.

    Length - Supplies the length of the transfer.

Return Value:

    None.

--*/

{
    UsbStartPipe(&FunctionHost->Host, Pipe, Address, Length);
}

VOID
UsbFunctionPipeInterrupt(
    IN PUSB_PIPE Pipe,
    IN BOOLEAN Enable
    )

/*++

Routine Description:

    This routine lets the channel of a pipe interrupt, or stops it
    (UsbPipeInterrupt).

Arguments:

    Pipe - Supplies the pipe.

    Enable - Supplies TRUE to let the channel interrupt.

Return Value:

    None.

--*/

{
    UsbPipeInterrupt(&FunctionHost->Host, Pipe, Enable);
}

BOOLEAN
UsbFunctionPipeHalted(
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine, of the interrupt service routine of a function driver,
    sees if the channel of a pipe halted (UsbPipeHalted).

Arguments:

    Pipe - Supplies the pipe.

Return Value:

    TRUE if the channel halted, else FALSE.

--*/

{
    return UsbPipeHalted(&FunctionHost->Host, Pipe);
}

LONG
UsbFunctionPipeResult(
    IN PUSB_PIPE Pipe
    )

/*++

Routine Description:

    This routine gives the result of the transfer a pipe halted at
    (UsbPipeResult).

Arguments:

    Pipe - Supplies the pipe.

Return Value:

    The length moved, or a negative XFER_ status.

--*/

{
    return UsbPipeResult(&FunctionHost->Host, Pipe);
}
