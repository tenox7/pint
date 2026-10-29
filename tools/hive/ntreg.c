/*++

Module Name:

    ntreg.c

Abstract:

    The registry services REGINI calls, for the registry of "regini -h": one
    hive file loaded under \Registry. The hive is opened with EDITHIVE and
    the keys are created, secured and given values by the Configuration
    Manager routines behind the kernel's key parse and security methods.
    Key creation follows CmpDoCreateChild; descriptors are assigned and set
    with the user mode equivalents of SeAssignSecurity and
    SeSetSecurityDescriptorInfo (RtlNewSecurityObject, RtlSetSecurityObject)
    for the process token.

--*/

#include "edithive.h"
#include "nturtl.h"
#include "host.h"

extern GENERIC_MAPPING CmpKeyMapping;

PSECURITY_DESCRIPTOR CmpHiveRootSecurityDescriptor(VOID);

NTSTATUS
CmpSetSecurityDescriptorInfo(PCM_KEY_CONTROL_BLOCK Key, PSECURITY_INFORMATION SecurityInformation,
                             PSECURITY_DESCRIPTOR ModificationDescriptor,
                             PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
                             POOL_TYPE PoolType, PGENERIC_MAPPING GenericMapping);

VOID
CmpGetObjectSecurity(HCELL_INDEX Cell, PHHIVE Hive, PCM_KEY_SECURITY *Security,
                     PHCELL_INDEX SecurityCell);

static PCMHIVE RegHive;
static UNICODE_STRING RegLinkName;
static HOST_HANDLE RegRoot = { HOST_MAGIC, HOST_KEY, NULL, HCELL_NIL };

static PHOST_HANDLE
RegKey (HANDLE Handle)
{
    PHOST_HANDLE h = (PHOST_HANDLE)Handle;

    return (h != NULL && h->Type == HOST_KEY) ? h : NULL;
}

static HANDLE
RegNewKey (PVOID Hive, HCELL_INDEX Cell)
{
    PHOST_HANDLE h = HostNewHandle(HOST_KEY);

    h->Hive = Hive;
    h->Cell = Cell;
    return (HANDLE)h;
}

static BOOLEAN
RegNextComponent (PUNICODE_STRING Path, PUNICODE_STRING Component)
{
    while (Path->Length != 0 && Path->Buffer[0] == L'\\') {
        Path->Buffer++;
        Path->Length -= sizeof(WCHAR);
    }

    if (Path->Length == 0) {
        return FALSE;
    }

    Component->Buffer = Path->Buffer;
    Component->Length = 0;
    while (Path->Length != 0 && Path->Buffer[0] != L'\\') {
        Path->Buffer++;
        Path->Length -= sizeof(WCHAR);
        Component->Length += sizeof(WCHAR);
    }

    Component->MaximumLength = Component->Length;
    return TRUE;
}

//
// Resolve a name relative to a key or to \Registry. On success *Parent and
// *Cell name the key that contains the last component and that component's
// key, or HCELL_NIL in *Cell if it does not exist; Last returns its name.
//

static NTSTATUS
RegResolve (POBJECT_ATTRIBUTES ObjectAttributes, PHCELL_INDEX Parent,
            PHCELL_INDEX Cell, PUNICODE_STRING Last)
{
    UNICODE_STRING Path = *ObjectAttributes->ObjectName;
    UNICODE_STRING Component;
    UNICODE_STRING Registry;
    PHOST_HANDLE Root = RegKey(ObjectAttributes->RootDirectory);
    HCELL_INDEX Current;

    if (Root == NULL) {
        RtlInitUnicodeString(&Registry, L"\\Registry");
        if (!RtlPrefixUnicodeString(&Registry, &Path, TRUE)) {
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }

        Path.Buffer += Registry.Length / sizeof(WCHAR);
        Path.Length -= Registry.Length;
        Root = &RegRoot;
    }

    Current = Root->Cell;
    *Parent = HCELL_NIL;
    *Cell = Root->Hive ? Current : HCELL_NIL;
    Last->Length = 0;
    if (Root->Hive == NULL) {
        if (!RegNextComponent(&Path, Last)) {
            return STATUS_SUCCESS;
        }

        if (RegHive == NULL || !RtlEqualUnicodeString(Last, &RegLinkName, TRUE)) {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        Current = RegHive->Hive.BaseBlock->RootCell;
        *Cell = Current;
    }

    while (RegNextComponent(&Path, &Component)) {
        if (*Cell == HCELL_NIL) {
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }

        *Parent = Current;
        *Last = Component;
        *Cell = CmpFindSubKeyByName(&RegHive->Hive,
                                    (PCM_KEY_NODE)HvGetCell(&RegHive->Hive, Current), &Component);
        Current = *Cell;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RegAssignSecurity (HCELL_INDEX Cell, PSECURITY_DESCRIPTOR ParentDescriptor,
                   PSECURITY_DESCRIPTOR ExplicitDescriptor)
{
    PSECURITY_DESCRIPTOR NewDescriptor;
    HANDLE Token;
    NTSTATUS Status;

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = RtlNewSecurityObject(ParentDescriptor, ExplicitDescriptor, &NewDescriptor,
                                  TRUE, Token, &CmpKeyMapping);
    NtClose(Token);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = CmpAssignSecurityDescriptor(&RegHive->Hive, Cell,
                                         (PCM_KEY_NODE)HvGetCell(&RegHive->Hive, Cell),
                                         NewDescriptor, PagedPool);
    RtlDeleteSecurityObject(&NewDescriptor);
    return Status;
}

//
// Allocate and initialize a key node and its class, as CmpDoCreateChild does.
//

static NTSTATUS
RegNewNode (HCELL_INDEX ParentCell, PUNICODE_STRING Name, PUNICODE_STRING Class,
            USHORT Flags, PHCELL_INDEX KeyCell, PHCELL_INDEX ClassCellOut)
{
    PHHIVE Hive = &RegHive->Hive;
    HCELL_INDEX ClassCell = HCELL_NIL;
    PCM_KEY_NODE Node;

    *KeyCell = HvAllocateCell(Hive, CmpHKeyNodeSize(Hive, Name), Stable);
    if (*KeyCell == HCELL_NIL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Class != NULL && Class->Length > 0) {
        ClassCell = HvAllocateCell(Hive, Class->Length, Stable);
        if (ClassCell == HCELL_NIL) {
            HvFreeCell(Hive, *KeyCell);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlMoveMemory(HvGetCell(Hive, ClassCell), Class->Buffer, Class->Length);
    }

    Node = (PCM_KEY_NODE)HvGetCell(Hive, *KeyCell);
    Node->Signature = CM_KEY_NODE_SIGNATURE;
    Node->Flags = Flags;
    KeQuerySystemTime(&Node->LastWriteTime);
    Node->Spare = 0;
    Node->Parent = ParentCell;
    Node->SubKeyCounts[Stable] = 0;
    Node->SubKeyCounts[Volatile] = 0;
    Node->SubKeyLists[Stable] = HCELL_NIL;
    Node->SubKeyLists[Volatile] = HCELL_NIL;
    Node->ValueList.Count = 0;
    Node->ValueList.List = HCELL_NIL;
    Node->u1.s1.Security = HCELL_NIL;
    Node->u1.s1.Class = ClassCell;
    Node->ClassLength = ClassCell != HCELL_NIL ? Class->Length : 0;
    Node->MaxValueDataLen = 0;
    Node->MaxNameLen = 0;
    Node->MaxValueNameLen = 0;
    Node->MaxClassLen = 0;
    Node->NameLength = CmpCopyName(Hive, Node->Name, Name);
    if (Node->NameLength < Name->Length) {
        Node->Flags |= KEY_COMP_NAME;
    }

    *ClassCellOut = ClassCell;
    return STATUS_SUCCESS;
}

static NTSTATUS
RegCreateChild (HCELL_INDEX ParentCell, PUNICODE_STRING Name, PUNICODE_STRING Class,
                PSECURITY_DESCRIPTOR SecurityDescriptor, PHCELL_INDEX KeyCell)
{
    PHHIVE Hive = &RegHive->Hive;
    HCELL_INDEX ClassCell;
    PCM_KEY_SECURITY ParentSecurity;
    PCM_KEY_NODE Parent;
    NTSTATUS Status;

    if (!HvMarkCellDirty(Hive, ParentCell)) {
        return STATUS_NO_LOG_SPACE;
    }

    Status = RegNewNode(ParentCell, Name, Class, 0, KeyCell, &ClassCell);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    CmpGetObjectSecurity(ParentCell, Hive, &ParentSecurity, NULL);
    Status = RegAssignSecurity(*KeyCell, &ParentSecurity->Descriptor, SecurityDescriptor);
    if (!NT_SUCCESS(Status) || !CmpAddSubKey(Hive, ParentCell, *KeyCell)) {
        if (ClassCell != HCELL_NIL) {
            HvFreeCell(Hive, ClassCell);
        }

        HvFreeCell(Hive, *KeyCell);
        return NT_SUCCESS(Status) ? STATUS_INSUFFICIENT_RESOURCES : Status;
    }

    Parent = (PCM_KEY_NODE)HvGetCell(Hive, ParentCell);
    if (Parent->MaxNameLen < Name->Length) {
        Parent->MaxNameLen = Name->Length;
    }

    if (Class != NULL && Parent->MaxClassLen < Class->Length) {
        Parent->MaxClassLen = Class->Length;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NtOpenKey (PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    HCELL_INDEX Parent;
    HCELL_INDEX Cell;
    UNICODE_STRING Last;
    NTSTATUS Status;

    Status = RegResolve(ObjectAttributes, &Parent, &Cell, &Last);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if (Cell == HCELL_NIL) {
        if (Last.Length == 0 && RegKey(ObjectAttributes->RootDirectory) == NULL) {
            *KeyHandle = RegNewKey(NULL, HCELL_NIL);
            return STATUS_SUCCESS;
        }

        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    *KeyHandle = RegNewKey(RegHive, Cell);
    return STATUS_SUCCESS;
}

NTSTATUS
NtCreateKey (PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
             ULONG TitleIndex, PUNICODE_STRING Class, ULONG CreateOptions, PULONG Disposition)
{
    HCELL_INDEX Parent;
    HCELL_INDEX Cell;
    UNICODE_STRING Last;
    NTSTATUS Status;

    Status = RegResolve(ObjectAttributes, &Parent, &Cell, &Last);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if (Cell == HCELL_NIL) {
        if (Parent == HCELL_NIL) {
            return STATUS_ACCESS_DENIED;
        }

        Status = RegCreateChild(Parent, &Last, Class, ObjectAttributes->SecurityDescriptor, &Cell);
        if (!NT_SUCCESS(Status)) {
            return Status;
        }

        if (Disposition != NULL) {
            *Disposition = REG_CREATED_NEW_KEY;
        }

    } else if (Disposition != NULL) {
        *Disposition = REG_OPENED_EXISTING_KEY;
    }

    *KeyHandle = RegNewKey(RegHive, Cell);
    return STATUS_SUCCESS;
}

NTSTATUS
NtSetSecurityObject (HANDLE Handle, SECURITY_INFORMATION SecurityInformation,
                     PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PHOST_HANDLE h = RegKey(Handle);
    PSECURITY_DESCRIPTOR ObjectsSecurityDescriptor = NULL;
    CM_KEY_CONTROL_BLOCK Kcb;

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    RtlZeroMemory(&Kcb, sizeof(Kcb));
    Kcb.RefCount = 1;
    Kcb.KeyHive = &RegHive->Hive;
    Kcb.KeyCell = h->Cell;
    Kcb.KeyNode = (PCM_KEY_NODE)HvGetCell(&RegHive->Hive, h->Cell);
    return CmpSetSecurityDescriptorInfo(&Kcb, &SecurityInformation, SecurityDescriptor,
                                        &ObjectsSecurityDescriptor, PagedPool, &CmpKeyMapping);
}

//
// CmpSetSecurityDescriptorInfo frees the copy it passes after the call; the
// user mode RtlSetSecurityObject replaces the descriptor it is given.
//

NTSTATUS
SeSetSecurityDescriptorInfo (PVOID Object, PSECURITY_INFORMATION SecurityInformation,
                             PSECURITY_DESCRIPTOR ModificationDescriptor,
                             PSECURITY_DESCRIPTOR *ObjectsSecurityDescriptor,
                             POOL_TYPE PoolType, PGENERIC_MAPPING GenericMapping)
{
    ULONG Length = RtlLengthSecurityDescriptor(*ObjectsSecurityDescriptor);
    PSECURITY_DESCRIPTOR Copy = RtlAllocateHeap(RtlProcessHeap(), 0, Length);
    HANDLE Token;
    NTSTATUS Status;

    RtlMoveMemory(Copy, *ObjectsSecurityDescriptor, Length);
    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &Token);
    if (NT_SUCCESS(Status)) {
        Status = RtlSetSecurityObject(*SecurityInformation, ModificationDescriptor, &Copy,
                                      GenericMapping, Token);
        NtClose(Token);
    }

    if (!NT_SUCCESS(Status)) {
        RtlFreeHeap(RtlProcessHeap(), 0, Copy);
        return Status;
    }

    //
    // The new descriptor comes from pool, as the kernel's does: the caller frees it
    // with ExFreePool.
    //

    Length = RtlLengthSecurityDescriptor(Copy);
    *ObjectsSecurityDescriptor = ExAllocatePool(PoolType, Length);
    if (*ObjectsSecurityDescriptor != NULL) {
        RtlMoveMemory(*ObjectsSecurityDescriptor, Copy, Length);
    }

    RtlFreeHeap(RtlProcessHeap(), 0, Copy);
    return *ObjectsSecurityDescriptor != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NtQueryKey (HANDLE KeyHandle, KEY_INFORMATION_CLASS KeyInformationClass, PVOID KeyInformation,
            ULONG Length, PULONG ResultLength)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return EhQueryKey(h->Hive, (HANDLE)h->Cell, KeyInformationClass, KeyInformation,
                      Length, ResultLength);
}

NTSTATUS
NtEnumerateKey (HANDLE KeyHandle, ULONG Index, KEY_INFORMATION_CLASS KeyInformationClass,
                PVOID KeyInformation, ULONG Length, PULONG ResultLength)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return EhEnumerateKey(h->Hive, (HANDLE)h->Cell, Index, KeyInformationClass, KeyInformation,
                          Length, ResultLength);
}

NTSTATUS
NtQueryValueKey (HANDLE KeyHandle, PUNICODE_STRING ValueName,
                 KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass, PVOID KeyValueInformation,
                 ULONG Length, PULONG ResultLength)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return EhQueryValueKey(h->Hive, (HANDLE)h->Cell, ValueName, KeyValueInformationClass,
                           KeyValueInformation, Length, ResultLength);
}

NTSTATUS
NtEnumerateValueKey (HANDLE KeyHandle, ULONG Index,
                     KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
                     PVOID KeyValueInformation, ULONG Length, PULONG ResultLength)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return EhEnumerateValueKey(h->Hive, (HANDLE)h->Cell, Index, KeyValueInformationClass,
                               KeyValueInformation, Length, ResultLength);
}

NTSTATUS
NtSetValueKey (HANDLE KeyHandle, PUNICODE_STRING ValueName, ULONG TitleIndex, ULONG Type,
               PVOID Data, ULONG DataSize)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return EhSetValueKey(h->Hive, (HANDLE)h->Cell, ValueName, TitleIndex, Type, Data, DataSize);
}

NTSTATUS
NtDeleteValueKey (HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return EhDeleteValueKey(h->Hive, (HANDLE)h->Cell, ValueName);
}

NTSTATUS
NtFlushKey (HANDLE KeyHandle)
{
    PHOST_HANDLE h = RegKey(KeyHandle);

    if (h == NULL || h->Hive == NULL) {
        return STATUS_INVALID_HANDLE;
    }

    return HvSyncHive(h->Hive) ? STATUS_SUCCESS : STATUS_REGISTRY_IO_FAILED;
}

//
// Load a hive under \Registry (CmLoadKey). A new hive gets its root node,
// as CmpLinkHiveToMaster creates it, named by the last component of the
// link name and secured as a key created under \Registry.
//

NTSTATUS
NtLoadKey (POBJECT_ATTRIBUTES TargetKey, POBJECT_ATTRIBUTES SourceFile)
{
    HANDLE RootCell;
    UNICODE_STRING RootName;
    UNICODE_STRING Path = *TargetKey->ObjectName;
    UNICODE_STRING Component;
    UNICODE_STRING FileName;
    PSECURITY_DESCRIPTOR Parent;
    HCELL_INDEX Cell;
    HCELL_INDEX ClassCell;
    NTSTATUS Status;

    if (RegHive != NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RegLinkName.Length = 0;
    while (RegNextComponent(&Path, &Component)) {
        RegLinkName = Component;
    }

    FileName.MaximumLength = SourceFile->ObjectName->Length + 16 * sizeof(WCHAR);
    FileName.Length = SourceFile->ObjectName->Length;
    FileName.Buffer = RtlAllocateHeap(RtlProcessHeap(), 0, FileName.MaximumLength);
    RtlMoveMemory(FileName.Buffer, SourceFile->ObjectName->Buffer, FileName.Length);
    FileName.Buffer[FileName.Length / sizeof(WCHAR)] = UNICODE_NULL;
    RegHive = (PCMHIVE)EhOpenHive(&FileName, &RootCell, &RootName, TYPE_SIMPLE);
    if (RegHive == NULL) {
        return STATUS_REGISTRY_CORRUPT;
    }

    if ((HCELL_INDEX)RootCell != HCELL_NIL) {
        return STATUS_SUCCESS;
    }

    Status = RegNewNode(HCELL_NIL, &RegLinkName, NULL, KEY_HIVE_ENTRY | KEY_NO_DELETE,
                        &Cell, &ClassCell);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    RegHive->Hive.BaseBlock->RootCell = Cell;
    Parent = CmpHiveRootSecurityDescriptor();
    Status = RegAssignSecurity(Cell, Parent, TargetKey->SecurityDescriptor);
    ExFreePool(Parent);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    //
    // CmpCreateLinkNode then marks the new root as the hive entry, leaving its
    // name compressed; its parent is the link node in the master hive, which a
    // hive file does not have.
    //

    ((PCM_KEY_NODE)HvGetCell(&RegHive->Hive, Cell))->Flags = KEY_HIVE_ENTRY | KEY_NO_DELETE;
    return HvSyncHive(&RegHive->Hive) ? STATUS_SUCCESS : STATUS_REGISTRY_IO_FAILED;
}

NTSTATUS
NtUnloadKey (POBJECT_ATTRIBUTES TargetKey)
{
    UNICODE_STRING Path = *TargetKey->ObjectName;
    UNICODE_STRING Component;
    UNICODE_STRING Last = { 0, 0, NULL };

    while (RegNextComponent(&Path, &Component)) {
        Last = Component;
    }

    if (RegHive == NULL || !RtlEqualUnicodeString(&Last, &RegLinkName, TRUE)) {
        return STATUS_INVALID_PARAMETER;
    }

    EhCloseHive((HANDLE)RegHive);
    RegHive = NULL;
    return STATUS_SUCCESS;
}
