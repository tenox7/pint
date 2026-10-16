/*++

Module Name:

    tbarm.c

Abstract:

    The ARM translation buffer: the TB miss handling of KE/MIPS/X4TRAP.S, the TB
    primitives the genuine KE\MIPS\XXFLSHTB.C flushes through (KeFlushCurrentTb,
    KiFlushMultipleTb, KiFlushSingleTb), and KiSwapProcess.

    The memory manager's page tables are the MIPS R4000 ones (a 4 KB page
    directory that maps itself at PDE_BASE, EntryLo-format PTEs), which the
    ARMv7 MMU cannot walk. As on MIPS, where the TLB is a software-refilled
    cache of those PTEs, the hardware translation tables are a cache of them: a
    translation or write fault on an address the memory manager has validly
    mapped is satisfied by copying its PTE into a hardware descriptor
    (KiFillTb), and the TB flush routines purge the copies.

    The kernel direct map (KSEG0: every physical address below 1 GB, with
    memory, VideoCore and device attributes) is a fixed part of every hardware
    table, like the unmapped KSEG0/KSEG1 of MIPS, and so are the HAL's device
    window for the BCM2836 local peripherals and the second PCR page (the
    shared user data page) at KIPCR2, like the fixed entries of the MIPS TB;
    its read-only user alias at USPCR2 is refilled whenever it is missing.

    Each process id (the MIPS PID, the ARMv7 ASID) owns one hardware
    first-level table, whose user and per-process system regions (the page
    table window and hyperspace) hold that process's translations; the rest
    of system space shares second-level tables. Process ids are assigned and
    recycled exactly as MIPS assigns PIDs.

Environment:

    Kernel mode only.

--*/

#include "ki.h"

//
// ARMv7-A short-descriptor fields (SCTLR.AFE = SCTLR.TRE = 0, domain 0).
//

#define L1_COARSE           0x00000001
#define L1_SECTION          0x00000002
#define SECTION_B           (1 << 2)
#define SECTION_C           (1 << 3)
#define SECTION_XN          (1 << 4)
#define SECTION_AP_PRIV_RW  (1 << 10)
#define SECTION_TEX_NORMAL  (1 << 12)

#define L2_XN               0x00000001
#define L2_SMALL            0x00000002
#define L2_B                (1 << 2)
#define L2_C                (1 << 3)
#define L2_AP0              (1 << 4)
#define L2_AP1              (1 << 5)
#define L2_TEX_NORMAL       (1 << 6)
#define L2_AP2              (1 << 9)
#define L2_NG               (1 << 11)

#define L1_ENTRIES          4096
#define L2_ENTRIES          256
#define L1_TABLE_SIZE       (L1_ENTRIES * sizeof(ULONG))
#define L2_TABLE_SIZE       (L2_ENTRIES * sizeof(ULONG))
#define PDE_INDEX(Va)       ((ULONG)(Va) >> PDI_SHIFT)
#define PTE_INDEX(Va)       (((ULONG)(Va) >> PTI_SHIFT) & 0x3FF)
#define L2_INDEX(Va)        (((ULONG)(Va) >> PTI_SHIFT) & (L2_ENTRIES - 1))

//
// Megabyte ranges: user space, the kernel direct map, the per-process system
// regions (page table window and hyperspace), and the shared system space.
//

#define KSEG0_MEGABYTE          (KSEG0_BASE >> 20)
#define KSEG2_MEGABYTE          (KSEG2_BASE >> 20)
#define SHARED_SYSTEM_MEGABYTE  ((ULONG)MM_LOWEST_SYSTEM_ADDRESS >> 20)
#define SHARED_SYSTEM_MEGABYTES (L1_ENTRIES - SHARED_SYSTEM_MEGABYTE)

#define PERIPHERAL_PHYSICAL     0x3F000000
#define LOCAL_PHYSICAL          0x40000000

//
// Process ids and the second-level table pool.
//

#define TB_PROCESS_IDS          64
#define TB_L2_TABLES            4096

static PULONG KiKernelTable;
static PULONG KiProcessTable[TB_PROCESS_IDS];
static PULONG KiSystemTable[SHARED_SYSTEM_MEGABYTES];
static PULONG KiFreeL2;
static ULONG KiSharedDataPage;

static __inline ULONG
KiPhysical (
    PVOID VirtualAddress
    )
{
    return (ULONG)VirtualAddress - KSEG0_BASE;
}

static __inline BOOLEAN
KiPrivateMegabyte (
    ULONG Megabyte
    )
{
    return (Megabyte < KSEG0_MEGABYTE) ||
           ((Megabyte >= KSEG2_MEGABYTE) && (Megabyte < SHARED_SYSTEM_MEGABYTE));
}

static __inline VOID
KiInvalidateTbEntry (
    ULONG VirtualAddress
    )
{
    __dsb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(VirtualAddress & ~(PAGE_SIZE - 1), 15, 0, 8, 7, 3);
    __dsb(_ARM_BARRIER_SY);
    __isb(_ARM_BARRIER_SY);
}

static __inline VOID
KiInvalidateEntireTb (
    VOID
    )
{
    __dsb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(0, 15, 0, 8, 7, 0);
    __dsb(_ARM_BARRIER_SY);
    __isb(_ARM_BARRIER_SY);
}

//
// Second-level table pool. A table on the free list is linked through its
// first entry; tables are zeroed when allocated.
//

static VOID
KiFreeL2Table (
    PULONG Table
    )
{
    Table[0] = (ULONG)KiFreeL2;
    KiFreeL2 = Table;
}

static VOID
KiPurgeTable (
    PULONG Tb
    )

/*++

Routine Description:

    This function releases the second-level tables of a hardware first-level
    table's process-private regions, discarding the translations they cache.

--*/

{
    ULONG Megabyte;

    for (Megabyte = 0; Megabyte < SHARED_SYSTEM_MEGABYTE; Megabyte += 1) {
        if (!KiPrivateMegabyte(Megabyte)) {
            continue;
        }

        if ((Tb[Megabyte] & 3) == L1_COARSE) {
            KiFreeL2Table((PULONG)(KSEG0_BASE + (Tb[Megabyte] & ~(L2_TABLE_SIZE - 1))));
            Tb[Megabyte] = 0;
        }
    }
}

static PULONG
KiAllocateL2Table (
    VOID
    )
{
    PULONG Table;
    ULONG Index;

    if (KiFreeL2 == NULL) {

        //
        // The pool is exhausted: discard every process's private translations,
        // as a MIPS TB reclaims entries, and take a table from them.
        //

        for (Index = 0; Index < TB_PROCESS_IDS; Index += 1) {
            KiPurgeTable(KiProcessTable[Index]);
        }

        KiInvalidateEntireTb();
        if (KiFreeL2 == NULL) {
            KeBugCheckEx(MEMORY_MANAGEMENT, 0x41A, 0, 0, 0);
        }
    }

    Table = KiFreeL2;
    KiFreeL2 = (PULONG)Table[0];
    for (Index = 0; Index < L2_ENTRIES; Index += 1) {
        Table[Index] = 0;
    }

    return Table;
}

static PULONG
KiGetL2Table (
    ULONG Megabyte,
    BOOLEAN Allocate
    )

/*++

Routine Description:

    This function returns the second-level table that maps a megabyte in the
    current hardware first-level table, optionally creating it.

--*/

{
    PULONG Tb = (PULONG)PCR->CurrentTb;
    PULONG Table;

    if ((Tb[Megabyte] & 3) == L1_COARSE) {
        return (PULONG)(KSEG0_BASE + (Tb[Megabyte] & ~(L2_TABLE_SIZE - 1)));
    }

    if (Megabyte >= SHARED_SYSTEM_MEGABYTE) {
        Table = KiSystemTable[Megabyte - SHARED_SYSTEM_MEGABYTE];
        if (Table == NULL) {
            if (Allocate == FALSE) {
                return NULL;
            }

            Table = KiAllocateL2Table();
            KiSystemTable[Megabyte - SHARED_SYSTEM_MEGABYTE] = Table;
        }

    } else {
        if (Allocate == FALSE) {
            return NULL;
        }

        Table = KiAllocateL2Table();
    }

    Tb[Megabyte] = KiPhysical(Table) | L1_COARSE;
    __dsb(_ARM_BARRIER_SY);
    return Table;
}

static VOID
KiInstallTbEntry (
    IN ULONG VirtualAddress,
    IN HARDWARE_PTE Pte
    )

/*++

Routine Description:

    This function copies a valid PTE into the hardware descriptor that maps
    the virtual address. The page is writable only when the PTE is dirty, as
    the R4000 D bit; user pages are those below 2 GB.

--*/

{
    ULONG Descriptor;
    ULONG Megabyte = VirtualAddress >> 20;
    PULONG Table = KiGetL2Table(Megabyte, TRUE);

    Descriptor = (Pte.PageFrameNumber << PAGE_SHIFT) | L2_SMALL;
    if (Pte.CachePolicy == UNCACHED_POLICY) {
        Descriptor |= L2_B;

    } else {
        Descriptor |= L2_TEX_NORMAL | L2_C | L2_B;
    }

    if (VirtualAddress < KSEG0_BASE) {
        Descriptor |= L2_AP0 | L2_AP1;
        if (Pte.Dirty == 0) {
            Descriptor |= L2_AP2;
        }

    } else {
        Descriptor |= L2_AP0;
        if (Pte.Dirty == 0) {
            Descriptor |= L2_AP2;
        }
    }

    if (KiPrivateMegabyte(Megabyte) || (Pte.Global == 0)) {
        Descriptor |= L2_NG;
    }

    Table[L2_INDEX(VirtualAddress)] = Descriptor;
    KiInvalidateTbEntry(VirtualAddress);
}

static VOID
KiFlushTbEntry (
    IN ULONG VirtualAddress
    )

/*++

Routine Description:

    This function removes the hardware translation of a virtual address. The
    direct map and the HAL device window are fixed and never flushed.

--*/

{
    ULONG Megabyte = VirtualAddress >> 20;
    PULONG Table;

    if ((Megabyte >= KSEG0_MEGABYTE) && (Megabyte < KSEG2_MEGABYTE)) {
        return;
    }

    if (Megabyte >= SHARED_SYSTEM_MEGABYTE) {
        Table = KiSystemTable[Megabyte - SHARED_SYSTEM_MEGABYTE];

    } else {
        Table = KiGetL2Table(Megabyte, FALSE);
    }

    if (Table != NULL) {
        Table[L2_INDEX(VirtualAddress)] = 0;
    }

    KiInvalidateTbEntry(VirtualAddress);
}

BOOLEAN
KiFillTb (
    IN ULONG VirtualAddress,
    IN ULONG FaultStatus,
    IN ULONG ProcessorMode
    )

/*++

Routine Description:

    This function is the TB miss handler, called from the data and prefetch
    abort vectors with interrupts disabled. A translation fault, or a write to
    a page whose hardware descriptor is read only, is satisfied here when the
    memory manager's PTE for the address is valid (and dirty, for a write); the
    faulting instruction is then restarted. Every other fault is left to the
    memory management fault handler. Only the direct map is referenced.

Arguments:

    VirtualAddress - Supplies the faulting virtual address (DFAR or IFAR).

    FaultStatus - Supplies the fault status (DFSR or IFSR).

    ProcessorMode - Supplies the saved program status of the faulting mode.

Return Value:

    TRUE if the translation was filled, FALSE otherwise.

--*/

{
    ULONG Status = (FaultStatus & 0xF) | ((FaultStatus >> 6) & 0x10);
    BOOLEAN Write = (FaultStatus & (1 << 11)) != 0;
    PHARDWARE_PTE PageDirectory;
    PHARDWARE_PTE PageTable;
    HARDWARE_PTE Pde;
    HARDWARE_PTE Pte;

    if ((Status != 0x05) && (Status != 0x07) && (Status != 0x0F)) {
        return FALSE;
    }

    if ((VirtualAddress >= KSEG0_BASE) && (VirtualAddress < KSEG2_BASE)) {
        return FALSE;
    }

    if (((ProcessorMode & 0x1F) == 0x10) && (VirtualAddress >= KSEG0_BASE)) {
        return FALSE;
    }

    //
    // The user mapping of the shared user data page is part of the fixed TB:
    // read only, and refilled whenever it is not present.
    //

    if ((VirtualAddress & ~(PAGE_SIZE - 1)) == USPCR2) {
        if (Write) {
            return FALSE;
        }

        PageTable = (PHARDWARE_PTE)KiGetL2Table(USPCR2 >> 20, TRUE);
        ((PULONG)PageTable)[L2_INDEX(USPCR2)] = (KiSharedDataPage << PAGE_SHIFT) |
            L2_SMALL | L2_XN | L2_TEX_NORMAL | L2_C | L2_B | L2_AP2 | L2_AP1 | L2_AP0 | L2_NG;
        KiInvalidateTbEntry(USPCR2);
        return TRUE;
    }

    PageDirectory = (PHARDWARE_PTE)(KSEG0_BASE + PCR->CurrentPdr);
    Pde = PageDirectory[PDE_INDEX(VirtualAddress)];
    if (Pde.Valid == 0) {
        return FALSE;
    }

    PageTable = (PHARDWARE_PTE)(KSEG0_BASE + (Pde.PageFrameNumber << PAGE_SHIFT));
    Pte = PageTable[PTE_INDEX(VirtualAddress)];
    if ((Pte.Valid == 0) || (Write && (Pte.Dirty == 0))) {
        return FALSE;
    }

    KiInstallTbEntry(VirtualAddress, Pte);
    return TRUE;
}

VOID
KeFlushCurrentTb (
    VOID
    )

/*++

Routine Description:

    This function flushes the translation buffer of the current processor:
    every translation cached for the current process and for shared system
    space (the random part of the MIPS TB).

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();
    ULONG Index;
    ULONG Entry;
    PULONG Table;

    KiPurgeTable((PULONG)PCR->CurrentTb);
    for (Index = 0; Index < SHARED_SYSTEM_MEGABYTES; Index += 1) {
        Table = KiSystemTable[Index];
        if (Table != NULL) {
            for (Entry = 0; Entry < L2_ENTRIES; Entry += 1) {
                Table[Entry] = 0;
            }
        }
    }

    KiInvalidateEntireTb();
    KiRestoreInterrupts(Enable);
}

VOID
KiFlushMultipleTb (
    IN BOOLEAN Invalid,
    IN PVOID *Virtual,
    IN ULONG Count
    )

/*++

Routine Description:

    This function flushes the translations of multiple virtual addresses
    whose PTEs the caller has already changed.

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();
    ULONG Index;

    for (Index = 0; Index < Count; Index += 1) {
        KiFlushTbEntry((ULONG)Virtual[Index]);
    }

    KiRestoreInterrupts(Enable);
}


VOID
KeFillEntryTb (
    IN HARDWARE_PTE Pte[],
    IN PVOID Virtual,
    IN BOOLEAN Invalid
    )

/*++

Routine Description:

    This function fills the translation of a virtual address from its PTE.

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();

    if (Pte[0].Valid != 0) {
        KiInstallTbEntry((ULONG)Virtual, Pte[0]);

    } else {
        KiFlushTbEntry((ULONG)Virtual);
    }

    KiRestoreInterrupts(Enable);
}

static VOID
KiLoadTb (
    IN ULONG ProcessId,
    IN ULONG PageDirectory
    )

/*++

Routine Description:

    This function makes a process id's hardware first-level table and ASID
    current. The switch passes through the kernel-only table so that no
    translation is walked under the new ASID through the old process's table.

--*/

{
    PULONG Tb = KiProcessTable[ProcessId];

    PCR->CurrentPdr = ((PHARDWARE_PTE)&PageDirectory)->PageFrameNumber << PAGE_SHIFT;
    PCR->CurrentTb = (ULONG)Tb;
    _MoveToCoprocessor(KiPhysical(KiKernelTable), 15, 0, 2, 0, 0);
    __isb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(ProcessId, 15, 0, 13, 0, 1);
    __isb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(KiPhysical(Tb), 15, 0, 2, 0, 0);
    __isb(_ARM_BARRIER_SY);
}

BOOLEAN
KiSwapProcess (
    IN PKPROCESS NewProcess,
    IN PKPROCESS OldProcess
    )

/*++

Routine Description:

    This function swaps the address space to another process. If the process
    sequence number matches the master sequence number its process id is still
    valid; otherwise a new id is assigned, and when the ids are exhausted the
    master sequence advances and every process's translations are discarded
    (the MIPS PID rollover).

Return Value:

    TRUE if a process id rollover occurred, FALSE otherwise.

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();
    BOOLEAN Rollover = FALSE;
    ULONG ProcessId;
    ULONG Index;

    UNREFERENCED_PARAMETER(OldProcess);

    if (NewProcess->ProcessSequence == KiMasterSequence) {
        ProcessId = NewProcess->ProcessPid;

    } else {
        ProcessId = KiMasterPid + 1;
        NewProcess->ProcessSequence = KiMasterSequence;
        if (ProcessId >= KeNumberProcessIds) {
            KiMasterSequence += 1;
            NewProcess->ProcessSequence = KiMasterSequence;
            for (Index = 0; Index < TB_PROCESS_IDS; Index += 1) {
                KiPurgeTable(KiProcessTable[Index]);
            }

            KiInvalidateEntireTb();
            ProcessId = 0;
            Rollover = TRUE;
        }

        KiMasterPid = ProcessId;
        NewProcess->ProcessPid = ProcessId;
    }

    KiLoadTb(ProcessId, NewProcess->DirectoryTableBase[0]);
    KiRestoreInterrupts(Enable);
    return Rollover;
}

VOID
KiArmStartTranslation (
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

/*++

Routine Description:

    This function builds the hardware translation tables and makes the boot
    process id's table current, then initializes the page directory the OS
    Loader allocated: its recursive entry at PDE_BASE and the hyperspace page
    table (the PDR setup of KE/MIPS/X4START.S).

    The tables are taken from the top of the largest free memory descriptor,
    as MiInitMachineDependent takes the PFN database: those pages leave the
    descriptor list, so the memory manager never allocates them.

--*/

{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    PMEMORY_ALLOCATION_DESCRIPTOR FreeDescriptor = NULL;
    PLIST_ENTRY NextMd;
    PHARDWARE_PTE PageDirectory;
    HARDWARE_PTE Pde;
    ULONG TopPage = 0;
    ULONG MemoryTop;
    ULONG Pages;
    ULONG BasePage;
    ULONG Megabyte;
    ULONG Physical;
    ULONG Index;
    PUCHAR Area;
    PULONG Word;
    PULONG Table;

    for (NextMd = LoaderBlock->MemoryDescriptorListHead.Flink;
         NextMd != &LoaderBlock->MemoryDescriptorListHead;
         NextMd = NextMd->Flink) {

        Descriptor = CONTAINING_RECORD(NextMd, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
        if (Descriptor->BasePage + Descriptor->PageCount > TopPage) {
            TopPage = Descriptor->BasePage + Descriptor->PageCount;
        }

        if ((Descriptor->MemoryType == LoaderFree) &&
            ((FreeDescriptor == NULL) ||
             (Descriptor->PageCount > FreeDescriptor->PageCount))) {
            FreeDescriptor = Descriptor;
        }
    }

    Pages = ((TB_PROCESS_IDS + 1) * L1_TABLE_SIZE + TB_L2_TABLES * L2_TABLE_SIZE) >> PAGE_SHIFT;
    BasePage = (FreeDescriptor->BasePage + FreeDescriptor->PageCount - Pages) &
               ~((L1_TABLE_SIZE >> PAGE_SHIFT) - 1);
    FreeDescriptor->PageCount = BasePage - FreeDescriptor->BasePage;
    Area = (PUCHAR)(KSEG0_BASE + (BasePage << PAGE_SHIFT));
    for (Word = (PULONG)Area; Word < (PULONG)(Area + (Pages << PAGE_SHIFT)); Word += 1) {
        *Word = 0;
    }

    KiKernelTable = (PULONG)Area;
    Area += L1_TABLE_SIZE;
    for (Index = 0; Index < TB_PROCESS_IDS; Index += 1) {
        KiProcessTable[Index] = (PULONG)Area;
        Area += L1_TABLE_SIZE;
    }

    for (Index = 0; Index < TB_L2_TABLES; Index += 1) {
        KiFreeL2Table((PULONG)Area);
        Area += L2_TABLE_SIZE;
    }

    //
    // The kernel-only table holds the fixed part of the TB: the direct map
    // (memory, then VideoCore memory, then the BCM2835 peripherals), the HAL's
    // device window, and the second PCR page.
    //

    MemoryTop = (TopPage + ((1 << (20 - PAGE_SHIFT)) - 1)) >> (20 - PAGE_SHIFT);
    for (Megabyte = 0; Megabyte < KSEG2_MEGABYTE - KSEG0_MEGABYTE; Megabyte += 1) {
        Physical = Megabyte << 20;
        if (Megabyte < MemoryTop) {
            KiKernelTable[KSEG0_MEGABYTE + Megabyte] = Physical | L1_SECTION |
                SECTION_AP_PRIV_RW | SECTION_TEX_NORMAL | SECTION_C | SECTION_B;

        } else if (Physical < PERIPHERAL_PHYSICAL) {
            KiKernelTable[KSEG0_MEGABYTE + Megabyte] = Physical | L1_SECTION |
                SECTION_AP_PRIV_RW | SECTION_TEX_NORMAL | SECTION_XN;

        } else {
            KiKernelTable[KSEG0_MEGABYTE + Megabyte] = Physical | L1_SECTION |
                SECTION_AP_PRIV_RW | SECTION_B | SECTION_XN;
        }
    }

    KiKernelTable[LOCAL_PERIPHERAL_VIRTUAL >> 20] = LOCAL_PHYSICAL | L1_SECTION |
        SECTION_AP_PRIV_RW | SECTION_B | SECTION_XN;

    KiSharedDataPage = LoaderBlock->u.Arm.PcrPage2;
    Table = KiAllocateL2Table();
    Table[L2_INDEX(KIPCR2)] = (KiSharedDataPage << PAGE_SHIFT) | L2_SMALL | L2_XN |
        L2_TEX_NORMAL | L2_C | L2_B | L2_AP0;
    KiKernelTable[KIPCR2 >> 20] = KiPhysical(Table) | L1_COARSE;

    for (Index = 0; Index < TB_PROCESS_IDS; Index += 1) {
        for (Megabyte = 0; Megabyte < L1_ENTRIES; Megabyte += 1) {
            KiProcessTable[Index][Megabyte] = KiKernelTable[Megabyte];
        }
    }

    KeNumberProcessIds = TB_PROCESS_IDS;

    //
    // The page directory maps itself at PDE_BASE and maps the hyperspace page
    // table in the next entry.
    //

    PageDirectory = (PHARDWARE_PTE)(KSEG0_BASE + (LoaderBlock->u.Arm.PdrPage << PAGE_SHIFT));
    *(PULONG)&Pde = 0;
    Pde.Valid = 1;
    Pde.Dirty = 1;
    Pde.Write = 1;
    Pde.CachePolicy = CACHED_POLICY;
    Pde.PageFrameNumber = LoaderBlock->u.Arm.PdrPage;
    PageDirectory[PDE_INDEX(PDE_BASE)] = Pde;
    Pde.PageFrameNumber = LoaderBlock->u.Arm.PdrPage + 1;
    PageDirectory[PDE_INDEX(PDE_BASE) + 1] = Pde;

    //
    // Load the boot process id's table.
    //

    _MoveToCoprocessor(0, 15, 0, 2, 0, 2);
    _MoveToCoprocessor(1, 15, 0, 3, 0, 0);
    KiLoadTb(0, *(PULONG)&PageDirectory[PDE_INDEX(PDE_BASE)]);
    KiInvalidateEntireTb();
}

VOID
KiFlushSingleTb (
    IN BOOLEAN Invalid,
    IN PVOID Virtual
    )

/*++

Routine Description:

    This function flushes the translation of a virtual address whose PTE the
    caller has already changed.

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();

    KiFlushTbEntry((ULONG)Virtual);
    KiRestoreInterrupts(Enable);
}

ULONG
KiProbeEntryTb (
    IN PVOID VirtualAddress
    )

/*++

Routine Description:

    This function determines if a virtual address is mapped by a fixed part
    of the translation buffer: the direct map or the HAL's device window.

Return Value:

    TRUE if the address is mapped by a fixed entry, FALSE otherwise.

--*/

{
    return (KiKernelTable[(ULONG)VirtualAddress >> 20] & 3) == L1_SECTION;
}
