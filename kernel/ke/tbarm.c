/*++

Module Name:

    tbarm.c

Abstract:

    The ARM translation buffer: the TB miss handling of KE/MIPS/X4TRAP.S, the TB
    flush routines of KE/MIPS/XXFLSHTB.C, and the address space swap of a context
    switch (KiSwapAddressSpace) and of an attach or detach (KiSwapProcess).

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
    windows for the BCM2835 peripherals and the per-core block and the second
    PCR page (the shared user data page) at KIPCR2, like the fixed entries of
    the MIPS TB; its read-only user alias at USPCR2 is refilled whenever it is
    missing.

    Each process id (the MIPS PID, the ARMv7 ASID) owns one hardware
    first-level table, whose user and per-process system regions (the page
    table window and hyperspace) hold that process's translations; the rest
    of system space shares second-level tables. Process ids are assigned and
    recycled exactly as MIPS assigns PIDs.

    On a multiprocessor the processors share the tables: a processor running
    a process walks the table of its id, which the others running it walk too.
    The tables are changed under the TB lock, with interrupts disabled (the
    miss handler runs with them disabled), and the TLB maintenance is
    broadcast to every processor (the multiprocessing extensions), so that a
    flush on one processor flushes them all and needs no interprocessor
    request, unlike the MIPS flushes. A second-level table is reused only
    after the broadcast invalidation that follows its removal (break before
    make). Memory and the tables are inner shareable, so the data caches and
    the table walks are coherent. The process id rollover is synchronized with
    the other processors as on MIPS: each moves the process it runs to a new id
    before the processor that rolled over continues.

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
#define SECTION_S           (1 << 16)
#define L1_SUPERSECTION     (1 << 18)
#define SUPERSECTION_HIGH(High) ((High) << 20)

#define L2_XN               0x00000001
#define L2_SMALL            0x00000002
#define L2_B                (1 << 2)
#define L2_C                (1 << 3)
#define L2_AP0              (1 << 4)
#define L2_AP1              (1 << 5)
#define L2_TEX_NORMAL       (1 << 6)
#define L2_AP2              (1 << 9)
#define L2_S                (1 << 10)
#define L2_NG               (1 << 11)

//
// The translation table walks are cached as the tables are (inner and outer
// write back, write allocate: TTBR0 IRGN and RGN), so they see the descriptors
// the kernel writes through the data cache; on a multiprocessor the walks are
// inner shareable (TTBR0 S and NOS), as normal memory is.
//

#if defined(NT_UP)

#define TTBR_WALK_CACHED    ((1 << 6) | (1 << 3))
#define MEMORY_SHAREABLE    0

#else

#define TTBR_WALK_CACHED    ((1 << 6) | (1 << 5) | (1 << 3) | (1 << 1))
#define MEMORY_SHAREABLE    1

#endif

#define L1_ENTRIES          4096
#define L2_ENTRIES          256
#define L1_TABLE_SIZE       (L1_ENTRIES * sizeof(ULONG))
#define L2_TABLE_SIZE       (L2_ENTRIES * sizeof(ULONG))
#define PDE_INDEX(Va)       ((ULONG)(Va) >> PDI_SHIFT)
#define PTE_INDEX(Va)       (((ULONG)(Va) >> PTI_SHIFT) & 0x3FF)
#define L2_INDEX(Va)        (((ULONG)(Va) >> PTI_SHIFT) & (L2_ENTRIES - 1))

#define SECTION_SHAREABLE   (MEMORY_SHAREABLE ? SECTION_S : 0)
#define L2_SHAREABLE        (MEMORY_SHAREABLE ? L2_S : 0)

//
// Megabyte ranges: user space, the kernel direct map, the per-process system
// regions (page table window and hyperspace), and the shared system space.
//

#define KSEG0_MEGABYTE          (KSEG0_BASE >> 20)
#define KSEG2_MEGABYTE          (KSEG2_BASE >> 20)
#define SHARED_SYSTEM_MEGABYTE  ((ULONG)MM_LOWEST_SYSTEM_ADDRESS >> 20)
#define SHARED_SYSTEM_MEGABYTES (L1_ENTRIES - SHARED_SYSTEM_MEGABYTE)

//
// The peripherals of the system on a chip: the BCM2835 set and the per-core
// block (which holds the GIC-400 of the BCM2711), at their physical addresses
// on the Cortex-A7 and Cortex-A53 boards (BCM2836, BCM2837) and on the
// Cortex-A72 one (BCM2711, in its low peripheral mode). The HAL reaches the
// BCM2835 set through the fixed window at PERIPHERAL_VIRTUAL, above the
// nonpaged pool, and the per-core block at LOCAL_PERIPHERAL_VIRTUAL.
//
// The memory of the PCI Express bus of the BCM2711 is the outbound window of
// its root complex, at 0x6_0000_0000 in the 35-bit space of the Cortex-A72:
// a supersection, which carries bits 35:32 of the address, maps its first
// 16 MB at PCI_MEMORY_VIRTUAL, the HAL's window for the bus memory.
//

#define BCM2836_PERIPHERAL_PHYSICAL     0x3F000000
#define BCM2836_LOCAL_PHYSICAL          0x40000000
#define BCM2711_PERIPHERAL_PHYSICAL     0xFE000000
#define BCM2711_LOCAL_PHYSICAL          0xFF800000
#define BCM2711_PCI_MEMORY_HIGH         6
#define PERIPHERAL_WINDOW_MEGABYTES     16
#define PCI_MEMORY_VIRTUAL              0xFD000000
#define SUPERSECTION_MEGABYTES          16
#define CORTEX_A72                      0xD08

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

//
// The VideoCore memory, above the memory of the system and below the
// peripherals (pages KiVideoCoreFirstPage up to KiVideoCoreEndPage), holds the
// frame buffer. Its uncached mappings are normal noncacheable memory, as in the
// direct map, so that the processor's writes merge into bursts; every other
// uncached mapping is device memory, where each access is a bus access of its
// own.
//

static ULONG KiVideoCoreFirstPage;
static ULONG KiVideoCoreEndPage;

//
// The translation table base a secondary processor loads first, with its caches
// still off (armstart.asm): the kernel-only table and its walk attributes,
// written back to memory before the processor is started. It then takes the
// table of the idle process (KiArmStartProcessorTranslation).
//

ULONG KiArmKernelTtbr;

//
// The second-level tables removed from the hardware tables since the last
// broadcast invalidation, which join the free list after it.
//

static PULONG KiRemovedL2[TB_L2_TABLES];
static ULONG KiRemovedL2Count;

//
// The TB lock: held, with interrupts disabled, while the hardware tables and
// the table pool change. Its owner is the thread that holds it.
//

#if defined(NT_UP)

#define KiAcquireTbLock()
#define KiReleaseTbLock()
#define KiTbLockHeld() FALSE

#else

KSPIN_LOCK KiTbLock;

#define KiAcquireTbLock() KiAcquireSpinLock(&KiTbLock)
#define KiReleaseTbLock() KiReleaseSpinLock(&KiTbLock)
#define KiTbLockHeld() (KiTbLock == (KSPIN_LOCK)KeGetCurrentThread())

#endif

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

//
// Invalidate the translations of a virtual address of every address space id,
// or every translation, in the TB of this processor or, on a multiprocessor,
// of every processor (TLBIMVAA, TLBIALL; TLBIMVAAIS, TLBIALLIS).
//

static __inline VOID
KiInvalidateTbEntry (
    ULONG VirtualAddress
    )
{
    __dsb(_ARM_BARRIER_SY);

#if defined(NT_UP)

    _MoveToCoprocessor(VirtualAddress & ~(PAGE_SIZE - 1), 15, 0, 8, 7, 3);

#else

    _MoveToCoprocessor(VirtualAddress & ~(PAGE_SIZE - 1), 15, 0, 8, 3, 3);

#endif

    __dsb(_ARM_BARRIER_SY);
    __isb(_ARM_BARRIER_SY);
}

static __inline VOID
KiInvalidateEntireTb (
    VOID
    )
{
    __dsb(_ARM_BARRIER_SY);

#if defined(NT_UP)

    _MoveToCoprocessor(0, 15, 0, 8, 7, 0);

#else

    _MoveToCoprocessor(0, 15, 0, 8, 3, 0);

#endif

    __dsb(_ARM_BARRIER_SY);
    __isb(_ARM_BARRIER_SY);
}

//
// Second-level table pool. A table on the free list is linked through its
// first entry; tables are zeroed when allocated. A table removed from the
// hardware tables may still be walked by another processor until the TB is
// invalidated: it is set aside and freed by KiInvalidateRemovedTables.
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
KiInvalidateRemovedTables (
    VOID
    )

/*++

Routine Description:

    This function invalidates the entire TB of every processor and frees the
    second-level tables removed since the last invalidation.

--*/

{
    KiInvalidateEntireTb();
    while (KiRemovedL2Count != 0) {
        KiRemovedL2Count -= 1;
        KiFreeL2Table(KiRemovedL2[KiRemovedL2Count]);
    }
}

static VOID
KiPurgeTable (
    PULONG Tb
    )

/*++

Routine Description:

    This function removes the second-level tables of a hardware first-level
    table's process-private regions, discarding the translations they cache.
    The caller invalidates the TB (KiInvalidateRemovedTables).

--*/

{
    ULONG Megabyte;

    for (Megabyte = 0; Megabyte < SHARED_SYSTEM_MEGABYTE; Megabyte += 1) {
        if (!KiPrivateMegabyte(Megabyte)) {
            continue;
        }

        if ((Tb[Megabyte] & 3) == L1_COARSE) {
            KiRemovedL2[KiRemovedL2Count] =
                (PULONG)(KSEG0_BASE + (Tb[Megabyte] & ~(L2_TABLE_SIZE - 1)));
            KiRemovedL2Count += 1;
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

        KiInvalidateRemovedTables();
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

    __dsb(_ARM_BARRIER_SY);
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
        if ((Pte.PageFrameNumber >= KiVideoCoreFirstPage) &&
            (Pte.PageFrameNumber < KiVideoCoreEndPage)) {
            Descriptor |= L2_TEX_NORMAL | L2_SHAREABLE | L2_XN;

        } else {
            Descriptor |= L2_B;
        }

    } else {
        Descriptor |= L2_TEX_NORMAL | L2_C | L2_B | L2_SHAREABLE;
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

    A miss taken while the current thread holds the TB lock (on the stack of
    the thread being switched to, or a stack page a flush just purged) is
    filled under that hold, as the uniprocessor kernel nests this handler.

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
    BOOLEAN Filled = FALSE;
    BOOLEAN Held;
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

    Held = KiTbLockHeld();
    if (Held == FALSE) {
        KiAcquireTbLock();
    }

    //
    // The user mapping of the shared user data page is part of the fixed TB:
    // read only, and refilled whenever it is not present.
    //

    if ((VirtualAddress & ~(PAGE_SIZE - 1)) == USPCR2) {
        if (Write == FALSE) {
            PageTable = (PHARDWARE_PTE)KiGetL2Table(USPCR2 >> 20, TRUE);
            ((PULONG)PageTable)[L2_INDEX(USPCR2)] = (KiSharedDataPage << PAGE_SHIFT) |
                L2_SMALL | L2_XN | L2_TEX_NORMAL | L2_C | L2_B | L2_SHAREABLE |
                L2_AP2 | L2_AP1 | L2_AP0 | L2_NG;
            KiInvalidateTbEntry(USPCR2);
            Filled = TRUE;
        }

        if (Held == FALSE) {
            KiReleaseTbLock();
        }

        return Filled;
    }

    //
    // The PTE is read under the TB lock, which a flush takes after the
    // memory manager changed it: a translation filled from an older PTE is
    // purged by that flush.
    //

    PageDirectory = (PHARDWARE_PTE)(KSEG0_BASE + PCR->CurrentPdr);
    Pde = PageDirectory[PDE_INDEX(VirtualAddress)];
    if (Pde.Valid != 0) {
        PageTable = (PHARDWARE_PTE)(KSEG0_BASE + (Pde.PageFrameNumber << PAGE_SHIFT));
        Pte = PageTable[PTE_INDEX(VirtualAddress)];
        if ((Pte.Valid != 0) && ((Write == FALSE) || (Pte.Dirty != 0))) {
            KiInstallTbEntry(VirtualAddress, Pte);
            Filled = TRUE;
        }
    }

    if (Held == FALSE) {
        KiReleaseTbLock();
    }

    return Filled;
}

VOID
KeFlushCurrentTb (
    VOID
    )

/*++

Routine Description:

    This function flushes the translation buffer: every translation cached
    for the current process and for shared system space (the random part of
    the MIPS TB), and the TB of every processor.

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();
    ULONG Index;
    ULONG Entry;
    PULONG Table;

    KiAcquireTbLock();
    KiPurgeTable((PULONG)PCR->CurrentTb);
    for (Index = 0; Index < SHARED_SYSTEM_MEGABYTES; Index += 1) {
        Table = KiSystemTable[Index];
        if (Table != NULL) {
            for (Entry = 0; Entry < L2_ENTRIES; Entry += 1) {
                Table[Entry] = 0;
            }
        }
    }

    KiInvalidateRemovedTables();
    KiReleaseTbLock();
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

    KiAcquireTbLock();
    for (Index = 0; Index < Count; Index += 1) {
        KiFlushTbEntry((ULONG)Virtual[Index]);
    }

    KiReleaseTbLock();
    KiRestoreInterrupts(Enable);
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

    KiAcquireTbLock();
    KiFlushTbEntry((ULONG)Virtual);
    KiReleaseTbLock();
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

    KiAcquireTbLock();
    if (Pte[0].Valid != 0) {
        KiInstallTbEntry((ULONG)Virtual, Pte[0]);

    } else {
        KiFlushTbEntry((ULONG)Virtual);
    }

    KiReleaseTbLock();
    KiRestoreInterrupts(Enable);
}

#if !defined(NT_UP)

VOID
KeFlushEntireTb (
    IN BOOLEAN Invalid,
    IN BOOLEAN AllProcessors
    )

/*++

Routine Description:

    This function flushes the entire translation buffer of every processor
    (KE/MIPS/XXFLSHTB.C, whose interprocessor requests the broadcast TB
    maintenance replaces).

Arguments:

    Invalid - Supplies a boolean value that specifies the reason for
        flushing the translation buffer.

    AllProcessors - Supplies a boolean value that determines which
        translation buffers are to be flushed.

--*/

{
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KeFlushCurrentTb();
    KeLowerIrql(OldIrql);
}

VOID
KeFlushMultipleTb (
    IN ULONG Number,
    IN PVOID *Virtual,
    IN BOOLEAN Invalid,
    IN BOOLEAN AllProcessors,
    IN PHARDWARE_PTE *PtePointer OPTIONAL,
    IN HARDWARE_PTE PteValue
    )

/*++

Routine Description:

    This function flushes multiple entries from the translation buffer of
    every processor, after setting the specified page table entries to the
    specified value. The process id wrap lock is held so that the process id
    cannot change meanwhile.

Arguments:

    Number - Supplies the number of TB entries to flush.

    Virtual - Supplies a pointer to an array of virtual addresses that are
        within the pages whose translation buffer entries are to be flushed.

    Invalid - Supplies a boolean value that specifies the reason for
        flushing the translation buffer.

    AllProcessors - Supplies a boolean value that determines which
        translation buffers are to be flushed.

    PtePointer - Supplies an optional pointer to an array of pointers to
        page table entries that receive the specified page table entry
        value.

    PteValue - Supplies the the new page table entry value.

--*/

{
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KiAcquireSpinLock(&KiProcessIdWrapLock);
    if (ARGUMENT_PRESENT(PtePointer)) {
        for (Index = 0; Index < Number; Index += 1) {
            *PtePointer[Index] = PteValue;
        }
    }

    KiFlushMultipleTb(Invalid, &Virtual[0], Number);
    KiReleaseSpinLock(&KiProcessIdWrapLock);
}

HARDWARE_PTE
KeFlushSingleTb (
    IN PVOID Virtual,
    IN BOOLEAN Invalid,
    IN BOOLEAN AllProcessors,
    IN PHARDWARE_PTE PtePointer,
    IN HARDWARE_PTE PteValue
    )

/*++

Routine Description:

    This function flushes a single entry from the translation buffer of
    every processor, after setting the specified page table entry to the
    specified value. The process id wrap lock is held so that the process id
    cannot change meanwhile.

Arguments:

    Virtual - Supplies a virtual address that is within the page whose
        translation buffer entry is to be flushed.

    Invalid - Supplies a boolean value that specifies the reason for
        flushing the translation buffer.

    AllProcessors - Supplies a boolean value that determines which
        translation buffers are to be flushed.

    PtePointer - Supplies a pointer to the page table entry which receives
        the specified value.

    PteValue - Supplies the the new page table entry value.

Return Value:

    The previous contents of the specified page table entry is returned as
    the function value.

--*/

{
    HARDWARE_PTE OldPte;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KiAcquireSpinLock(&KiProcessIdWrapLock);
    OldPte = *PtePointer;
    *PtePointer = PteValue;
    KiFlushSingleTb(Invalid, Virtual);
    KiReleaseSpinLock(&KiProcessIdWrapLock);
    return OldPte;
}

#endif

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
    _MoveToCoprocessor(KiPhysical(KiKernelTable) | TTBR_WALK_CACHED, 15, 0, 2, 0, 0);
    __isb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(ProcessId, 15, 0, 13, 0, 1);
    __isb(_ARM_BARRIER_SY);
    _MoveToCoprocessor(KiPhysical(Tb) | TTBR_WALK_CACHED, 15, 0, 2, 0, 0);
    __isb(_ARM_BARRIER_SY);
}

static ULONG
KiAssignProcessId (
    IN PKPROCESS Process,
    OUT PBOOLEAN Rollover
    )

/*++

Routine Description:

    This function returns the process id of a process, assigning a new one if
    its sequence number is not the master sequence number. When the ids are
    exhausted the master sequence advances and every process's translations
    are discarded (the MIPS PID rollover). Called with the TB lock held.

--*/

{
    ULONG ProcessId;
    ULONG Index;

    if (Process->ProcessSequence == KiMasterSequence) {
        return Process->ProcessPid;
    }

    ProcessId = KiMasterPid + 1;
    Process->ProcessSequence = KiMasterSequence;
    if (ProcessId >= KeNumberProcessIds) {
        KiMasterSequence += 1;
        Process->ProcessSequence = KiMasterSequence;
        for (Index = 0; Index < TB_PROCESS_IDS; Index += 1) {
            KiPurgeTable(KiProcessTable[Index]);
        }

        KiInvalidateRemovedTables();
        ProcessId = 0;
        *Rollover = TRUE;
    }

    KiMasterPid = ProcessId;
    Process->ProcessPid = ProcessId;
    return ProcessId;
}

BOOLEAN
KiSwapAddressSpace (
    IN PKPROCESS NewProcess
    )

/*++

Routine Description:

    This function swaps the address space to another process. If the process
    sequence number matches the master sequence number its process id is still
    valid; otherwise a new id is assigned, and when the ids are exhausted the
    master sequence advances and every process's translations are discarded
    (the MIPS PID rollover). SwapContext calls it with interrupts disabled,
    KiSwapProcess with them enabled.

    On a multiprocessor a rollover takes the process id wrap lock, which the
    flush routines hold, and returns with it held: the caller synchronizes the
    process ids of the other processors (KiSynchronizeProcessIds) and then
    releases it. While the lock is owned interrupts are enabled between the
    attempts to take it, so that this processor serves the interprocessor
    requests of the owner.

Arguments:

    NewProcess - Supplies a pointer to the process that is switched to.

Return Value:

    TRUE if a process id rollover occurred, FALSE otherwise.

--*/

{
    BOOLEAN Enable = KiDisableInterrupts();
    BOOLEAN Rollover = FALSE;
    ULONG ProcessId;

    KiAcquireTbLock();

#if !defined(NT_UP)

    //
    // A rollover is due: take the process id wrap lock first, then the TB
    // lock again; the process may meanwhile have an id of the master sequence
    // another processor's rollover assigned.
    //

    if ((NewProcess->ProcessSequence != KiMasterSequence) &&
        (KiMasterPid + 1 >= KeNumberProcessIds)) {

        KiReleaseTbLock();
        while (KiTryToAcquireSpinLock(&KiProcessIdWrapLock) == FALSE) {
            _enable();
            __yield();
            _disable();
        }

        KiAcquireTbLock();
        ProcessId = KiAssignProcessId(NewProcess, &Rollover);
        if (Rollover == FALSE) {
            KiReleaseSpinLock(&KiProcessIdWrapLock);
        }

    } else {
        ProcessId = KiAssignProcessId(NewProcess, &Rollover);
    }

#else

    ProcessId = KiAssignProcessId(NewProcess, &Rollover);

#endif

    KiLoadTb(ProcessId, NewProcess->DirectoryTableBase[0]);
    KiReleaseTbLock();
    KiRestoreInterrupts(Enable);
    return Rollover;
}

BOOLEAN
KiSwapProcess (
    IN PKPROCESS NewProcess,
    IN PKPROCESS OldProcess
    )

/*++

Routine Description:

    This function swaps the address space from one process to another: the
    attach and detach of a thread to another process (KeAttachProcess,
    KeDetachProcess). It is called at SYNCH_LEVEL with the dispatcher
    database locked and interrupts enabled.

    On a multiprocessor the processor moves from the active processor set of
    the old process to that of the new one, under the context swap lock as in
    SwapContext, and a process id rollover is synchronized with the other
    processors before the process id wrap lock is released.

Arguments:

    NewProcess - Supplies a pointer to the process that is switched to.

    OldProcess - Supplies a pointer to the process that is switched from.

Return Value:

    TRUE if a process id rollover occurred, FALSE otherwise.

--*/

{
    BOOLEAN Rollover;

#if !defined(NT_UP)

    KAFFINITY SetMember = KeGetCurrentPrcb()->SetMember;

    KiAcquireSpinLock(&KiContextSwapLock);
    OldProcess->ActiveProcessors &= ~SetMember;
    NewProcess->ActiveProcessors |= SetMember;
    KiReleaseSpinLock(&KiContextSwapLock);

#else

    UNREFERENCED_PARAMETER(OldProcess);

#endif

    Rollover = KiSwapAddressSpace(NewProcess);

#if !defined(NT_UP)

    if (Rollover != FALSE) {
        KiSynchronizeProcessIds();
        KiReleaseSpinLock(&KiProcessIdWrapLock);
    }

#endif

    return Rollover;
}

#if !defined(NT_UP)

VOID
KiSynchronizeProcessIdsTarget (
    IN PULONG SignalDone,
    IN PVOID Parameter1,
    IN PVOID Parameter2,
    IN PVOID Parameter3
    )

/*++

Routine Description:

    This function synchronizes the process id of the current process with a
    rollover on another processor (KE/MIPS/X4TRAP.S): the translations the
    process cached under its old id since the rollover are discarded, and it
    is given an id of the new master sequence. It is called at IPI_LEVEL;
    the processor that rolled over holds the process id wrap lock.

Arguments:

    SignalDone - Supplies a pointer to a variable that is cleared when the
        requested operation has been performed.

    Parameter1 - Parameter3 - Not used.

--*/

{
    PKPROCESS Process = KeGetCurrentThread()->ApcState.Process;
    BOOLEAN Rollover = FALSE;
    BOOLEAN Enable = KiDisableInterrupts();
    ULONG ProcessId;

    KiAcquireTbLock();
    KiPurgeTable((PULONG)PCR->CurrentTb);
    KiInvalidateRemovedTables();
    ProcessId = KiAssignProcessId(Process, &Rollover);
    KiLoadTb(ProcessId, Process->DirectoryTableBase[0]);
    KiReleaseTbLock();
    KiRestoreInterrupts(Enable);
    KiIpiSignalPacketDone(SignalDone);
}

VOID
KiSynchronizeProcessIds (
    VOID
    )

/*++

Routine Description:

    This function synchronizes the process ids of all other processors after
    a rollover on this one (KE/MIPS/XXFLSHTB.C). It is called at
    DISPATCH_LEVEL with interrupts enabled and the process id wrap lock held.

--*/

{
    PKPRCB Prcb;
    KAFFINITY TargetProcessors;

    Prcb = KeGetCurrentPrcb();
    TargetProcessors = KeActiveProcessors & ~Prcb->SetMember;
    if (TargetProcessors != 0) {
        KiIpiSendPacket(TargetProcessors,
                        KiSynchronizeProcessIdsTarget,
                        NULL,
                        NULL,
                        NULL);

        KiIpiStallOnPacketTargets();
    }
}

#endif

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
    ULONG PeripheralPhysical;
    ULONG LocalPhysical;
    ULONG DeviceStart;
    BOOLEAN Bcm2711;
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
    // (memory, then VideoCore memory, then on the BCM2836 and BCM2837 their
    // peripherals; the first gigabyte of a BCM2711 is all memory), the HAL's
    // device windows (on a BCM2711 also the PCI memory window), and the second
    // PCR page.
    //

    Bcm2711 = (BOOLEAN)(((_MoveFromCoprocessor(15, 0, 0, 0, 0) >> 4) & 0xFFF) == CORTEX_A72);
    if (Bcm2711) {
        PeripheralPhysical = BCM2711_PERIPHERAL_PHYSICAL;
        LocalPhysical = BCM2711_LOCAL_PHYSICAL;
        DeviceStart = (KSEG2_MEGABYTE - KSEG0_MEGABYTE) << 20;

    } else {
        PeripheralPhysical = BCM2836_PERIPHERAL_PHYSICAL;
        LocalPhysical = BCM2836_LOCAL_PHYSICAL;
        DeviceStart = BCM2836_PERIPHERAL_PHYSICAL;
    }

    MemoryTop = (TopPage + ((1 << (20 - PAGE_SHIFT)) - 1)) >> (20 - PAGE_SHIFT);
    KiVideoCoreFirstPage = MemoryTop << (20 - PAGE_SHIFT);
    KiVideoCoreEndPage = DeviceStart >> PAGE_SHIFT;
    for (Megabyte = 0; Megabyte < KSEG2_MEGABYTE - KSEG0_MEGABYTE; Megabyte += 1) {
        Physical = Megabyte << 20;
        if (Megabyte < MemoryTop) {
            KiKernelTable[KSEG0_MEGABYTE + Megabyte] = Physical | L1_SECTION |
                SECTION_AP_PRIV_RW | SECTION_TEX_NORMAL | SECTION_C | SECTION_B |
                SECTION_SHAREABLE;

        } else if (Physical < DeviceStart) {
            KiKernelTable[KSEG0_MEGABYTE + Megabyte] = Physical | L1_SECTION |
                SECTION_AP_PRIV_RW | SECTION_TEX_NORMAL | SECTION_XN | SECTION_SHAREABLE;

        } else {
            KiKernelTable[KSEG0_MEGABYTE + Megabyte] = Physical | L1_SECTION |
                SECTION_AP_PRIV_RW | SECTION_B | SECTION_XN;
        }
    }

    for (Megabyte = 0; Megabyte < PERIPHERAL_WINDOW_MEGABYTES; Megabyte += 1) {
        KiKernelTable[(PERIPHERAL_VIRTUAL >> 20) + Megabyte] =
            (PeripheralPhysical + (Megabyte << 20)) | L1_SECTION |
            SECTION_AP_PRIV_RW | SECTION_B | SECTION_XN;
    }

    KiKernelTable[LOCAL_PERIPHERAL_VIRTUAL >> 20] = LocalPhysical | L1_SECTION |
        SECTION_AP_PRIV_RW | SECTION_B | SECTION_XN;

    for (Megabyte = 0; Bcm2711 && Megabyte < SUPERSECTION_MEGABYTES; Megabyte += 1) {
        KiKernelTable[(PCI_MEMORY_VIRTUAL >> 20) + Megabyte] = SUPERSECTION_HIGH(BCM2711_PCI_MEMORY_HIGH) |
            L1_SUPERSECTION | L1_SECTION | SECTION_AP_PRIV_RW | SECTION_B | SECTION_XN;
    }

    KiSharedDataPage = LoaderBlock->u.Arm.PcrPage2;
    Table = KiAllocateL2Table();
    Table[L2_INDEX(KIPCR2)] = (KiSharedDataPage << PAGE_SHIFT) | L2_SMALL | L2_XN |
        L2_TEX_NORMAL | L2_C | L2_B | L2_SHAREABLE | L2_AP0;
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
    KiArmKernelTtbr = KiPhysical(KiKernelTable) | TTBR_WALK_CACHED;
}

#if !defined(NT_UP)

VOID
KiArmStartProcessorTranslation (
    IN PKPROCESS Process
    )

/*++

Routine Description:

    This function makes the table of the idle process current on a secondary
    processor, which runs on the kernel-only table: the process is given an
    id of the master sequence, with the rollover synchronized as at a context
    switch.

Arguments:

    Process - Supplies a pointer to the idle process.

--*/

{
    if (KiSwapAddressSpace(Process) != FALSE) {
        KiSynchronizeProcessIds();
        KiReleaseSpinLock(&KiProcessIdWrapLock);
    }
}

#endif

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
