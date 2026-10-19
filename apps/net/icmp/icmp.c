/*++

Module Name:

    icmp.c

Abstract:

    The ICMP echo API of the TCP/IP utilities (icmpapi.h): an ICMP handle
    is a handle to the IP device of TCP/IP (\Device\Ip), and an echo the
    echo request of IP, which completes with the reply of the host, or the
    error ICMP reported, or the time out. The request goes in the buffer
    the reply comes back in: IP builds the reply over it.

    The register_icmp, do_echo_req and do_echo_rep routines of the STREAMS
    TCP/IP (icmp.h) run on the same request: the reply of an echo is
    waited for by the next do_echo_rep.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windows.h>
#include <ipexport.h>
#include <icmpapi.h>
#include <ntddip.h>
#include <icmp.h>
#include <errno.h>

#define DEFAULT_TIMEOUT         4000
#define LEGACY_DATA_MAXIMUM     0x10000

//
// The echo request of IP: the request, its options and data, in a buffer
// long enough for the reply.
//

static DWORD
IcmpEcho(
    IN HANDLE Device,
    IN HANDLE Event,
    OUT PIO_STATUS_BLOCK IoStatus,
    IN IPAddr Address,
    IN LPVOID Data,
    IN WORD DataSize,
    IN PIP_OPTION_INFORMATION Options,
    IN PUCHAR Buffer,
    IN ULONG BufferLength,
    IN DWORD Timeout
    )
{
    PICMP_ECHO_REQUEST Request = (PICMP_ECHO_REQUEST)Buffer;
    ULONG OptionsSize = Options != NULL ? Options->OptionsSize : 0;
    NTSTATUS Status;

    RtlZeroMemory(Request, sizeof(ICMP_ECHO_REQUEST));
    Request->Address = Address;
    Request->Timeout = Timeout;
    Request->OptionsOffset = sizeof(ICMP_ECHO_REQUEST);
    Request->DataOffset = (USHORT)(sizeof(ICMP_ECHO_REQUEST) + OptionsSize);
    Request->DataSize = DataSize;
    if (Options != NULL) {
        Request->OptionsValid = 1;
        Request->Ttl = Options->Ttl;
        Request->Tos = Options->Tos;
        Request->Flags = Options->Flags;
        Request->OptionsSize = (UCHAR)OptionsSize;
        if (OptionsSize != 0) {
            RtlMoveMemory(Buffer + Request->OptionsOffset, Options->OptionsData, OptionsSize);
        }
    }

    if (DataSize != 0) {
        RtlMoveMemory(Buffer + Request->DataOffset, Data, DataSize);
    }

    Status = NtDeviceIoControlFile(Device, Event, NULL, NULL, IoStatus, IOCTL_ICMP_ECHO_REQUEST, Buffer,
                                   BufferLength, Buffer, BufferLength);
    return RtlNtStatusToDosError(Status);
}

//
// The reply IP built over the request, with its pointers made from the
// offsets IP gave into the buffer of the caller.
//

static DWORD
IcmpReply(
    IN PUCHAR Buffer,
    IN ULONG Length,
    OUT LPVOID ReplyBuffer,
    IN DWORD ReplySize
    )
{
    PICMP_ECHO_REPLY Reply = ReplyBuffer;

    RtlMoveMemory(ReplyBuffer, Buffer, Length < ReplySize ? Length : ReplySize);
    if (Reply->Reserved == 0) {
        Reply->Data = Reply->Options.OptionsData = (PUCHAR)(Reply + 1);
        Reply->DataSize = 0;
        Reply->Options.OptionsSize = 0;
        SetLastError(Reply->Status);
        return 0;
    }

    Reply->Options.OptionsData = (PUCHAR)ReplyBuffer + (ULONG)Reply->Options.OptionsData;
    Reply->Data = (PUCHAR)ReplyBuffer + (ULONG)Reply->Data;
    return Reply->Reserved;
}

HANDLE
WINAPI
IcmpCreateFile(
    VOID
    )

/*++

Routine Description:

    This routine opens a handle on which ICMP echo requests can be issued.

Return Value:

    The handle, or INVALID_HANDLE_VALUE; GetLastError gives the error.

--*/

{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Handle;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, DD_IP_DEVICE_NAME);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&Handle, GENERIC_EXECUTE, &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, 0, NULL, 0);
    if (!NT_SUCCESS(Status)) {
        SetLastError(RtlNtStatusToDosError(Status));
        return INVALID_HANDLE_VALUE;
    }

    return Handle;
}

BOOL
WINAPI
IcmpCloseHandle(
    HANDLE IcmpHandle
    )

/*++

Routine Description:

    This routine closes a handle opened by IcmpCreateFile.

Arguments:

    IcmpHandle - Supplies the handle.

Return Value:

    TRUE if it was closed, else FALSE.

--*/

{
    NTSTATUS Status = NtClose(IcmpHandle);

    if (!NT_SUCCESS(Status)) {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

DWORD
WINAPI
IcmpSendEcho(
    HANDLE IcmpHandle,
    IPAddr DestinationAddress,
    LPVOID RequestData,
    WORD RequestSize,
    PIP_OPTION_INFORMATION RequestOptions,
    LPVOID ReplyBuffer,
    DWORD ReplySize,
    DWORD Timeout
    )

/*++

Routine Description:

    This routine sends an ICMP echo request and waits for the reply, or the
    time out.

Arguments:

    IcmpHandle - Supplies a handle from IcmpCreateFile.

    DestinationAddress - Supplies the address to send to.

    RequestData, RequestSize - Supply the data of the request.

    RequestOptions - Supplies the IP options of the request, or NULL.

    ReplyBuffer - Receives the reply (ICMP_ECHO_REPLY) with its data and
        options.

    ReplySize - Supplies the length of the reply buffer, at least that of a
        reply and 8 bytes more (the ICMP error data).

    Timeout - Supplies the time to wait for the reply, in milliseconds.

Return Value:

    The count of replies in the buffer; 0 if none, and GetLastError gives
    the IP status.

--*/

{
    ULONG OptionsSize = RequestOptions != NULL ? RequestOptions->OptionsSize : 0;
    ULONG RequestLength = sizeof(ICMP_ECHO_REQUEST) + OptionsSize + RequestSize;
    ULONG Length = RequestLength > ReplySize ? RequestLength : ReplySize;
    IO_STATUS_BLOCK IoStatus;
    PUCHAR Buffer;
    HANDLE Event;
    DWORD Error;
    DWORD Replies;

    if (ReplySize < sizeof(ICMP_ECHO_REPLY) + 8) {
        SetLastError(IP_BUF_TOO_SMALL);
        return 0;
    }

    Buffer = RtlAllocateHeap(RtlProcessHeap(), 0, Length);
    Event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (Buffer == NULL || Event == NULL) {
        if (Buffer != NULL) {
            RtlFreeHeap(RtlProcessHeap(), 0, Buffer);
        }

        SetLastError(IP_NO_RESOURCES);
        return 0;
    }

    Error = IcmpEcho(IcmpHandle, Event, &IoStatus, DestinationAddress, RequestData, RequestSize, RequestOptions,
                     Buffer, Length, Timeout);
    if (Error == ERROR_IO_PENDING) {
        WaitForSingleObject(Event, INFINITE);
        Error = RtlNtStatusToDosError(IoStatus.Status);
    }

    CloseHandle(Event);
    Replies = 0;
    if (Error == NO_ERROR) {
        Replies = IcmpReply(Buffer, IoStatus.Information, ReplyBuffer, ReplySize);
    } else {
        SetLastError(Error);
    }

    RtlFreeHeap(RtlProcessHeap(), 0, Buffer);
    return Replies;
}

//
// The echo of the STREAMS interface: the request pending on its handle.
//

typedef struct _ICMP_STREAM {
    HANDLE Device;
    HANDLE Event;
    IO_STATUS_BLOCK IoStatus;
    BOOLEAN Pending;
    ULONG Length;
    PUCHAR Buffer;
} ICMP_STREAM, *PICMP_STREAM;

HANDLE
STRMAPI
register_icmp(
    void
    )

/*++

Routine Description:

    This routine opens an ICMP stream of the STREAMS interface.

Return Value:

    The handle of the stream, or ICMP_ERROR; GetLastError gives
    ICMP_OPEN_ERROR.

--*/

{
    PICMP_STREAM Stream;

    Stream = RtlAllocateHeap(RtlProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ICMP_STREAM));
    if (Stream == NULL) {
        SetLastError(ICMP_OPEN_ERROR);
        return ICMP_ERROR;
    }

    Stream->Device = IcmpCreateFile();
    Stream->Event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (Stream->Device == INVALID_HANDLE_VALUE || Stream->Event == NULL) {
        if (Stream->Device != INVALID_HANDLE_VALUE) {
            NtClose(Stream->Device);
        }

        RtlFreeHeap(RtlProcessHeap(), 0, Stream);
        SetLastError(ICMP_OPEN_ERROR);
        return ICMP_ERROR;
    }

    return (HANDLE)Stream;
}

int
STRMAPI
do_echo_req(
    HANDLE fd,
    long addr,
    char *data,
    int datalen,
    char *optptr,
    int optlen,
    int df,
    int ttl,
    int tos,
    int preced
    )

/*++

Routine Description:

    This routine sends an echo request on an ICMP stream; do_echo_rep
    waits for its reply.

Arguments:

    fd - Supplies the stream.

    addr - Supplies the address to send to.

    data, datalen - Supply the data of the request.

    optptr, optlen - Supply the IP options.

    df, ttl, tos, preced - Supply the don't fragment flag, the time to
        live, the type of service and the precedence.

Return Value:

    0, ERANGE if the request is too long, ENOMEM if no memory is left, or
    EIO if the stream has a request pending already.

--*/

{
    PICMP_STREAM Stream = (PICMP_STREAM)fd;
    IP_OPTION_INFORMATION Options;
    ULONG Length;
    DWORD Error;

    if (Stream->Pending) {
        return EIO;
    }

    if (datalen < 0 || datalen >= LEGACY_DATA_MAXIMUM || optlen < 0 || optlen > MAX_OPT_SIZE) {
        return ERANGE;
    }

    Length = sizeof(ICMP_ECHO_REPLY) + MAX_OPT_SIZE + datalen + 8;
    if (Length < sizeof(ICMP_ECHO_REQUEST) + optlen + datalen) {
        Length = sizeof(ICMP_ECHO_REQUEST) + optlen + datalen;
    }

    if (Stream->Buffer != NULL && Stream->Length < Length) {
        RtlFreeHeap(RtlProcessHeap(), 0, Stream->Buffer);
        Stream->Buffer = NULL;
    }

    if (Stream->Buffer == NULL) {
        Stream->Buffer = RtlAllocateHeap(RtlProcessHeap(), 0, Length);
        if (Stream->Buffer == NULL) {
            return ENOMEM;
        }

        Stream->Length = Length;
    }

    Options.Ttl = (UCHAR)ttl;
    Options.Tos = (UCHAR)((tos & 0x1F) | (preced << 5));
    Options.Flags = df ? IP_FLAG_DF : 0;
    Options.OptionsSize = (UCHAR)optlen;
    Options.OptionsData = (PUCHAR)optptr;
    Error = IcmpEcho(Stream->Device, Stream->Event, &Stream->IoStatus, (IPAddr)addr, data, (WORD)datalen,
                     &Options, Stream->Buffer, Stream->Length, DEFAULT_TIMEOUT);
    if (Error != NO_ERROR && Error != ERROR_IO_PENDING) {
        return EIO;
    }

    if (Error == NO_ERROR) {
        SetEvent(Stream->Event);
    }

    Stream->Pending = TRUE;
    return 0;
}

int
STRMAPI
do_echo_rep(
    HANDLE fd,
    char *rdata,
    int rdatalen,
    int *rtype,
    int *rttl,
    int *rtos,
    int *rpreced,
    int *rdf,
    char *roptptr,
    int *roptlen
    )

/*++

Routine Description:

    This routine waits for the reply of the echo request of an ICMP stream
    and gives what it held.

Arguments:

    fd - Supplies the stream.

    rdata, rdatalen - Receive the data of the reply.

    rtype - Receives the type of the reply: ECHO_REPLY, DEST_UNR,
        TIME_EXCEEDED or PARAMETER_ERROR.

    rttl, rtos, rpreced, rdf - Receive the time to live, type of service,
        precedence and don't fragment flag of the reply.

    roptptr, roptlen - Receive the IP options of the reply.

Return Value:

    0, or -1 with GetLastError giving POLL_TIMEOUT or POLL_FAILED.

--*/

{
    PICMP_STREAM Stream = (PICMP_STREAM)fd;
    PICMP_ECHO_REPLY Reply = (PICMP_ECHO_REPLY)Stream->Buffer;
    ULONG Length;

    if (!Stream->Pending) {
        SetLastError(POLL_FAILED);
        return -1;
    }

    WaitForSingleObject(Stream->Event, INFINITE);
    Stream->Pending = FALSE;
    if (!NT_SUCCESS(Stream->IoStatus.Status)) {
        SetLastError(POLL_FAILED);
        return -1;
    }

    switch (Reply->Status) {
    case IP_SUCCESS:
        *rtype = ECHO_REPLY;
        break;

    case IP_DEST_NET_UNREACHABLE:
    case IP_DEST_HOST_UNREACHABLE:
    case IP_DEST_PROT_UNREACHABLE:
    case IP_DEST_PORT_UNREACHABLE:
        *rtype = DEST_UNR;
        break;

    case IP_TTL_EXPIRED_TRANSIT:
    case IP_TTL_EXPIRED_REASSEM:
        *rtype = TIME_EXCEEDED;
        break;

    case IP_PARAM_PROBLEM:
        *rtype = PARAMETER_ERROR;
        break;

    case IP_REQ_TIMED_OUT:
        SetLastError(POLL_TIMEOUT);
        return -1;

    default:
        SetLastError(POLL_FAILED);
        return -1;
    }

    if (Reply->Reserved == 0) {
        *roptlen = 0;
        *rttl = *rtos = *rpreced = *rdf = 0;
        return 0;
    }

    Length = Reply->DataSize < (ULONG)rdatalen ? Reply->DataSize : (ULONG)rdatalen;
    RtlMoveMemory(rdata, Stream->Buffer + (ULONG)Reply->Data, Length);
    Length = Reply->Options.OptionsSize < (ULONG)*roptlen ? Reply->Options.OptionsSize : (ULONG)*roptlen;
    RtlMoveMemory(roptptr, Stream->Buffer + (ULONG)Reply->Options.OptionsData, Length);
    *roptlen = Length;
    *rttl = Reply->Options.Ttl;
    *rtos = Reply->Options.Tos & 0x1F;
    *rpreced = Reply->Options.Tos >> 5;
    *rdf = (Reply->Options.Flags & IP_FLAG_DF) != 0;
    return 0;
}
