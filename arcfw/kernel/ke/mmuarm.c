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

#define L1_ENTRIES 4096                 // 4 GB / 1 MB, 16 KB directory
#define L2_ENTRIES 256                  // 1 MB / 4 KB, 1 KB table
#define KSEG0 0x80000000u               // high-half base (== ntarm.h KSEG0_BASE)

//
// The boot page directory (16 KB aligned, as TTBR0 requires) and one second-level
// table for the HARDWARE_PTE demonstration. Global so ke/armstart.S can take their
// addresses; static-storage so they sit at a fixed physical address. Linked in
// KSEG0 (0x81xxxxxx); their physical address is the link address minus KSEG0.
//

ULONG MiArmL1[L1_ENTRIES] __attribute__((aligned(16384)));
static ULONG MiArmL2Demo[L2_ENTRIES] __attribute__((aligned(1024)));

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
