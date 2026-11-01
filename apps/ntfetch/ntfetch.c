/*
 * ntfetch.c - neofetch for Windows NT, as one small window in the style of
 * the NT 4.0 winmsd: OS, version, build, service pack, system, BIOS, screen
 * resolution, CPU type, number of CPUs with a chip icon for each, and memory.
 *
 * Runs on every Windows NT from 3.1 to 11. Anything newer than NT 3.1 is
 * looked up at run time. ntfetch --text prints the same to standard output.
 */

#include <windows.h>
#include "resource.h"

/* Newer than the NT 3.1 SDK headers */
#ifndef VREFRESH
#define VREFRESH 116
#endif
#ifndef DESKTOPVERTRES
#define DESKTOPVERTRES 117
#endif
#ifndef DESKTOPHORZRES
#define DESKTOPHORZRES 118
#endif
#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif
#ifndef ATTACH_PARENT_PROCESS
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
#endif
#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS 0xFFFF
#endif

#define TEXT_SIZE 256
#define REG_SIZE 1024
#define MAX_FIELDS 9

/* Window layout, in pixels */
#define MARGIN 12
#define ICON_SIZE 32
#define ICON_CELL 36
#define ICON_COLUMNS 4
#define ICON_GAP 12
#define LABEL_GAP 8
#define LINE_GAP 3
#define TITLE_GAP 8
#define BUTTON_WIDTH 75
#define BUTTON_HEIGHT 23

#define KEY_VERSION "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
#define KEY_PRODUCT "SYSTEM\\CurrentControlSet\\Control\\ProductOptions"
#define KEY_SYSTEM "HARDWARE\\DESCRIPTION\\System"
#define KEY_BIOS "HARDWARE\\DESCRIPTION\\System\\BIOS"
#define KEY_CPU "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"

/* CPU architectures, numbered as PROCESSOR_ARCHITECTURE_* */
#define ARCH_X86 0
#define ARCH_MIPS 1
#define ARCH_ALPHA 2
#define ARCH_PPC 3
#define ARCH_ARM 5
#define ARCH_IA64 6
#define ARCH_ALPHA64 7
#define ARCH_X64 9
#define ARCH_ARM64 12
#define ARCH_UNKNOWN 0xFFFF

/* Where --text goes */
#define OUTPUT_INHERITED 0
#define OUTPUT_ATTACHED 1
#define OUTPUT_NEW_CONSOLE 2

typedef struct {
    char *label;
    char value[TEXT_SIZE];
} FIELD;

typedef struct {
    DWORD major;
    DWORD minor;
    DWORD build;
    char servicePack[TEXT_SIZE];
} OSDATA;

/* OSVERSIONINFOA and RTL_OSVERSIONINFOW */
typedef struct {
    DWORD size;
    DWORD major;
    DWORD minor;
    DWORD build;
    DWORD platform;
    char servicePack[128];
} OSINFOA;

typedef struct {
    DWORD size;
    DWORD major;
    DWORD minor;
    DWORD build;
    DWORD platform;
    WCHAR servicePack[128];
} OSINFOW;

/* MEMORYSTATUSEX, with each 64-bit count as two halves: cl386 has no __int64 */
typedef struct {
    DWORD size;
    DWORD load;
    DWORD totalPhysLow;
    DWORD totalPhysHigh;
    DWORD unused[12];
} MEMINFOEX;

typedef LONG (WINAPI *FN_RTLGETVERSION)(OSINFOW *);
typedef BOOL (WINAPI *FN_GETVERSIONEXA)(OSINFOA *);
typedef void (WINAPI *FN_GETNATIVESYSTEMINFO)(SYSTEM_INFO *);
typedef BOOL (WINAPI *FN_ISWOW64PROCESS)(HANDLE, BOOL *);
typedef BOOL (WINAPI *FN_ISWOW64PROCESS2)(HANDLE, USHORT *, USHORT *);
typedef DWORD (WINAPI *FN_GETACTIVEPROCESSORCOUNT)(WORD);
typedef BOOL (WINAPI *FN_GLOBALMEMORYSTATUSEX)(MEMINFOEX *);
typedef BOOL (WINAPI *FN_ATTACHCONSOLE)(DWORD);

static char title[TEXT_SIZE];
static FIELD fields[MAX_FIELDS];
static int fieldCount;
static int cpuArch;
static int cpuCount;
static int cpuIconId;
static DWORD regView;
static BOOL consoleOutput;

/* Registry values land here. The DWORD keeps it aligned for the RISC CPUs. */
static union {
    DWORD number;
    char text[REG_SIZE];
} regData;

static HFONT normalFont;
static HFONT boldFont;
static HBRUSH faceBrush;
static HICON cpuIcon;
static HWND okButton;
static int iconCount;
static int contentWidth;
static int labelWidth;
static int lineHeight;
static int titleTop;
static int fieldsTop;

/* Copy src to dst, cut to fit size bytes with the terminator */
static void copyText(char *dst, char *src, int size)
{
    int i;

    for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void appendText(char *dst, char *src, int size)
{
    int length;

    length = lstrlen(dst);
    copyText(dst + length, src, size - length);
}

/* Append src after a space, unless one of them is empty */
static void appendWord(char *dst, char *src, int size)
{
    if (src[0] == '\0') {
        return;
    }
    if (dst[0] != '\0') {
        appendText(dst, " ", size);
    }
    appendText(dst, src, size);
}

/* Append " (note)" */
static void appendNote(char *dst, char *note, int size)
{
    appendText(dst, " (", size);
    appendText(dst, note, size);
    appendText(dst, ")", size);
}

/* Turn control characters and runs of blanks into one space, trim both ends */
static void tidyText(char *text)
{
    char *src;
    char *dst;
    BOOL blank;

    blank = TRUE;
    dst = text;
    for (src = text; *src != '\0'; src++) {
        if ((unsigned char)*src > ' ') {
            *dst++ = *src;
            blank = FALSE;
            continue;
        }
        if (!blank) {
            *dst++ = ' ';
            blank = TRUE;
        }
    }
    if (dst > text && dst[-1] == ' ') {
        dst--;
    }
    *dst = '\0';
}

static char lowerChar(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/* Case-insensitive prefix test */
static BOOL startsWith(char *text, char *prefix)
{
    for (; *prefix != '\0'; text++, prefix++) {
        if (lowerChar(*text) != lowerChar(*prefix)) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL containsText(char *text, char *word)
{
    for (; *text != '\0'; text++) {
        if (startsWith(text, word)) {
            return TRUE;
        }
    }
    return FALSE;
}

static DWORD parseNumber(char *text)
{
    DWORD number;

    number = 0;
    for (; *text >= '0' && *text <= '9'; text++) {
        number = number * 10 + (DWORD)(*text - '0');
    }
    return number;
}

/* A function that this Windows may not have, or NULL */
static FARPROC getProc(char *module, char *name)
{
    HMODULE handle;

    handle = GetModuleHandle(module);
    if (handle == NULL) {
        return NULL;
    }
    return GetProcAddress(handle, name);
}

/* Read a HKEY_LOCAL_MACHINE value into regData. Returns its type, REG_NONE when missing. */
static DWORD readRegValue(char *path, char *name, DWORD *size)
{
    HKEY key;
    DWORD type;
    LONG result;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | regView, &key) != ERROR_SUCCESS) {
        return REG_NONE;
    }
    *size = REG_SIZE - 2;
    result = RegQueryValueEx(key, name, NULL, &type, (LPBYTE)regData.text, size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || *size > REG_SIZE - 2) {
        return REG_NONE;
    }
    regData.text[*size] = '\0';
    regData.text[*size + 1] = '\0';
    return type;
}

/* Read a string value into out, TEXT_SIZE bytes. REG_MULTI_SZ parts are joined with spaces. */
static BOOL readRegString(char *path, char *name, char *out)
{
    DWORD size;
    DWORD i;

    out[0] = '\0';
    switch (readRegValue(path, name, &size)) {
    case REG_SZ:
    case REG_EXPAND_SZ:
        break;
    case REG_MULTI_SZ:
        for (i = 0; i < size; i++) {
            if (regData.text[i] != '\0') {
                continue;
            }
            if (regData.text[i + 1] == '\0') {
                break;
            }
            regData.text[i] = ' ';
        }
        break;
    default:
        return FALSE;
    }
    copyText(out, regData.text, TEXT_SIZE);
    tidyText(out);
    return out[0] != '\0';
}

static BOOL readRegNumber(char *path, char *name, DWORD *number)
{
    DWORD size;

    if (readRegValue(path, name, &size) != REG_DWORD || size != sizeof(DWORD)) {
        return FALSE;
    }
    *number = regData.number;
    return TRUE;
}

/* Append a string value after a space, if there is one */
static void appendRegString(char *out, char *path, char *name)
{
    char part[TEXT_SIZE];

    if (readRegString(path, name, part)) {
        appendWord(out, part, TEXT_SIZE);
    }
}

/* A 32-bit ntfetch on 64-bit Windows reads the 64-bit registry, not Wow6432Node */
static void detectRegView(void)
{
    FN_ISWOW64PROCESS isWow64Process;
    BOOL wow64;

    isWow64Process = (FN_ISWOW64PROCESS)getProc("kernel32.dll", "IsWow64Process");
    if (isWow64Process != NULL && isWow64Process(GetCurrentProcess(), &wow64) && wow64) {
        regView = KEY_WOW64_64KEY;
    }
}

/* NT 3.1 has no GetVersionEx: the build and service pack come from the registry */
static void getRegistryVersion(OSDATA *os)
{
    char text[TEXT_SIZE];
    char *build;
    DWORD csd;

    if (readRegString(KEY_VERSION, "CurrentBuildNumber", text) || readRegString(KEY_VERSION, "CurrentBuild", text)) {
        build = text;
        if (build[0] == '1' && build[1] == '.') {
            build += 2;     /* CurrentBuild of NT 3.1 reads 1.511.1 */
        }
        os->build = parseNumber(build);
    }
    if (readRegString(KEY_VERSION, "CSDVersion", os->servicePack)) {
        return;
    }
    /* before NT 3.5 the service pack was a number, 0x300 for Service Pack 3 */
    if (!readRegNumber(KEY_VERSION, "CSDVersion", &csd) && !readRegNumber(KEY_PRODUCT, "CSDVersion", &csd)) {
        return;
    }
    if ((csd & 0xFF00) != 0) {
        wsprintf(os->servicePack, "Service Pack %lu", (csd >> 8) & 0xFF);
    }
}

/* RtlGetVersion is not fooled by the Windows 8.1 compatibility shims, GetVersionEx is */
static void getOsVersion(OSDATA *os)
{
    FN_RTLGETVERSION rtlGetVersion;
    FN_GETVERSIONEXA getVersionExA;
    OSINFOW infoW;
    OSINFOA infoA;
    DWORD version;

    os->servicePack[0] = '\0';
    rtlGetVersion = (FN_RTLGETVERSION)getProc("ntdll.dll", "RtlGetVersion");
    infoW.size = sizeof(infoW);
    if (rtlGetVersion != NULL && rtlGetVersion(&infoW) == 0) {
        os->major = infoW.major;
        os->minor = infoW.minor;
        os->build = infoW.build;
        if (WideCharToMultiByte(CP_ACP, 0, infoW.servicePack, -1, os->servicePack, TEXT_SIZE, NULL, NULL) == 0) {
            os->servicePack[0] = '\0';
        }
        return;
    }
    getVersionExA = (FN_GETVERSIONEXA)getProc("kernel32.dll", "GetVersionExA");
    infoA.size = sizeof(infoA);
    if (getVersionExA != NULL && getVersionExA(&infoA)) {
        os->major = infoA.major;
        os->minor = infoA.minor;
        os->build = infoA.build;
        copyText(os->servicePack, infoA.servicePack, TEXT_SIZE);
        return;
    }
    version = GetVersion();
    os->major = LOBYTE(LOWORD(version));
    os->minor = HIBYTE(LOWORD(version));
    os->build = HIWORD(version) & 0x7FFF;
    getRegistryVersion(os);
}

/* 3.10 is NT 3.1 and 3.50 is NT 3.5 */
static void formatVersion(OSDATA *os, char *out)
{
    DWORD minor;

    minor = os->minor;
    if (os->major == 3 && minor % 10 == 0) {
        minor /= 10;
    }
    wsprintf(out, "%lu.%lu", os->major, minor);
}

static void getOsName(OSDATA *os, char *out)
{
    char type[TEXT_SIZE];
    char number[TEXT_SIZE];
    BOOL server;

    if (readRegString(KEY_VERSION, "ProductName", out)) {
        /* Windows 11 still calls itself Windows 10 here */
        if (os->build >= 22000 && startsWith(out, "Windows 10") && (out[10] == '\0' || out[10] == ' ')) {
            out[9] = '1';
        }
        return;
    }
    /* NT 3.x and 4.0 have no ProductName */
    formatVersion(os, number);
    readRegString(KEY_PRODUCT, "ProductType", type);
    server = type[0] != '\0' && lstrcmpi(type, "WinNT") != 0;
    if (os->major == 3 && os->minor == 10) {
        wsprintf(out, server ? "Windows NT Advanced Server %s" : "Windows NT %s", number);
        return;
    }
    if (type[0] == '\0') {
        wsprintf(out, "Windows NT %s", number);
        return;
    }
    wsprintf(out, server ? "Windows NT Server %s" : "Windows NT Workstation %s", number);
}

/* Windows 10 and 11 add the feature update: 10.0 (22H2) */
static void getVersionText(OSDATA *os, char *out)
{
    char release[TEXT_SIZE];

    formatVersion(os, out);
    if (readRegString(KEY_VERSION, "DisplayVersion", release) || readRegString(KEY_VERSION, "ReleaseId", release)) {
        appendNote(out, release, TEXT_SIZE);
    }
}

/* 1381 (Uniprocessor Free), or 22631.4317 (Multiprocessor Free) with the update revision */
static void getBuildText(OSDATA *os, char *out)
{
    char type[TEXT_SIZE];
    DWORD revision;

    wsprintf(out, "%lu", os->build);
    if (readRegNumber(KEY_VERSION, "UBR", &revision)) {
        wsprintf(out + lstrlen(out), ".%lu", revision);
    }
    if (readRegString(KEY_VERSION, "CurrentType", type)) {
        appendNote(out, type, TEXT_SIZE);
    }
}

/* What board makers leave in an unset system name */
static BOOL isPlaceholder(char *text)
{
    return text[0] == '\0' || startsWith(text, "System Product Name") || startsWith(text, "To be filled") || startsWith(text, "Default string");
}

/* The maker and model on Vista and later, else the firmware's name for the machine */
static void getSystemText(char *out)
{
    char model[TEXT_SIZE];

    readRegString(KEY_BIOS, "SystemManufacturer", out);
    readRegString(KEY_BIOS, "SystemProductName", model);
    if (isPlaceholder(model)) {
        readRegString(KEY_BIOS, "BaseBoardManufacturer", out);
        readRegString(KEY_BIOS, "BaseBoardProduct", model);
    }
    appendWord(out, model, TEXT_SIZE);
    if (out[0] != '\0') {
        return;
    }
    if (!readRegString(KEY_SYSTEM, "Identifier", out)) {
        copyText(out, "Not available", TEXT_SIZE);
    }
}

/* Vista and later have the BIOS key, older x86 NT the SystemBios values; ARC firmware neither */
static void getBiosText(char *out)
{
    out[0] = '\0';
    appendRegString(out, KEY_BIOS, "BIOSVendor");
    appendRegString(out, KEY_BIOS, "BIOSVersion");
    appendRegString(out, KEY_BIOS, "BIOSReleaseDate");
    if (out[0] != '\0') {
        return;
    }
    appendRegString(out, KEY_SYSTEM, "SystemBiosVersion");
    appendRegString(out, KEY_SYSTEM, "SystemBiosDate");
    if (out[0] == '\0') {
        copyText(out, "Not available", TEXT_SIZE);
    }
}

static void getResolution(OSDATA *os, char *out)
{
    HDC dc;
    int width;
    int height;
    int bits;
    int refresh;

    dc = GetDC(NULL);
    if (dc == NULL) {
        copyText(out, "Not available", TEXT_SIZE);
        return;
    }
    width = 0;
    height = 0;
    refresh = 0;
    if (os->major >= 6) {
        /* the real size: Vista and later scale it for programs that are not DPI aware */
        width = GetDeviceCaps(dc, DESKTOPHORZRES);
        height = GetDeviceCaps(dc, DESKTOPVERTRES);
    }
    if (width <= 0 || height <= 0) {
        width = GetDeviceCaps(dc, HORZRES);
        height = GetDeviceCaps(dc, VERTRES);
    }
    bits = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
    if (os->major >= 4) {
        refresh = GetDeviceCaps(dc, VREFRESH);
    }
    ReleaseDC(NULL, dc);
    wsprintf(out, "%d x %d", width, height);
    if (bits > 0 && bits <= 8) {
        wsprintf(out + lstrlen(out), ", %lu colors", (DWORD)1 << bits);
    }
    if (bits > 8) {
        wsprintf(out + lstrlen(out), ", %d-bit color", bits);
    }
    if (refresh > 1) {
        wsprintf(out + lstrlen(out), ", %d Hz", refresh);
    }
}

static char *archName(int arch)
{
    switch (arch) {
    case ARCH_X86:
        return "x86";
    case ARCH_MIPS:
        return "MIPS";
    case ARCH_ALPHA:
        return "Alpha AXP";
    case ARCH_PPC:
        return "PowerPC";
    case ARCH_ARM:
        return "ARM";
    case ARCH_IA64:
        return "Itanium";
    case ARCH_ALPHA64:
        return "Alpha AXP64";
    case ARCH_X64:
        return "x64";
    case ARCH_ARM64:
        return "ARM64";
    }
    return "unknown";
}

static int archIcon(int arch)
{
    switch (arch) {
    case ARCH_X86:
        return IDI_X86;
    case ARCH_MIPS:
        return IDI_MIPS;
    case ARCH_ALPHA:
        return IDI_AXP;
    case ARCH_PPC:
        return IDI_PPC;
    case ARCH_ARM:
        return IDI_ARM;
    case ARCH_IA64:
        return IDI_IA64;
    case ARCH_ALPHA64:
        return IDI_AXP64;
    case ARCH_X64:
        return IDI_X64;
    case ARCH_ARM64:
        return IDI_ARM64;
    }
    return IDI_NTFETCH;     /* the question mark chip */
}

/* IMAGE_FILE_MACHINE_* to an architecture */
static int machineArch(USHORT machine)
{
    switch (machine) {
    case 0x014C:
        return ARCH_X86;
    case 0x0162:
    case 0x0166:
    case 0x0168:
        return ARCH_MIPS;
    case 0x0184:
        return ARCH_ALPHA;
    case 0x01F0:
    case 0x01F1:
        return ARCH_PPC;
    case 0x01C0:
    case 0x01C2:
    case 0x01C4:
        return ARCH_ARM;
    case 0x0200:
        return ARCH_IA64;
    case 0x0284:
        return ARCH_ALPHA64;
    case 0x8664:
        return ARCH_X64;
    case 0xAA64:
        return ARCH_ARM64;
    }
    return ARCH_UNKNOWN;
}

/* NT 3.1 has no wProcessorArchitecture, it reads 0 on every CPU: go by the CPU type */
static int typeArch(DWORD type)
{
    switch (type) {
    case 2000:
    case 3000:
    case 4000:
        return ARCH_MIPS;
    case 21064:
    case 21066:
    case 21164:
        return ARCH_ALPHA;
    case 601:
    case 603:
    case 604:
    case 620:
        return ARCH_PPC;
    case 0xC07:     /* ARM Cortex-A7, A53 and A72: NT 3.5 on the Raspberry Pi (PINT) */
    case 0xD03:
    case 0xD08:
        return ARCH_ARM;
    }
    return ARCH_X86;
}

/* The real CPU, also when x64 or ARM64 Windows runs this x86 program */
static int getCpuArch(SYSTEM_INFO *system)
{
    FN_ISWOW64PROCESS2 isWow64Process2;
    FN_GETNATIVESYSTEMINFO getNativeSystemInfo;
    SYSTEM_INFO native;
    USHORT processMachine;
    USHORT nativeMachine;
    int arch;

    isWow64Process2 = (FN_ISWOW64PROCESS2)getProc("kernel32.dll", "IsWow64Process2");
    if (isWow64Process2 != NULL && isWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
        arch = machineArch(nativeMachine);
        if (arch != ARCH_UNKNOWN) {
            return arch;
        }
    }
    getNativeSystemInfo = (FN_GETNATIVESYSTEMINFO)getProc("kernel32.dll", "GetNativeSystemInfo");
    if (getNativeSystemInfo != NULL) {
        getNativeSystemInfo(&native);
        return LOWORD(native.dwOemId);
    }
    arch = LOWORD(system->dwOemId);
    if (arch != ARCH_X86) {
        return arch;
    }
    return typeArch(system->dwProcessorType);
}

/* The chip icon: the MIPS R2000 and R3000 have their own, other CPUs go by architecture */
static int getCpuIconId(SYSTEM_INFO *system)
{
    if (cpuArch == ARCH_MIPS && (system->dwProcessorType == 2000 || system->dwProcessorType == 3000)) {
        return IDI_R3000;
    }
    return archIcon(cpuArch);
}

/* GetSystemInfo counts one processor group: 64 CPUs at most, 32 in a 32-bit program */
static int getCpuCount(SYSTEM_INFO *system)
{
    FN_GETACTIVEPROCESSORCOUNT getActiveProcessorCount;
    DWORD count;

    getActiveProcessorCount = (FN_GETACTIVEPROCESSORCOUNT)getProc("kernel32.dll", "GetActiveProcessorCount");
    if (getActiveProcessorCount != NULL) {
        count = getActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (count > 0) {
            return (int)count;
        }
    }
    if (system->dwNumberOfProcessors > 0) {
        return (int)system->dwNumberOfProcessors;
    }
    return 1;
}

/* The count, the name of CPU 0 and its clock unless the name has one: 4 x MIPS-R4400 @ 150 MHz */
static void getCpuText(SYSTEM_INFO *system, char *out)
{
    char name[TEXT_SIZE];
    char speed[TEXT_SIZE];
    DWORD mhz;

    if (!readRegString(KEY_CPU, "ProcessorNameString", name)) {
        readRegString(KEY_CPU, "Identifier", name);
        appendRegString(name, KEY_CPU, "VendorIdentifier");
    }
    if (name[0] == '\0') {
        wsprintf(name, "%s %lu", archName(cpuArch), system->dwProcessorType);
    }
    wsprintf(out, "%d x ", cpuCount);
    appendText(out, name, TEXT_SIZE);
    if (!readRegNumber(KEY_CPU, "~MHz", &mhz) || mhz == 0 || containsText(name, "Hz")) {
        return;
    }
    wsprintf(speed, "@ %lu MHz", mhz);
    appendWord(out, speed, TEXT_SIZE);
}

/* GlobalMemoryStatus stops at 4 GB, GlobalMemoryStatusEx is Windows 2000 and later */
static void getMemoryText(char *out)
{
    FN_GLOBALMEMORYSTATUSEX globalMemoryStatusEx;
    MEMINFOEX infoEx;
    MEMORYSTATUS info;
    DWORD megabytes;

    globalMemoryStatusEx = (FN_GLOBALMEMORYSTATUSEX)getProc("kernel32.dll", "GlobalMemoryStatusEx");
    infoEx.size = sizeof(infoEx);
    if (globalMemoryStatusEx != NULL && globalMemoryStatusEx(&infoEx)) {
        megabytes = (infoEx.totalPhysHigh << 12) | (infoEx.totalPhysLow >> 20);
    } else {
        info.dwLength = sizeof(info);
        GlobalMemoryStatus(&info);
        megabytes = (DWORD)(info.dwTotalPhys >> 20);
    }
    wsprintf(out, "%lu MB", megabytes);
}

/* user@HOST, like the first line of neofetch */
static void getTitle(char *out)
{
    char user[TEXT_SIZE];
    char host[TEXT_SIZE];
    DWORD size;

    size = sizeof(user);
    if (!GetUserName(user, &size)) {
        user[0] = '\0';
    }
    size = sizeof(host);
    if (!GetComputerName(host, &size)) {
        host[0] = '\0';
    }
    copyText(out, user, TEXT_SIZE);
    if (user[0] != '\0' && host[0] != '\0') {
        appendText(out, "@", TEXT_SIZE);
    }
    appendText(out, host, TEXT_SIZE);
    if (out[0] == '\0') {
        copyText(out, "ntfetch", TEXT_SIZE);
    }
}

/* The next field's value buffer */
static char *addField(char *label)
{
    FIELD *field;

    if (fieldCount == MAX_FIELDS) {
        fieldCount--;   /* never: MAX_FIELDS has a slot for each field */
    }
    field = &fields[fieldCount++];
    field->label = label;
    field->value[0] = '\0';
    return field->value;
}

static void collectInfo(void)
{
    OSDATA os;
    SYSTEM_INFO system;

    detectRegView();
    getOsVersion(&os);
    tidyText(os.servicePack);
    GetSystemInfo(&system);
    cpuArch = getCpuArch(&system);
    cpuCount = getCpuCount(&system);
    cpuIconId = getCpuIconId(&system);
    getTitle(title);
    getOsName(&os, addField("OS:"));
    getVersionText(&os, addField("Version:"));
    getBuildText(&os, addField("Build:"));
    copyText(addField("Service Pack:"), os.servicePack[0] != '\0' ? os.servicePack : "None", TEXT_SIZE);
    getSystemText(addField("System:"));
    getBiosText(addField("BIOS:"));
    getResolution(&os, addField("Resolution:"));
    getCpuText(&system, addField("CPUs:"));
    getMemoryText(addField("Memory:"));
}

/* Write to the --text output; a console shows the OEM code page */
static void writeText(HANDLE output, char *text)
{
    char oem[TEXT_SIZE * 2];
    DWORD length;
    DWORD written;

    length = lstrlen(text);
    if (length >= sizeof(oem)) {
        length = sizeof(oem) - 1;
    }
    if (consoleOutput) {
        CharToOemBuff(text, oem, length);
        text = oem;
    }
    WriteFile(output, text, length, &written, NULL);
}

static void writeLine(HANDLE output, char *text)
{
    writeText(output, text);
    writeText(output, "\r\n");
}

/* A GUI program has no console: use the redirected output, the parent's console, or a new one */
static HANDLE openOutput(int *mode)
{
    FN_ATTACHCONSOLE attachConsole;
    HANDLE output;

    output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != NULL && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN) {
        *mode = OUTPUT_INHERITED;
        return output;
    }
    attachConsole = (FN_ATTACHCONSOLE)getProc("kernel32.dll", "AttachConsole");
    if (attachConsole != NULL && attachConsole(ATTACH_PARENT_PROCESS)) {
        *mode = OUTPUT_ATTACHED;
    } else if (AllocConsole()) {
        *mode = OUTPUT_NEW_CONSOLE;
    } else {
        return INVALID_HANDLE_VALUE;
    }
    return CreateFile("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

/* A console of our own closes with the program: keep it until Enter */
static void waitForEnter(HANDLE output)
{
    HANDLE input;
    char buffer[16];
    DWORD got;

    writeText(output, "\r\nPress Enter to close.");
    input = CreateFile("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (input == INVALID_HANDLE_VALUE) {
        return;
    }
    ReadFile(input, buffer, sizeof(buffer), &got, NULL);
    CloseHandle(input);
}

static int printText(void)
{
    HANDLE output;
    char line[TEXT_SIZE * 2];
    int mode;
    int width;
    int length;
    int i;

    output = openOutput(&mode);
    if (output == INVALID_HANDLE_VALUE) {
        return 1;
    }
    consoleOutput = GetFileType(output) == FILE_TYPE_CHAR;
    if (mode == OUTPUT_ATTACHED) {
        writeText(output, "\r\n");  /* cmd.exe has printed its prompt already */
    }
    writeLine(output, title);
    length = lstrlen(title);
    for (i = 0; i < length; i++) {
        line[i] = '-';
    }
    line[i] = '\0';
    writeLine(output, line);
    width = 0;
    for (i = 0; i < fieldCount; i++) {
        length = lstrlen(fields[i].label);
        if (length > width) {
            width = length;
        }
    }
    for (i = 0; i < fieldCount; i++) {
        copyText(line, fields[i].label, sizeof(line));
        for (length = lstrlen(line); length <= width; length++) {
            line[length] = ' ';
        }
        line[length] = '\0';
        appendText(line, fields[i].value, sizeof(line));
        writeLine(output, line);
    }
    if (mode == OUTPUT_NEW_CONSOLE) {
        waitForEnter(output);
    }
    if (mode != OUTPUT_INHERITED) {
        CloseHandle(output);
    }
    return 0;
}

/* MS Sans Serif 8, the NT dialog font */
static HFONT createFont(HDC dc, int weight)
{
    HFONT font;

    font = CreateFont(-MulDiv(8, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif");
    if (font == NULL) {
        font = (HFONT)GetStockObject(ANSI_VAR_FONT);
    }
    return font;
}

static int widestText(HDC dc, HFONT font, int first, BOOL labels)
{
    SIZE size;
    HFONT oldFont;
    char *text;
    int widest;
    int i;

    widest = first;
    oldFont = (HFONT)SelectObject(dc, font);
    for (i = 0; i < fieldCount; i++) {
        text = labels ? fields[i].label : fields[i].value;
        if (GetTextExtentPoint32(dc, text, lstrlen(text), &size) && size.cx > widest) {
            widest = size.cx;
        }
    }
    SelectObject(dc, oldFont);
    return widest;
}

/* The height of the tallest client area that fits on the screen */
static int screenRoom(DWORD style)
{
    RECT frame;

    frame.left = 0;
    frame.top = 0;
    frame.right = 0;
    frame.bottom = 0;
    AdjustWindowRect(&frame, style, FALSE);
    /* SM_CYFULLSCREEN leaves out the caption, and the taskbar where there is one */
    return GetSystemMetrics(SM_CYFULLSCREEN) + GetSystemMetrics(SM_CYCAPTION) - (frame.bottom - frame.top);
}

/* Lay out the chips on top and the text below them, and return the client area size */
static void measureLayout(HDC dc, DWORD style, RECT *client)
{
    TEXTMETRIC metrics;
    SIZE size;
    HFONT oldFont;
    int textHeight;
    int iconRows;
    int maxRows;
    int iconsWidth;

    oldFont = (HFONT)SelectObject(dc, boldFont);
    GetTextMetrics(dc, &metrics);
    lineHeight = metrics.tmHeight + LINE_GAP;
    size.cx = 0;
    GetTextExtentPoint32(dc, title, lstrlen(title), &size);
    SelectObject(dc, oldFont);
    labelWidth = widestText(dc, boldFont, 0, TRUE) + LABEL_GAP;
    contentWidth = labelWidth + widestText(dc, normalFont, 0, FALSE);
    if (size.cx > contentWidth) {
        contentWidth = size.cx;
    }
    textHeight = lineHeight + TITLE_GAP + fieldCount * lineHeight;

    /* one chip per CPU, in rows of four, where neofetch has its logo: as many rows as fit on the screen */
    iconCount = 0;
    titleTop = MARGIN;
    if (cpuIcon != NULL) {
        maxRows = (screenRoom(style) - (MARGIN + ICON_GAP + textHeight + MARGIN + BUTTON_HEIGHT + MARGIN) + ICON_CELL - ICON_SIZE) / ICON_CELL;
        iconRows = (cpuCount + ICON_COLUMNS - 1) / ICON_COLUMNS;
        if (iconRows > maxRows) {
            iconRows = maxRows;
        }
        if (iconRows < 1) {
            iconRows = 1;
        }
        iconCount = min(cpuCount, iconRows * ICON_COLUMNS);
        iconsWidth = min(cpuCount, ICON_COLUMNS) * ICON_CELL - (ICON_CELL - ICON_SIZE);
        if (iconsWidth > contentWidth) {
            contentWidth = iconsWidth;
        }
        titleTop += iconRows * ICON_CELL - (ICON_CELL - ICON_SIZE) + ICON_GAP;
    }
    fieldsTop = titleTop + lineHeight + TITLE_GAP;
    client->left = 0;
    client->top = 0;
    client->right = MARGIN + contentWidth + MARGIN;
    client->bottom = titleTop + textHeight + MARGIN + BUTTON_HEIGHT + MARGIN;
}

static void drawLine(HDC dc, int color, int y)
{
    HPEN pen;
    HPEN oldPen;

    pen = CreatePen(PS_SOLID, 1, GetSysColor(color));
    if (pen == NULL) {
        return;
    }
    oldPen = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, MARGIN, y, NULL);
    LineTo(dc, MARGIN + contentWidth, y);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void paintWindow(HWND window)
{
    PAINTSTRUCT paint;
    HDC dc;
    HFONT oldFont;
    int i;
    int y;

    dc = BeginPaint(window, &paint);
    if (dc == NULL) {
        return;
    }
    for (i = 0; i < iconCount; i++) {
        DrawIcon(dc, MARGIN + (i % ICON_COLUMNS) * ICON_CELL, MARGIN + (i / ICON_COLUMNS) * ICON_CELL, cpuIcon);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    oldFont = (HFONT)SelectObject(dc, boldFont);
    TextOut(dc, MARGIN, titleTop, title, lstrlen(title));
    drawLine(dc, COLOR_BTNSHADOW, titleTop + lineHeight + 1);
    drawLine(dc, COLOR_BTNHIGHLIGHT, titleTop + lineHeight + 2);
    y = fieldsTop;
    for (i = 0; i < fieldCount; i++) {
        SelectObject(dc, boldFont);
        TextOut(dc, MARGIN, y, fields[i].label, lstrlen(fields[i].label));
        SelectObject(dc, normalFont);
        TextOut(dc, MARGIN + labelWidth, y, fields[i].value, lstrlen(fields[i].value));
        y += lineHeight;
    }
    SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_PAINT:
        paintWindow(window);
        return 0;
    case WM_SETFOCUS:
        if (okButton != NULL) {
            SetFocus(okButton);
        }
        return 0;
    case WM_CTLCOLORBTN:
        /* NT 3.x paints the corners of a push button with this brush */
        if (faceBrush == NULL) {
            break;
        }
        SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)faceBrush;
    case WM_COMMAND:
        /* the OK button, Enter or Escape */
        if (HIWORD(wParam) == BN_CLICKED) {
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

static void deleteGdiObjects(void)
{
    if (normalFont != NULL) {
        DeleteObject(normalFont);
    }
    if (boldFont != NULL) {
        DeleteObject(boldFont);
    }
    if (faceBrush != NULL) {
        DeleteObject(faceBrush);
    }
}

static BOOL registerWindowClass(HINSTANCE instance)
{
    WNDCLASS windowClass;

    windowClass.style = 0;
    windowClass.lpfnWndProc = windowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance;
    windowClass.hIcon = cpuIcon != NULL ? cpuIcon : LoadIcon(NULL, IDI_APPLICATION);
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = "ntfetch";
    return RegisterClass(&windowClass) != 0;
}

static int runWindow(HINSTANCE instance, int showCmd)
{
    HWND window;
    HDC dc;
    RECT client;
    MSG message;
    DWORD style;
    int buttonX;
    int buttonY;
    int width;
    int height;

    cpuIcon = LoadIcon(instance, MAKEINTRESOURCE(cpuIconId));
    if (!registerWindowClass(instance)) {
        return 1;
    }
    dc = GetDC(NULL);
    if (dc == NULL) {
        return 1;
    }
    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    normalFont = createFont(dc, FW_NORMAL);
    boldFont = createFont(dc, FW_BOLD);
    measureLayout(dc, style, &client);
    ReleaseDC(NULL, dc);
    faceBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

    buttonX = client.right - MARGIN - BUTTON_WIDTH;
    buttonY = client.bottom - MARGIN - BUTTON_HEIGHT;
    AdjustWindowRect(&client, style, FALSE);
    width = client.right - client.left;
    height = client.bottom - client.top;
    window = CreateWindow("ntfetch", "NTFETCH", style,
        max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2), max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2),
        width, height, NULL, NULL, instance, NULL);
    if (window == NULL) {
        deleteGdiObjects();
        return 1;
    }
    okButton = CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        buttonX, buttonY, BUTTON_WIDTH, BUTTON_HEIGHT, window, NULL, instance, NULL);
    if (okButton != NULL) {
        SendMessage(okButton, WM_SETFONT, (WPARAM)normalFont, 0);
    }
    ShowWindow(window, showCmd);
    UpdateWindow(window);
    while (GetMessage(&message, NULL, 0, 0) > 0) {
        if (IsDialogMessage(window, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    deleteGdiObjects();
    return (int)message.wParam;
}

/* --text, -text or /text anywhere on the command line */
static BOOL wantsText(char *commandLine)
{
    char word[16];
    int length;

    while (*commandLine != '\0') {
        length = 0;
        for (; *commandLine != '\0' && *commandLine != ' ' && *commandLine != '\t'; commandLine++) {
            if (length < (int)sizeof(word) - 1) {
                word[length++] = *commandLine;
            }
        }
        word[length] = '\0';
        if (lstrcmpi(word, "--text") == 0 || lstrcmpi(word, "-text") == 0 || lstrcmpi(word, "/text") == 0) {
            return TRUE;
        }
        while (*commandLine == ' ' || *commandLine == '\t') {
            commandLine++;
        }
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR commandLine, int showCmd)
{
    collectInfo();
    if (wantsText(commandLine)) {
        return printText();
    }
    return runWindow(instance, showCmd);
}
