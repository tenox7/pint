/*++

Module Name:

    ipconfig.c

Abstract:

    The TCP/IP configuration utility: it prints the TCP/IP configuration of
    the machine and of each adapter TCP/IP is bound to (the registry), and
    releases or renews the DHCP lease of an adapter (dhcpcsvc.dll).

    ipconfig [/all | /renew [adapter] | /release [adapter]]

--*/

#include <windows.h>
#include <dhcpcapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SERVICES        "SYSTEM\\CurrentControlSet\\Services\\"
#define NETWORK_CARDS   "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards"
#define VALUE_SIZE      1024

static void
Usage(
    void
    )
{
    printf("\nusage: ipconfig [/? | /all | /release [adapter] | /renew [adapter]]\n\n"
           "    /?       Display this help message.\n"
           "    /all     Display full configuration information.\n"
           "    /release Release the IP address for the specified adapter.\n"
           "    /renew   Renew the IP address for the specified adapter.\n\n"
           "The default is to display only the IP address, subnet mask and\n"
           "default gateway for each adapter bound to TCP/IP.\n\n"
           "For Release and Renew, if no adapter name is specified, then the IP address\n"
           "leases for all adapters bound to TCP/IP will be released or renewed.\n");
}

static BOOL
QueryValue(
    char *Key,
    char *Name,
    char *Value,
    DWORD *Type
    )
{
    HKEY Handle;
    DWORD Size = VALUE_SIZE - 2;
    LONG Status;

    Value[0] = Value[1] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, Key, 0, KEY_QUERY_VALUE, &Handle) != ERROR_SUCCESS) {
        return FALSE;
    }

    Status = RegQueryValueExA(Handle, Name, NULL, Type, (LPBYTE)Value, &Size);
    RegCloseKey(Handle);
    if (Status == ERROR_SUCCESS) {
        Value[Size] = Value[Size + 1] = '\0';
    }

    return Status == ERROR_SUCCESS;
}

static char *
String(
    char *Key,
    char *Name,
    char *Value
    )
{
    DWORD Type;

    if (!QueryValue(Key, Name, Value, &Type) || (Type != REG_SZ && Type != REG_MULTI_SZ && Type != REG_EXPAND_SZ)) {
        Value[0] = Value[1] = '\0';
    }

    return Value;
}

static DWORD
Dword(
    char *Key,
    char *Name,
    DWORD Default
    )
{
    char Value[VALUE_SIZE];
    DWORD Type;

    if (!QueryValue(Key, Name, Value, &Type) || Type != REG_DWORD) {
        return Default;
    }

    return *(DWORD *)Value;
}

//
// A value of one string, or of several (a REG_MULTI_SZ, or a list separated
// by spaces or commas): each on its own line under the label.
//

static void
PrintList(
    char *Label,
    char *Value
    )
{
    char *Item;
    char *After;
    char *Next;
    BOOL First = TRUE;

    for (Item = Value; *Item != '\0'; Item = After) {
        After = Item + strlen(Item) + 1;
        for (Next = strtok(Item, " ,"); Next != NULL; Next = strtok(NULL, " ,")) {
            if (strcmp(Next, "0.0.0.0") == 0) {
                continue;
            }

            printf(First ? "\t%s : %s\n" : "\t%s   %s\n", First ? Label : "                            ", Next);
            First = FALSE;
        }
    }

    if (First) {
        printf("\t%s : \n", Label);
    }
}

static void
PrintTime(
    char *Label,
    DWORD Seconds
    )
{
    time_t Time = (time_t)Seconds;
    char *Text = ctime(&Time);

    printf("\t%s : %s", Label, Text != NULL ? Text : "\n");
}

static char *
NodeType(
    DWORD Type
    )
{
    switch (Type) {
    case 1: return "Broadcast";
    case 2: return "Peer-Peer";
    case 4: return "Mixed";
    case 8: return "Hybrid";
    }

    return "Unknown";
}

static void
PrintHardwareAddress(
    char *Key
    )
{
    BYTE Type;
    LPBYTE Address;
    DWORD Length;
    DWORD Index;
    DWORD Context = Dword(Key, "IpInterfaceContext", 0xFFFFFFFF);

    if (Context == 0xFFFFFFFF || DhcpQueryHWInfo(Context, &Type, &Address, &Length) != ERROR_SUCCESS) {
        return;
    }

    printf("\tPhysical Address. . . . . : ");
    for (Index = 0; Index < Length; Index += 1) {
        printf(Index == 0 ? "%02X" : "-%02X", Address[Index]);
    }

    printf("\n");
    LocalFree(Address);
}

static void
PrintDescription(
    char *Adapter
    )
{
    char Key[MAX_PATH];
    char Name[64];
    char Value[VALUE_SIZE];
    HKEY Handle;
    DWORD Index;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, NETWORK_CARDS, 0, KEY_ENUMERATE_SUB_KEYS, &Handle) != ERROR_SUCCESS) {
        return;
    }

    for (Index = 0; RegEnumKeyA(Handle, Index, Name, sizeof(Name)) == ERROR_SUCCESS; Index += 1) {
        sprintf(Key, "%s\\%s", NETWORK_CARDS, Name);
        if (_stricmp(String(Key, "ServiceName", Value), Adapter) != 0) {
            continue;
        }

        printf("\tDescription . . . . . . . : %s\n", String(Key, "Description", Value));
        break;
    }

    RegCloseKey(Handle);
}

static void
PrintAdapter(
    char *Adapter,
    BOOL All
    )
{
    char Key[MAX_PATH];
    char Value[VALUE_SIZE];
    BOOL Dhcp;

    sprintf(Key, SERVICES "%s\\Parameters\\Tcpip", Adapter);
    Dhcp = Dword(Key, "EnableDHCP", 0) != 0;

    printf("\nEthernet adapter %s:\n\n", Adapter);
    if (All) {
        PrintDescription(Adapter);
        PrintHardwareAddress(Key);
        printf("\tDHCP Enabled. . . . . . . : %s\n", Dhcp ? "Yes" : "No");
    }

    PrintList("IP Address. . . . . . . . ", String(Key, Dhcp ? "DhcpIPAddress" : "IPAddress", Value));
    PrintList("Subnet Mask . . . . . . . ", String(Key, Dhcp ? "DhcpSubnetMask" : "SubnetMask", Value));
    String(Key, "DefaultGateway", Value);
    if (Dhcp && Value[0] == '\0') {
        String(Key, "DhcpDefaultGateway", Value);
    }

    PrintList("Default Gateway . . . . . ", Value);
    if (!All || !Dhcp) {
        return;
    }

    PrintList("DHCP Server . . . . . . . ", String(Key, "DhcpServer", Value));
    if (Dword(Key, "LeaseObtainedTime", 0) != 0) {
        PrintTime("Lease Obtained. . . . . . ", Dword(Key, "LeaseObtainedTime", 0));
        PrintTime("Lease Expires . . . . . . ", Dword(Key, "LeaseTerminatesTime", 0));
    }
}

//
// The adapters TCP/IP is bound to: the names after \Device\ of its Bind list.
//

static char *
Adapters(
    char *List
    )
{
    char Bind[VALUE_SIZE];
    char *Device;
    char *Out = List;

    String(SERVICES "Tcpip\\Linkage", "Bind", Bind);
    for (Device = Bind; *Device != '\0'; Device += strlen(Device) + 1) {
        char *Name = strrchr(Device, '\\');

        strcpy(Out, Name != NULL ? Name + 1 : Device);
        Out += strlen(Out) + 1;
    }

    *Out = '\0';
    return List;
}

static int
Lease(
    BOOL Renew,
    char *Only
    )
{
    char List[VALUE_SIZE];
    char *Adapter;
    WCHAR Name[MAX_PATH];
    DWORD Status;
    BOOL Found = FALSE;
    char Key[MAX_PATH];

    for (Adapter = Adapters(List); *Adapter != '\0'; Adapter += strlen(Adapter) + 1) {
        if (Only != NULL && _stricmp(Only, Adapter) != 0) {
            continue;
        }

        Found = TRUE;
        sprintf(Key, SERVICES "%s\\Parameters\\Tcpip", Adapter);
        if (Dword(Key, "EnableDHCP", 0) == 0) {
            if (Only != NULL) {
                printf("\nAdapter %s is not enabled for DHCP.\n", Adapter);
            }
            continue;
        }

        MultiByteToWideChar(CP_ACP, 0, Adapter, -1, Name, MAX_PATH);
        Status = Renew ? DhcpAcquireParameters(Name) : DhcpReleaseParameters(Name);
        if (Status != ERROR_SUCCESS) {
            printf("\nDHCP %s failed for adapter %s: error %u.\n", Renew ? "renew" : "release", Adapter, Status);
            continue;
        }

        if (!Renew) {
            printf("\nIP address successfully released for adapter %s.\n", Adapter);
        }
    }

    if (Only != NULL && !Found) {
        printf("\nAdapter %s was not found.\n", Only);
        return 1;
    }

    return 0;
}

int __cdecl
main(
    int argc,
    char **argv
    )
{
    char List[VALUE_SIZE];
    char Value[VALUE_SIZE];
    char *Adapter;
    char *Tcpip = SERVICES "Tcpip\\Parameters";
    char *NetBT = SERVICES "NetBT\\Parameters";
    BOOL All = FALSE;

    if (argc > 1) {
        char *Option = argv[1];

        if (Option[0] != '/' && Option[0] != '-') {
            Usage();
            return 1;
        }

        Option += 1;
        if (_stricmp(Option, "all") == 0 && argc == 2) {
            All = TRUE;
        } else if (_stricmp(Option, "release") == 0 && argc <= 3) {
            printf("\nWindows NT IP Configuration\n");
            return Lease(FALSE, argc == 3 ? argv[2] : NULL);
        } else if (_stricmp(Option, "renew") == 0 && argc <= 3) {
            if (Lease(TRUE, argc == 3 ? argv[2] : NULL) != 0) {
                return 1;
            }
        } else {
            Usage();
            return 1;
        }
    }

    printf("\nWindows NT IP Configuration\n");
    if (All) {
        printf("\n");
        printf("\tHost Name . . . . . . . . : %s", String(Tcpip, "Hostname", Value));
        if (String(Tcpip, "Domain", Value)[0] != '\0') {
            printf(".%s", Value);
        }

        printf("\n");
        String(Tcpip, "NameServer", Value);
        if (Value[0] == '\0') {
            String(Tcpip, "DhcpNameServer", Value);
        }

        PrintList("DNS Servers . . . . . . . ", Value);
        printf("\tNode Type . . . . . . . . : %s\n",
               NodeType(Dword(NetBT, "NodeType", Dword(NetBT, "DhcpNodeType", 1))));
        printf("\tNetBIOS Scope ID. . . . . : %s\n", String(NetBT, "ScopeId", Value));
        printf("\tIP Routing Enabled. . . . : %s\n", Dword(Tcpip, "IPEnableRouter", 0) ? "Yes" : "No");
        printf("\tWINS Proxy Enabled. . . . : %s\n", Dword(NetBT, "EnableProxy", 0) ? "Yes" : "No");
        printf("\tNetBIOS Resolution Uses DNS : %s\n", Dword(NetBT, "EnableDNS", 0) ? "Yes" : "No");
    }

    for (Adapter = Adapters(List); *Adapter != '\0'; Adapter += strlen(Adapter) + 1) {
        PrintAdapter(Adapter, All);
    }

    return 0;
}
