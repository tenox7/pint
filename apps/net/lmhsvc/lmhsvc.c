/*++

Module Name:

    lmhsvc.c

Abstract:

    The LmHosts service of TCP/IP (lmhsvc.dll, run by services.exe): NetBIOS
    over TCP/IP resolves with DNS the names it finds neither in its cache, on
    its name servers nor in LMHOSTS. NetBT holds a request of this service
    (IOCTL_NETBT_DNS_NAME_RESOLVE, a tIPADDR_BUFFER) and completes it with the
    name to resolve; the service resolves the name with the host name
    resolution of Windows Sockets and answers with its next request, which
    carries the address (in host order, as NetBT keeps addresses) and whether
    the name resolved.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <winsock.h>
#include <lmcons.h>
#include <lmsname.h>
#include <svcs.h>
#include <nbtioctl.h>

#define NETBT_LINKAGE   L"System\\CurrentControlSet\\Services\\NetBT\\Linkage"

static SERVICE_STATUS_HANDLE LmhStatusHandle;
static SERVICE_STATUS LmhStatus;
static HANDLE LmhStopEvent;

//
// The request NetBT holds, and its status: NetBT may complete it after the
// service stopped.
//

static tIPADDR_BUFFER LmhBuffer;
static IO_STATUS_BLOCK LmhIoStatus;

static VOID
LmhSetStatus (
    DWORD State,
    DWORD Error
    )
{
    LmhStatus.dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    LmhStatus.dwCurrentState = State;
    LmhStatus.dwControlsAccepted = (State == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    LmhStatus.dwWin32ExitCode = Error;
    LmhStatus.dwServiceSpecificExitCode = 0;
    LmhStatus.dwCheckPoint = 0;
    LmhStatus.dwWaitHint = (State == SERVICE_START_PENDING || State == SERVICE_STOP_PENDING) ? 10000 : 0;
    SetServiceStatus(LmhStatusHandle, &LmhStatus);
}

static VOID
LmhControlHandler (
    DWORD Opcode
    )
{
    switch (Opcode) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        LmhSetStatus(SERVICE_STOP_PENDING, NO_ERROR);
        SetEvent(LmhStopEvent);
        return;

    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(LmhStatusHandle, &LmhStatus);
        return;
    }
}

//
// Open the first device NetBT exports (Linkage\Export): any of them reaches
// the name queries of all of them.
//

static NTSTATUS
LmhOpenNetBT (
    PHANDLE Handle
    )
{
    HKEY Key;
    WCHAR Export[MAX_PATH];
    DWORD Size = sizeof(Export) - sizeof(WCHAR);
    DWORD Type;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, NETBT_LINKAGE, 0, KEY_READ, &Key);
    if (Error != ERROR_SUCCESS) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Error = RegQueryValueExW(Key, L"Export", NULL, &Type, (LPBYTE)Export, &Size);
    RegCloseKey(Key);
    if (Error != ERROR_SUCCESS || Type != REG_MULTI_SZ || Export[0] == L'\0') {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Export[Size / sizeof(WCHAR)] = L'\0';
    RtlInitUnicodeString(&Name, Export);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NtCreateFile(Handle,
                        SYNCHRONIZE | GENERIC_EXECUTE,
                        &Attributes,
                        &IoStatus,
                        NULL,
                        FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_OPEN,
                        0,
                        NULL,
                        0);
}

//
// Resolve the name of the request (15 characters, padded with spaces) with
// the host name resolution of Windows Sockets.
//

static VOID
LmhResolve (
    tIPADDR_BUFFER *Buffer
    )
{
    CHAR Name[NETBIOS_NAMESIZE];
    struct hostent *Host;
    int Length;

    RtlCopyMemory(Name, Buffer->Name, NETBIOS_NAMESIZE - 1);
    Name[NETBIOS_NAMESIZE - 1] = '\0';
    for (Length = lstrlenA(Name); Length > 0 && Name[Length - 1] == ' '; Length -= 1) {
        Name[Length - 1] = '\0';
    }

    Buffer->IpAddress = 0;
    Buffer->Resolved = FALSE;
    if (Length == 0) {
        return;
    }

    Host = gethostbyname(Name);
    if (Host == NULL || Host->h_length != sizeof(ULONG) || Host->h_addr_list[0] == NULL) {
        return;
    }

    Buffer->IpAddress = ntohl(*(u_long *)Host->h_addr_list[0]);
    Buffer->Resolved = TRUE;
}

//
// Hold a request in NetBT until it has a name to resolve, then answer with
// the next one, until the service stops.
//

static DWORD
LmhResolveNames (
    HANDLE NetBT
    )
{
    HANDLE Done;
    HANDLE Events[2];
    NTSTATUS Status;

    Done = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (Done == NULL) {
        return GetLastError();
    }

    Events[0] = LmhStopEvent;
    Events[1] = Done;
    RtlZeroMemory(&LmhBuffer, sizeof(LmhBuffer));
    for (;;) {
        ResetEvent(Done);
        Status = NtDeviceIoControlFile(NetBT,
                                       Done,
                                       NULL,
                                       NULL,
                                       &LmhIoStatus,
                                       IOCTL_NETBT_DNS_NAME_RESOLVE,
                                       NULL,
                                       0,
                                       &LmhBuffer,
                                       sizeof(LmhBuffer));

        if (Status == STATUS_PENDING) {
            if (WaitForMultipleObjects(2, Events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                break;
            }

            Status = LmhIoStatus.Status;
        }

        if (!NT_SUCCESS(Status)) {
            CloseHandle(Done);
            return RtlNtStatusToDosError(Status);
        }

        LmhResolve(&LmhBuffer);
    }

    CloseHandle(Done);
    return NO_ERROR;
}

VOID
ServiceEntry (
    DWORD argc,
    LPTSTR argv[],
    PSVCS_GLOBAL_DATA GlobalData,
    HANDLE SvcReferenceHandle
    )
{
    WSADATA WsaData;
    HANDLE NetBT;
    NTSTATUS Status;
    DWORD Error;

    LmhStatusHandle = RegisterServiceCtrlHandler(SERVICE_LMHOSTS, LmhControlHandler);
    if (LmhStatusHandle == 0) {
        return;
    }

    LmhSetStatus(SERVICE_START_PENDING, NO_ERROR);
    LmhStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (LmhStopEvent == NULL) {
        LmhSetStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    Error = WSAStartup(0x0101, &WsaData);
    if (Error != 0) {
        CloseHandle(LmhStopEvent);
        LmhSetStatus(SERVICE_STOPPED, Error);
        return;
    }

    Status = LmhOpenNetBT(&NetBT);
    if (!NT_SUCCESS(Status)) {
        WSACleanup();
        CloseHandle(LmhStopEvent);
        LmhSetStatus(SERVICE_STOPPED, RtlNtStatusToDosError(Status));
        return;
    }

    LmhSetStatus(SERVICE_RUNNING, NO_ERROR);
    Error = LmhResolveNames(NetBT);

    NtClose(NetBT);
    WSACleanup();
    CloseHandle(LmhStopEvent);
    LmhSetStatus(SERVICE_STOPPED, Error);
}
