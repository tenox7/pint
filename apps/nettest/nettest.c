/*++

Module Name:

    nettest.c

Abstract:

    A Win32 console program that measures a TCP connection without the LAN
    Manager stack: it connects to a peer and sends or receives a number of
    megabytes, printing the rate each second, every send or receive that
    waited more than a second, and the total.

    nettest send HOST PORT [MB]    the peer reads, e.g. nc -l PORT > /dev/null
    nettest recv HOST PORT [MB]    the peer writes, e.g. nc -l PORT < FILE

--*/

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192

static char Buffer[BUFFER_SIZE];

int
main(
    int argc,
    char *argv[]
    )
{
    WSADATA WsaData;
    SOCKET Socket;
    struct sockaddr_in Address;
    BOOL Send;
    ULONG Total;
    ULONG Done = 0;
    ULONG Second = 0;
    ULONG Start, Mark, Last, Now;
    int Count = 0;

    if (argc < 4 || (strcmp(argv[1], "send") != 0 && strcmp(argv[1], "recv") != 0)) {
        printf("usage: nettest send|recv HOST PORT [MB]\n");
        return 1;
    }

    Send = strcmp(argv[1], "send") == 0;
    Total = (argc > 4 ? atol(argv[4]) : 10) * 1024 * 1024;
    if (WSAStartup(MAKEWORD(1, 1), &WsaData) != 0) {
        printf("no Windows Sockets\n");
        return 1;
    }

    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = htons((u_short)atoi(argv[3]));
    Address.sin_addr.s_addr = inet_addr(argv[2]);
    Socket = socket(AF_INET, SOCK_STREAM, 0);
    if (Socket == INVALID_SOCKET || connect(Socket, (struct sockaddr *)&Address, sizeof(Address)) != 0) {
        printf("connect: error %d\n", WSAGetLastError());
        return 1;
    }

    memset(Buffer, 'x', sizeof(Buffer));
    Start = Mark = Last = GetTickCount();
    while (Done < Total) {
        Count = Send ? send(Socket, Buffer, (int)min(BUFFER_SIZE, Total - Done), 0) :
                       recv(Socket, Buffer, BUFFER_SIZE, 0);
        if (Count <= 0) {
            break;
        }

        Now = GetTickCount();
        if (Now - Last > 1000) {
            printf("waited %lu ms at %lu KB\n", Now - Last, Done / 1024);
        }

        Last = Now;
        Done += Count;
        if (Now - Mark >= 1000) {
            printf("%3lu s %6lu KB/s\n", (Now - Start) / 1000, (Done - Second) / (Now - Mark) * 1000 / 1024);
            Second = Done;
            Mark = Now;
        }
    }

    if (Count < 0) {
        printf("%s: error %d after %lu KB\n", Send ? "send" : "recv", WSAGetLastError(), Done / 1024);
    }

    Now = GetTickCount();
    printf("%lu KB in %lu ms, %lu KB/s\n", Done / 1024, Now - Start, Done / (Now - Start + 1) * 1000 / 1024);
    closesocket(Socket);
    WSACleanup();
    return Count < 0;
}
