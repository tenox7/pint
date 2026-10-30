/*++

Module Name:

    lmhsvc.c

Abstract:

    The LmHosts service of TCP/IP (lmhsvc.dll, run by services.exe), the
    TCP/IP NetBIOS Helper: it does for NetBIOS over TCP/IP what NetBT cannot
    do in the kernel. NetBT holds a request of each kind (a
    tIPADDR_BUFFER_DNS) and completes it with work; the service answers in
    the buffer of its next request of that kind.

    IOCTL_NETBT_DNS_NAME_RESOLVE: a name that NetBT found neither in its
    cache, on its name servers nor in LMHOSTS. The service looks it up in
    DNS, and only there (the host name resolution of Windows Sockets would
    ask NetBT again), and answers with its addresses, ending with 0, and
    whether it resolved.

    IOCTL_NETBT_CHECK_IP_ADDR: the addresses of a multihomed host to which
    NetBT sets up a session, the best first, ending with 0. The service sends
    each an echo request and answers with the first that replies, or with
    the first of the list if none does.

    NetBT keeps addresses in host order; those it sends to check are in
    network order, as the name query response carried them.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <winsock2.h>
#include <svcguid.h>
#include <ipexport.h>
#include <icmpapi.h>
#include <lmcons.h>
#include <lmsname.h>
#include <svcs.h>
#include <nbtioctl.h>

#define NETBT_LINKAGE   L"System\\CurrentControlSet\\Services\\NetBT\\Linkage"
#define ECHO_TIMEOUT    1000            // milliseconds an address has to answer

//
// A request NetBT holds, and the routine that does its work.
//

typedef struct _LMH_REQUEST {
    ULONG IoControlCode;
    VOID (*Answer)(tIPADDR_BUFFER_DNS *Buffer);
    tIPADDR_BUFFER_DNS Buffer;
    IO_STATUS_BLOCK IoStatus;
} LMH_REQUEST;

static VOID LmhResolveName(tIPADDR_BUFFER_DNS *Buffer);
static VOID LmhCheckAddresses(tIPADDR_BUFFER_DNS *Buffer);

static LMH_REQUEST LmhDnsRequest = { IOCTL_NETBT_DNS_NAME_RESOLVE, LmhResolveName };
static LMH_REQUEST LmhCheckRequest = { IOCTL_NETBT_CHECK_IP_ADDR, LmhCheckAddresses };

static SERVICE_STATUS_HANDLE LmhStatusHandle;
static SERVICE_STATUS LmhStatus;
static HANDLE LmhStopEvent;
static HANDLE LmhNetBT;
static HANDLE LmhIcmp;

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
// the requests of all of them.
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
// Look the name up in DNS (a NetBIOS name comes padded with spaces to 15
// characters).
//

static VOID
LmhResolveName (
    tIPADDR_BUFFER_DNS *Buffer
    )
{
    static GUID HostName = SVCID_INET_HOSTADDRBYNAME;
    WSAQUERYSETA Query;
    union {
        WSAQUERYSETA Set;
        UCHAR Space[2048];
    } Results;
    DWORD Size = sizeof(Results);
    HANDLE Lookup;
    PCHAR Base;
    struct hostent *Host;
    ULONG *Offset;
    ULONG Count = 0;
    int Length;

    Buffer->pName[sizeof(Buffer->pName) - 1] = '\0';
    for (Length = lstrlenA((LPSTR)Buffer->pName); Length > 0 && Buffer->pName[Length - 1] == ' '; Length -= 1) {
        Buffer->pName[Length - 1] = '\0';
    }

    if (Length != 0) {
        RtlZeroMemory(&Query, sizeof(Query));
        Query.dwSize = sizeof(Query);
        Query.lpszServiceInstanceName = (LPSTR)Buffer->pName;
        Query.lpServiceClassId = &HostName;
        Query.dwNameSpace = NS_DNS;
        if (WSALookupServiceBeginA(&Query, LUP_RETURN_BLOB, &Lookup) == 0) {
            if (WSALookupServiceNextA(Lookup, 0, &Size, &Results.Set) == 0 && Results.Set.lpBlob != NULL) {

                //
                // The blob is a hostent whose pointers are offsets from it.
                //

                Base = (PCHAR)Results.Set.lpBlob->pBlobData;
                Host = (struct hostent *)Base;
                if (Host->h_addrtype == AF_INET && Host->h_length == sizeof(ULONG) && Host->h_addr_list != NULL) {
                    Offset = (ULONG *)(Base + (ULONG)Host->h_addr_list);
                    while (Count < MAX_IPADDRS_PER_HOST && Offset[Count] != 0) {
                        Buffer->IpAddrsList[Count] = ntohl(*(ULONG UNALIGNED *)(Base + Offset[Count]));
                        Count += 1;
                    }
                }
            }
            WSALookupServiceEnd(Lookup);
        }
    }

    Buffer->IpAddrsList[Count] = 0;
    Buffer->Resolved = Count != 0;
}

//
// Find the first address of the list that answers an echo request.
//

static VOID
LmhCheckAddresses (
    tIPADDR_BUFFER_DNS *Buffer
    )
{
    union {
        ICMP_ECHO_REPLY Echo;
        UCHAR Space[sizeof(ICMP_ECHO_REPLY) + 8];
    } Reply;
    ULONG Count;
    ULONG Index;

    for (Count = 0; Count <= MAX_IPADDRS_PER_HOST && Buffer->IpAddrsList[Count] != 0; Count += 1) {
    }

    for (Index = 0; Index < Count; Index += 1) {
        if (IcmpSendEcho(LmhIcmp, Buffer->IpAddrsList[Index], NULL, 0, NULL, &Reply, sizeof(Reply), ECHO_TIMEOUT) != 0 &&
            Reply.Echo.Status == IP_SUCCESS) {
            break;
        }
    }

    if (Index == Count) {
        Index = 0;
    }

    Buffer->IpAddrsList[0] = ntohl(Buffer->IpAddrsList[Index]);
    Buffer->IpAddrsList[1] = 0;
    Buffer->Resolved = Count != 0;
}

//
// Hold a request in NetBT until it has work, then answer with the next one,
// until the service stops; then take the request back.
//

static DWORD
LmhServe (
    LMH_REQUEST *Request
    )
{
    HANDLE Done;
    HANDLE Events[2];
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Done = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (Done == NULL) {
        return GetLastError();
    }

    Events[0] = LmhStopEvent;
    Events[1] = Done;
    RtlZeroMemory(&Request->Buffer, sizeof(Request->Buffer));
    for (;;) {
        Status = NtDeviceIoControlFile(LmhNetBT,
                                       Done,
                                       NULL,
                                       NULL,
                                       &Request->IoStatus,
                                       Request->IoControlCode,
                                       NULL,
                                       0,
                                       &Request->Buffer,
                                       sizeof(Request->Buffer));

        if (Status == STATUS_PENDING) {
            if (WaitForMultipleObjects(2, Events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                NtCancelIoFile(LmhNetBT, &IoStatus);
                WaitForSingleObject(Done, INFINITE);
                Status = STATUS_SUCCESS;
                break;
            }

            Status = Request->IoStatus.Status;
        }

        if (!NT_SUCCESS(Status)) {
            break;
        }

        Request->Answer(&Request->Buffer);
    }

    CloseHandle(Done);
    return NT_SUCCESS(Status) ? NO_ERROR : RtlNtStatusToDosError(Status);
}

static DWORD WINAPI
LmhCheckThread (
    LPVOID Parameter
    )
{
    return LmhServe(&LmhCheckRequest);
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
    HANDLE Thread;
    DWORD ThreadId;
    DWORD CheckError;
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

    Error = WSAStartup(MAKEWORD(2, 0), &WsaData);
    if (Error != 0) {
        CloseHandle(LmhStopEvent);
        LmhSetStatus(SERVICE_STOPPED, Error);
        return;
    }

    Status = LmhOpenNetBT(&LmhNetBT);
    if (!NT_SUCCESS(Status)) {
        WSACleanup();
        CloseHandle(LmhStopEvent);
        LmhSetStatus(SERVICE_STOPPED, RtlNtStatusToDosError(Status));
        return;
    }

    LmhIcmp = IcmpCreateFile();
    Thread = CreateThread(NULL, 0, LmhCheckThread, NULL, 0, &ThreadId);
    if (Thread == NULL) {
        Error = GetLastError();
    } else {
        LmhSetStatus(SERVICE_RUNNING, NO_ERROR);
        Error = LmhServe(&LmhDnsRequest);

        SetEvent(LmhStopEvent);
        WaitForSingleObject(Thread, INFINITE);
        if (Error == NO_ERROR && GetExitCodeThread(Thread, &CheckError)) {
            Error = CheckError;
        }
        CloseHandle(Thread);
    }

    if (LmhIcmp != INVALID_HANDLE_VALUE) {
        IcmpCloseHandle(LmhIcmp);
    }
    NtClose(LmhNetBT);
    WSACleanup();
    CloseHandle(LmhStopEvent);
    LmhSetStatus(SERVICE_STOPPED, Error);
}
