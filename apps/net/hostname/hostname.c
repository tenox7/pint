/*++

Module Name:

    hostname.c

Abstract:

    Prints the TCP/IP host name of the machine.

    hostname

--*/

#include <windows.h>
#include <winsock.h>
#include <stdio.h>

int __cdecl
main(
    int argc,
    char **argv
    )
{
    WSADATA Data;
    char Name[256];

    if (argc > 1) {
        printf("\nusage: hostname\n");
        return 1;
    }

    if (WSAStartup(0x0101, &Data) != 0) {
        printf("hostname: Windows Sockets could not start.\n");
        return 1;
    }

    if (gethostname(Name, sizeof(Name)) != 0) {
        printf("gethostname: error %d.\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    printf("%s\n", Name);
    WSACleanup();
    return 0;
}
