//
// Handles of the build machine's process token and of the registry services
// (hostnt.c, ntreg.c), told from its own handles by their mark.
//

#define HOST_MAGIC  0x74736f48
#define HOST_TOKEN  2
#define HOST_KEY    3

typedef struct _HOST_HANDLE {
    ULONG Magic;
    ULONG Type;
    PVOID Hive;
    ULONG Cell;
} HOST_HANDLE, *PHOST_HANDLE;

PHOST_HANDLE
HostNewHandle (
    ULONG Type
    );
