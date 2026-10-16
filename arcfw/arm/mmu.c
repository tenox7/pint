//
// Firmware address map (the ARM analog of the x86 SU module turning on paging
// before NTLDR runs the OS loader). The ARC firmware and the OS loader run with
// translation on:
//
//   [0x00000000, 0x3F000000)  identity, Normal non-cacheable: firmware code and
//                             data, loader heap, staged ramdisk, VideoCore memory
//   [0x3F000000, 0x41000000)  identity, Device: BCM2836 peripherals + local block
//   [0x80000000, 0xBF000000)  KSEG0, Normal non-cacheable: the OS loader forms every
//                             address it gives the kernel as KSEG0 | physical, and
//                             the kernel is entered at its KSEG0 link address
//
// Caches stay off in the firmware, so DMA engines and the VideoCore see memory
// coherently without maintenance.
//

#define L1_ENTRIES      4096
#define SECTION         0x00002u
#define AP_PRIV_RW      (1u << 10)
#define NORMAL_NC       (1u << 12)              // TEX=001 C=0 B=0
#define DEVICE          (1u << 2)               // TEX=000 C=0 B=1
#define XN              (1u << 4)

#define PERIPH_BASE     0x3F000000u
#define PERIPH_END      0x41000000u
#define KSEG0           0x80000000u

#include "fwp.h"

//
// The translation table, which must be 16 KB aligned: within a space of twice its
// size.
//

static ULONG ArcL1Space[L1_ENTRIES * 2];

VOID
FwMmuInitialize (
    VOID
    )
{
    PULONG ArcL1 = (PULONG)(((ULONG)ArcL1Space + 0x3fff) & ~0x3fff);
    ULONG mb, sctlr;

    for (mb = 0; mb < L1_ENTRIES; mb++)
        ArcL1[mb] = 0;

    for (mb = 0; mb < (PERIPH_BASE >> 20); mb++)
        ArcL1[mb] = (mb << 20) | SECTION | AP_PRIV_RW | NORMAL_NC;

    for (mb = PERIPH_BASE >> 20; mb < (PERIPH_END >> 20); mb++)
        ArcL1[mb] = (mb << 20) | SECTION | AP_PRIV_RW | DEVICE | XN;

    for (mb = 0; mb < (PERIPH_BASE >> 20); mb++)
        ArcL1[(KSEG0 >> 20) + mb] = (mb << 20) | SECTION | AP_PRIV_RW | NORMAL_NC;

    __dsb(_ARM_BARRIER_SY);
    _MoveToCoprocessor((ULONG)ArcL1, 15, 0, 2, 0, 0);
    _MoveToCoprocessor(0, 15, 0, 2, 0, 2);
    _MoveToCoprocessor(1, 15, 0, 3, 0, 0);
    _MoveToCoprocessor(0, 15, 0, 8, 7, 0);
    __dsb(_ARM_BARRIER_SY);
    __isb(_ARM_BARRIER_SY);
    sctlr = _MoveFromCoprocessor(15, 0, 1, 0, 0);
    _MoveToCoprocessor(sctlr | 1u, 15, 0, 1, 0, 0);
    __isb(_ARM_BARRIER_SY);
}
