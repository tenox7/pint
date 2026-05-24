//
// arcdos_compat.h - the ADK ARC/vendor/libc surface arcdos.c needs, hand-written
// for the ARM loader port. arcdos.c (arcfw/ported/arcdos.c) is compiled against
// THIS header alone - never the loader's NT headers (arcfw/inc/) - because the
// ADK names (Open/Read/Print, OPEN_MODE OpenReadOnly...) collide with the NT ones
// (ArcOpen macros, OPEN_MODE ArcOpenReadOnly...). The two are layout/value
// compatible, so the bridge in arcfw/arm/arcdos_rt.c - which DOES include the NT
// headers - implements these by forwarding to the emulated ARC firmware vector.
//
// Declares exactly what arcdos.c references (verified by grep), no more. Mirrors
// arc/adk/adk/include/{arc.h,vendor.h,errno.h,types.h}.
//
#ifndef _ARCDOS_COMPAT_H_
#define _ARCDOS_COMPAT_H_

// ---- basic types (arc/adk/adk/include/types.h) -----------------------------

#ifndef IN
#define IN
#define OUT
#endif
#ifndef NULL
#define NULL 0
#endif
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#ifndef VOID
#define VOID void
#endif

typedef signed char        CHAR;
typedef unsigned char      UCHAR;
typedef signed short       SHORT;
typedef unsigned short     USHORT;
typedef signed long        LONG;
typedef unsigned long      ULONG;
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;
typedef UCHAR              BOOLEAN;
typedef unsigned short     WCHAR;

typedef void  *PVOID;
typedef CHAR  *PCHAR;
typedef UCHAR *PUCHAR;
typedef LONG  *PLONG;
typedef ULONG *PULONG;

typedef union {
    struct { ULONG LowPart; LONG HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef LONG ARC_STATUS;

#ifndef _SIZE_T_DEFINED
typedef unsigned int size_t;
#define _SIZE_T_DEFINED
#endif

// ---- ARC status codes (arc/adk/adk/include/errno.h) ------------------------
// Numeric values match the loader's arccodes.h enum order, so codes returned by
// the firmware vector reach arcdos unchanged.

#define ESUCCESS     0
#define EACCES       2
#define EAGAIN       3
#define EBADF        4
#define EINVAL       7
#define EIO          8
#define ENODEV      13
#define ENOENT      14
#define ENOTDIR     18
#define ENOTTY      19

// ---- ARC I/O contract (arc/adk/adk/include/arc.h) --------------------------

#define StandardIn   0
#define StandardOut  1

#define ReadOnlyFile   1
#define HiddenFile     2
#define SystemFile     4
#define ArchiveFile    8
#define DirectoryFile 16
#define DeleteFile    32

typedef enum {
    OpenReadOnly,
    OpenWriteOnly,
    OpenReadWrite,
    CreateWriteOnly,
    CreateReadWrite,
    SupersedeWriteOnly,
    SupersedeReadWrite,
    OpenDirectory,
    CreateDirectory,
    OpenMaximumMode
} OPEN_MODE;

typedef struct {
    LARGE_INTEGER StartingAddress;
    LARGE_INTEGER EndingAddress;
    LARGE_INTEGER CurrentAddress;
    ULONG Type;                 // CONFIGURATION_TYPE; unused by arcdos
    ULONG FileNameLength;
    UCHAR Attributes;
    CHAR FileName[32];
} FILE_INFORMATION;

typedef struct {
    ULONG FileNameLength;
    UCHAR FileAttribute;
    CHAR FileName[32];
} DIRECTORY_ENTRY;

typedef struct {
    USHORT CursorXPosition;
    USHORT CursorYPosition;
    USHORT CursorMaxXPosition;
    USHORT CursorMaxYPosition;
    UCHAR ForegroundColor;
    UCHAR BackgroundColor;
    BOOLEAN HighIntensity;
    BOOLEAN Underscored;
    BOOLEAN ReverseVideo;
} ARC_DISPLAY_STATUS;

ARC_STATUS Open(const CHAR *Path, OPEN_MODE OpenMode, PULONG FileId);
ARC_STATUS Close(ULONG FileId);
ARC_STATUS Read(ULONG FileId, PVOID Buffer, ULONG N, PULONG Count);
ARC_STATUS GetReadStatus(ULONG FileId);
ARC_STATUS Write(ULONG FileId, PVOID Buffer, ULONG N, PULONG Count);
ARC_STATUS GetFileInformation(ULONG FileId, FILE_INFORMATION *Information);
ARC_STATUS SetFileInformation(ULONG FileId, ULONG AttributeFlags, ULONG AttributeMask);
ARC_STATUS GetDirectoryEntry(ULONG FileId, DIRECTORY_ENTRY *Buffer, ULONG Length, PULONG Count);
PCHAR GetEnvironmentVariable(PCHAR Name);
ARC_DISPLAY_STATUS *GetDisplayStatus(ULONG FileId);

// ---- vendor services (arc/adk/adk/include/vendor.h) ------------------------

#define ASCII_NUL 0x00
#define ASCII_BEL 0x07
#define ASCII_BS  0x08
#define ASCII_HT  0x09
#define ASCII_LF  0x0A
#define ASCII_CR  0x0D
#define ASCII_ESC 0x1B
#define ASCII_CSI 0x9B

ULONG Print(char *Format, ...);
void StallExecution(ULONG Microseconds);

#define VenSetScreenAttributes(HighIntensity, Underscored, ReverseVideo) \
    Print("%c0m", ASCII_CSI); \
    if (HighIntensity) { Print("%c1m", ASCII_CSI); } \
    if (Underscored)   { Print("%c4m", ASCII_CSI); } \
    if (ReverseVideo)  { Print("%c7m", ASCII_CSI); }

#define VenSetPosition(Row, Column) \
    Print("%c%d;", ASCII_CSI, Row); \
    Print("%dH", Column)

// ---- libc subset (arc/adk/adk/include/{stdio,string}.h) --------------------

int printf(const char *Format, ...);

size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
int strcmp(const char *a, const char *b);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

#endif // _ARCDOS_COMPAT_H_
