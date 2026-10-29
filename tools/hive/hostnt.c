/*++

Module Name:

    hostnt.c

Abstract:

    What the registry tools (REGINI, HIVEDMP) take from the system that the build
    machine does not give as NT 3.5 did: the process token Microsoft built the NT 3.5
    hives with, as recorded in their security descriptors (owner BUILTIN\Administrators,
    primary group <build domain>\Domain Users, no privileges but restore and backup),
    the handles of the registry services of ntreg.c, and the string allocator and NLS
    tables of the RTL they link, the part of LdrpInitializeProcess they need. Files,
    memory, time and path names are the build machine's.

--*/

#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#include <ntseapi.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"

PRTL_ALLOCATE_STRING_ROUTINE RtlAllocateStringRoutine;
PRTL_FREE_STRING_ROUTINE RtlFreeStringRoutine;

static PVOID
HostAllocateString (ULONG NumberOfBytes)
{
    return RtlAllocateHeap(RtlProcessHeap(), 0, NumberOfBytes);
}

static VOID
HostFreeString (PVOID Buffer)
{
    RtlFreeHeap(RtlProcessHeap(), 0, Buffer);
}

PHOST_HANDLE
HostNewHandle (ULONG Type)
{
    PHOST_HANDLE Handle = calloc(1, sizeof(HOST_HANDLE));

    Handle->Magic = HOST_MAGIC;
    Handle->Type = Type;
    return Handle;
}

static BOOLEAN
HostHandle (HANDLE Handle)
{
    PHOST_HANDLE h = (PHOST_HANDLE)Handle;

    return (ULONG)Handle >= 0x10000 && (ULONG)Handle < 0xffff0000 && h->Magic == HOST_MAGIC;
}

NTSTATUS
NtClose (HANDLE Handle)
{
    if (!HostHandle(Handle)) {
        return CloseHandle(Handle) ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
    }

    ((PHOST_HANDLE)Handle)->Magic = 0;
    free(Handle);
    return STATUS_SUCCESS;
}

//
// The process token.
//

static SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
static SID_IDENTIFIER_AUTHORITY WorldAuthority = SECURITY_WORLD_SID_AUTHORITY;
static ULONG BuildDomain[4] = { 21, 2115134065, 1453017417, 1990678075 };

static PSID
HostSid (PSID_IDENTIFIER_AUTHORITY Authority, ULONG Count, PULONG SubAuthorities, ULONG Rid)
{
    PSID Sid = malloc(RtlLengthRequiredSid(Count + 1));
    ULONG i;

    RtlInitializeSid(Sid, Authority, (UCHAR)(Count + 1));
    for (i = 0; i < Count; i += 1) {
        *RtlSubAuthoritySid(Sid, i) = SubAuthorities[i];
    }

    *RtlSubAuthoritySid(Sid, Count) = Rid;
    return Sid;
}

static struct {
    PSID User;
    PSID Owner;
    PSID PrimaryGroup;
    PSID Groups[4];
    ULONG GroupAttributes[4];
    PACL DefaultDacl;
    BOOLEAN Restore;
    BOOLEAN Backup;
} HostToken;

static VOID
HostInitializeToken (VOID)
{
    ULONG BuiltinDomain = SECURITY_BUILTIN_DOMAIN_RID;
    PSID System;

    HostToken.User = HostSid(&NtAuthority, 4, BuildDomain, DOMAIN_USER_RID_ADMIN);
    HostToken.PrimaryGroup = HostSid(&NtAuthority, 4, BuildDomain, DOMAIN_GROUP_RID_USERS);
    HostToken.Owner = HostSid(&NtAuthority, 1, &BuiltinDomain, DOMAIN_ALIAS_RID_ADMINS);
    HostToken.Groups[0] = HostToken.PrimaryGroup;
    HostToken.Groups[1] = HostSid(&WorldAuthority, 0, NULL, SECURITY_WORLD_RID);
    HostToken.Groups[2] = HostToken.Owner;
    HostToken.Groups[3] = HostSid(&NtAuthority, 1, &BuiltinDomain, DOMAIN_ALIAS_RID_USERS);
    HostToken.GroupAttributes[0] = SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED;
    HostToken.GroupAttributes[1] = HostToken.GroupAttributes[0];
    HostToken.GroupAttributes[2] = HostToken.GroupAttributes[0] | SE_GROUP_OWNER;
    HostToken.GroupAttributes[3] = HostToken.GroupAttributes[0];
    System = HostSid(&NtAuthority, 0, NULL, SECURITY_LOCAL_SYSTEM_RID);
    HostToken.DefaultDacl = malloc(256);
    RtlCreateAcl(HostToken.DefaultDacl, 256, ACL_REVISION2);
    RtlAddAccessAllowedAce(HostToken.DefaultDacl, ACL_REVISION2, GENERIC_ALL, HostToken.Owner);
    RtlAddAccessAllowedAce(HostToken.DefaultDacl, ACL_REVISION2, GENERIC_ALL, System);
}

NTSTATUS
NtOpenProcessToken (HANDLE ProcessHandle, ACCESS_MASK DesiredAccess, PHANDLE TokenHandle)
{
    *TokenHandle = (HANDLE)HostNewHandle(HOST_TOKEN);
    return STATUS_SUCCESS;
}

NTSTATUS
NtOpenThreadToken (HANDLE ThreadHandle, ACCESS_MASK DesiredAccess, BOOLEAN OpenAsSelf,
                   PHANDLE TokenHandle)
{
    return STATUS_NO_TOKEN;
}

static NTSTATUS
HostTokenSids (PVOID TokenInformation, ULONG Length, PULONG ReturnLength,
               PSID *Sids, PULONG Attributes, ULONG Count, BOOLEAN Array)
{
    ULONG Header = Array ? sizeof(ULONG) + Count * sizeof(SID_AND_ATTRIBUTES) : sizeof(PSID);
    ULONG Needed = Header;
    PUCHAR Data;
    ULONG i;

    for (i = 0; i < Count; i += 1) {
        Needed += RtlLengthSid(Sids[i]);
    }

    *ReturnLength = Needed;
    if (Length < Needed) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Data = (PUCHAR)TokenInformation + Header;
    if (Array) {
        ((PTOKEN_GROUPS)TokenInformation)->GroupCount = Count;
    }

    for (i = 0; i < Count; i += 1) {
        RtlCopySid(RtlLengthSid(Sids[i]), Data, Sids[i]);
        if (Array) {
            ((PTOKEN_GROUPS)TokenInformation)->Groups[i].Sid = Data;
            ((PTOKEN_GROUPS)TokenInformation)->Groups[i].Attributes = Attributes[i];
        } else {
            *(PSID *)TokenInformation = Data;
        }

        Data += RtlLengthSid(Sids[i]);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtQueryInformationToken (HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass,
                         PVOID TokenInformation, ULONG TokenInformationLength, PULONG ReturnLength)
{
    ULONG Zero = 0;

    switch (TokenInformationClass) {
    case TokenUser:
        if (TokenInformationLength >= sizeof(TOKEN_USER)) {
            ((PTOKEN_USER)TokenInformation)->User.Attributes = 0;
        }

        return HostTokenSids(TokenInformation, TokenInformationLength, ReturnLength,
                             &HostToken.User, &Zero, 1, FALSE);
    case TokenGroups:
        return HostTokenSids(TokenInformation, TokenInformationLength, ReturnLength,
                             HostToken.Groups, HostToken.GroupAttributes, 4, TRUE);
    case TokenOwner:
        return HostTokenSids(TokenInformation, TokenInformationLength, ReturnLength,
                             &HostToken.Owner, &Zero, 1, FALSE);
    case TokenPrimaryGroup:
        return HostTokenSids(TokenInformation, TokenInformationLength, ReturnLength,
                             &HostToken.PrimaryGroup, &Zero, 1, FALSE);
    case TokenDefaultDacl:
        *ReturnLength = sizeof(TOKEN_DEFAULT_DACL) + HostToken.DefaultDacl->AclSize;
        if (TokenInformationLength < *ReturnLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        ((PTOKEN_DEFAULT_DACL)TokenInformation)->DefaultDacl =
            (PACL)((PUCHAR)TokenInformation + sizeof(TOKEN_DEFAULT_DACL));
        memcpy(((PTOKEN_DEFAULT_DACL)TokenInformation)->DefaultDacl, HostToken.DefaultDacl,
               HostToken.DefaultDacl->AclSize);
        return STATUS_SUCCESS;
    case TokenType:
        *ReturnLength = sizeof(TOKEN_TYPE);
        if (TokenInformationLength < *ReturnLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        *(PTOKEN_TYPE)TokenInformation = TokenPrimary;
        return STATUS_SUCCESS;
    case TokenStatistics: {
        PTOKEN_STATISTICS Statistics = TokenInformation;

        *ReturnLength = sizeof(TOKEN_STATISTICS);
        if (TokenInformationLength < *ReturnLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        memset(Statistics, 0, sizeof(*Statistics));
        Statistics->TokenType = TokenPrimary;
        Statistics->GroupCount = 4;
        Statistics->PrivilegeCount = 2;
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS
NtAdjustPrivilegesToken (HANDLE TokenHandle, BOOLEAN DisableAllPrivileges, PTOKEN_PRIVILEGES NewState,
                         ULONG BufferLength, PTOKEN_PRIVILEGES PreviousState, PULONG ReturnLength)
{
    ULONG i;
    ULONG Changed = 0;

    for (i = 0; NewState != NULL && i < NewState->PrivilegeCount; i += 1) {
        PBOOLEAN Enabled;
        BOOLEAN Enable = (NewState->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0;

        if (NewState->Privileges[i].Luid.LowPart == SE_RESTORE_PRIVILEGE) {
            Enabled = &HostToken.Restore;
        } else if (NewState->Privileges[i].Luid.LowPart == SE_BACKUP_PRIVILEGE) {
            Enabled = &HostToken.Backup;
        } else {
            return STATUS_NOT_ALL_ASSIGNED;
        }

        if (PreviousState != NULL && *Enabled != Enable) {
            PreviousState->Privileges[Changed].Luid = NewState->Privileges[i].Luid;
            PreviousState->Privileges[Changed].Attributes = *Enabled ? SE_PRIVILEGE_ENABLED : 0;
            Changed += 1;
        }

        *Enabled = Enable;
    }

    if (PreviousState != NULL) {
        PreviousState->PrivilegeCount = Changed;
    }

    if (ReturnLength != NULL) {
        *ReturnLength = sizeof(TOKEN_PRIVILEGES) + Changed * sizeof(LUID_AND_ATTRIBUTES);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtPrivilegeCheck (HANDLE ClientToken, PPRIVILEGE_SET RequiredPrivileges, PBOOLEAN Result)
{
    *Result = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS
NtPrivilegedServiceAuditAlarm (PUNICODE_STRING SubsystemName, PUNICODE_STRING ServiceName,
                               HANDLE ClientToken, PPRIVILEGE_SET Privileges, BOOLEAN AccessGranted)
{
    return STATUS_SUCCESS;
}

//
// The Zw entry points of these services (ntdll exports both names).
//

NTSTATUS
ZwClose (HANDLE Handle)
{
    return NtClose(Handle);
}

NTSTATUS
ZwOpenProcessToken (HANDLE ProcessHandle, ACCESS_MASK DesiredAccess, PHANDLE TokenHandle)
{
    return NtOpenProcessToken(ProcessHandle, DesiredAccess, TokenHandle);
}

NTSTATUS
ZwOpenThreadToken (HANDLE ThreadHandle, ACCESS_MASK DesiredAccess, BOOLEAN OpenAsSelf,
                   PHANDLE TokenHandle)
{
    return NtOpenThreadToken(ThreadHandle, DesiredAccess, OpenAsSelf, TokenHandle);
}

NTSTATUS
ZwQueryInformationToken (HANDLE TokenHandle, TOKEN_INFORMATION_CLASS TokenInformationClass,
                         PVOID TokenInformation, ULONG TokenInformationLength, PULONG ReturnLength)
{
    return NtQueryInformationToken(TokenHandle, TokenInformationClass, TokenInformation,
                                   TokenInformationLength, ReturnLength);
}

NTSTATUS
ZwAdjustPrivilegesToken (HANDLE TokenHandle, BOOLEAN DisableAllPrivileges, PTOKEN_PRIVILEGES NewState,
                         ULONG BufferLength, PTOKEN_PRIVILEGES PreviousState, PULONG ReturnLength)
{
    return NtAdjustPrivilegesToken(TokenHandle, DisableAllPrivileges, NewState, BufferLength,
                                   PreviousState, ReturnLength);
}

//
// Process initialization, a C initializer of the run-time: the string allocator and
// the NLS tables of the code page files of NTNLS.
//

static PVOID
HostReadFile (const char *Directory, const char *Name)
{
    char Path[MAX_PATH];
    PVOID Data;
    long Size;
    FILE *f;

    sprintf(Path, "%s\\%s", Directory, Name);
    if ((f = fopen(Path, "rb")) == NULL || fseek(f, 0, SEEK_END) != 0 || (Size = ftell(f)) <= 0 ||
        (Data = malloc(Size)) == NULL || fseek(f, 0, SEEK_SET) != 0 || fread(Data, 1, Size, f) != (size_t)Size) {
        fprintf(stderr, "hostnt: cannot read %s\n", Path);
        exit(1);
    }

    fclose(f);
    return Data;
}

static VOID __cdecl
HostInitializeProcess (VOID)
{
    NLSTABLEINFO TableInfo;
    const char *Nls = getenv("NTNLS");

    RtlAllocateStringRoutine = HostAllocateString;
    RtlFreeStringRoutine = HostFreeString;
    if (Nls == NULL) {
        fprintf(stderr, "hostnt: NTNLS (the NLS table directory) is not set\n");
        exit(1);
    }

    RtlInitNlsTables(HostReadFile(Nls, "C_1252.NLS"), HostReadFile(Nls, "C_437.NLS"),
                     HostReadFile(Nls, "L_INTL.NLS"), &TableInfo);
    RtlResetRtlTranslations(&TableInfo);
    HostInitializeToken();
}

#pragma data_seg(".CRT$XIU")
static VOID (__cdecl *HostInitialize)(VOID) = HostInitializeProcess;
#pragma data_seg()
