/*++

Module Name:

    wshtcpip.c

Abstract:

    The Windows Sockets helper DLL of TCP/IP: what the sockets DLL (wsock32)
    does not know about the transport. It maps the address family, socket
    type and protocol triples of TCP and UDP to the devices of TCP/IP
    (\Device\Tcp and \Device\Udp), interprets the addresses of the internet
    family, and gets and sets the options of the protocol levels, which it
    keeps in the context of each socket and gives to the transport through
    the set information request of TCP/IP: those of an address object once
    the socket is bound, those of a connection once it is connected or
    accepted.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <windef.h>
#include <winbase.h>
#include <tdi.h>
#include <winsock.h>
#include <wsahelp.h>
#include <basetyps.h>
#include <nspapi.h>
#include <ipexport.h>
#include <tdiinfo.h>
#include <tcpinfo.h>
#include <ntddtcp.h>

#define TL_INSTANCE             0
#define IP_OPTIONS_MAXIMUM      40
#define UDP_MESSAGE_MAXIMUM     65467

#define TCP_NAME                L"TCP/IP"
#define UDP_NAME                L"UDP/IP"

//
// The triples of TCP and UDP, in the order the sockets DLL takes them.
//

typedef struct _MAPPING_TRIPLE {
    INT AddressFamily;
    INT SocketType;
    INT Protocol;
} MAPPING_TRIPLE, *PMAPPING_TRIPLE;

static MAPPING_TRIPLE TcpMappingTriples[] = {
    { AF_INET,   SOCK_STREAM, IPPROTO_TCP },
    { AF_INET,   SOCK_STREAM, 0 },
    { AF_INET,   0,           IPPROTO_TCP },
    { AF_UNSPEC, 0,           IPPROTO_TCP },
    { AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP }
};

static MAPPING_TRIPLE UdpMappingTriples[] = {
    { AF_INET,   SOCK_DGRAM,  IPPROTO_UDP },
    { AF_INET,   SOCK_DGRAM,  0 },
    { AF_INET,   0,           IPPROTO_UDP },
    { AF_UNSPEC, 0,           IPPROTO_UDP },
    { AF_UNSPEC, SOCK_DGRAM,  IPPROTO_UDP }
};

#define TRIPLES(Triples) (sizeof(Triples) / sizeof(MAPPING_TRIPLE))

//
// The context of a socket: its triple, and the options it was given.
//

#define CONTEXT_KEEPALIVE       0x0001
#define CONTEXT_DONTROUTE       0x0002
#define CONTEXT_NODELAY         0x0004
#define CONTEXT_MULTICAST_LOOP  0x0008

typedef struct _WSHTCPIP_SOCKET_CONTEXT {
    INT AddressFamily;
    INT SocketType;
    INT Protocol;
    INT ReceiveBufferSize;
    DWORD Flags;
    INT MulticastTtl;
    IN_ADDR MulticastInterface;
    INT IpOptionsLength;
    UCHAR IpOptions[IP_OPTIONS_MAXIMUM];
} WSHTCPIP_SOCKET_CONTEXT, *PWSHTCPIP_SOCKET_CONTEXT;

#define DEFAULT_RECEIVE_BUFFER_SIZE 8192

static BOOLEAN
IsTripleInList(
    IN PMAPPING_TRIPLE List,
    IN ULONG Count,
    IN INT AddressFamily,
    IN INT SocketType,
    IN INT Protocol
    )
{
    ULONG Index;

    for (Index = 0; Index < Count; Index += 1) {
        if (List[Index].AddressFamily == AddressFamily && List[Index].SocketType == SocketType &&
            List[Index].Protocol == Protocol) {
            return TRUE;
        }
    }

    return FALSE;
}

//
// Give an option to TCP/IP: an option of the address object or of the
// connection the handle is open on.
//

static INT
SetTdiInformation(
    IN HANDLE Handle,
    IN ULONG Entity,
    IN ULONG Type,
    IN ULONG Id,
    IN PVOID Value,
    IN ULONG ValueLength
    )
{
    PTCP_REQUEST_SET_INFORMATION_EX Request;
    ULONG Length = FIELD_OFFSET(TCP_REQUEST_SET_INFORMATION_EX, Buffer) + ValueLength;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    HANDLE Event;

    Request = RtlAllocateHeap(RtlProcessHeap(), 0, Length);
    if (Request == NULL) {
        return WSAENOBUFS;
    }

    RtlZeroMemory(Request, Length);
    Request->ID.toi_entity.tei_entity = Entity;
    Request->ID.toi_entity.tei_instance = TL_INSTANCE;
    Request->ID.toi_class = INFO_CLASS_PROTOCOL;
    Request->ID.toi_type = Type;
    Request->ID.toi_id = Id;
    Request->BufferSize = ValueLength;
    RtlMoveMemory(Request->Buffer, Value, ValueLength);

    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE);
    if (!NT_SUCCESS(Status)) {
        RtlFreeHeap(RtlProcessHeap(), 0, Request);
        return WSAENOBUFS;
    }

    Status = NtDeviceIoControlFile(Handle, Event, NULL, NULL, &IoStatus, IOCTL_TCP_SET_INFORMATION_EX,
                                   Request, Length, NULL, 0);
    if (Status == STATUS_PENDING) {
        NtWaitForSingleObject(Event, FALSE, NULL);
        Status = IoStatus.Status;
    }

    NtClose(Event);
    RtlFreeHeap(RtlProcessHeap(), 0, Request);
    if (NT_SUCCESS(Status)) {
        return NO_ERROR;
    }

    return Status == STATUS_INSUFFICIENT_RESOURCES ? WSAENOBUFS : WSAEINVAL;
}

static INT
SetConnectionOption(
    IN HANDLE Connection,
    IN ULONG Id,
    IN ULONG Value
    )
{
    TCPSocketOption Option;

    Option.tso_value = Value;
    return SetTdiInformation(Connection, CO_TL_ENTITY, INFO_TYPE_CONNECTION, Id, &Option, sizeof(Option));
}

static INT
SetAddressOption(
    IN HANDLE Address,
    IN ULONG Entity,
    IN ULONG Id,
    IN PVOID Value,
    IN ULONG ValueLength
    )
{
    return SetTdiInformation(Address, Entity, INFO_TYPE_ADDRESS_OBJECT, Id, Value, ValueLength);
}

static ULONG
AddressEntity(
    IN PWSHTCPIP_SOCKET_CONTEXT Context
    )
{
    return Context->SocketType == SOCK_STREAM ? CO_TL_ENTITY : CL_TL_ENTITY;
}

//
// The options of a connection, given to TCP once it has one, and those of
// an address object, once the socket is bound.
//

static INT
SetConnectionOptions(
    IN PWSHTCPIP_SOCKET_CONTEXT Context,
    IN HANDLE Connection
    )
{
    INT Error = NO_ERROR;

    if (Context->Flags & CONTEXT_NODELAY) {
        Error = SetConnectionOption(Connection, TCP_SOCKET_NODELAY, TRUE);
    }

    if (Error == NO_ERROR && (Context->Flags & CONTEXT_KEEPALIVE)) {
        Error = SetConnectionOption(Connection, TCP_SOCKET_KEEPALIVE, TRUE);
    }

    if (Error == NO_ERROR && Context->ReceiveBufferSize != DEFAULT_RECEIVE_BUFFER_SIZE) {
        Error = SetConnectionOption(Connection, TCP_SOCKET_WINDOW, Context->ReceiveBufferSize);
    }

    return Error;
}

static INT
SetAddressOptions(
    IN PWSHTCPIP_SOCKET_CONTEXT Context,
    IN HANDLE Address
    )
{
    UDPMCastIFReq Interface;
    UCHAR Ttl;
    INT Error = NO_ERROR;

    if (Context->IpOptionsLength != 0) {
        Error = SetAddressOption(Address, AddressEntity(Context), AO_OPTION_IPOPTIONS, Context->IpOptions,
                                 Context->IpOptionsLength);
    }

    if (Context->SocketType != SOCK_DGRAM || Error != NO_ERROR) {
        return Error;
    }

    if (Context->MulticastTtl != IP_DEFAULT_MULTICAST_TTL) {
        Ttl = (UCHAR)Context->MulticastTtl;
        Error = SetAddressOption(Address, CL_TL_ENTITY, AO_OPTION_MCASTTTL, &Ttl, sizeof(Ttl));
    }

    if (Error == NO_ERROR && Context->MulticastInterface.s_addr != INADDR_ANY) {
        Interface.umi_addr = Context->MulticastInterface.s_addr;
        Error = SetAddressOption(Address, CL_TL_ENTITY, AO_OPTION_MCASTIF, &Interface, sizeof(Interface));
    }

    return Error;
}

INT
WINAPI
WSHOpenSocket(
    IN OUT PINT AddressFamily,
    IN OUT PINT SocketType,
    IN OUT PINT Protocol,
    OUT PUNICODE_STRING TransportDeviceName,
    OUT PVOID *HelperDllSocketContext,
    OUT PDWORD NotificationEvents
    )

/*++

Routine Description:

    This routine gives the device of the transport of a new socket and its
    full triple, and makes its context.

Arguments:

    AddressFamily, SocketType, Protocol - Supply the triple of the socket,
        and receive it filled in.

    TransportDeviceName - Receives the name of the device of the transport.

    HelperDllSocketContext - Receives the context of the socket.

    NotificationEvents - Receives the events of the socket to notify.

Return Value:

    NO_ERROR, or a Windows Sockets error code.

--*/

{
    PWSHTCPIP_SOCKET_CONTEXT Context;

    if (IsTripleInList(TcpMappingTriples, TRIPLES(TcpMappingTriples), *AddressFamily, *SocketType, *Protocol)) {
        *AddressFamily = AF_INET;
        *SocketType = SOCK_STREAM;
        *Protocol = IPPROTO_TCP;
        RtlInitUnicodeString(TransportDeviceName, DD_TCP_DEVICE_NAME);
    } else if (IsTripleInList(UdpMappingTriples, TRIPLES(UdpMappingTriples), *AddressFamily, *SocketType,
                              *Protocol)) {
        *AddressFamily = AF_INET;
        *SocketType = SOCK_DGRAM;
        *Protocol = IPPROTO_UDP;
        RtlInitUnicodeString(TransportDeviceName, DD_UDP_DEVICE_NAME);
    } else {
        return WSAEINVAL;
    }

    Context = RtlAllocateHeap(RtlProcessHeap(), 0, sizeof(WSHTCPIP_SOCKET_CONTEXT));
    if (Context == NULL) {
        return WSAENOBUFS;
    }

    RtlZeroMemory(Context, sizeof(WSHTCPIP_SOCKET_CONTEXT));
    Context->AddressFamily = *AddressFamily;
    Context->SocketType = *SocketType;
    Context->Protocol = *Protocol;
    Context->ReceiveBufferSize = DEFAULT_RECEIVE_BUFFER_SIZE;
    Context->MulticastTtl = IP_DEFAULT_MULTICAST_TTL;
    Context->Flags = CONTEXT_MULTICAST_LOOP;
    Context->MulticastInterface.s_addr = INADDR_ANY;

    *NotificationEvents = WSH_NOTIFY_CLOSE | WSH_NOTIFY_BIND;
    if (*SocketType == SOCK_STREAM) {
        *NotificationEvents |= WSH_NOTIFY_CONNECT | WSH_NOTIFY_ACCEPT;
    }

    *HelperDllSocketContext = Context;
    return NO_ERROR;
}

INT
WINAPI
WSHNotify(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN DWORD NotifyEvent
    )

/*++

Routine Description:

    This routine gives the transport the options of a socket that was
    bound, connected or accepted, and frees the context of one closed.

Arguments:

    HelperDllSocketContext - Supplies the context of the socket.

    SocketHandle - Supplies the socket.

    TdiAddressObjectHandle - Supplies its address object.

    TdiConnectionObjectHandle - Supplies its connection.

    NotifyEvent - Supplies what happened.

Return Value:

    NO_ERROR, or a Windows Sockets error code.

--*/

{
    PWSHTCPIP_SOCKET_CONTEXT Context = HelperDllSocketContext;

    switch (NotifyEvent) {
    case WSH_NOTIFY_CLOSE:
        RtlFreeHeap(RtlProcessHeap(), 0, Context);
        return NO_ERROR;

    case WSH_NOTIFY_BIND:
        return SetAddressOptions(Context, TdiAddressObjectHandle);

    case WSH_NOTIFY_CONNECT:
    case WSH_NOTIFY_ACCEPT:
        return SetConnectionOptions(Context, TdiConnectionObjectHandle);
    }

    return WSAEINVAL;
}

INT
WINAPI
WSHGetSocketInformation(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN INT Level,
    IN INT OptionName,
    OUT PCHAR OptionValue,
    OUT PINT OptionLength
    )

/*++

Routine Description:

    This routine gives an option of a socket the sockets DLL does not keep,
    or the context of the socket, for a socket it makes of it.

Arguments:

    HelperDllSocketContext - Supplies the context of the socket.

    SocketHandle, TdiAddressObjectHandle, TdiConnectionObjectHandle - Supply
        the socket and its objects.

    Level, OptionName - Supply the option.

    OptionValue - Receives the value of the option.

    OptionLength - Supplies the length of the buffer of the value, and
        receives the length of the value.

Return Value:

    NO_ERROR, or a Windows Sockets error code.

--*/

{
    PWSHTCPIP_SOCKET_CONTEXT Context = HelperDllSocketContext;
    INT Value;

    if (Level == SOL_INTERNAL && OptionName == SO_CONTEXT) {
        if (OptionValue != NULL) {
            if (*OptionLength < sizeof(WSHTCPIP_SOCKET_CONTEXT)) {
                return WSAEFAULT;
            }

            RtlMoveMemory(OptionValue, Context, sizeof(WSHTCPIP_SOCKET_CONTEXT));
        }

        *OptionLength = sizeof(WSHTCPIP_SOCKET_CONTEXT);
        return NO_ERROR;
    }

    if (Level == IPPROTO_IP && OptionName == IP_OPTIONS) {
        if (*OptionLength < Context->IpOptionsLength) {
            return WSAEFAULT;
        }

        RtlMoveMemory(OptionValue, Context->IpOptions, Context->IpOptionsLength);
        *OptionLength = Context->IpOptionsLength;
        return NO_ERROR;
    }

    if (*OptionLength < sizeof(INT)) {
        return WSAEFAULT;
    }

    switch (Level) {
    case SOL_SOCKET:
        switch (OptionName) {
        case SO_KEEPALIVE:
            if (Context->SocketType != SOCK_STREAM) {
                return WSAENOPROTOOPT;
            }

            Value = (Context->Flags & CONTEXT_KEEPALIVE) != 0;
            break;

        case SO_DONTROUTE:
            Value = (Context->Flags & CONTEXT_DONTROUTE) != 0;
            break;

        default:
            return WSAENOPROTOOPT;
        }

        break;

    case IPPROTO_TCP:
        if (Context->SocketType != SOCK_STREAM || OptionName != TCP_NODELAY) {
            return WSAENOPROTOOPT;
        }

        Value = (Context->Flags & CONTEXT_NODELAY) != 0;
        break;

    case IPPROTO_IP:
        if (Context->SocketType != SOCK_DGRAM) {
            return WSAENOPROTOOPT;
        }

        switch (OptionName) {
        case IP_MULTICAST_TTL:
            Value = Context->MulticastTtl;
            break;

        case IP_MULTICAST_IF:
            Value = Context->MulticastInterface.s_addr;
            break;

        case IP_MULTICAST_LOOP:
            Value = (Context->Flags & CONTEXT_MULTICAST_LOOP) != 0;
            break;

        default:
            return WSAENOPROTOOPT;
        }

        break;

    default:
        return WSAEINVAL;
    }

    *(PINT)OptionValue = Value;
    *OptionLength = sizeof(INT);
    return NO_ERROR;
}

INT
WINAPI
WSHSetSocketInformation(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN INT Level,
    IN INT OptionName,
    IN PCHAR OptionValue,
    IN INT OptionLength
    )

/*++

Routine Description:

    This routine sets an option of a socket the sockets DLL does not keep:
    it keeps it in the context of the socket and gives it to the transport
    if the socket has the object it is an option of. The context of the
    socket is the one of the socket it was made of: a new context for a
    socket the sockets DLL made in another process, or the context of a
    listening socket for a socket accepted on it.

Arguments:

    HelperDllSocketContext - Supplies the context of the socket.

    SocketHandle, TdiAddressObjectHandle, TdiConnectionObjectHandle - Supply
        the socket and its objects.

    Level, OptionName - Supply the option.

    OptionValue - Supplies the value of the option.

    OptionLength - Supplies the length of the value.

Return Value:

    NO_ERROR, or a Windows Sockets error code.

--*/

{
    PWSHTCPIP_SOCKET_CONTEXT Context = HelperDllSocketContext;
    PWSHTCPIP_SOCKET_CONTEXT Parent;
    UDPMCastReq Membership;
    UDPMCastIFReq Interface;
    struct ip_mreq *Request;
    UCHAR Ttl;
    INT Value;

    if (Level == SOL_INTERNAL && OptionName == SO_CONTEXT) {
        if (OptionLength < sizeof(WSHTCPIP_SOCKET_CONTEXT)) {
            return WSAEINVAL;
        }

        Parent = (PWSHTCPIP_SOCKET_CONTEXT)OptionValue;
        if (Context == NULL) {
            Context = RtlAllocateHeap(RtlProcessHeap(), 0, sizeof(WSHTCPIP_SOCKET_CONTEXT));
            if (Context == NULL) {
                return WSAENOBUFS;
            }

            RtlMoveMemory(Context, Parent, sizeof(WSHTCPIP_SOCKET_CONTEXT));
            *(PWSHTCPIP_SOCKET_CONTEXT *)OptionValue = Context;
            return NO_ERROR;
        }

        Context->Flags = Parent->Flags;
        Context->ReceiveBufferSize = Parent->ReceiveBufferSize;
        return NO_ERROR;
    }

    if (Level == IPPROTO_IP && OptionName == IP_OPTIONS) {
        if (OptionLength < 0 || OptionLength > IP_OPTIONS_MAXIMUM) {
            return WSAEINVAL;
        }

        RtlMoveMemory(Context->IpOptions, OptionValue, OptionLength);
        Context->IpOptionsLength = OptionLength;
        if (TdiAddressObjectHandle == NULL) {
            return NO_ERROR;
        }

        return SetAddressOption(TdiAddressObjectHandle, AddressEntity(Context), AO_OPTION_IPOPTIONS,
                                Context->IpOptions, OptionLength);
    }

    if (Level == IPPROTO_IP && (OptionName == IP_ADD_MEMBERSHIP || OptionName == IP_DROP_MEMBERSHIP)) {
        if (Context->SocketType != SOCK_DGRAM) {
            return WSAENOPROTOOPT;
        }

        if (OptionLength < sizeof(struct ip_mreq)) {
            return WSAEFAULT;
        }

        if (TdiAddressObjectHandle == NULL) {
            return WSAEINVAL;
        }

        Request = (struct ip_mreq *)OptionValue;
        Membership.umr_addr = Request->imr_multiaddr.s_addr;
        Membership.umr_if = Request->imr_interface.s_addr;
        return SetAddressOption(TdiAddressObjectHandle, CL_TL_ENTITY,
                                OptionName == IP_ADD_MEMBERSHIP ? AO_OPTION_ADD_MCAST : AO_OPTION_DEL_MCAST,
                                &Membership, sizeof(Membership));
    }

    if (OptionLength < sizeof(INT)) {
        return WSAEFAULT;
    }

    Value = *(PINT)OptionValue;
    switch (Level) {
    case SOL_SOCKET:
        switch (OptionName) {
        case SO_KEEPALIVE:
            if (Context->SocketType != SOCK_STREAM) {
                return WSAENOPROTOOPT;
            }

            Context->Flags = Value ? Context->Flags | CONTEXT_KEEPALIVE : Context->Flags & ~CONTEXT_KEEPALIVE;
            if (TdiConnectionObjectHandle == NULL) {
                return NO_ERROR;
            }

            return SetConnectionOption(TdiConnectionObjectHandle, TCP_SOCKET_KEEPALIVE, Value != 0);

        case SO_DONTROUTE:

            //
            // TCP/IP routes every datagram: the option is kept and given
            // back, as BSD does on a host that is no router.
            //

            Context->Flags = Value ? Context->Flags | CONTEXT_DONTROUTE : Context->Flags & ~CONTEXT_DONTROUTE;
            return NO_ERROR;

        case SO_RCVBUF:
            if (Context->SocketType != SOCK_STREAM) {
                return WSAENOPROTOOPT;
            }

            Context->ReceiveBufferSize = Value;
            if (TdiConnectionObjectHandle == NULL || Value <= DEFAULT_RECEIVE_BUFFER_SIZE) {
                return NO_ERROR;
            }

            return SetConnectionOption(TdiConnectionObjectHandle, TCP_SOCKET_WINDOW, Value);
        }

        return WSAENOPROTOOPT;

    case IPPROTO_TCP:
        if (Context->SocketType != SOCK_STREAM || OptionName != TCP_NODELAY) {
            return WSAENOPROTOOPT;
        }

        Context->Flags = Value ? Context->Flags | CONTEXT_NODELAY : Context->Flags & ~CONTEXT_NODELAY;
        if (TdiConnectionObjectHandle == NULL) {
            return NO_ERROR;
        }

        return SetConnectionOption(TdiConnectionObjectHandle, TCP_SOCKET_NODELAY, Value != 0);

    case IPPROTO_IP:
        if (Context->SocketType != SOCK_DGRAM) {
            return WSAENOPROTOOPT;
        }

        switch (OptionName) {
        case IP_MULTICAST_TTL:
            if (Value < 0 || Value > 255) {
                return WSAEINVAL;
            }

            Context->MulticastTtl = Value;
            if (TdiAddressObjectHandle == NULL) {
                return NO_ERROR;
            }

            Ttl = (UCHAR)Value;
            return SetAddressOption(TdiAddressObjectHandle, CL_TL_ENTITY, AO_OPTION_MCASTTTL, &Ttl, sizeof(Ttl));

        case IP_MULTICAST_IF:
            Context->MulticastInterface.s_addr = (ULONG)Value;
            if (TdiAddressObjectHandle == NULL) {
                return NO_ERROR;
            }

            Interface.umi_addr = (ULONG)Value;
            return SetAddressOption(TdiAddressObjectHandle, CL_TL_ENTITY, AO_OPTION_MCASTIF, &Interface,
                                    sizeof(Interface));

        case IP_MULTICAST_LOOP:

            //
            // TCP/IP of this version always loops a multicast back to the
            // groups the host is a member of.
            //

            if (Value == 0) {
                return WSAENOPROTOOPT;
            }

            Context->Flags |= CONTEXT_MULTICAST_LOOP;
            return NO_ERROR;
        }

        return WSAENOPROTOOPT;
    }

    return WSAEINVAL;
}

DWORD
WINAPI
WSHGetWinsockMapping(
    OUT PWINSOCK_MAPPING Mapping,
    IN DWORD MappingLength
    )

/*++

Routine Description:

    This routine gives the triples of TCP/IP, which setup records in the
    Winsock key of the transport.

Arguments:

    Mapping - Receives the triples.

    MappingLength - Supplies the length of the buffer.

Return Value:

    The length of the mapping; if more than MappingLength, nothing was
    written.

--*/

{
    DWORD Length = sizeof(WINSOCK_MAPPING) - sizeof(MAPPING_TRIPLE) + sizeof(TcpMappingTriples) +
                   sizeof(UdpMappingTriples);

    if (Length > MappingLength) {
        return Length;
    }

    Mapping->Rows = TRIPLES(TcpMappingTriples) + TRIPLES(UdpMappingTriples);
    Mapping->Columns = sizeof(MAPPING_TRIPLE) / sizeof(DWORD);
    RtlMoveMemory(Mapping->Mapping, TcpMappingTriples, sizeof(TcpMappingTriples));
    RtlMoveMemory((PCHAR)Mapping->Mapping + sizeof(TcpMappingTriples), UdpMappingTriples,
                  sizeof(UdpMappingTriples));
    return Length;
}

INT
WINAPI
WSHGetSockaddrType(
    IN PSOCKADDR Sockaddr,
    IN DWORD SockaddrLength,
    OUT PSOCKADDR_INFO SockaddrInfo
    )

/*++

Routine Description:

    This routine tells what an internet address is: the wildcard, the
    broadcast address, a loopback address or another, and its port: the
    wildcard, a reserved port (below 1024) or another.

Arguments:

    Sockaddr - Supplies the address.

    SockaddrLength - Supplies its length.

    SockaddrInfo - Receives what the address is.

Return Value:

    NO_ERROR, or a Windows Sockets error code.

--*/

{
    PSOCKADDR_IN Address = (PSOCKADDR_IN)Sockaddr;
    ULONG Host;
    USHORT Port;

    if (SockaddrLength < sizeof(SOCKADDR_IN)) {
        return WSAEFAULT;
    }

    if (Address->sin_family != AF_INET) {
        return WSAEAFNOSUPPORT;
    }

    Host = ntohl(Address->sin_addr.s_addr);
    if (Host == INADDR_ANY) {
        SockaddrInfo->AddressInfo = SockaddrAddressInfoWildcard;
    } else if (Host == INADDR_BROADCAST) {
        SockaddrInfo->AddressInfo = SockaddrAddressInfoBroadcast;
    } else if ((Host >> 24) == 127) {
        SockaddrInfo->AddressInfo = SockaddrAddressInfoLoopback;
    } else {
        SockaddrInfo->AddressInfo = SockaddrAddressInfoNormal;
    }

    Port = ntohs(Address->sin_port);
    if (Port == 0) {
        SockaddrInfo->EndpointInfo = SockaddrEndpointInfoWildcard;
    } else if (Port < IPPORT_RESERVED) {
        SockaddrInfo->EndpointInfo = SockaddrEndpointInfoReserved;
    } else {
        SockaddrInfo->EndpointInfo = SockaddrEndpointInfoNormal;
    }

    RtlZeroMemory(Address->sin_zero, sizeof(Address->sin_zero));
    return NO_ERROR;
}

INT
WINAPI
WSHGetWildcardSockaddr(
    IN PVOID HelperDllSocketContext,
    OUT PSOCKADDR Sockaddr,
    OUT PINT SockaddrLength
    )

/*++

Routine Description:

    This routine gives the wildcard internet address, which an unbound
    socket is bound to.

Arguments:

    HelperDllSocketContext - Supplies the context of the socket.

    Sockaddr - Receives the address.

    SockaddrLength - Supplies the length of the buffer, and receives that of
        the address.

Return Value:

    NO_ERROR, or a Windows Sockets error code.

--*/

{
    if (*SockaddrLength < sizeof(SOCKADDR_IN)) {
        return WSAEFAULT;
    }

    RtlZeroMemory(Sockaddr, sizeof(SOCKADDR_IN));
    Sockaddr->sa_family = AF_INET;
    *SockaddrLength = sizeof(SOCKADDR_IN);
    return NO_ERROR;
}

INT
WINAPI
WSHEnumProtocols(
    IN LPINT lpiProtocols,
    IN LPWSTR lpTransportKeyName,
    IN OUT LPVOID lpProtocolBuffer,
    IN OUT LPDWORD lpdwBufferLength
    )

/*++

Routine Description:

    This routine describes TCP and UDP for EnumProtocols, those of them the
    caller asks for; the names go at the end of the buffer.

Arguments:

    lpiProtocols - Supplies the protocols asked for, or NULL for all.

    lpTransportKeyName - Supplies the name of the transport key.

    lpProtocolBuffer - Receives the descriptions.

    lpdwBufferLength - Supplies the length of the buffer, and receives the
        length needed if it is too small.

Return Value:

    The count of protocols described, or -1 if the buffer is too small.

--*/

{
    PPROTOCOL_INFOW Info = lpProtocolBuffer;
    PWCHAR Name;
    BOOL UseTcp = FALSE;
    BOOL UseUdp = FALSE;
    DWORD Required;
    INT Count = 0;
    DWORD Index;

    if (lpiProtocols != NULL) {
        for (Index = 0; lpiProtocols[Index] != 0; Index += 1) {
            UseTcp |= lpiProtocols[Index] == IPPROTO_TCP;
            UseUdp |= lpiProtocols[Index] == IPPROTO_UDP;
        }
    } else {
        UseTcp = UseUdp = TRUE;
    }

    if (!UseTcp && !UseUdp) {
        *lpdwBufferLength = 0;
        return 0;
    }

    Required = sizeof(PROTOCOL_INFOW) * 2 + (wcslen(TCP_NAME) + 1) * sizeof(WCHAR) +
               (wcslen(UDP_NAME) + 1) * sizeof(WCHAR);
    if (Required > *lpdwBufferLength) {
        *lpdwBufferLength = Required;
        return -1;
    }

    Name = (PWCHAR)((PCHAR)lpProtocolBuffer + *lpdwBufferLength);
    if (UseTcp) {
        Info->dwServiceFlags = XP_GUARANTEED_DELIVERY | XP_GUARANTEED_ORDER | XP_GRACEFUL_CLOSE |
                               XP_EXPEDITED_DATA | XP_FRAGMENTATION;
        Info->iAddressFamily = AF_INET;
        Info->iMaxSockAddr = sizeof(SOCKADDR_IN);
        Info->iMinSockAddr = sizeof(SOCKADDR_IN);
        Info->iSocketType = SOCK_STREAM;
        Info->iProtocol = IPPROTO_TCP;
        Info->dwMessageSize = 0;
        Name -= wcslen(TCP_NAME) + 1;
        Info->lpProtocol = Name;
        wcscpy(Name, TCP_NAME);
        Info += 1;
        Count += 1;
    }

    if (UseUdp) {
        Info->dwServiceFlags = XP_CONNECTIONLESS | XP_MESSAGE_ORIENTED | XP_SUPPORTS_BROADCAST |
                               XP_SUPPORTS_MULTICAST | XP_FRAGMENTATION;
        Info->iAddressFamily = AF_INET;
        Info->iMaxSockAddr = sizeof(SOCKADDR_IN);
        Info->iMinSockAddr = sizeof(SOCKADDR_IN);
        Info->iSocketType = SOCK_DGRAM;
        Info->iProtocol = IPPROTO_UDP;
        Info->dwMessageSize = UDP_MESSAGE_MAXIMUM;
        Name -= wcslen(UDP_NAME) + 1;
        Info->lpProtocol = Name;
        wcscpy(Name, UDP_NAME);
        Count += 1;
    }

    *lpdwBufferLength = Required;
    return Count;
}
