//
// Shared declarations for the stand-in NT kernel (kernel.c + jxdisp.c).
//
// The kernel is built freestanding with no NT header chain (unlike the loader,
// which pulls arcfw/inc/). So the few NT types and structures the ported HAL
// display code (jxdisp.c) and the entry (kernel.c) need are reproduced here,
// byte-for-byte with the originals:
//
//   - LOADER_PARAMETER_BLOCK: a faithful mirror of arcfw/inc/arc.h. ARM is ILP32 /
//     little-endian like the loader, so the layout matches; we model only the
//     fields up to the architecture union plus its ARM member (u.Arm).
//   - OEM_FONT_FILE_HEADER: copied verbatim from PRIVATE/NTOS/INC/HAL.H (a
//     "#include pshpack1.h" byte-packed struct) - the font layout the OS loader
//     hands us via OemFontFile and HalpInitializeDisplay0/HalpOutputCharacter read.
//

#ifndef _KERNEL_H_
#define _KERNEL_H_

typedef unsigned char  UCHAR;
typedef unsigned short USHORT;
typedef unsigned int   ULONG;
typedef int            BOOLEAN;
typedef void           VOID;
typedef void          *PVOID;
typedef UCHAR         *PUCHAR;
typedef ULONG         *PULONG;

#define NULL  ((void *)0)
#define TRUE  1
#define FALSE 0

// NT SAL-style parameter annotations (no-ops), kept so the ported HAL signatures
// in jxdisp.c read verbatim against NTHALS/.../JXDISP.C.
#define IN
#define OUT

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

//
// Mirror of arc.h LOADER_PARAMETER_BLOCK. KernelStack is at offset 24 (three 8-byte
// LIST_ENTRYs precede it) - the same offset start.S loads sp from. The architecture
// union is the last member; we model its ARM arm (ARM_LOADER_BLOCK), which the
// firmware emulator fills with the VideoCore framebuffer geometry.
//
typedef struct _LOADER_PARAMETER_BLOCK {
    LIST_ENTRY LoadOrderListHead;
    LIST_ENTRY MemoryDescriptorListHead;
    LIST_ENTRY BootDriverListHead;
    ULONG  KernelStack;
    ULONG  Prcb;
    ULONG  Process;
    ULONG  Thread;
    ULONG  RegistryLength;
    PVOID  RegistryBase;
    PVOID  ConfigurationRoot;
    PUCHAR ArcBootDeviceName;
    PUCHAR ArcHalDeviceName;
    PUCHAR NtBootPathName;
    PUCHAR NtHalPathName;
    PUCHAR LoadOptions;
    PVOID  NlsData;
    PVOID  ArcDiskInformation;
    PVOID  OemFontFile;
    PVOID  SetupLoaderBlock;
    ULONG  Spare1;
    struct {                        // union u; modeled as its ARM member u.Arm
        ULONG Reserved;
        ULONG FrameBuffer;          // ARM-physical base of the 32-bpp framebuffer
        ULONG FrameBufferWidth;     // visible width in pixels
        ULONG FrameBufferHeight;    // visible height in pixels
        ULONG FrameBufferPitch;     // bytes per scanline (>= width*4)
        ULONG FrameBufferPixelOrder; // fb.c PIXORDER_RGB(1) / PIXORDER_BGR(0)
    } Arm;
} LOADER_PARAMETER_BLOCK, *PLOADER_PARAMETER_BLOCK;

//
// Copied verbatim from PRIVATE/NTOS/INC/HAL.H (between pshpack1.h / poppack.h).
//
typedef struct _OEM_FONT_FILE_HEADER {
    USHORT Version;
    ULONG  FileSize;
    UCHAR  Copyright[60];
    USHORT Type;
    USHORT Points;
    USHORT VerticleResolution;
    USHORT HorizontalResolution;
    USHORT Ascent;
    USHORT InternalLeading;
    USHORT ExternalLeading;
    UCHAR  Italic;
    UCHAR  Underline;
    UCHAR  StrikeOut;
    USHORT Weight;
    UCHAR  CharacterSet;
    USHORT PixelWidth;
    USHORT PixelHeight;
    UCHAR  Family;
    USHORT AverageWidth;
    USHORT MaximumWidth;
    UCHAR  FirstCharacter;
    UCHAR  LastCharacter;
    UCHAR  DefaultCharacter;
    UCHAR  BreakCharacter;
    USHORT WidthInBytes;
    ULONG  Device;
    ULONG  Face;
    ULONG  BitsPointer;
    ULONG  BitsOffset;
    UCHAR  Filler;
    struct {
        USHORT Width;
        USHORT Offset;
    } Map[1];
} __attribute__((packed)) OEM_FONT_FILE_HEADER, *POEM_FONT_FILE_HEADER;

// jxdisp.c - ported NT HAL display (NTHALS/.../JXDISP.C).
BOOLEAN HalpInitializeDisplay0(PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID    HalDisplayString(PUCHAR String);

#endif // _KERNEL_H_
