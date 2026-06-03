/*++

Copyright (c) 2026

Module Name:

    mmuarm.c

Abstract:

    ARMv7-A paging / high-half bring-up for the NT 3.5 / Raspberry Pi 2 port.
    Builds the boot page table the MMU walks and enables translation so the
    kernel runs in the high half (KSEG0, 0x80000000+), as NT expects.

    The boot table (short-descriptor sections):
      - identity map of low RAM            (Normal, VA == PA - so the PIC startup
                                            head, its stack, and the loader block
                                            keep working across the SCTLR.M write)
      - KSEG0 window 0x80000000 -> PA 0     (Normal - the kernel is LINKED here and
                                            runs here once paging is on; the MIPS
                                            KSEG0 direct map, MM_SYSTEM_RANGE_START)
      - BCM2836 peripherals 0x3F000000..    (Device - UART / mailbox FB / timer /
                                            interrupt controller stay reachable)

    Split for the startup head: MiArmBuildBootPageTable / MiArmTurnOnMmu are
    position-independent leaves (no globals, strings, or external calls) so
    ke/armstart.S can call them while still running at the *physical* load address
    with the MMU off; they take physical pointers. MiArmReportPaging runs later,
    in the high half, and uses the full kernel runtime (KiEmit, the globals).

    The kernel is linked at KSEG0 | physical-load = 0x81000000 (ImageBase), but
    peldr loads it at physical 0x01000000 (it masks ImageBase & 0x1fffffff), so
    KSEG0 0x80000000->PA0 makes the linked VA 0x81xxxxxx resolve to the loaded
    image - no loader change is needed.

    HARDWARE_PTE translation (inc/ntarm.h) is demonstrated by MiArmReportPaging,
    which fills an L2 table from NT PTEs: present<-Valid, AP[2]<-!Write (inverted),
    nG<-!Global (inverted), TEX/C/B<-CachePolicy, base<-PFN.

    Caches (SCTLR.C/I) are left off this stage; they come on with the proper
    cache-maintenance in a later step.

Environment:

    Kernel mode, uniprocessor, early boot (head runs MMU-off/physical/PIC).

--*/

#include "ki.h"

extern void KiEmit(const char *s);
extern void KiEmitHex(ULONG v);

//
// KI_RUN_EXECUTIVE: set by make-execlink.sh (the full-executive kernel) so the
// genuine MmInitSystem / MiInitMachineDependent (ke/initarm.c) runs. When it is
// on, MiArmInitMachineDependent additionally reserves a CONTIGUOUS physical
// chunk for the executive's initial nonpaged pool (so MmNonPagedPoolStart can be
// a KSEG0 direct-map alias) and skips the in-kernel self-map demos. The minimal
// make-kernel.sh kernel leaves it 0 -> behaviour is byte-identical to before.
//

#ifndef KI_RUN_EXECUTIVE
#define KI_RUN_EXECUTIVE 0
#endif

#define L1_ENTRIES 4096                 // 4 GB / 1 MB, 16 KB directory
#define L2_ENTRIES 256                  // 1 MB / 4 KB, 1 KB table
#define KSEG0 0x80000000u               // high-half base (== ntarm.h KSEG0_BASE)

//
// Initial nonpaged-pool reservation (KI_RUN_EXECUTIVE). Reserved contiguous and
// physically above the PFN database, in the KSEG0 direct map; ke/initarm.c points
// MmNonPagedPoolStart at its KSEG0 alias. 4 MB is ample for executive Phase 0.
//

#define MI_ARM_INITIAL_POOL_PAGES 0x400u        // 4 MB

ULONG MiArmPoolBasePage = 0;            // first page of the pool reservation (0 = none)
ULONG MiArmPoolPages = 0;               // pages reserved

//
// L2/window arena (KI_RUN_EXECUTIVE). mmuarm's own contiguous page source for
// page-table / self-map-window pages (MiArmAllocKseg0Page), reserved next to the
// pool. So the real MM free lists (ke/initarm.c hands the rest of physical memory
// to MiInsertPageInList / MiRemoveAnyPage) and mmuarm's page-table allocations
// draw from DISJOINT pages - no double-use.
//

#define MI_ARM_L2_ARENA_PAGES 0x800u            // 8 MB (window L2s + real L2s for the system region)
ULONG MiArmL2ArenaBase = 0;
ULONG MiArmL2ArenaPages = 0;
static ULONG MiArmL2ArenaNext = 0;

//
// The boot page directory (16 KB aligned, as TTBR0 requires) and one second-level
// table for the HARDWARE_PTE demonstration. Global so ke/armstart.S can take their
// addresses; static-storage so they sit at a fixed physical address. Linked in
// KSEG0 (0x81xxxxxx); their physical address is the link address minus KSEG0.
//

ULONG MiArmL1[L1_ENTRIES] __attribute__((aligned(16384)));
static ULONG MiArmL2Demo[L2_ENTRIES] __attribute__((aligned(1024)));

//
// The LOGICAL page directory (REAL MM). MM writes its page-directory entries
// (*StartPde, NT software-PTE format) HERE - reached via MiGetPdeAddress once the PDE
// self-map window is repointed at it (MiArmBuildSelfMap, KI_RUN_EXECUTIVE) - NOT into
// the live MiArmL1 the MMU walks. The hardware L1 is filled lazily FROM this directory
// on a section-translation fault (the two-level fill in MiArmTryFillFault), so MM's PDE
// writes drive the MMU-walked tables without ever touching the live TTBR0 directory.
// 16 KB / 16 KB-aligned, the logical analog of MiArmL1; never loaded into TTBR0. (Per
// process in step C; one system directory in step A.)
//
ULONG MiArmLogPd[L1_ENTRIES] __attribute__((aligned(16384)));

static volatile ULONG MiArmMmuProbe = 0xABCD1234;

//
// ARMv7-A descriptor builders. Domain 0, non-shared, global for the boot table.
// Leaf + no globals -> position-independent (callable from the MMU-off head).
//

static ULONG
MiArmSection (
    ULONG PhysicalBase,
    int Device,
    int NoExecute,
    int Writable
    )
{
    ULONG d = (PhysicalBase & 0xFFF00000) | 0x2;        // section, base
    d |= Device ? (1u << 2)                             // Device: TEX=000 C=0 B=1
                : ((1u << 12) | (1u << 3) | (1u << 2)); // Normal WBWA: TEX=001 C=1 B=1
    d |= (1u << 10);                                    // AP[1:0]=01 (priv, no user)
    if (!Writable) d |= (1u << 15);                     // AP[2]=1 -> read-only
    if (NoExecute) d |= (1u << 4);                      // XN
    return d;
}

static ULONG
MiArmPteToDescriptor (
    HARDWARE_PTE Pte
    )
{
    ULONG d;

    if (!Pte.Valid)
        return 0;                                       // fault entry (software PTE)

    d = (Pte.PageFrameNumber << 12) | 0x2;              // small page present, base
    d |= (Pte.CachePolicy == UNCACHED_POLICY)
            ? (1u << 2)                                 // Device: TEX=000 C=0 B=1
            : ((1u << 6) | (1u << 3) | (1u << 2));      // Normal WBWA: TEX=001 C=1 B=1
    d |= (1u << 4);                                     // AP[1:0]=01 (priv, no user)
    if (!Pte.Write)   d |= (1u << 9);                   // AP[2]=1 -> read-only
    if (!Pte.Global)  d |= (1u << 11);                  // nG=1 -> not global
    return d;
}

//
// Build the essential boot map into the L1 referenced by L1Phys. PIC: called from
// ke/armstart.S at the physical load address with the MMU off, so it must touch
// nothing but its argument (no globals / strings / external calls).
//

VOID
MiArmBuildBootPageTable (
    ULONG *L1Phys
    )
{
    ULONG va, i;

    for (i = 0; i < L1_ENTRIES; i += 1)
        L1Phys[i] = 0;                                  // all fault

    for (va = 0x00000000; va < 0x3F000000; va += 0x100000)
        L1Phys[va >> 20] = MiArmSection(va, 0, 0, 1);   // identity RAM, Normal

    for (i = 0; i < 512; i += 1)                        // KSEG0 0x80000000 -> PA 0
        L1Phys[(KSEG0 >> 20) + i] = MiArmSection(i << 20, 0, 0, 1);

    for (va = 0x3F000000; va < 0x41000000; va += 0x100000)
        L1Phys[va >> 20] = MiArmSection(va, 1, 1, 1);   // peripherals, Device, XN
}

//
// Load the translation registers and set SCTLR.M. PIC leaf (cp15 + argument only).
// Caches stay off (only M is set). After this the MMU is on; ke/armstart.S then
// branches to the KSEG0 (high) virtual address to run the rest of the kernel there.
//

VOID
MiArmTurnOnMmu (
    ULONG L1Phys
    )
{
    ULONG sctlr, zero = 0, dacr = 1;

    __asm__ __volatile__("mcr p15, 0, %0, c2, c0, 0" :: "r"(L1Phys));   // TTBR0 = L1
    __asm__ __volatile__("mcr p15, 0, %0, c2, c0, 2" :: "r"(zero));     // TTBCR = 0
    __asm__ __volatile__("mcr p15, 0, %0, c3, c0, 0" :: "r"(dacr));     // DACR: domain0 client
    __asm__ __volatile__("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero));     // TLBIALL
    __asm__ __volatile__("dsb ; isb" ::: "memory");
    __asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    __asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr | 1u) : "memory"); // M=1
    __asm__ __volatile__("isb" ::: "memory");
}

//
// Decode and print one descriptor word so the dump is human-readable.
//

static VOID
MiArmDumpEntry (
    const char *Label,
    ULONG Descriptor
    )
{
    ULONG type = Descriptor & 0x3;

    KiEmit("    ");
    KiEmit(Label);
    KiEmit(" = ");
    KiEmitHex(Descriptor);
    KiEmit("  ");
    if (type == 0) { KiEmit("(fault)\n"); return; }
    if (type == 1) { KiEmit("(-> L2 table)\n"); return; }
    KiEmit("section pa=");
    KiEmitHex(Descriptor & 0xFFF00000);
    KiEmit((Descriptor & (1u << 12)) ? " normal" : " device");
    KiEmit((Descriptor & (1u << 15)) ? " ro\n" : " rw\n");
}

//
// The ARMv7 page-table self-map - the mechanism NT's MM edits page tables through
// (MiGetPteAddress). x86/MIPS use uniform 1024-entry 4 KB tables and a clean
// recursive PDE; ARMv7 short descriptors split 4096 1 MB L1 entries from 256 4 KB
// L2 entries, so it is hand-built: the L2 table for megabyte M is exposed in the
// PTE window at PTE_BASE + M*1024 (== MiGetPteAddress of M's first page). This demo
// maps test VA 0xD0000000 via an L2 table, exposes that L2 in the PTE window, then
// EDITS the PTE through its window VA and confirms the mapping resolves - exactly
// what MiInitMachineDependent and the data-abort fill path will do.
//

static ULONG MiArmL2Test[L2_ENTRIES]   __attribute__((aligned(4096)));
static ULONG MiArmL2PteWin[L2_ENTRIES] __attribute__((aligned(1024)));
static ULONG MiArmTargetPage[1024]     __attribute__((aligned(4096)));

#define SMALL_PAGE_NORMAL_RW 0x5Eu      // present | TEX=001,C,B | AP[0]  (Normal, priv RW)

static VOID
MiArmSelfMapDemo (
    VOID
    )
{
    ULONG physL2Test = (ULONG)MiArmL2Test - KSEG0;
    ULONG physL2Win  = (ULONG)MiArmL2PteWin - KSEG0;
    ULONG physTarget = (ULONG)MiArmTargetPage - KSEG0;
    ULONG pteVa = PTE_BASE + ((0xD0000000u >> 12) << 2);   // = MiGetPteAddress(0xD0000000)
    ULONG i, before, after;

    for (i = 0; i < L2_ENTRIES; i += 1) { MiArmL2Test[i] = 0; MiArmL2PteWin[i] = 0; }
    MiArmTargetPage[0] = 0x1234ABCDu;

    MiArmL1[0xD00] = (physL2Test & 0xFFFFFC00) | 0x1;      // VA 0xD0000000 -> L2Test
    MiArmL1[0xC03] = (physL2Win  & 0xFFFFFC00) | 0x1;      // the PTE-window megabyte -> L2PteWin
    MiArmL2PteWin[(pteVa >> 12) & 0xFF] =                  // expose L2Test at pteVa
        (physL2Test & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
    __asm__ __volatile__("mcr p15,0,%0,c8,c7,0 ; dsb ; isb" :: "r"(0) : "memory"); // TLBIALL

    before = *(volatile ULONG *)pteVa;                     // L2Test[0] seen via the window (0)

    *(volatile ULONG *)pteVa =                             // EDIT the PTE through its VA
        (physTarget & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
    __asm__ __volatile__("mcr p15,0,%0,c8,c7,0 ; dsb ; isb" :: "r"(0) : "memory");

    after = *(volatile ULONG *)0xD0000000u;                // now resolves to the target page

    KiEmit("\nARMv7 page-table self-map (the MiInitMachineDependent keystone):\n");
    KiEmit("  MiGetPteAddress(0xD0000000) : ");
    KiEmitHex(pteVa);
    KiEmit("\n  PTE before edit (via window): ");
    KiEmitHex(before);
    KiEmit(before == 0 ? "  (unmapped)\n" : "\n");
    KiEmit("  wrote a PTE through that VA, then read VA 0xD0000000:\n  *0xD0000000                 : ");
    KiEmitHex(after);
    KiEmit(after == 0x1234ABCDu ? "  OK - PTE edited via the self-map took effect\n"
                                : "  *** FAIL ***\n");
}

//
// Runs in the high half after the head has enabled paging. Confirms the kernel's
// PC is now in KSEG0, demonstrates the HARDWARE_PTE -> ARMv7 descriptor path by
// building an L2 table from NT PTEs and mapping it live, and dumps the boot table.
//

VOID
MiArmReportPaging (
    VOID
    )
{
    HARDWARE_PTE pte;
    ULONG pc, sctlr, i;

    __asm__ __volatile__("mov %0, pc" : "=r"(pc));
    __asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));

    KiEmit("\nARMv7-A high-half: the kernel now executes translated in KSEG0.\n");
    KiEmit("  current PC                  : ");
    KiEmitHex(pc);
    KiEmit((pc >= KSEG0) ? "  (high half!)\n" : "  *** still low ***\n");
    KiEmit("  SCTLR.M (MMU enable)        : ");
    KiEmitHex(sctlr);
    KiEmit((sctlr & 1u) ? "  ON\n" : "  *** OFF ***\n");
    KiEmit("  translated read of probe    : ");
    KiEmitHex(MiArmMmuProbe);
    KiEmit((MiArmMmuProbe == 0xABCD1234) ? "  OK\n" : "  *** FAIL ***\n");

    //
    // HARDWARE_PTE demonstration: fill the L2 table from NT PTEs (PFN i -> the i-th
    // physical page) and map it live at VA 0xC0000000 (PTE_BASE in the NT layout).
    // The L1 descriptor needs the L2 table's PHYSICAL address (KSEG0 link - KSEG0).
    //

    for (i = 0; i < L2_ENTRIES; i += 1) {
        pte.Global = 1; pte.Valid = 1; pte.Dirty = 0; pte.CachePolicy = 1;
        pte.PageFrameNumber = i; pte.Write = 1; pte.CopyOnWrite = 0;
        MiArmL2Demo[i] = MiArmPteToDescriptor(pte);
    }
    MiArmL1[0xC00] = (((ULONG)MiArmL2Demo - KSEG0) & 0xFFFFFC00) | 0x1;
    __asm__ __volatile__("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));        // TLBIALL
    __asm__ __volatile__("dsb ; isb" ::: "memory");

    KiEmit("  L1 @ ");
    KiEmitHex((ULONG)MiArmL1);
    KiEmit(" (KSEG0 VA; physical ");
    KiEmitHex((ULONG)MiArmL1 - KSEG0);
    KiEmit("):\n");
    MiArmDumpEntry("[0x000] VA 00000000 identity", MiArmL1[0]);
    MiArmDumpEntry("[0x3F0] VA 3F000000 uart/fb ", MiArmL1[0x3F0]);
    MiArmDumpEntry("[0x800] VA 80000000 KSEG0   ", MiArmL1[0x800]);
    MiArmDumpEntry("[0xC00] VA C0000000 ptes    ", MiArmL1[0xC00]);
    KiEmit("    demo L2[5] PFN 5 (HARDWARE_PTE) = ");
    KiEmitHex(MiArmL2Demo[5]);
    KiEmit("\n");

    MiArmSelfMapDemo();

    KiEmit("\nKernel runs in the high half. Clock + HDMI + idle loop follow.\n\n");
}

//
// ===========================================================================
//  MiInitMachineDependent (ARMv7) - the MM init body.
//
//  Brought up incrementally and SELF-CONTAINED, the way the rest of mmuarm.c
//  proves each ARM page-table mechanism in the running kernel before the
//  portable executive (Ex/Ob/Ps/Mm) is linked. Adapted from the genuine
//  PRIVATE/NTOS/MM/MIPS/INITMIPS.C MiInitMachineDependent:
//
//    1. walk LoaderBlock->MemoryDescriptorListHead -> physical page counts +
//       the largest free run;
//    2. carve + build the PFN database in the KSEG0 direct map;
//    3. thread the free physical pages onto the free list (the NT
//       MiInsertPageInList discipline: Flink/Blink are PFN *indices*);
//    4. the data-abort -> fill path (ke/trap.S) consumes that free list.
//
//  MMPTE / MMPFN / MI_PFN_ELEMENT / the MiGet* macros live in miarm.h + mi.h,
//  which are NOT in this kernel build's include chain (only HARDWARE_PTE and
//  the address constants from arm.h/ntarm.h are). So the PFN structures and
//  the free-list discipline are mirrored here field-for-field from the real NT
//  definitions - a drop-in once the executive is linked and owns them.
// ===========================================================================
//

//
// The NT MMPFN (24 bytes), mirrored field-for-field (MI.H _MMPFN / _MMPFNENTRY)
// so the future executive link is a drop-in. Free-list links are PFN INDICES,
// not pointers; MM_EMPTY_LIST terminates a list. PageLocation occupies bits
// 8..10 of the u3 word, exactly as MMPFNENTRY packs it.
//

#define MM_EMPTY_LIST 0xFFFFFFFFu

//
// PageLocation values (ZeroedPageList / FreePageList / BadPageList /
// ActiveAndValid ...) come from the _MMLISTS enum in the in-chain mm.h.
//

typedef struct _ARM_MMPFN {
    ULONG  Flink;               // u1.Flink   (free-list next, PFN index)
    ULONG  PteAddress;          // PMMPTE that maps this page
    ULONG  Blink;               // u2.Blink   (free-list prev, PFN index)
    USHORT ReferenceCount;
    USHORT ValidPteCount;
    ULONG  OriginalPte;
    ULONG  u3;                  // MMPFNENTRY: PageLocation in bits 8..10
} ARM_MMPFN, *PARM_MMPFN;

typedef struct _ARM_PFNLIST {
    ULONG Total;
    ULONG Flink;                // head PFN index (MM_EMPTY_LIST if empty)
    ULONG Blink;                // tail PFN index
} ARM_PFNLIST;

//
// MiArm* prefixes (not the real Mm* names) because mm.h - which IS in this
// build's chain - already declares MmNumberOfPhysicalPages / Mm{Lowest,Highest}
// PhysicalPage extern; these are our self-contained bring-up structures that
// mirror the real ones field-for-field, not the linked executive globals.
//

static PARM_MMPFN MiArmPfnDb;
static ULONG MiArmPhysPages;
static ULONG MiArmLowestPage = 0xFFFFFFFFu;
static ULONG MiArmHighestPage;
static ARM_PFNLIST MiArmFreeList = { 0, MM_EMPTY_LIST, MM_EMPTY_LIST };
static ARM_PFNLIST MiArmBadList  = { 0, MM_EMPTY_LIST, MM_EMPTY_LIST };

#define MI_PFN(idx) (&MiArmPfnDb[(idx)])

//
// PageLocation lives in bits 8..10 of the u3 word (MMPFNENTRY layout).
//

static VOID
MiArmSetLocation (
    PARM_MMPFN p,
    ULONG Location
    )
{
    p->u3 = (p->u3 & ~(7u << 8)) | ((Location & 7u) << 8);
}

//
// MiInsertPageInList / MiRemoveAnyPage discipline (PFNLIST.C): thread/unthread
// a page by PFN INDEX through the database (Flink/Blink are indices, not
// pointers; MM_EMPTY_LIST terminates). Insert at tail, remove at head.
//

static VOID
MiArmInsertPageInList (
    ARM_PFNLIST *List,
    ULONG PageFrameIndex
    )
{
    PARM_MMPFN p = MI_PFN(PageFrameIndex);
    ULONG last = List->Blink;

    if (last == MM_EMPTY_LIST)
        List->Flink = PageFrameIndex;
    else
        MI_PFN(last)->Flink = PageFrameIndex;

    List->Blink = PageFrameIndex;
    p->Flink = MM_EMPTY_LIST;
    p->Blink = last;
    p->ReferenceCount = 0;
    MiArmSetLocation(p, (List == &MiArmBadList) ? BadPageList : FreePageList);
    List->Total += 1;
}

static ULONG
MiArmRemovePageFromList (
    ARM_PFNLIST *List
    )
{
    ULONG pfn = List->Flink;
    PARM_MMPFN p;

    if (pfn == MM_EMPTY_LIST)
        return 0;

    p = MI_PFN(pfn);
    List->Flink = p->Flink;
    if (p->Flink == MM_EMPTY_LIST)
        List->Blink = MM_EMPTY_LIST;
    else
        MI_PFN(p->Flink)->Blink = MM_EMPTY_LIST;

    List->Total -= 1;
    p->Flink = 0;
    p->Blink = 0;
    p->ReferenceCount = 1;
    MiArmSetLocation(p, ActiveAndValid);
    return pfn;
}

//
// The KSEG0 direct map covers only PA 0..0x20000000 (512 MB), but RPi2 RAM is
// 896 MB, so a free PFN above the window cannot be reached as KSEG0 + (pfn<<12).
// Every page this bring-up touches that way (L2 tables, demand-fill pages) must
// be in-window, so allocate them through this guard: the free list is FIFO and
// low-first, so the head stays well under 512 MB here; peek before popping so a
// (theoretical) high head is refused rather than handed out and faulted on. The
// real MM uses the INITMIPS PfnInKseg0-vs-virtual split for pages above KSEG0.
//

#define MI_KSEG0_PAGE_LIMIT (0x20000000u >> PAGE_SHIFT)

static ULONG
MiArmAllocKseg0Page (
    VOID
    )
{
#if KI_RUN_EXECUTIVE
    //
    // Bump-allocate from the dedicated arena (disjoint from the real MM free list).
    // The arena was carved low (KSEG0-reachable) and marked active.
    //
    if (MiArmL2ArenaNext >= MiArmL2ArenaBase + MiArmL2ArenaPages)
        return 0;
    return MiArmL2ArenaNext++;
#else
    if (MiArmFreeList.Flink == MM_EMPTY_LIST ||
        MiArmFreeList.Flink >= MI_KSEG0_PAGE_LIMIT)
        return 0;
    return MiArmRemovePageFromList(&MiArmFreeList);
#endif
}

static VOID
MiArmZeroPages (
    PVOID Va,
    ULONG Bytes
    )
{
    volatile ULONG *p = (volatile ULONG *)Va;
    ULONG n = Bytes >> 2;

    while (n--)
        *p++ = 0;
}

static VOID MiArmTlbiAll (VOID)
{
    __asm__ __volatile__("mcr p15,0,%0,c8,c7,0 ; dsb ; isb" :: "r"(0) : "memory");
}

static VOID MiArmTlbiMva (ULONG Va)
{
    __asm__ __volatile__("mcr p15,0,%0,c8,c7,1 ; dsb ; isb"
                         :: "r"(Va & 0xFFFFF000) : "memory");
}

//
// ---------------------------------------------------------------------------
//  The general ARMv7 page-table self-map (Task: MiGetPteAddress works across
//  the system region, not just the one demo megabyte).
//
//  x86/MIPS use a uniform recursive self-map (1024-entry 4 KB tables at both
//  levels + a PDE that points at itself). ARMv7 short descriptors split 4096
//  1 MB L1 entries (16 KB table) from 256 4 KB L2 entries (1 KB table), so the
//  recursion is hand-built:
//
//    PTE window:  MiGetPteAddress(va) = PTE_BASE + (va>>12)*4 (4 MB at 0xC0000000)
//                 -> megabyte M's L2 table is exposed at PTE_BASE + M*0x400.
//                 The window is itself 4 megabytes (L1[0xC00..0xC03]); each is an
//                 L2-of-L2 (MiArmWindowL2[w]) whose entries map the physical pages
//                 holding the real L2 tables. L2 tables pack 4-per-page (1 KB
//                 each), so 4 source megabytes (M&~3..M|3) share one window page.
//
//    PDE window:  MiGetPdeAddress(va) = PDE_BASE + (va>>20)*4 (16 KB at 0xC0400000)
//                 -> the 16 KB L1 directory itself, via MiArmPdeWinL2[0..3].
//
//  L2 tables are allocated from the PFN free list (real physical pages); for the
//  bring-up the free-list head hands out low pages (< 16 MB), so their KSEG0 VA
//  (0x80000000 + phys) is inside the 512 MB KSEG0 window.
// ---------------------------------------------------------------------------
//

static ULONG MiArmWindowL2[4][256] __attribute__((aligned(1024))); // L1[0xC00..0xC03]
static ULONG MiArmPdeWinL2[256]    __attribute__((aligned(1024))); // L1[0xC04] -> the L1
static ULONG MiArmL2GroupPage[1024];   // (M>>2) -> phys page of 4 packed L2 tables, 0=none

//
// Wire the four PTE-window L2-of-L2 tables into L1[0xC00..0xC03] and expose the
// 16 KB L1 directory at PDE_BASE. Supersedes the one-shot demo wirings made by
// MiArmReportPaging / MiArmSelfMapDemo (those already printed their results).
//

static VOID
MiArmBuildSelfMap (
    VOID
    )
{
    ULONG w, j, l1phys;

    for (w = 0; w < 4; w += 1) {
        for (j = 0; j < 256; j += 1)
            MiArmWindowL2[w][j] = 0;
        MiArmL1[0xC00 + w] =
            (((ULONG)MiArmWindowL2[w] - KSEG0) & 0xFFFFFC00) | 0x1;
    }

    l1phys = (ULONG)MiArmL1 - KSEG0;            // 16 KB directory = 4 pages
#if KI_RUN_EXECUTIVE
    //
    // REAL MM (step A): repoint the PDE self-map window at the LOGICAL page directory,
    // so MM's *StartPde (via MiGetPdeAddress) writes land in MiArmLogPd - the inert
    // logical directory - NOT the live TTBR0 MiArmL1 the MMU walks. The hardware L1 is
    // filled lazily FROM MiArmLogPd by the two-level fill in MiArmTryFillFault. This is
    // the un-fake of "MM writes NT-format PDEs straight into the live hardware L1."
    //
    MiArmZeroPages((PVOID)MiArmLogPd, sizeof(MiArmLogPd));
    l1phys = (ULONG)MiArmLogPd - KSEG0;
#endif
    for (j = 0; j < 256; j += 1)
        MiArmPdeWinL2[j] = (j < 4)
            ? (((l1phys + (j << PAGE_SHIFT)) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW)
            : 0;
    MiArmL1[0xC04] = (((ULONG)MiArmPdeWinL2 - KSEG0) & 0xFFFFFC00) | 0x1;
#if KI_RUN_EXECUTIVE
    //
    // The self-referential PDE: MiGetPdeAddress(PDE_BASE) (read by MM at MMINIT.C:533 and
    // :1272 for MmSystemPageDirectory = the page-directory PFN) now resolves through the
    // repointed window to MiArmLogPd[PDE_BASE>>20]; point it at the logical directory's
    // own first page so that read returns a sane PFN.
    //
    {
        HARDWARE_PTE pde;
        *(ULONG *)&pde = 0;
        pde.Global = 1; pde.Valid = 1; pde.Write = 1; pde.CachePolicy = 1;
        pde.PageFrameNumber = (((ULONG)MiArmLogPd - KSEG0) >> PAGE_SHIFT);
        MiArmLogPd[PDE_BASE >> 20] = *(ULONG *)&pde;
    }
#endif

    MiArmTlbiAll();
}

//
// Return the KSEG0 VA of megabyte M's 1 KB L2 table, allocating its packed
// storage page (4 L2s per page) and wiring it into the PTE window on first use.
//

static ULONG
MiArmL2Va (
    ULONG M
    )
{
    ULONG group = M >> 2;
    ULONG phys = MiArmL2GroupPage[group];

    if (phys == 0) {
        ULONG pfn = MiArmAllocKseg0Page();
        phys = pfn << PAGE_SHIFT;
        MiArmL2GroupPage[group] = phys;
        MiArmZeroPages((PVOID)(KSEG0 + phys), PAGE_SIZE);
        MiArmWindowL2[M >> 10][(M >> 2) & 0xFF] =
            (phys & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;   // expose in the PTE window
        MiArmTlbiAll();
    }
    return KSEG0 + phys + ((M & 3) << 10);
}

//
// Ensure megabyte M of `va` has a zeroed L2 exposed in the PTE window, so
// MiGetPteAddress(va) resolves to a valid (zeroed = invalid) descriptor MM can
// read/write - WITHOUT wiring L1[M] (the MMU must NOT walk this region: it holds
// the nonpaged-pool *expansion* system PTEs, which are NT software-PTE format, not
// ARMv7 descriptors). Used by the real MiInitMachineDependent (ke/initarm.c) so
// MiInitializeNonPagedPool's MiGetPteAddress(expansion) reads Valid==0 instead of
// faulting. Non-static (callable from initarm.c).
//
VOID
MiArmEnsureL2 (
    ULONG va
    )
{
    (void)MiArmL2Va(va >> 20);          // allocate + zero + expose in the PTE window
    MiArmTlbiAll();
}

//
// Map one Normal-cacheable RW page at system VA `va` to a fresh free physical page
// (off the free list); wires L1[M]->L2, the page's L2 descriptor, and the PTE
// window. Returns the physical PFN, or 0 if out of free memory. The pages need not
// be physically contiguous (each is paged individually) - the x86-style nonpaged
// pool. Non-static (callable from initarm.c).
//
ULONG
MiArmMapSystemPage (
    ULONG va
    )
{
    ULONG M = va >> 20;
    ULONG idx = (va >> 12) & 0xFF;
    volatile ULONG *l2 = (volatile ULONG *)MiArmL2Va(M);
    ULONG pfn = MiArmAllocKseg0Page();

    if (pfn == 0)
        return 0;
    l2[idx] = ((pfn << PAGE_SHIFT) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
    if ((MiArmL1[M] & 0x3) != 0x1)
        MiArmL1[M] = (((ULONG)l2 - KSEG0) & 0xFFFFFC00) | 0x1;
    MiArmTlbiMva(va);
    return pfn;
}

static const char *
MiArmMemTypeName (
    ULONG t
    )
{
    switch (t) {
    case LoaderExceptionBlock:    return "ExceptionBlock";
    case LoaderSystemBlock:       return "SystemBlock";
    case LoaderFree:              return "Free";
    case LoaderBad:               return "Bad";
    case LoaderLoadedProgram:     return "LoadedProgram";
    case LoaderFirmwareTemporary: return "FirmwareTemp";
    case LoaderFirmwarePermanent: return "FirmwarePerm";
    case LoaderOsloaderHeap:      return "OsloaderHeap";
    case LoaderOsloaderStack:     return "OsloaderStack";
    case LoaderSystemCode:        return "SystemCode";
    case LoaderHalCode:           return "HalCode";
    case LoaderBootDriver:        return "BootDriver";
    case LoaderMemoryData:        return "MemoryData";
    case LoaderNlsData:           return "NlsData";
    case LoaderRegistryData:      return "RegistryData";
    default:                      return "other";
    }
}

//
// MiGetPteAddress / MiGetPdeAddress (the ARMv7 forms from miarm.h, inlined here
// since miarm.h is not in this build's chain).
//

#define MI_PTE_VA(va) (PTE_BASE + ((((ULONG)(va)) >> 12) << 2))
#define MI_PDE_VA(va) (PDE_BASE + ((((ULONG)(va)) >> 20) << 2))

//
// Demonstrate, on a safe KSEG0 megabyte the kernel does not run from, the two
// MiInitMachineDependent mechanisms MM needs: (1) remap a 1 MB section as 256
// per-page (4 KB) PTEs without changing what it maps, and (2) the L2 is wired
// into the self-map so editing a PTE through its MiGetPteAddress window VA
// changes the live mapping - exactly what MmAccessFault does at runtime.
//

static VOID
MiArmRemapDemo (
    VOID
    )
{
    ULONG testVa = 0x90000000;                      // KSEG0 + 0x10000000 (free RAM)
    ULONG M = testVa >> 20;
    ULONG basePhys = (M - (KSEG0 >> 20)) << 20;     // PA the section maps (0x10000000)
    volatile ULONG *l2;
    ULONG j, before, afterRemap, pteWin, pde, freshPfn, afterEdit;

    KiEmit("\nARMv7 4 KB remap + self-map edit:\n");

    *(volatile ULONG *)testVa = 0xCAFEF00Du;        // scratch sentinel via the 1 MB section
    before = *(volatile ULONG *)testVa;

    l2 = (volatile ULONG *)MiArmL2Va(M);            // megabyte M's L2 (allocated + windowed)
    for (j = 0; j < L2_ENTRIES; j += 1)             // mirror the section: 256 x 4 KB pages
        l2[j] = ((basePhys + (j << PAGE_SHIFT)) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;

    MiArmL1[M] = (((ULONG)l2 - KSEG0) & 0xFFFFFC00) | 0x1;   // section -> L2 pointer
    MiArmTlbiMva(testVa);
    afterRemap = *(volatile ULONG *)testVa;         // same physical page -> sentinel survives

    pteWin = MI_PTE_VA(testVa);
    pde = MI_PDE_VA(testVa);

    freshPfn = MiArmAllocKseg0Page();
    *(volatile ULONG *)(KSEG0 + (freshPfn << PAGE_SHIFT)) = 0xDEADBEEFu;
    *(volatile ULONG *)pteWin =                     // edit page 0's PTE through the window VA
        ((freshPfn << PAGE_SHIFT) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
    MiArmTlbiMva(testVa);
    afterEdit = *(volatile ULONG *)testVa;          // now resolves to the fresh page

    KiEmit("  test VA                     : "); KiEmitHex(testVa);
    KiEmit("\n  sentinel via 1MB section    : "); KiEmitHex(before);
    KiEmit("\n  after remap to 4KB pages    : "); KiEmitHex(afterRemap);
    KiEmit(afterRemap == 0xCAFEF00Du ? "  OK - same physical page\n" : "  *** FAIL ***\n");
    KiEmit("  MiGetPdeAddress(VA)         : "); KiEmitHex(pde);
    KiEmit(" -> L1 entry "); KiEmitHex(*(volatile ULONG *)pde);
    KiEmit("\n  MiGetPteAddress(VA)         : "); KiEmitHex(pteWin);
    KiEmit(" -> PTE "); KiEmitHex(*(volatile ULONG *)pteWin);
    KiEmit("\n  edited that PTE, read VA    : "); KiEmitHex(afterEdit);
    KiEmit(afterEdit == 0xDEADBEEFu ? "  OK - self-map edit took effect\n" : "  *** FAIL ***\n");
}

//
// ---------------------------------------------------------------------------
//  The data-abort -> page-fault fill path (the MmAccessFault analog).
//
//  ke/trap.S's data-abort handler tail-calls MiArmTryFillFault(DFAR) before the
//  SEH-dispatch / bug-check path; if it maps a page it returns 1 and the handler
//  retries the faulting instruction. This is the ARMv7 equivalent of MM faulting
//  in a demand-zero page: remove a free PFN, zero it, write the L2 descriptor,
//  invalidate the TLB entry. ARMv7 is hardware-walked, so writing the descriptor
//  + TLBIMVA is the whole fill (no software TLB load like MIPS KeFillEntryTb).
// ---------------------------------------------------------------------------
//

static ULONG MiArmFillBase;             // demand-fill window [base, end), 0 = disabled
static ULONG MiArmFillEnd;
static ULONG MiArmFillCount;            // pages filled on fault (for the demo report)

#if !KI_RUN_EXECUTIVE
//
// Real-model log-fill window (the hardware-page-table-as-software-TLB demo). Unlike
// MiArmFillBase above - which blindly demand-zeros any faulting page regardless of
// what maps it - this path READS the LOGICAL NT-PTE for the faulting VA (through the
// self-map, MiGetPteAddress) and fills the hardware ARMv7 L2 *from it*: a valid
// logical PTE is translated and installed (a pure "TLB miss"), and a committed
// demand-zero PTE allocates+zeroes a page and makes the logical PTE valid first.
// This is the faithful ARMv7 equivalent of the MIPS software-TLB fill (the hardware
// page table is a never-evicting, software-managed TLB over the logical NT-PTE
// table), the model REAL MM is built on. State is live only while MiArmRealModelDemo
// runs, so the fault path is unchanged for every other fault.
//
static ULONG MiArmLogFillBase;          // logical-table-driven fill window, 0 = off
static ULONG MiArmLogFillEnd;
static volatile ULONG *MiArmLogHwL2;    // the hardware L2 the MMU walks for that window
static ULONG MiArmLogFillCount;         // hardware descriptors filled from the logical table
static ULONG MiArmLogZeroCount;         // of those, demand-zero (committed) resolutions

#define MI_LOG_DEMAND_ZERO 0x00000400u  // logical-PTE marker: committed, fill on touch
                                        // (Valid = bit 1, clear here; NT proper carries
                                        //  commit in the VAD - a self-contained stand-in)

//
// Logical-PD / hardware-L1 split window (the two-level lazy fill). MiArmLogFill above
// fills the hardware L2 from the logical L2 but assumes the hardware L1 entry is
// already wired. The real model goes one level up: MM writes its page-directory
// entries (*StartPde) into a SEPARATE LOGICAL page directory (reached via
// MiGetPdeAddress), NOT the live TTBR0 L1 - and the hardware L1 entry is filled
// lazily from the logical PDE on a section-translation fault, exactly as the L2 is
// filled from the logical PTE. This decouples MM's PDE writes from the hardware L1
// (today they go straight into the live L1, which only survives because the demand-
// fault later overwrites the megabyte). Proven by MiArmPdSplitDemo.
//
static ULONG MiArmPdFillBase;           // two-level fill window, 0 = off
static ULONG MiArmPdFillEnd;
static ULONG MiArmPdLogPde;             // the LOGICAL page-directory entry (NT PDE) for the test MB
static volatile ULONG *MiArmPdHwL2;     // the hardware L2 the handler wires L1[M] -> on the L1 fill
static ULONG MiArmPdL1Fills;            // hardware L1 entries filled from the logical PD
static ULONG MiArmPdL2Fills;            // hardware L2 entries filled from the logical L2

//
// Repointed-window two-level fill (REAL MM step-A self-contained proof). Like
// MiArmPdFill above, but the logical page-directory entry is read from the FULL
// MiArmLogPd directory through the repointed PDE window (MiGetPdeAddress / MI_PDE_VA) -
// exactly as the live executive integration will - rather than from a single scalar
// (MiArmPdLogPde). Proves a PDE written through MiGetPdeAddress lands in MiArmLogPd and
// the fault handler reads it back to fill the hardware L1.
//
static ULONG MiArmLogPdFillBase;        // window, 0 = off
static ULONG MiArmLogPdFillEnd;
static volatile ULONG *MiArmLogPdHwL2;  // the hardware L2 the handler wires L1[M] -> on the L1 fill
static ULONG MiArmLogPdL1Fills;
static ULONG MiArmLogPdL2Fills;
#endif

#if KI_RUN_EXECUTIVE
//
// System-region demand fault (the MmAccessFault analog for the executive). The
// portable MM builds the system cache / paged pool / system-PTE page tables in NT
// software-PTE format (which the ARMv7 MMU cannot walk), writing them through the
// self-map window (MiGetPteAddress). So the MMU has no real mapping for those VAs;
// we map them ourselves on first access with REAL ARMv7 L2s - PRIVATE to mmuarm,
// separate from the window L2s MM writes (those stay MM's inert logical page table).
// MM's structure writes (working-set list, pool headers, ...) then land in the
// demand-filled pages. ke/initarm.c provides MiArmDemandPage = MiRemoveAnyPage.
//

extern ULONG MiArmDemandPage(VOID);     // ke/initarm.c: a free page off the real MM list

static ULONG MiArmRealL2Group[1024];    // (M>>2) -> phys page of 4 packed REAL L2s, 0=none

static ULONG MiArmExecHonored;          // system faults where the LOGICAL PTE was Valid -> honored
static ULONG MiArmExecZeroed;           // system faults demand-zeroed (no valid logical PTE yet)
static ULONG MiArmExecPdL1Fills;        // hardware L1 fills AUTHORIZED by a valid logical PDE (MM's *StartPde)
static ULONG MiArmExecPdL1Demand;       // hardware L1 fills with no logical PDE yet (demand region; step-B residual)

//
// KSEG0 VA of megabyte M's private real L2, allocating its packed page (arena) and
// wiring L1[M] -> it (an ARMv7 coarse-page-table descriptor the MMU walks) on first use.
//

static ULONG
MiArmRealL2Va (
    ULONG M
    )
{
    ULONG group = M >> 2;
    ULONG phys = MiArmRealL2Group[group];
    volatile ULONG *l2;

    if (phys == 0) {
        ULONG pfn = MiArmAllocKseg0Page();
        if (pfn == 0)
            return 0;
        phys = pfn << PAGE_SHIFT;
        MiArmRealL2Group[group] = phys;
        MiArmZeroPages((PVOID)(KSEG0 + phys), PAGE_SIZE);
    }
    l2 = (volatile ULONG *)(KSEG0 + phys + ((M & 3) << 10));
    if ((MiArmL1[M] & 0x3) != 0x1)
        MiArmL1[M] = (((ULONG)l2 - KSEG0) & 0xFFFFFC00) | 0x1;
    return (ULONG)l2;
}
#endif

#if !KI_RUN_EXECUTIVE
//
// The real fill: translate the LOGICAL NT-PTE that maps FaultVa into a hardware
// ARMv7 descriptor and install it (the MmAccessFault analog, logical-table-driven).
// The logical L2 is pre-exposed in the self-map window, so reading MiGetPteAddress
// (FaultVa) here is a backed Normal-memory access and cannot nest-fault (the trap.S
// no-nesting invariant); the real MmAccessFault likewise requires the page-table
// page to be resident, faulting it in first if not.
//
static ULONG
MiArmLogFill (
    ULONG FaultVa
    )
{
    volatile ULONG *logPtePtr = (volatile ULONG *)MI_PTE_VA(FaultVa);
    ULONG idx = (FaultVa >> 12) & 0xFF;
    HARDWARE_PTE pte;
    ULONG pfn;

    *(ULONG *)&pte = *logPtePtr;                 // the logical NT-PTE, read via the self-map

    if (pte.Valid) {                             // mapping known - a pure "TLB miss": translate + install
        MiArmLogHwL2[idx] = MiArmPteToDescriptor(pte);
        MiArmTlbiMva(FaultVa);
        MiArmLogFillCount += 1;
        return 1;
    }

    if (*(ULONG *)&pte == MI_LOG_DEMAND_ZERO) {  // committed: allocate, zero, make the logical PTE valid
        pfn = MiArmAllocKseg0Page();
        if (pfn == 0)
            return 0;
        MiArmZeroPages((PVOID)(KSEG0 + (pfn << PAGE_SHIFT)), PAGE_SIZE);
        *(ULONG *)&pte = 0;
        pte.Global = 1; pte.Valid = 1; pte.Write = 1;
        pte.CachePolicy = 1; pte.PageFrameNumber = pfn;
        *logPtePtr = *(ULONG *)&pte;             // write the now-valid logical PTE back (via the self-map)
        MiArmLogHwL2[idx] = MiArmPteToDescriptor(pte);
        MiArmTlbiMva(FaultVa);
        MiArmLogFillCount += 1;
        MiArmLogZeroCount += 1;
        return 1;
    }

    return 0;                                    // not committed -> genuine fault
}

//
// The two-level lazy fill: resolve a fault by filling BOTH the hardware L1 (from the
// LOGICAL page directory) and the hardware L2 (from the LOGICAL L2), so MM's PDE/PTE
// writes drive the MMU-walked tables without MM ever touching the hardware L1. A
// section-translation fault (L1[M] absent) consults the logical PDE: if it says a
// page table exists, install the coarse L1 descriptor -> the hardware L2; then fill
// the L2 entry from the logical PTE. This is the L1-level analog of MiArmLogFill, and
// the mechanism the executive integration will use after MiGetPdeAddress is repointed
// at a real logical page directory.
//
static ULONG
MiArmPdFill (
    ULONG FaultVa
    )
{
    ULONG M = FaultVa >> 20;
    ULONG idx = (FaultVa >> 12) & 0xFF;
    HARDWARE_PTE pde, pte;

    if ((MiArmL1[M] & 0x3) == 0) {               // L1 fill: hardware L1[M] absent
        *(ULONG *)&pde = MiArmPdLogPde;          // the NT PDE MM wrote (separate from the hw L1)
        if (!pde.Valid)
            return 0;                            // no page table here -> genuine fault
        MiArmL1[M] =                             // install the coarse descriptor -> hardware L2
            (((ULONG)MiArmPdHwL2 - KSEG0) & 0xFFFFFC00) | 0x1;
        MiArmTlbiMva(FaultVa);
        MiArmPdL1Fills += 1;
    }

    if ((MiArmPdHwL2[idx] & 0x3) == 0) {         // L2 fill: hardware L2 entry absent
        *(ULONG *)&pte = *(volatile ULONG *)MI_PTE_VA(FaultVa);   // logical PTE via the self-map
        if (!pte.Valid)
            return 0;                            // not mapped -> genuine fault
        MiArmPdHwL2[idx] = MiArmPteToDescriptor(pte);
        MiArmTlbiMva(FaultVa);
        MiArmPdL2Fills += 1;
    }

    return 1;                                    // retry: both levels now present
}

//
// The two-level fill reading the LOGICAL PDE from the full directory via the repointed
// window. MiArmLogPd[M] is read DIRECTLY (KSEG0-resident static -> always mapped, cannot
// nest-fault, the trap.S no-nesting invariant). The logical PTE is read via the PTE
// window (MI_PTE_VA), which the demo backs first (MiArmL2Va) so that read is also
// non-faulting. This is MiArmPdFill generalized from a scalar PDE to the real directory.
//
static ULONG
MiArmLogPdFill (
    ULONG FaultVa
    )
{
    ULONG M = FaultVa >> 20;
    ULONG idx = (FaultVa >> 12) & 0xFF;
    HARDWARE_PTE pde, pte;

    if ((MiArmL1[M] & 0x3) == 0) {               // hardware L1[M] absent
        *(ULONG *)&pde = MiArmLogPd[M];          // logical PDE (KSEG0 static - safe, no nest)
        if (!pde.Valid)
            return 0;                            // no page table here -> genuine fault
        MiArmL1[M] =                             // install coarse descriptor -> hardware L2
            (((ULONG)MiArmLogPdHwL2 - KSEG0) & 0xFFFFFC00) | 0x1;
        MiArmTlbiMva(FaultVa);
        MiArmLogPdL1Fills += 1;
    }

    if ((MiArmLogPdHwL2[idx] & 0x3) == 0) {      // hardware L2 entry absent
        *(ULONG *)&pte = *(volatile ULONG *)MI_PTE_VA(FaultVa);   // logical PTE via the window
        if (!pte.Valid)
            return 0;
        MiArmLogPdHwL2[idx] = MiArmPteToDescriptor(pte);
        MiArmTlbiMva(FaultVa);
        MiArmLogPdL2Fills += 1;
    }

    return 1;
}
#endif

ULONG
MiArmTryFillFault (
    ULONG FaultVa
    )
{
    volatile ULONG *l2;
    ULONG pfn;

#if KI_RUN_EXECUTIVE
    //
    // On-demand PTE-window backing. The genuine MM (system-cache build at
    // MMINIT.C:539, MiBuildPagedPool, MiInitializeSystemPtes) writes/zeros PTEs
    // across the system region through the self-map window MiGetPteAddress(va);
    // a window megabyte whose L2-of-L2 entry is not yet materialized faults here.
    // Back it from the arena and retry - lazily growing the window instead of
    // pre-backing the whole 512 MB+ system region. FaultVa is the window VA, so
    // the mapped source VA is FaultVa<<10 (MiGetVirtualAddressMappedByPte).
    //
    if (FaultVa >= PTE_BASE && FaultVa < PTE_BASE + 0x400000u) {
        MiArmEnsureL2(FaultVa << 10);
        return 1;                                   // retry the faulting store
    }

    //
    // System region (cache / hyperspace / paged pool / system PTEs / shared data):
    // demand-fill a real page through a private real L2. MM_SYSTEM_SPACE_START .. the
    // top page (includes the HAL region + the fixed system-time / shared-data page
    // KeQuerySystemTime reads). Excludes the self-map windows (< 0xC0800000) above.
    //
    if (FaultVa >= 0xC0800000u && FaultVa < 0xFFFFF000u) {
        ULONG M = FaultVa >> 20;
        ULONG idx = (FaultVa >> 12) & 0xFF;
        ULONG l1WasAbsent = ((MiArmL1[M] & 0x3) != 0x1);
        volatile ULONG *l2;
        HARDWARE_PTE lpde, lpte;

        l2 = (volatile ULONG *)MiArmRealL2Va(M);    // alloc the private hardware L2 + wire L1[M]
        if (l2 == 0)
            return 0;                               // arena exhausted -> real fault

        //
        // L1 level (REAL MM step A): the hardware L1 fill is DRIVEN by the LOGICAL page
        // directory. MiArmLogPd[M] is read DIRECTLY (KSEG0 static -> always mapped, cannot
        // nest-fault, the trap.S no-nesting invariant). A valid logical PDE means MM's
        // *StartPde declared a page table here, so this L1 fill is AUTHORIZED by MM's own
        // directory (the un-fake of the blind L1 wire). No logical PDE yet = a demand region
        // MM has not built a PDE for (kernel stacks, ...) - still mapped here for Phase 0;
        // the real MmAccessFault (step B) removes that residual. The hardware L1 points at
        // our PRIVATE ARMv7 L2 (the MMU cannot walk MM's NT-format logical L2): two page
        // tables are inherent on a hardware-walked MMU running NT software PTEs.
        //
        if (l1WasAbsent) {
            *(ULONG *)&lpde = MiArmLogPd[M];
            if (lpde.Valid)
                MiArmExecPdL1Fills += 1;
            else
                MiArmExecPdL1Demand += 1;
        }

        if ((l2[idx] & 0x3) == 0) {
            //
            // L2 level: consult the LOGICAL NT-PTE (the faithful, logical-table-driven
            // fill). Reading MiGetPteAddress(FaultVa) is only safe once its window L2 is
            // backed, so first check the L2-of-L2 entry (KSEG0, cannot fault) to avoid
            // nesting a window fault - the trap.S no-nesting invariant (do NOT remove this
            // guard). A VALID logical PTE means MM mapped a specific page here: install
            // THAT page. Otherwise demand-zero (committed but not yet backed - cache /
            // paged pool / kernel-stack growth). Step B (MmAccessFault) additionally
            // resolves transition / prototype / paging-file PTEs rather than blind zero.
            //
            *(ULONG *)&lpte = 0;
            if (MiArmWindowL2[M >> 10][(M >> 2) & 0xFF] != 0)
                *(ULONG *)&lpte = *(volatile ULONG *)MI_PTE_VA(FaultVa);

            if (lpte.Valid) {
                l2[idx] = MiArmPteToDescriptor(lpte);       // honor the page the logical PTE named
                MiArmTlbiMva(FaultVa);
                MiArmExecHonored += 1;
            } else {
                pfn = MiArmDemandPage();                     // committed demand-zero
                if (pfn == 0)
                    return 0;
                l2[idx] = ((pfn << PAGE_SHIFT) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
                MiArmTlbiMva(FaultVa);
                MiArmZeroPages((PVOID)(FaultVa & ~0xFFFu), PAGE_SIZE);   // demand-zero via the mapping
                MiArmExecZeroed += 1;
            }
        }
        MiArmFillCount += 1;
        return 1;                                    // retry the faulting access
    }
#endif

#if !KI_RUN_EXECUTIVE
    //
    // Real-model log-fill window: drive the fill from the logical NT-PTE table
    // (the faithful model) rather than the blind demand-zero below.
    //
    if (MiArmLogFillBase != 0 &&
        FaultVa >= MiArmLogFillBase && FaultVa < MiArmLogFillEnd)
        return MiArmLogFill(FaultVa);

    //
    // Two-level fill window: fill the hardware L1 (from the logical PD) AND the
    // hardware L2 (from the logical L2) - the logical-PD / hardware-L1 split.
    //
    if (MiArmPdFillBase != 0 &&
        FaultVa >= MiArmPdFillBase && FaultVa < MiArmPdFillEnd)
        return MiArmPdFill(FaultVa);

    //
    // Repointed-window two-level fill (REAL MM step-A demo): the logical PDE is read
    // from the full MiArmLogPd directory via MiGetPdeAddress.
    //
    if (MiArmLogPdFillBase != 0 &&
        FaultVa >= MiArmLogPdFillBase && FaultVa < MiArmLogPdFillEnd)
        return MiArmLogPdFill(FaultVa);
#endif

    if (MiArmFillBase == 0 || FaultVa < MiArmFillBase || FaultVa >= MiArmFillEnd)
        return 0;                                   // not ours - let the trap path run

    pfn = MiArmAllocKseg0Page();
    if (pfn == 0)
        return 0;                                   // out of memory -> genuine fault

    MiArmZeroPages((PVOID)(KSEG0 + (pfn << PAGE_SHIFT)), PAGE_SIZE);   // demand-zero
    l2 = (volatile ULONG *)MiArmL2Va(FaultVa >> 20);                   // L2 pre-built in setup
    l2[(FaultVa >> 12) & 0xFF] =
        ((pfn << PAGE_SHIFT) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
    MiArmTlbiMva(FaultVa);
    MiArmFillCount += 1;
    return 1;                                        // retry the faulting instruction
}

static VOID
MiArmFaultFillDemo (
    VOID
    )
{
    ULONG testVa = 0xE0000000;                      // a fresh, all-fault megabyte
    volatile ULONG *l2;
    ULONG before, v1, v2, v3;

    KiEmit("\nARMv7 demand-fill fault path (the MmAccessFault analog):\n");

    l2 = (volatile ULONG *)MiArmL2Va(testVa >> 20);          // all-fault L2 (zeroed)
    MiArmL1[testVa >> 20] = (((ULONG)l2 - KSEG0) & 0xFFFFFC00) | 0x1;
    MiArmTlbiAll();

    MiArmFillBase = testVa;
    MiArmFillEnd = testVa + 0x100000;
    MiArmFillCount = 0;
    before = MiArmFreeList.Total;

    v1 = *(volatile ULONG *)testVa;                 // page 0: data abort -> fill -> retry -> read
    *(volatile ULONG *)testVa = 0x5AA55AA5u;
    v2 = *(volatile ULONG *)testVa;
    *(volatile ULONG *)(testVa + 0x4000) = 0x1234u; // page 4: a second fault-fill
    v3 = *(volatile ULONG *)(testVa + 0x4000);

    KiEmit("  fill window                 : "); KiEmitHex(MiArmFillBase);
    KiEmit(" .. "); KiEmitHex(MiArmFillEnd);
    KiEmit("\n  read page 0 (faulted in)    : "); KiEmitHex(v1);
    KiEmit(v1 == 0 ? "  OK - demand-zero page\n" : "  *** not zero ***\n");
    KiEmit("  write + read back           : "); KiEmitHex(v2);
    KiEmit(v2 == 0x5AA55AA5u ? "  OK\n" : "  *** FAIL ***\n");
    KiEmit("  page 4 write + read back    : "); KiEmitHex(v3);
    KiEmit(v3 == 0x1234u ? "  OK\n" : "  *** FAIL ***\n");
    KiEmit("  pages filled on fault       : "); KiEmitHex(MiArmFillCount);
    KiEmit("\n  free pages consumed         : "); KiEmitHex(before - MiArmFreeList.Total);
    KiEmit("\n");

    MiArmFillBase = 0;                              // close the demand-fill window
}

//
// Prove the REAL MM page-table model: a hardware ARMv7 page table the MMU walks,
// filled lazily and FAITHFULLY from a LOGICAL NT-PTE page table (HARDWARE_PTE words,
// the form MM reads/writes through the self-map). This is the un-faked successor to
// MiArmFaultFillDemo above: that one blindly demand-zeros any faulting page; this one
// consults the logical PTE and installs exactly the page it names - a valid PTE maps
// its page (a pure TLB miss), a committed demand-zero PTE resolves to a fresh zeroed
// page. A flush + logical-PTE change re-fills to the new page, proving the hardware
// table is a coherent software-managed "TLB" over the logical table (the ARMv7 analog
// of the MIPS TLB-miss fill). This is the mechanism the real MmAccessFault rides on.
//

#if !KI_RUN_EXECUTIVE
static VOID
MiArmRealModelDemo (
    VOID
    )
{
    ULONG testVa = 0xB0000000;                  // a currently-unmapped megabyte
    ULONG M = testVa >> 20;
    volatile ULONG *win;                        // &logical PTE of page 0, in the self-map window
    volatile ULONG *logL2;
    ULONG hwPhys, pfnA, pfnB;
    ULONG vValid, vZero, vWrite, vRemap;
    HARDWARE_PTE pte;

    KiEmit("\nARMv7 REAL MM model - hardware page table filled from the logical NT-PTE table:\n");

    //
    // Logical L2 (HARDWARE_PTE words), exposed in the PTE window so MiGetPteAddress
    // reaches it; the hardware L2 (ARMv7 descriptors) is a separate page the MMU
    // walks via the real L1. Both start all-fault.
    //
    logL2 = (volatile ULONG *)MiArmL2Va(M);
    MiArmZeroPages((PVOID)logL2, PAGE_SIZE);

    hwPhys = MiArmAllocKseg0Page() << PAGE_SHIFT;
    MiArmLogHwL2 = (volatile ULONG *)(KSEG0 + hwPhys);
    MiArmZeroPages((PVOID)MiArmLogHwL2, PAGE_SIZE);
    MiArmL1[M] = (hwPhys & 0xFFFFFC00) | 0x1;   // real L1 the MMU walks -> hardware L2
    MiArmTlbiAll();

    //
    // Two backing pages off the free list, each tagged so the read proves WHICH page
    // the fill chose. Set the logical PTEs THROUGH the self-map window, exactly as MM
    // does: page 0 valid -> page A, page 1 a committed demand-zero.
    //
    pfnA = MiArmAllocKseg0Page();
    pfnB = MiArmAllocKseg0Page();
    *(volatile ULONG *)(KSEG0 + (pfnA << PAGE_SHIFT)) = 0x0A11600Du;   // page A sentinel
    *(volatile ULONG *)(KSEG0 + (pfnB << PAGE_SHIFT)) = 0x0B0B0B0Bu;   // page B sentinel

    win = (volatile ULONG *)MI_PTE_VA(testVa);
    *(ULONG *)&pte = 0;
    pte.Global = 1; pte.Valid = 1; pte.Write = 1; pte.CachePolicy = 1;
    pte.PageFrameNumber = pfnA;
    win[0] = *(ULONG *)&pte;                     // page 0: VALID -> page A
    win[1] = MI_LOG_DEMAND_ZERO;                 // page 1: committed demand-zero

    //
    // Touch the pages. Each access data-aborts (hardware L2 all-fault), trap.S calls
    // MiArmTryFillFault -> MiArmLogFill, which reads the logical PTE and fills the
    // hardware descriptor from it, then the instruction retries.
    //
    MiArmLogFillBase = testVa;
    MiArmLogFillEnd = testVa + 0x100000;
    MiArmLogFillCount = 0;
    MiArmLogZeroCount = 0;

    vValid = *(volatile ULONG *)(testVa + 0x0000);   // -> fill from the VALID logical PTE -> page A
    vZero  = *(volatile ULONG *)(testVa + 0x1000);   // -> demand-zero -> 0
    *(volatile ULONG *)(testVa + 0x1000) = 0x600D600Du;
    vWrite = *(volatile ULONG *)(testVa + 0x1000);   // write/readback on the zeroed page

    //
    // Coherence: drop page 0's hardware descriptor (KeFlushSingleTb = clear + TLBIMVA),
    // repoint the LOGICAL PTE at page B, touch again -> the fill tracks the new PTE.
    //
    MiArmLogHwL2[0] = 0;
    MiArmTlbiMva(testVa);
    *(ULONG *)&pte = 0;
    pte.Global = 1; pte.Valid = 1; pte.Write = 1; pte.CachePolicy = 1;
    pte.PageFrameNumber = pfnB;
    win[0] = *(ULONG *)&pte;
    vRemap = *(volatile ULONG *)(testVa + 0x0000);   // -> refill from the CHANGED logical PTE -> page B

    KiEmit("  logical L2 (NT PTEs) @ MiGetPteAddress   : "); KiEmitHex((ULONG)win);
    KiEmit("\n  hardware L2 (ARMv7) the MMU walks        : "); KiEmitHex((ULONG)MiArmLogHwL2);
    KiEmit("\n  page0 read (filled from the VALID PTE)   : "); KiEmitHex(vValid);
    KiEmit(vValid == 0x0A11600Du ? "  OK - installed the page the logical PTE named\n"
                                 : "  *** FAIL (a blind demand-zero fill would read 0) ***\n");
    KiEmit("  page1 read (committed demand-zero)       : "); KiEmitHex(vZero);
    KiEmit(vZero == 0 ? "  OK - demand-zero\n" : "  *** FAIL ***\n");
    KiEmit("  page1 write + read back                  : "); KiEmitHex(vWrite);
    KiEmit(vWrite == 0x600D600Du ? "  OK\n" : "  *** FAIL ***\n");
    KiEmit("  page0 reread after flush + PTE change    : "); KiEmitHex(vRemap);
    KiEmit(vRemap == 0x0B0B0B0Bu ? "  OK - hardware table tracked the logical table\n"
                                 : "  *** FAIL ***\n");
    KiEmit("  hw descriptors filled / demand-zero      : ");
    KiEmitHex(MiArmLogFillCount); KiEmit(" / "); KiEmitHex(MiArmLogZeroCount);
    KiEmit("\n");

    MiArmLogFillBase = 0;                        // close the window
}

//
// Prove the LOGICAL-PD / HARDWARE-L1 SPLIT: MM's page-directory entry lives in a
// SEPARATE logical page directory, and the hardware L1 (the live TTBR0 directory the
// MMU walks) is filled lazily FROM it - so MM's *StartPde writes never touch the
// hardware L1 directly (today they do, surviving only because the demand-fault later
// rewrites the megabyte). This is the L1-level analog of MiArmRealModelDemo and the
// last structural prerequisite for real per-process address spaces (each process gets
// its own logical PD + hardware L1/TTBR0). Touch an unmapped megabyte whose hardware
// L1 entry is ABSENT but whose logical PDE is valid -> the two-level fill installs the
// L1 coarse descriptor (from the logical PD) then the L2 entry (from the logical PTE).
//

static VOID
MiArmPdSplitDemo (
    VOID
    )
{
    ULONG testVa = 0xB1000000;                  // a fresh unmapped megabyte (past KSEG0)
    ULONG M = testVa >> 20;
    volatile ULONG *logL2, *win;
    ULONG hwPhys, pfnA, l1Before, l1After, vRead;
    HARDWARE_PTE pte;

    KiEmit("\nARMv7 logical-PD / hardware-L1 split (two-level lazy fill):\n");

    //
    // Logical L2 (windowed) + a hardware L2 page that is NOT yet wired into the
    // hardware L1 - the L1 entry is filled by the fault handler, not here.
    //
    logL2 = (volatile ULONG *)MiArmL2Va(M);
    MiArmZeroPages((PVOID)logL2, PAGE_SIZE);

    hwPhys = MiArmAllocKseg0Page() << PAGE_SHIFT;
    MiArmPdHwL2 = (volatile ULONG *)(KSEG0 + hwPhys);
    MiArmZeroPages((PVOID)MiArmPdHwL2, PAGE_SIZE);

    pfnA = MiArmAllocKseg0Page();
    *(volatile ULONG *)(KSEG0 + (pfnA << PAGE_SHIFT)) = 0x0DDF00D5u;   // page A sentinel

    //
    // MM-style writes: a valid logical PTE (page 0 -> page A) THROUGH the self-map,
    // and a valid logical PDE into the SEPARATE logical page directory (its PFN names
    // the logical L2 page, as *StartPde would) - NOT the hardware L1.
    //
    win = (volatile ULONG *)MI_PTE_VA(testVa);
    *(ULONG *)&pte = 0;
    pte.Global = 1; pte.Valid = 1; pte.Write = 1; pte.CachePolicy = 1;
    pte.PageFrameNumber = pfnA;
    win[0] = *(ULONG *)&pte;

    *(ULONG *)&pte = 0;
    pte.Global = 1; pte.Valid = 1; pte.Write = 1; pte.CachePolicy = 1;
    pte.PageFrameNumber = (((ULONG)logL2 - KSEG0) >> PAGE_SHIFT);      // the page-table page
    MiArmPdLogPde = *(ULONG *)&pte;

    l1Before = *(volatile ULONG *)&MiArmL1[M];   // hardware L1[M] - still ABSENT (decoupled); volatile:
                                                 // the fault handler writes it via a fault the compiler
                                                 // cannot see, so the re-read must not be optimized away
    MiArmPdFillBase = testVa;
    MiArmPdFillEnd = testVa + 0x100000;
    MiArmPdL1Fills = 0;
    MiArmPdL2Fills = 0;

    vRead = *(volatile ULONG *)testVa;           // fault -> L1 fill (from logical PD) + L2 fill -> page A
    l1After = *(volatile ULONG *)&MiArmL1[M];

    KiEmit("  logical PDE (MM wrote, separate PD)      : "); KiEmitHex(MiArmPdLogPde);
    KiEmit("\n  hardware L1[M] BEFORE touch              : "); KiEmitHex(l1Before);
    KiEmit(l1Before == 0 ? "  (absent - decoupled from the logical PDE)\n"
                         : "  *** not absent ***\n");
    KiEmit("  hardware L1[M] AFTER touch (filled)      : "); KiEmitHex(l1After);
    KiEmit((l1After & 0x3) == 0x1 ? "  OK - coarse -> hw L2, filled by the fault handler\n"
                                  : "  *** FAIL ***\n");
    KiEmit("  page read (L1 fill + L2 fill)            : "); KiEmitHex(vRead);
    KiEmit(vRead == 0x0DDF00D5u ? "  OK - two-level fill from the logical PD + L2\n"
                                : "  *** FAIL ***\n");
    KiEmit("  L1 fills / L2 fills                      : "); KiEmitHex(MiArmPdL1Fills);
    KiEmit(" / "); KiEmitHex(MiArmPdL2Fills); KiEmit("\n");

    MiArmPdFillBase = 0;                          // close the window
}

//
// Prove the REPOINTED-WINDOW logical page directory (the executive-integration
// mechanism, step A): point the PDE self-map window at the full MiArmLogPd directory,
// write a page-directory entry THROUGH MiGetPdeAddress (as MM's *StartPde does), confirm
// it landed in MiArmLogPd, then touch the megabyte -> the two-level fill reads that PDE
// back via the window, fills the hardware L1 from it and the hardware L2 from the logical
// PTE -> the named page. MiArmPdSplitDemo generalized from a scalar PDE to the real
// directory read through MiGetPdeAddress - the last piece the live repoint needs proven.
//

static VOID
MiArmLogPdRepointDemo (
    VOID
    )
{
    ULONG testVa = 0xB3000000;                  // fresh unmapped megabyte (past KSEG0)
    ULONG M = testVa >> 20;
    volatile ULONG *logL2, *win;
    ULONG hwPhys, pfnA, savedWin[4], logPdPhys, j, pdeInPd, vRead, l1After;
    HARDWARE_PTE pte;

    KiEmit("\nARMv7 REAL-MM step A: logical page directory via the repointed PDE window:\n");

    MiArmZeroPages((PVOID)MiArmLogPd, sizeof(MiArmLogPd));

    //
    // Repoint MiArmPdeWinL2[0..3] (the PDE window) from MiArmL1 to MiArmLogPd, so
    // MiGetPdeAddress reads/writes the logical directory. Save the originals to restore
    // at the end (this is a demo; the live executive path repoints permanently).
    //
    logPdPhys = (ULONG)MiArmLogPd - KSEG0;
    for (j = 0; j < 4; j += 1) {
        savedWin[j] = MiArmPdeWinL2[j];
        MiArmPdeWinL2[j] = ((logPdPhys + (j << PAGE_SHIFT)) & 0xFFFFF000) | SMALL_PAGE_NORMAL_RW;
    }
    MiArmTlbiAll();

    //
    // Logical L2 (windowed) + a hardware L2 page the fault handler wires L1[M] -> on the
    // L1 fill (it is NOT wired here - the handler installs it from the logical PD).
    //
    logL2 = (volatile ULONG *)MiArmL2Va(M);
    MiArmZeroPages((PVOID)logL2, PAGE_SIZE);

    hwPhys = MiArmAllocKseg0Page() << PAGE_SHIFT;
    MiArmLogPdHwL2 = (volatile ULONG *)(KSEG0 + hwPhys);
    MiArmZeroPages((PVOID)MiArmLogPdHwL2, PAGE_SIZE);

    pfnA = MiArmAllocKseg0Page();
    *(volatile ULONG *)(KSEG0 + (pfnA << PAGE_SHIFT)) = 0x1093D00Du;   // page A sentinel

    //
    // MM-style writes through the self-map: a valid logical PTE (page 0 -> page A) and a
    // valid logical PDE written THROUGH MiGetPdeAddress (the repointed window) naming the
    // logical L2 page - exactly as *StartPde does.
    //
    win = (volatile ULONG *)MI_PTE_VA(testVa);
    *(ULONG *)&pte = 0;
    pte.Global = 1; pte.Valid = 1; pte.Write = 1; pte.CachePolicy = 1;
    pte.PageFrameNumber = pfnA;
    win[0] = *(ULONG *)&pte;

    *(ULONG *)&pte = 0;
    pte.Global = 1; pte.Valid = 1; pte.Write = 1; pte.CachePolicy = 1;
    pte.PageFrameNumber = (((ULONG)logL2 - KSEG0) >> PAGE_SHIFT);
    *(volatile ULONG *)MI_PDE_VA(testVa) = *(ULONG *)&pte;   // *StartPde via the repointed window

    pdeInPd = MiArmLogPd[M];                     // read it back from the directory itself

    MiArmLogPdFillBase = testVa;
    MiArmLogPdFillEnd = testVa + 0x100000;
    MiArmLogPdL1Fills = 0;
    MiArmLogPdL2Fills = 0;

    vRead = *(volatile ULONG *)testVa;          // fault -> L1 fill (from MiArmLogPd) + L2 fill -> page A
    l1After = *(volatile ULONG *)&MiArmL1[M];

    KiEmit("  wrote *MiGetPdeAddress(VA); MiArmLogPd[M] : "); KiEmitHex(pdeInPd);
    KiEmit((pdeInPd & 0x2) ? "  OK - the window write reached the logical PD\n"
                           : "  *** FAIL - PDE did not land in MiArmLogPd ***\n");
    KiEmit("  hardware L1[M] after touch (filled)       : "); KiEmitHex(l1After);
    KiEmit((l1After & 0x3) == 0x1 ? "  OK - coarse -> hw L2, filled from MiArmLogPd\n"
                                  : "  *** FAIL ***\n");
    KiEmit("  page read (two-level fill via the window) : "); KiEmitHex(vRead);
    KiEmit(vRead == 0x1093D00Du ? "  OK - logical PD (read via MiGetPdeAddress) drove the fill\n"
                                : "  *** FAIL ***\n");
    KiEmit("  L1 fills / L2 fills                       : "); KiEmitHex(MiArmLogPdL1Fills);
    KiEmit(" / "); KiEmitHex(MiArmLogPdL2Fills); KiEmit("\n");

    MiArmLogPdFillBase = 0;                      // close the window

    //
    // Restore the PDE window to MiArmL1 and drop the test L1 mapping (demo hygiene - the
    // live executive path leaves the window repointed at MiArmLogPd permanently).
    //
    for (j = 0; j < 4; j += 1)
        MiArmPdeWinL2[j] = savedWin[j];
    MiArmL1[M] = 0;
    MiArmTlbiAll();
}
#endif

//
// Step 1: walk the loader's memory-descriptor list, accumulate the physical
// page counts, and pick the largest free run (the INITMIPS.C opening). Read
// only - this is where we learn the real RPi2/QEMU + peldr layout before
// carving anything (the kernel image is loaded inside this map).
//

VOID
MiArmInitMachineDependent (
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    PLIST_ENTRY next, head;
    PMEMORY_ALLOCATION_DESCRIPTOR md = NULL;
    PMEMORY_ALLOCATION_DESCRIPTOR freeDesc = NULL;
    ULONG mostFree = 0;

    KiEmit("\nMiInitMachineDependent (ARMv7) - MM init body:\n");
    KiEmit("  loader memory-descriptor list:\n");

    head = &LoaderBlock->MemoryDescriptorListHead;
    for (next = head->Flink; next != head; next = md->ListEntry.Flink) {
        md = CONTAINING_RECORD(next, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

        KiEmit("    ");
        KiEmit(MiArmMemTypeName((ULONG)md->MemoryType));
        KiEmit(" base="); KiEmitHex(md->BasePage);
        KiEmit(" count="); KiEmitHex(md->PageCount);
        KiEmit(" end="); KiEmitHex(md->BasePage + md->PageCount);
        KiEmit("\n");

        MiArmPhysPages += md->PageCount;
        if (md->BasePage < MiArmLowestPage)
            MiArmLowestPage = md->BasePage;
        if (md->BasePage + md->PageCount > MiArmHighestPage)
            MiArmHighestPage = md->BasePage + md->PageCount - 1;
        if (md->MemoryType == LoaderFree && md->PageCount > mostFree) {
            mostFree = md->PageCount;
            freeDesc = md;
        }
    }

    KiEmit("  total physical pages : "); KiEmitHex(MiArmPhysPages);
    KiEmit("\n  lowest physical page : "); KiEmitHex(MiArmLowestPage);
    KiEmit("\n  highest physical page: "); KiEmitHex(MiArmHighestPage);
    KiEmit("\n  largest free run     : ");
    if (freeDesc) {
        KiEmit("base="); KiEmitHex(freeDesc->BasePage);
        KiEmit(" count="); KiEmitHex(freeDesc->PageCount);
        KiEmit(" (KSEG0 VA "); KiEmitHex(KSEG0_BASE + (freeDesc->BasePage << PAGE_SHIFT));
        KiEmit(")");
    } else {
        KiEmit("(none)");
    }
    KiEmit("\n");

    if (freeDesc == NULL || MiArmPhysPages < 1024) {
        KiEmit("  *** too little physical memory - INSTALL_MORE_MEMORY ***\n");
        return;
    }

    //
    // Step 2: build the PFN database in the KSEG0 direct map (the INITMIPS.C
    // PfnInKseg0 path). Carve it from the LOW end of the largest free run -
    // which the layout above proves sits above the kernel/HAL/registry/NLS -
    // so its KSEG0 VA (0x80000000 + (page<<12)) is inside the 512 MB KSEG0
    // window and clobbers nothing in use. (INITMIPS carves from the high end;
    // the low end is what keeps ARM's 512 MB KSEG0 reachable when RAM is
    // 896 MB > 512 MB and the run's top is above the window.)
    //

    {
        ULONG pfnBytes = (MiArmHighestPage + 1) * sizeof(ARM_MMPFN);
        ULONG PfnAllocation = (pfnBytes + PAGE_SIZE - 1) >> PAGE_SHIFT;
        ULONG carveStart = freeDesc->BasePage;
        ULONG carveEnd = carveStart + PfnAllocation;
        ULONG p, e, a, b, c;

        MiArmPfnDb = (PARM_MMPFN)(KSEG0_BASE + (carveStart << PAGE_SHIFT));
        MiArmZeroPages(MiArmPfnDb, PfnAllocation << PAGE_SHIFT);

#if KI_RUN_EXECUTIVE
        //
        // Reserve the executive's initial nonpaged pool contiguously, immediately
        // above the PFN database (still inside the free run, still inside the
        // 512 MB KSEG0 window). Extending carveEnd folds these pages into the
        // "keep active, off the free list" range below, exactly like the PFN DB;
        // ke/initarm.c reads MiArmPoolBasePage/Pages and maps the pool at its
        // KSEG0 alias (KSEG0_BASE + BasePage*PAGE_SIZE), a physical address so
        // MiAllocatePoolPages derives the PFN from the address (no PTE read).
        //
        MiArmPoolBasePage = carveEnd;
        MiArmPoolPages = MI_ARM_INITIAL_POOL_PAGES;
        carveEnd += MiArmPoolPages;

        //
        // The L2/window arena (kept active, off the real free list - just like the
        // PFN DB + pool). MiArmAllocKseg0Page bump-allocates from it.
        //
        MiArmL2ArenaBase = carveEnd;
        MiArmL2ArenaPages = MI_ARM_L2_ARENA_PAGES;
        MiArmL2ArenaNext = MiArmL2ArenaBase;
        carveEnd += MiArmL2ArenaPages;
#endif

        //
        // Step 3: walk the descriptors again and populate the PFN database -
        // free physical pages onto the free list, bad pages onto the bad list,
        // everything else marked active (the INITMIPS.C MemoryType switch). The
        // carved PFN-database pages and page zero stay active (never handed out).
        //

        for (next = head->Flink; next != head; next = md->ListEntry.Flink) {
            md = CONTAINING_RECORD(next, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
            p = md->BasePage;
            e = md->BasePage + md->PageCount;

            for (; p < e; p += 1) {
                PARM_MMPFN pfn = MI_PFN(p);

                if ((p >= carveStart && p < carveEnd) || p == 0) {
                    pfn->ReferenceCount = 1;            // PFN DB / page zero: keep
                    MiArmSetLocation(pfn, ActiveAndValid);
                    continue;
                }

                switch (md->MemoryType) {
                case LoaderBad:
                case LoaderFree:
                case LoaderLoadedProgram:
                case LoaderFirmwareTemporary:
                case LoaderOsloaderStack:
                    //
                    // KI_RUN_EXECUTIVE: leave free/bad pages ReferenceCount==0 so
                    // ke/initarm.c threads them onto the REAL MM lists (MiInsertPageInList).
                    // Otherwise thread our self-contained lists for the bring-up demos.
                    //
#if !KI_RUN_EXECUTIVE
                    MiArmInsertPageInList(
                        (md->MemoryType == LoaderBad) ? &MiArmBadList : &MiArmFreeList, p);
#endif
                    break;
                default:
                    pfn->ReferenceCount = 1;            // in use: kernel/HAL/stack/...
                    MiArmSetLocation(pfn, ActiveAndValid);
                    break;
                }
            }
        }

        KiEmit("  PFN database VA      : ");
        KiEmitHex((ULONG)MiArmPfnDb);
        KiEmit(" .. ");
        KiEmitHex((ULONG)MiArmPfnDb + pfnBytes);
        KiEmit("\n  PFN entry size       : ");
        KiEmitHex(sizeof(ARM_MMPFN));
        KiEmit("\n  PFN pages (in KSEG0) : ");
        KiEmitHex(PfnAllocation);
#if KI_RUN_EXECUTIVE
        //
        // KI_RUN_EXECUTIVE: the free pages are left ReferenceCount==0 for the real
        // MM lists (ke/initarm.c). mmuarm's page-table pages come from the arena.
        //
        (void)a; (void)b; (void)c;
        KiEmit("\n  L2/window arena      : base="); KiEmitHex(MiArmL2ArenaBase);
        KiEmit(" pages="); KiEmitHex(MiArmL2ArenaPages);
        KiEmit("\n  (free pages handed to the real MM lists in ke/initarm.c)\n");
#else
        KiEmit("\n  free pages on list   : ");
        KiEmitHex(MiArmFreeList.Total);
        KiEmit("\n  bad pages on list    : ");
        KiEmitHex(MiArmBadList.Total);

        //
        // Verify the free list is real: remove three pages (MiRemoveAnyPage),
        // confirm they are distinct, non-zero, and inside described RAM.
        //

        a = MiArmRemovePageFromList(&MiArmFreeList);
        b = MiArmRemovePageFromList(&MiArmFreeList);
        c = MiArmRemovePageFromList(&MiArmFreeList);
        KiEmit("\n  alloc 3 from list    : ");
        KiEmitHex(a); KiEmit(" "); KiEmitHex(b); KiEmit(" "); KiEmitHex(c);
        KiEmit((a && b && c && a != b && b != c && a != c &&
                a <= MiArmHighestPage && b <= MiArmHighestPage && c <= MiArmHighestPage)
               ? "  OK - distinct, in range\n"
               : "  *** FAIL ***\n");
        KiEmit("  free pages remaining : ");
        KiEmitHex(MiArmFreeList.Total);
        KiEmit("\n");
#endif
    }

    //
    // Step 4: the free list now exists, so build the general self-map (the PTE
    // window MiGetPteAddress/MiGetPdeAddress resolve through). ke/initarm.c needs
    // it to back the pool expansion's system PTEs, so it is built on every path.
    //

    MiArmBuildSelfMap();

    //
    // Step 5 (verification only): demonstrate the 4 KB remap + self-map edit and
    // the demand-fill fault path. Skipped under KI_RUN_EXECUTIVE so the executive
    // boot is clean and deterministic (the mechanisms are proven; the real MM
    // owns these VAs once linked).
    //

#if !KI_RUN_EXECUTIVE
    MiArmRemapDemo();
    MiArmFaultFillDemo();
    MiArmRealModelDemo();
    MiArmPdSplitDemo();
    MiArmLogPdRepointDemo();
#endif
}

//
// Accessors for ke/initarm.c (the real MiInitMachineDependent). initarm.c is
// compiled with the MM include chain (mi.h/miarm.h), so it owns the real MMPFN /
// Mm* globals; these hand it the values this self-contained bring-up computed -
// the PFN database (ARM_MMPFN is byte-compatible with MMPFN), the physical-page
// accounting, and the reserved KSEG0 pool chunk.
//

PVOID MiArmGetPfnDatabase  (VOID) { return (PVOID)MiArmPfnDb; }
ULONG MiArmGetPhysPages    (VOID) { return MiArmPhysPages; }
ULONG MiArmGetLowestPage   (VOID) { return MiArmLowestPage; }
ULONG MiArmGetHighestPage  (VOID) { return MiArmHighestPage; }
ULONG MiArmGetFreeCount    (VOID) { return MiArmFreeList.Total; }
ULONG MiArmGetPoolBasePage (VOID) { return MiArmPoolBasePage; }
ULONG MiArmGetPoolPages    (VOID) { return MiArmPoolPages; }

#if KI_RUN_EXECUTIVE
//
// System-region fault accounting: how many faults the logical-table-driven fill
// honored (a valid logical PTE -> installed the page MM named) vs demand-zeroed.
// Printed after ExpInitializeExecutive returns (ke/kearm.c) to show how much of the
// fill is now faithful to MM's own page tables.
//
ULONG MiArmGetExecHonored (VOID) { return MiArmExecHonored; }
ULONG MiArmGetExecZeroed  (VOID) { return MiArmExecZeroed; }
ULONG MiArmGetExecPdL1Fills  (VOID) { return MiArmExecPdL1Fills; }
ULONG MiArmGetExecPdL1Demand (VOID) { return MiArmExecPdL1Demand; }

//
// Count the valid entries MM's *StartPde wrote into the logical page directory - proof
// the repointed PDE window routed MM's writes to MiArmLogPd (not the live hardware L1).
//
ULONG
MiArmGetLogPdValid (VOID)
{
    ULONG i, n = 0;
    HARDWARE_PTE pde;
    for (i = 0; i < L1_ENTRIES; i += 1) {
        *(ULONG *)&pde = MiArmLogPd[i];
        if (pde.Valid)                      // HARDWARE_PTE.Valid (bit 1); miarm.h not in this chain
            n += 1;
    }
    return n;
}
#endif
