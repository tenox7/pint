//
// arcfw/arm/config.c - the ARC firmware configuration tree.
//
// The firmware builds the configuration component tree at initialization, as a
// Jazz firmware loads it from NVRAM (FW/MIPS/JXCONFIG.C), and serves it through the
// GetChild/GetPeer/GetParent/GetData/GetComponent and AddChild/DeleteComponent/
// SaveConfiguration vector entries. The OS Loader copies it into the loader block
// (BlConfigurationInitialize), where KeFindConfigurationEntry walks it.
//
// As on Jazz (FW/MIPS/JZCONFIG.C), the on-board devices sit on an internal bus
// adapter, and the disk controller is a ScsiAdapter child of the root whose
// configuration data describes it to the SCSI port driver:
//   System(arc) -> CentralProcessor(cpu) -> FloatingPointProcessor, caches
//   System(arc) -> MultiFunctionAdapter("BCM283x-Internal Bus") -> DisplayController(video)
//                  -> MonitorPeripheral(monitor)
//   MultiFunctionAdapter -> KeyboardController(key, the USB host) -> KeyboardPeripheral(keyboard)
//   MultiFunctionAdapter -> PointerController(point, the USB host) -> PointerPeripheral(pointer)
//   MultiFunctionAdapter -> NetworkController(net, a function of the USB host), if one is found
//   System(arc) -> ScsiAdapter(scsi) -> DiskController(disk) -> DiskPeripheral(rdisk)
// BlGetPathnameFromComponent renders the SD card as "scsi(0)disk(0)rdisk(0)".
//
#include "fwp.h"
#include "fwstring.h"
#include "bcmvdeo.h"
#include "bcmethhw.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static CONFIGURATION_COMPONENT_DATA ArcSystemNode;
static CONFIGURATION_COMPONENT_DATA ArcBusNode;
static CONFIGURATION_COMPONENT_DATA ArcScsiNode;
static CONFIGURATION_COMPONENT_DATA ArcControllerNode;
static CONFIGURATION_COMPONENT_DATA ArcDiskNode;
static CONFIGURATION_COMPONENT_DATA ArcVideoNode;
static CONFIGURATION_COMPONENT_DATA ArcMonitorNode;
static CONFIGURATION_COMPONENT_DATA ArcKeyboardControllerNode;
static CONFIGURATION_COMPONENT_DATA ArcKeyboardNode;
static CONFIGURATION_COMPONENT_DATA ArcPointerControllerNode;
static CONFIGURATION_COMPONENT_DATA ArcPointerNode;
static CONFIGURATION_COMPONENT_DATA ArcNetworkNode;

//
// The BCM2836 has four Cortex-A7 cores and the BCM2837 four Cortex-A53, each
// with split L1 caches and a VFPv4 unit, sharing one L2. As on every ARC machine,
// each processor component carries its floating point and cache components; its
// identifier names the core (the MIDR part number), and a cache's Key encodes its
// geometry (lines per block << 24 | log2 line size << 16 | log2 pages).
//

#define ARC_PROCESSORS 4

static CONFIGURATION_COMPONENT_DATA ArcCpuNode[ARC_PROCESSORS];
static CONFIGURATION_COMPONENT_DATA ArcFpuNode[ARC_PROCESSORS];
static CONFIGURATION_COMPONENT_DATA ArcIcacheNode[ARC_PROCESSORS];
static CONFIGURATION_COMPONENT_DATA ArcDcacheNode[ARC_PROCESSORS];
static CONFIGURATION_COMPONENT_DATA ArcL2cacheNode[ARC_PROCESSORS];

static CHAR ArcSystemId[64] = "Raspberry Pi";
static PCHAR ArcCpuId;
static CHAR ArcFpuId[] = "ARM-VFPv4";
static CHAR ArcBusId[24] = "BCM2836-Internal Bus";
static CHAR ArcScsiId[] = "BCM2835-EMMC";
static CHAR ArcVideoId[] = BCM_VIDEO_IDENTIFIER;
static CHAR ArcMonitorId[24];
static CHAR ArcUsbId[] = "BCM2835-USB";
static CHAR ArcKeyboardId[] = "PCAT_ENHANCED";
static CHAR ArcPointerId[] = "USB MOUSE";

//
// The system component describes the board as the VideoCore reports it: its model
// is the identifier, and the configuration data holds the dates and versions of
// this firmware and of the VideoCore's (ARM_SYSTEM_FIRMWARE_DATA). A board
// revision code with bit 23 set holds the model (bits 11:4), its revision (3:0),
// the processor (15:12), the maker (19:16) and the memory size (22:20).
//

static PCHAR BoardNames[] = {
    "Raspberry Pi Model A", "Raspberry Pi Model B", "Raspberry Pi Model A+",
    "Raspberry Pi Model B+", "Raspberry Pi 2 Model B", "Raspberry Pi Alpha",
    "Raspberry Pi Compute Module", NULL, "Raspberry Pi 3 Model B", "Raspberry Pi Zero",
    "Raspberry Pi Compute Module 3", NULL, "Raspberry Pi Zero W", "Raspberry Pi 3 Model B+",
    "Raspberry Pi 3 Model A+", NULL, "Raspberry Pi Compute Module 3+", "Raspberry Pi 4 Model B",
    "Raspberry Pi Zero 2 W", "Raspberry Pi 400", "Raspberry Pi Compute Module 4"
};
static PCHAR Processors[] = {"BCM2835", "BCM2836", "BCM2837", "BCM2711"};
static PCHAR Makers[] = {"Sony UK", "Egoman", "Embest", "Sony Japan", "Embest", "Stadium"};

#define LEAP(y) ((y) % 4 == 0 && ((y) % 100 != 0 || (y) % 400 == 0))
#define FIRMWARE_TIME_2012 1325376000

static struct {
    CM_PARTIAL_RESOURCE_LIST List;
    ARM_SYSTEM_FIRMWARE_DATA Firmware;
} ArcSystemData;

//
// The configuration data of the SD card controller (the EMMC): its registers,
// the VideoCore mailbox through which its clock is controlled, its interrupt at
// DEVICE_LEVEL, and the SCSI host identifier, as Jazz describes its SCSI
// controller.
//

#define EMMC_PHYSICAL_BASE      0x3F300000
#define EMMC_REGISTERS_SIZE     0x100
#define MAILBOX_PHYSICAL_BASE   0x3F00B880
#define MAILBOX_SIZE            0x40
#define EMMC_LEVEL              3
#define EMMC_VECTOR             62
#define SCSI_HOST_ID            7

static struct {
    CM_PARTIAL_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR More[3];
    CM_SCSI_DEVICE_DATA ScsiData;
} ArcScsiData;

//
// The configuration data of the keyboard and pointer controllers, the USB host
// (the DWC2 OTG core) of the keyboard and the mouse: its registers, its interrupt
// at DEVICE_LEVEL and the VideoCore mailbox through which it is powered, as Jazz
// describes its i8742 to both.
//

#define USB_PHYSICAL_BASE       0x3F980000
#define USB_REGISTERS_SIZE      0x10000
#define USB_LEVEL               3
#define USB_VECTOR              9

static struct {
    CM_PARTIAL_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR More[2];
} ArcUsbData;

//
// The frame buffer the VideoCore allocated (jxdisp.c), described as the display
// controller and its monitor, as Jazz describes its video board.
//

extern BCM_VIDEO_CONFIGURATION_DATA VideoData;
extern MONITOR_CONFIGURATION_DATA MonitorData;

static BCM_VIDEO_CONFIGURATION_DATA ArcVideoData;
static MONITOR_CONFIGURATION_DATA ArcMonitorData;

static PCONFIGURATION_COMPONENT_DATA ArcConfigTree;

static VOID
SetComponent(
    PCONFIGURATION_COMPONENT_DATA Node,
    CONFIGURATION_CLASS Class,
    CONFIGURATION_TYPE Type,
    ULONG Key
    )
{
    Node->Parent = NULL;
    Node->Child = NULL;
    Node->Sibling = NULL;
    Node->ConfigurationData = NULL;
    Node->ComponentEntry.Class = Class;
    Node->ComponentEntry.Type = Type;
    Node->ComponentEntry.Version = 0;
    Node->ComponentEntry.Revision = 0;
    Node->ComponentEntry.Key = Key;
    Node->ComponentEntry.AffinityMask = 0;
    Node->ComponentEntry.ConfigurationDataLength = 0;
    Node->ComponentEntry.IdentifierLength = 0;
    Node->ComponentEntry.Identifier = NULL;
}

static VOID
SetIdentifier(
    PCONFIGURATION_COMPONENT_DATA Node,
    PCHAR Identifier
    )
{
    Node->ComponentEntry.Identifier = Identifier;
    Node->ComponentEntry.IdentifierLength = strlen(Identifier) + 1;
}

static VOID
Link(
    PCONFIGURATION_COMPONENT_DATA Parent,
    PCONFIGURATION_COMPONENT_DATA Child
    )
{
    PCONFIGURATION_COMPONENT_DATA *Last = &Parent->Child;

    while (*Last != NULL) {
        Last = &(*Last)->Sibling;
    }
    *Last = Child;
    Child->Parent = Parent;
}

static VOID
SetDescriptor(
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial,
    UCHAR Type,
    USHORT Flags
    )
{
    memset(Partial, 0, sizeof(*Partial));
    Partial->Type = Type;
    Partial->ShareDisposition = CmResourceShareDeviceExclusive;
    Partial->Flags = Flags;
}

//
// A date as a PC BIOS keeps it, mm/dd/yy: of a time in seconds since 1970 (the
// VideoCore firmware revision), or of the __DATE__ this firmware was built on.
//

static VOID
FormatTime(
    PCHAR Buffer,
    ULONG Time
    )
{
    static UCHAR MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    ULONG Days = Time / 86400, Year = 1970, Month = 1, Length;

    while (Days >= (Length = 365 + LEAP(Year))) {
        Days -= Length;
        Year += 1;
    }
    while (Days >= (Length = MonthDays[Month - 1] + (Month == 2 ? LEAP(Year) : 0))) {
        Days -= Length;
        Month += 1;
    }
    sprintf(Buffer, "%02lu/%02lu/%02lu", Month, Days + 1, Year % 100);
}

static VOID
FormatBuildDate(
    PCHAR Buffer
    )
{
    static CHAR Date[] = __DATE__, Months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    ULONG Month = 0, Day;

    while (Month < 11 && strncmp(&Months[Month * 3], Date, 3) != 0) {
        Month += 1;
    }
    Day = (Date[4] == ' ' ? 0 : Date[4] - '0') * 10 + Date[5] - '0';
    sprintf(Buffer, "%02lu/%02lu/%c%c", Month + 1, Day, Date[9], Date[10]);
}

//
// Build the system component's identifier and configuration data and return
// the length of the data.
//

static ULONG
MakeSystemData(
    VOID
    )
{
    PARM_SYSTEM_FIRMWARE_DATA Data = &ArcSystemData.Firmware;
    PCHAR Version = Data->SystemFirmwareVersion;
    ULONG Revision = MailboxGetBoardRevision();
    ULONG Firmware = MailboxGetFirmwareRevision();
    ULONG Type = (Revision >> 4) & 0xff, Processor = (Revision >> 12) & 0xf;
    ULONG Maker = (Revision >> 16) & 0xf, Memory = 256 << ((Revision >> 20) & 7);

    memset(&ArcSystemData, 0, sizeof(ArcSystemData));
    ArcSystemData.List.Version = ARC_VERSION;
    ArcSystemData.List.Revision = ARC_REVISION;
    ArcSystemData.List.Count = 1;
    SetDescriptor(&ArcSystemData.List.PartialDescriptors[0], CmResourceTypeDeviceSpecific, 0);
    ArcSystemData.List.PartialDescriptors[0].u.DeviceSpecificData.DataSize = sizeof(*Data);

    if ((Revision & (1 << 23)) != 0 && Processor < 4) {
        sprintf(ArcBusId, "%s-Internal Bus", Processors[Processor]);
    }

    if ((Revision & (1 << 23)) != 0 && Type < sizeof(BoardNames) / sizeof(BoardNames[0]) &&
        BoardNames[Type] != NULL) {
        sprintf(ArcSystemId, "%s v1.%lu", BoardNames[Type], Revision & 0xf);
        Version += sprintf(Version, "%s", ArcSystemId) + 1;
        Version += sprintf(Version, "%s, %lu %s, %s",
                           Processor < 4 ? Processors[Processor] : "BCM",
                           Memory >= 1024 ? Memory / 1024 : Memory, Memory >= 1024 ? "GB" : "MB",
                           Maker < 6 ? Makers[Maker] : "") + 1;
    } else {
        Version += sprintf(Version, "%s", ArcSystemId) + 1;
    }
    sprintf(Version, "%s", FW_TITLE_MSG);
    FormatBuildDate(Data->SystemFirmwareDate);

    sprintf(Data->VideoFirmwareVersion, "%s firmware %08lX",
            Processor == 3 ? "VideoCore VI" : "VideoCore IV", Firmware);
    if (Firmware >= FIRMWARE_TIME_2012) {
        FormatTime(Data->VideoFirmwareDate, Firmware);
    }

    return sizeof(ArcSystemData);
}

//
// Build the SD card controller's configuration data and return its length.
//

static ULONG
MakeScsiData(
    VOID
    )
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = ArcScsiData.List.PartialDescriptors;

    ArcScsiData.List.Version = ARC_VERSION;
    ArcScsiData.List.Revision = ARC_REVISION;
    ArcScsiData.List.Count = 4;

    SetDescriptor(&Partial[0], CmResourceTypeInterrupt, CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE);
    Partial[0].u.Interrupt.Level = EMMC_LEVEL;
    Partial[0].u.Interrupt.Vector = EMMC_VECTOR;

    SetDescriptor(&Partial[1], CmResourceTypeMemory, CM_RESOURCE_MEMORY_READ_WRITE);
    Partial[1].u.Memory.Start.LowPart = EMMC_PHYSICAL_BASE;
    Partial[1].u.Memory.Length = EMMC_REGISTERS_SIZE;

    SetDescriptor(&Partial[2], CmResourceTypeMemory, CM_RESOURCE_MEMORY_READ_WRITE);
    Partial[2].u.Memory.Start.LowPart = MAILBOX_PHYSICAL_BASE;
    Partial[2].u.Memory.Length = MAILBOX_SIZE;

    SetDescriptor(&Partial[3], CmResourceTypeDeviceSpecific, 0);
    Partial[3].u.DeviceSpecificData.DataSize = sizeof(CM_SCSI_DEVICE_DATA);
    ArcScsiData.ScsiData.Version = ARC_VERSION;
    ArcScsiData.ScsiData.Revision = ARC_REVISION;
    ArcScsiData.ScsiData.HostIdentifier = SCSI_HOST_ID;

    return sizeof(CM_PARTIAL_RESOURCE_LIST) + 3 * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) +
           sizeof(CM_SCSI_DEVICE_DATA);
}

//
// Build the keyboard and pointer controllers and the keyboard and mouse
// components.
//

static VOID
MakeInputNodes(
    VOID
    )
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = ArcUsbData.List.PartialDescriptors;

    ArcUsbData.List.Version = ARC_VERSION;
    ArcUsbData.List.Revision = ARC_REVISION;
    ArcUsbData.List.Count = 3;

    SetDescriptor(&Partial[0], CmResourceTypeInterrupt, CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE);
    Partial[0].u.Interrupt.Level = USB_LEVEL;
    Partial[0].u.Interrupt.Vector = USB_VECTOR;

    SetDescriptor(&Partial[1], CmResourceTypeMemory, CM_RESOURCE_MEMORY_READ_WRITE);
    Partial[1].u.Memory.Start.LowPart = USB_PHYSICAL_BASE;
    Partial[1].u.Memory.Length = USB_REGISTERS_SIZE;

    SetDescriptor(&Partial[2], CmResourceTypeMemory, CM_RESOURCE_MEMORY_READ_WRITE);
    Partial[2].u.Memory.Start.LowPart = MAILBOX_PHYSICAL_BASE;
    Partial[2].u.Memory.Length = MAILBOX_SIZE;

    SetComponent(&ArcKeyboardControllerNode, ControllerClass, KeyboardController, 0);
    SetIdentifier(&ArcKeyboardControllerNode, ArcUsbId);
    ArcKeyboardControllerNode.ComponentEntry.Flags.ConsoleIn = 1;
    ArcKeyboardControllerNode.ComponentEntry.Flags.Input = 1;
    ArcKeyboardControllerNode.ConfigurationData = &ArcUsbData;
    ArcKeyboardControllerNode.ComponentEntry.ConfigurationDataLength =
        sizeof(CM_PARTIAL_RESOURCE_LIST) + 2 * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    SetComponent(&ArcKeyboardNode, PeripheralClass, KeyboardPeripheral, 0);
    SetIdentifier(&ArcKeyboardNode, ArcKeyboardId);
    ArcKeyboardNode.ComponentEntry.Flags.ConsoleIn = 1;
    ArcKeyboardNode.ComponentEntry.Flags.Input = 1;

    SetComponent(&ArcPointerControllerNode, ControllerClass, PointerController, 0);
    SetIdentifier(&ArcPointerControllerNode, ArcUsbId);
    ArcPointerControllerNode.ComponentEntry.Flags.Input = 1;
    ArcPointerControllerNode.ConfigurationData = &ArcUsbData;
    ArcPointerControllerNode.ComponentEntry.ConfigurationDataLength =
        ArcKeyboardControllerNode.ComponentEntry.ConfigurationDataLength;

    SetComponent(&ArcPointerNode, PeripheralClass, PointerPeripheral, 0);
    SetIdentifier(&ArcPointerNode, ArcPointerId);
    ArcPointerNode.ComponentEntry.Flags.Input = 1;
}

//
// The Ethernet controller, a function of the USB host the firmware found
// (usb.c): the network controller of the internal bus, with the interrupt and
// registers of the USB host and the station address of the board, as Jazz
// describes its SONIC.
//

static CHAR ArcNetworkId[16];

static struct {
    CM_PARTIAL_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR More[2];
    CM_BCMETH_DEVICE_DATA EthernetData;
} ArcNetworkData;

VOID
FwAddNetworkController (
    IN PCHAR Identifier
    )
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial = ArcNetworkData.List.PartialDescriptors;

    if (ArcNetworkNode.Parent != NULL) {
        return;
    }

    ArcNetworkData.List.Version = ARC_VERSION;
    ArcNetworkData.List.Revision = ARC_REVISION;
    ArcNetworkData.List.Count = 3;

    SetDescriptor(&Partial[0], CmResourceTypeInterrupt, CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE);
    Partial[0].u.Interrupt.Level = USB_LEVEL;
    Partial[0].u.Interrupt.Vector = USB_VECTOR;

    SetDescriptor(&Partial[1], CmResourceTypeMemory, CM_RESOURCE_MEMORY_READ_WRITE);
    Partial[1].u.Memory.Start.LowPart = USB_PHYSICAL_BASE;
    Partial[1].u.Memory.Length = USB_REGISTERS_SIZE;

    SetDescriptor(&Partial[2], CmResourceTypeDeviceSpecific, 0);
    Partial[2].u.DeviceSpecificData.DataSize = sizeof(CM_BCMETH_DEVICE_DATA);
    ArcNetworkData.EthernetData.Version = ARC_VERSION;
    ArcNetworkData.EthernetData.Revision = ARC_REVISION;
    MailboxGetBoardMacAddress(ArcNetworkData.EthernetData.EthernetAddress);

    strncpy(ArcNetworkId, Identifier, sizeof(ArcNetworkId) - 1);
    SetComponent(&ArcNetworkNode, ControllerClass, NetworkController, 0);
    SetIdentifier(&ArcNetworkNode, ArcNetworkId);
    ArcNetworkNode.ComponentEntry.Flags.Input = 1;
    ArcNetworkNode.ComponentEntry.Flags.Output = 1;
    ArcNetworkNode.ConfigurationData = &ArcNetworkData;
    ArcNetworkNode.ComponentEntry.ConfigurationDataLength =
        sizeof(CM_PARTIAL_RESOURCE_LIST) + 2 * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) +
        sizeof(CM_BCMETH_DEVICE_DATA);
    Link(&ArcBusNode, &ArcNetworkNode);
}

//
// Build the display controller and monitor components.
//

static VOID
MakeVideoNodes(
    VOID
    )
{
    SetComponent(&ArcVideoNode, ControllerClass, DisplayController, 0);
    SetIdentifier(&ArcVideoNode, ArcVideoId);
    ArcVideoNode.ComponentEntry.Flags.ConsoleOut = 1;
    ArcVideoNode.ComponentEntry.Flags.Output = 1;
    ArcVideoNode.ConfigurationData = &ArcVideoData;
    ArcVideoNode.ComponentEntry.ConfigurationDataLength = sizeof(ArcVideoData);
    ArcVideoData = VideoData;
    ArcVideoData.ControlBase = MAILBOX_PHYSICAL_BASE;
    ArcVideoData.ControlSize = MAILBOX_SIZE;

    SetComponent(&ArcMonitorNode, PeripheralClass, MonitorPeripheral, 0);
    sprintf(ArcMonitorId, "%dx%d", MonitorData.HorizontalResolution, MonitorData.VerticalResolution);
    SetIdentifier(&ArcMonitorNode, ArcMonitorId);
    ArcMonitorNode.ComponentEntry.Flags.ConsoleOut = 1;
    ArcMonitorNode.ComponentEntry.Flags.Output = 1;
    ArcMonitorNode.ConfigurationData = &ArcMonitorData;
    ArcMonitorNode.ComponentEntry.ConfigurationDataLength = sizeof(ArcMonitorData);
    ArcMonitorData = MonitorData;
}

//
// Read the geometry of one cache (CSSELR level/type select, CCSIDR) and encode
// it as an ARC cache component Key.
//

static ULONG
CacheKey(
    ULONG Select
    )
{
    ULONG Ccsidr, LineLog2, Ways, Sets, Size, SizeLog2;

    _MoveToCoprocessor(Select, 15, 2, 0, 0, 0);
    __isb(_ARM_BARRIER_SY);
    Ccsidr = _MoveFromCoprocessor(15, 1, 0, 0, 0);
    LineLog2 = (Ccsidr & 7) + 4;
    Ways = ((Ccsidr >> 3) & 0x3FF) + 1;
    Sets = ((Ccsidr >> 13) & 0x7FFF) + 1;
    Size = (Sets * Ways) << LineLog2;
    for (SizeLog2 = 0; (PAGE_SIZE << SizeLog2) < Size; SizeLog2 += 1) {
    }
    return (1 << 24) | (LineLog2 << 16) | SizeLog2;
}

static PCHAR
CpuIdentifier(
    VOID
    )
{
    switch ((_MoveFromCoprocessor(15, 0, 0, 0, 0) >> 4) & 0xFFF) {
    case 0xC07:
        return "ARM-CortexA7";
    case 0xD03:
        return "ARM-CortexA53";
    default:
        return "ARM";
    }
}

static VOID
FwBuildConfigurationTree(
    VOID
    )
{
    ULONG Clidr, Index;
    ULONG IcacheKey, DcacheKey, L2Key;

    ArcCpuId = CpuIdentifier();

    SetComponent(&ArcSystemNode,     SystemClass,     ArcSystem,            0);
    SetComponent(&ArcBusNode,        AdapterClass,    MultiFunctionAdapter, 0);
    SetComponent(&ArcScsiNode,       AdapterClass,    ScsiAdapter,          0);
    SetComponent(&ArcControllerNode, ControllerClass, DiskController,       0);
    SetComponent(&ArcDiskNode,       PeripheralClass, DiskPeripheral,       0);
    ArcSystemNode.ConfigurationData = &ArcSystemData;
    ArcSystemNode.ComponentEntry.ConfigurationDataLength = MakeSystemData();
    SetIdentifier(&ArcSystemNode, ArcSystemId);
    SetIdentifier(&ArcBusNode, ArcBusId);
    SetIdentifier(&ArcScsiNode, ArcScsiId);
    ArcScsiNode.ConfigurationData = &ArcScsiData;
    ArcScsiNode.ComponentEntry.ConfigurationDataLength = MakeScsiData();

    Clidr = _MoveFromCoprocessor(15, 1, 0, 0, 1);
    IcacheKey = CacheKey(1);
    DcacheKey = CacheKey(0);
    L2Key = (((Clidr >> 3) & 7) != 0) ? CacheKey(2) : 0;

    for (Index = 0; Index < ARC_PROCESSORS; Index += 1) {
        SetComponent(&ArcCpuNode[Index], ProcessorClass, CentralProcessor, Index);
        SetIdentifier(&ArcCpuNode[Index], ArcCpuId);
        SetComponent(&ArcFpuNode[Index], ProcessorClass, FloatingPointProcessor, Index);
        SetIdentifier(&ArcFpuNode[Index], ArcFpuId);
        SetComponent(&ArcIcacheNode[Index], CacheClass, PrimaryIcache, IcacheKey);
        SetComponent(&ArcDcacheNode[Index], CacheClass, PrimaryDcache, DcacheKey);
        Link(&ArcSystemNode, &ArcCpuNode[Index]);
        Link(&ArcCpuNode[Index], &ArcFpuNode[Index]);
        Link(&ArcCpuNode[Index], &ArcIcacheNode[Index]);
        Link(&ArcCpuNode[Index], &ArcDcacheNode[Index]);
        if (L2Key != 0) {
            SetComponent(&ArcL2cacheNode[Index], CacheClass, SecondaryCache, L2Key);
            Link(&ArcCpuNode[Index], &ArcL2cacheNode[Index]);
        }
    }

    Link(&ArcSystemNode, &ArcBusNode);
    if (DisplayOutput) {
        MakeVideoNodes();
        Link(&ArcBusNode, &ArcVideoNode);
        Link(&ArcVideoNode, &ArcMonitorNode);
    }
    MakeInputNodes();
    Link(&ArcBusNode, &ArcKeyboardControllerNode);
    Link(&ArcKeyboardControllerNode, &ArcKeyboardNode);
    Link(&ArcBusNode, &ArcPointerControllerNode);
    Link(&ArcPointerControllerNode, &ArcPointerNode);

    Link(&ArcSystemNode, &ArcScsiNode);
    Link(&ArcScsiNode, &ArcControllerNode);
    Link(&ArcControllerNode, &ArcDiskNode);

    ArcConfigTree = &ArcSystemNode;
}

PCONFIGURATION_COMPONENT
FwGetChild(
    IN PCONFIGURATION_COMPONENT Current
    )
{
    PCONFIGURATION_COMPONENT_DATA CurrentEntry;

    if (Current) {
        CurrentEntry = CONTAINING_RECORD(Current,
                                         CONFIGURATION_COMPONENT_DATA,
                                         ComponentEntry);
        if (CurrentEntry->Child) {
            return &CurrentEntry->Child->ComponentEntry;
        }
        return NULL;
    }

    if (ArcConfigTree) {
        return &ArcConfigTree->ComponentEntry;
    }
    return NULL;
}

PCONFIGURATION_COMPONENT
FwGetPeer(
    IN PCONFIGURATION_COMPONENT Current
    )
{
    PCONFIGURATION_COMPONENT_DATA CurrentEntry;

    if (Current) {
        CurrentEntry = CONTAINING_RECORD(Current,
                                         CONFIGURATION_COMPONENT_DATA,
                                         ComponentEntry);
        if (CurrentEntry->Sibling) {
            return &CurrentEntry->Sibling->ComponentEntry;
        }
    }
    return NULL;
}

PCONFIGURATION_COMPONENT
FwGetParent(
    IN PCONFIGURATION_COMPONENT Current
    )
{
    PCONFIGURATION_COMPONENT_DATA CurrentEntry;

    if (Current) {
        CurrentEntry = CONTAINING_RECORD(Current,
                                         CONFIGURATION_COMPONENT_DATA,
                                         ComponentEntry);
        if (CurrentEntry->Parent) {
            return &CurrentEntry->Parent->ComponentEntry;
        }
    }
    return NULL;
}

ARC_STATUS
FwGetConfigurationData(
    IN PVOID ConfigurationData,
    IN PCONFIGURATION_COMPONENT Current
    )
{
    PCONFIGURATION_COMPONENT_DATA CurrentEntry;

    if (Current) {
        CurrentEntry = CONTAINING_RECORD(Current,
                                         CONFIGURATION_COMPONENT_DATA,
                                         ComponentEntry);
        RtlMoveMemory(ConfigurationData,
                      CurrentEntry->ConfigurationData,
                      Current->ConfigurationDataLength);
        return ESUCCESS;
    }
    return EINVAL;
}

//
// The mnemonics of the component types in ARC pathnames.
//

static PCHAR MnemonicTable[] = {
    "arc", "cpu", "fpu", "pic", "pdc", "sic", "sdc", "sc", "eisa", "tc", "scsi",
    "dti", "multi", "disk", "tape", "cdrom", "worm", "serial", "net", "video",
    "par", "point", "key", "audio", "other", "rdisk", "fdisk", "tape", "modem",
    "monitor", "print", "pointer", "keyboard", "term", "other", "line",
    "netper", "memory"
};

PCONFIGURATION_COMPONENT
FwGetComponent (
    IN PCHAR Pathname
    )

/*++

Routine Description:

    This routine searches the configuration tree for the component that best
    matches the Pathname string: each component of the path is matched in
    turn among the children of the last match, and when the string is
    exhausted or no child matches, the last match is returned.

--*/

{
    PCONFIGURATION_COMPONENT Component;
    PCONFIGURATION_COMPONENT MatchComponent;
    PCHAR PathString = Pathname;
    PCHAR MatchString;
    PCHAR Token;
    ULONG Key;

    MatchComponent = FwGetChild(NULL);
    do {
        for (Component = FwGetChild(MatchComponent);
             Component != NULL;
             Component = FwGetPeer(Component)) {

            Token = PathString;
            for (MatchString = MnemonicTable[Component->Type];
                 *MatchString != 0 && *MatchString == (CHAR)tolower(*Token);
                 MatchString += 1, Token += 1) {
            }

            if (*MatchString != 0 || *Token != '(') {
                continue;
            }

            for (Key = 0, Token += 1; *Token != ')' && *Token != 0; Token += 1) {
                Key = Key * 10 + *Token - '0';
            }

            if (Component->Key == Key) {
                PathString = Token + 1;
                MatchComponent = Component;
                break;
            }
        }
    } while (Component != NULL && *PathString != 0);

    return MatchComponent;
}

//
// Components added by programs, held in the firmware's memory.
//

#define ARC_ADDED_COMPONENTS 16

static CONFIGURATION_COMPONENT_DATA ArcAddedNode[ARC_ADDED_COMPONENTS];
static UCHAR ArcAddedData[ARC_ADDED_COMPONENTS][256];
static CHAR ArcAddedIdentifier[ARC_ADDED_COMPONENTS][64];
static BOOLEAN ArcAddedInUse[ARC_ADDED_COMPONENTS];

PCONFIGURATION_COMPONENT
FwAddChild (
    IN PCONFIGURATION_COMPONENT Current,
    IN PCONFIGURATION_COMPONENT Template,
    IN PVOID ConfigurationData OPTIONAL
    )

/*++

Routine Description:

    This routine adds a new component as a child of the specified component,
    or as the root if there is none, copying its template, identifier and
    configuration data.

Return Value:

    The new component, or NULL if there is no room for it.

--*/

{
    PCONFIGURATION_COMPONENT_DATA Node;
    ULONG Index;

    if (Template->ConfigurationDataLength > sizeof(ArcAddedData[0]) ||
        Template->IdentifierLength > sizeof(ArcAddedIdentifier[0])) {
        return NULL;
    }

    for (Index = 0; Index < ARC_ADDED_COMPONENTS && ArcAddedInUse[Index]; Index += 1) {
    }

    if (Index == ARC_ADDED_COMPONENTS) {
        return NULL;
    }

    ArcAddedInUse[Index] = TRUE;
    Node = &ArcAddedNode[Index];
    memset(Node, 0, sizeof(*Node));
    Node->ComponentEntry = *Template;
    if (Template->IdentifierLength != 0) {
        memcpy(ArcAddedIdentifier[Index], Template->Identifier, Template->IdentifierLength);
        Node->ComponentEntry.Identifier = ArcAddedIdentifier[Index];
    }

    if (ARGUMENT_PRESENT(ConfigurationData) && Template->ConfigurationDataLength != 0) {
        memcpy(ArcAddedData[Index], ConfigurationData, Template->ConfigurationDataLength);
        Node->ConfigurationData = ArcAddedData[Index];
    }

    if (Current == NULL) {
        ArcConfigTree = Node;
    } else {
        Link(CONTAINING_RECORD(Current, CONFIGURATION_COMPONENT_DATA, ComponentEntry), Node);
    }

    return &Node->ComponentEntry;
}

ARC_STATUS
FwDeleteComponent (
    IN PCONFIGURATION_COMPONENT ComponentToDelete
    )

/*++

Routine Description:

    This routine deletes a component that has no children.

Return Value:

    ESUCCESS, EACCES if the component has children, or EINVAL.

--*/

{
    PCONFIGURATION_COMPONENT_DATA Node;
    PCONFIGURATION_COMPONENT_DATA *Link;

    if (ComponentToDelete == NULL) {
        return EINVAL;
    }

    Node = CONTAINING_RECORD(ComponentToDelete, CONFIGURATION_COMPONENT_DATA, ComponentEntry);
    if (Node->Child != NULL) {
        return EACCES;
    }

    if (Node->Parent == NULL) {
        ArcConfigTree = NULL;
    } else {
        for (Link = &Node->Parent->Child; *Link != Node; Link = &(*Link)->Sibling) {
        }

        *Link = Node->Sibling;
    }

    if (Node >= &ArcAddedNode[0] && Node < &ArcAddedNode[ARC_ADDED_COMPONENTS]) {
        ArcAddedInUse[Node - &ArcAddedNode[0]] = FALSE;
    }

    Node->Parent = Node->Sibling = NULL;
    return ESUCCESS;
}

ARC_STATUS
FwSaveConfiguration (
    VOID
    )

/*++

Routine Description:

    This routine stores the configuration in nonvolatile storage. The Pi has
    none, so there is never space for it.

--*/

{
    return ENOSPC;
}

VOID
FwConfigurationInitialize (
    VOID
    )

/*++

Routine Description:

    This routine builds the configuration tree and initializes the
    configuration entries of the firmware vector.

--*/

{
    FwBuildConfigurationTree();
    SYSTEM_BLOCK->FirmwareVector[GetChildRoutine] = (PVOID)FwGetChild;
    SYSTEM_BLOCK->FirmwareVector[GetPeerRoutine] = (PVOID)FwGetPeer;
    SYSTEM_BLOCK->FirmwareVector[GetParentRoutine] = (PVOID)FwGetParent;
    SYSTEM_BLOCK->FirmwareVector[GetDataRoutine] = (PVOID)FwGetConfigurationData;
    SYSTEM_BLOCK->FirmwareVector[AddChildRoutine] = (PVOID)FwAddChild;
    SYSTEM_BLOCK->FirmwareVector[DeleteComponentRoutine] = (PVOID)FwDeleteComponent;
    SYSTEM_BLOCK->FirmwareVector[GetComponentRoutine] = (PVOID)FwGetComponent;
    SYSTEM_BLOCK->FirmwareVector[SaveConfigurationRoutine] = (PVOID)FwSaveConfiguration;
}
