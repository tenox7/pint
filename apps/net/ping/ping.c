/*++

Module Name:

    ping.c

Abstract:

    The TCP/IP ping utility: it sends ICMP echo requests to a host (icmp.dll)
    and prints the replies, or why there was none.

    ping [-t] [-a] [-n count] [-l size] [-f] [-i TTL] [-v TOS] [-w timeout]
         destination

--*/

#include <windows.h>
#include <winsock.h>
#include <ipexport.h>
#include <icmpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_COUNT           4
#define DEFAULT_SIZE            32
#define DEFAULT_TTL             32
#define DEFAULT_TIMEOUT         1000
#define MAXIMUM_SIZE            8192
#define REPLY_EXTRA             (8 + MAX_OPT_SIZE)

static void
Usage(
    void
    )
{
    printf("\nUsage: ping [-t] [-a] [-n count] [-l size] [-f] [-i TTL] [-v TOS] [-w timeout]\n"
           "            destination\n\n"
           "Options:\n"
           "    -t             Ping the destination until interrupted.\n"
           "    -a             Resolve addresses to hostnames.\n"
           "    -n count       Number of echo requests to send.\n"
           "    -l size        Send buffer size.\n"
           "    -f             Set Don't Fragment flag in packet.\n"
           "    -i TTL         Time To Live.\n"
           "    -v TOS         Type Of Service.\n"
           "    -w timeout     Timeout in milliseconds to wait for each reply.\n");
}

static char *
StatusText(
    IP_STATUS Status
    )
{
    switch (Status) {
    case IP_BUF_TOO_SMALL:          return "Buffer too small.";
    case IP_DEST_NET_UNREACHABLE:   return "Destination net unreachable.";
    case IP_DEST_HOST_UNREACHABLE:  return "Destination host unreachable.";
    case IP_DEST_PROT_UNREACHABLE:  return "Destination protocol unreachable.";
    case IP_DEST_PORT_UNREACHABLE:  return "Destination port unreachable.";
    case IP_NO_RESOURCES:           return "No resources.";
    case IP_BAD_OPTION:             return "Bad option specified.";
    case IP_HW_ERROR:               return "Hardware error.";
    case IP_PACKET_TOO_BIG:         return "Packet too big.";
    case IP_REQ_TIMED_OUT:          return "Request timed out.";
    case IP_BAD_REQ:                return "Bad request.";
    case IP_BAD_ROUTE:              return "Bad route.";
    case IP_TTL_EXPIRED_TRANSIT:    return "TTL expired in transit.";
    case IP_TTL_EXPIRED_REASSEM:    return "TTL expired during reassembly.";
    case IP_PARAM_PROBLEM:          return "Parameter problem.";
    case IP_SOURCE_QUENCH:          return "Source quench received.";
    case IP_OPTION_TOO_BIG:         return "Option too big.";
    case IP_BAD_DESTINATION:        return "Bad destination.";
    }

    return "General failure.";
}

static int
Number(
    int Argc,
    char **Argv,
    int *Index,
    unsigned long Minimum,
    unsigned long Maximum,
    unsigned long *Value
    )
{
    char *End;

    if (*Index + 1 >= Argc) {
        printf("Value must be supplied for option %s.\n", Argv[*Index]);
        return 0;
    }

    *Index += 1;
    *Value = strtoul(Argv[*Index], &End, 0);
    if (*End != 0 || *Value < Minimum || *Value > Maximum) {
        printf("Bad value for option %s, valid range is from %lu to %lu.\n", Argv[*Index - 1], Minimum, Maximum);
        return 0;
    }

    return 1;
}

int __cdecl
main(
    int argc,
    char **argv
    )
{
    IP_OPTION_INFORMATION Options;
    PICMP_ECHO_REPLY Reply;
    struct hostent *Host;
    struct in_addr Address;
    WSADATA WsaData;
    HANDLE Icmp;
    char *Destination = NULL;
    char Name[256];
    char *Data;
    unsigned long Count = DEFAULT_COUNT;
    unsigned long Size = DEFAULT_SIZE;
    unsigned long Ttl = DEFAULT_TTL;
    unsigned long Tos = 0;
    unsigned long Timeout = DEFAULT_TIMEOUT;
    unsigned long Sent;
    unsigned long Index;
    int Forever = 0;
    int Resolve = 0;
    int DontFragment = 0;
    int Argument;
    DWORD Replies;

    for (Argument = 1; Argument < argc; Argument += 1) {
        if (argv[Argument][0] != '-' && argv[Argument][0] != '/') {
            Destination = argv[Argument];
            continue;
        }

        switch (argv[Argument][1]) {
        case 't': Forever = 1; break;
        case 'a': Resolve = 1; break;
        case 'f': DontFragment = 1; break;
        case 'n': if (!Number(argc, argv, &Argument, 1, 0xFFFFFFFF, &Count)) return 1; break;
        case 'l': if (!Number(argc, argv, &Argument, 0, MAXIMUM_SIZE, &Size)) return 1; break;
        case 'i': if (!Number(argc, argv, &Argument, 1, 255, &Ttl)) return 1; break;
        case 'v': if (!Number(argc, argv, &Argument, 0, 255, &Tos)) return 1; break;
        case 'w': if (!Number(argc, argv, &Argument, 0, 0xFFFFFFFF, &Timeout)) return 1; break;
        default:
            Usage();
            return 1;
        }
    }

    if (Destination == NULL) {
        Usage();
        return 1;
    }

    if (WSAStartup(0x0101, &WsaData) != 0) {
        printf("Unable to initialize Windows Sockets.\n");
        return 1;
    }

    Address.s_addr = inet_addr(Destination);
    strncpy(Name, Destination, sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = 0;
    if (Address.s_addr == INADDR_NONE) {
        Host = gethostbyname(Destination);
        if (Host == NULL) {
            printf("Bad IP address %s.\n", Destination);
            return 1;
        }

        memcpy(&Address, Host->h_addr, sizeof(Address));
        strncpy(Name, Host->h_name, sizeof(Name) - 1);
    } else if (Resolve) {
        Host = gethostbyaddr((char *)&Address, sizeof(Address), AF_INET);
        if (Host != NULL) {
            strncpy(Name, Host->h_name, sizeof(Name) - 1);
        }
    }

    Icmp = IcmpCreateFile();
    Data = malloc(Size + 1);
    Reply = malloc(sizeof(ICMP_ECHO_REPLY) + Size + REPLY_EXTRA);
    if (Icmp == INVALID_HANDLE_VALUE || Data == NULL || Reply == NULL) {
        printf("Unable to contact IP driver, error code %lu.\n", GetLastError());
        return 1;
    }

    for (Index = 0; Index < Size; Index += 1) {
        Data[Index] = (char)('a' + Index % 23);
    }

    if (strcmp(Name, inet_ntoa(Address)) != 0) {
        printf("\nPinging %s [%s] with %lu bytes of data:\n\n", Name, inet_ntoa(Address), Size);
    } else {
        printf("\nPinging %s with %lu bytes of data:\n\n", inet_ntoa(Address), Size);
    }

    Options.Ttl = (UCHAR)Ttl;
    Options.Tos = (UCHAR)Tos;
    Options.Flags = DontFragment ? IP_FLAG_DF : 0;
    Options.OptionsSize = 0;
    Options.OptionsData = NULL;
    for (Sent = 0; Forever || Sent < Count; Sent += 1) {
        Replies = IcmpSendEcho(Icmp, Address.s_addr, Data, (WORD)Size, &Options, Reply,
                               sizeof(ICMP_ECHO_REPLY) + Size + REPLY_EXTRA, Timeout);
        if (Replies == 0) {
            printf("%s\n", StatusText(GetLastError()));
        } else if (Reply->Status != IP_SUCCESS) {
            Address.s_addr = Reply->Address;
            printf("Reply from %s: %s\n", inet_ntoa(Address), StatusText(Reply->Status));
        } else {
            Address.s_addr = Reply->Address;
            printf("Reply from %s: bytes=%u time%s%lums TTL=%u\n", inet_ntoa(Address), Reply->DataSize,
                   Reply->RoundTripTime < 10 ? "<" : "=", Reply->RoundTripTime < 10 ? 10 : Reply->RoundTripTime,
                   Reply->Options.Ttl);
        }

        if (Forever || Sent + 1 < Count) {
            Sleep(Replies != 0 && Reply->RoundTripTime < 1000 ? 1000 - Reply->RoundTripTime : 0);
        }
    }

    IcmpCloseHandle(Icmp);
    WSACleanup();
    return 0;
}
