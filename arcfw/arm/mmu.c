//
// Firmware address map (the ARM analog of the x86 SU module turning on paging
// before NTLDR runs the OS loader). The ARC firmware and the OS loader run with
// translation on. On the BCM2836 and BCM2837:
//
//   [0x00000000, 0x3F000000)  identity, Normal non-cacheable: firmware code and
//                             data, loader heap, staged ramdisk, VideoCore memory
//   [0x3F000000, 0x41000000)  identity, Device: BCM2836 peripherals + local block
//   [0x80000000, 0xBF000000)  KSEG0, Normal non-cacheable: the OS loader forms every
//                             address it gives the kernel as KSEG0 | physical, and
//                             the kernel is entered at its KSEG0 link address
//
// On the BCM2711 the first gigabyte is memory and VideoCore memory, and the
// peripherals are at the top of the 32-bit space:
//
//   [0x00000000, 0x40000000)  identity, Normal non-cacheable
//   [0x80000000, 0xC0000000)  KSEG0, Normal non-cacheable
//   [0xF8000000, 0xF9000000)  Device: the outbound window of the PCI Express root
//                             complex, 0x6_0000_0000 of the 35-bit space (a 16 MB
//                             supersection), the bus addresses from 0xF8000000
//   [0xFC000000, 0x100000000) identity, Device: BCM2711 and BCM2835 peripherals,
//                             the local block and the GIC-400
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
#define SUPERSECTION    (1u << 18)
#define PA_35_32(Pa)    ((Pa) << 20)            // bits 35:32 of a supersection's address

#define PCIE_WINDOW     0xF80
#define PCIE_WINDOW_PA  0x6u

#define KSEG0           0x80000000u

#define CORTEX_A72      0xD08

#include "fwp.h"

BOOLEAN FwBcm2711 = FALSE;
ULONG FwPeripheralBase = 0x3F000000;

//
// The translation table, which must be 16 KB aligned: within a space of twice its
// size.
//

static ULONG ArcL1Space[L1_ENTRIES * 2];

VOID
FwSocInitialize (
    VOID
    )

/*++

Routine Description:

    This routine finds the system on a chip from the part number of the core
    it runs on: the BCM2711 is the only one with Cortex-A72 cores.

--*/

{
    if (((_MoveFromCoprocessor(15, 0, 0, 0, 0) >> 4) & 0xFFF) == CORTEX_A72) {
        FwBcm2711 = TRUE;
        FwPeripheralBase = 0xFE000000;
    }
}

VOID
FwMmuInitialize (
    VOID
    )
{
    PULONG ArcL1 = (PULONG)(((ULONG)ArcL1Space + 0x3fff) & ~0x3fff);
    ULONG MemoryEnd = FwBcm2711 ? 0x400 : 0x3F0;
    ULONG DeviceStart = FwBcm2711 ? 0xFC0 : 0x3F0;
    ULONG DeviceEnd = FwBcm2711 ? 0x1000 : 0x410;
    ULONG mb, sctlr;

    for (mb = 0; mb < L1_ENTRIES; mb++)
        ArcL1[mb] = 0;

    for (mb = 0; mb < MemoryEnd; mb++)
        ArcL1[mb] = (mb << 20) | SECTION | AP_PRIV_RW | NORMAL_NC;

    for (mb = DeviceStart; mb < DeviceEnd; mb++)
        ArcL1[mb] = (mb << 20) | SECTION | AP_PRIV_RW | DEVICE | XN;

    for (mb = 0; mb < MemoryEnd; mb++)
        ArcL1[(KSEG0 >> 20) + mb] = (mb << 20) | SECTION | AP_PRIV_RW | NORMAL_NC;

    for (mb = PCIE_WINDOW; FwBcm2711 && mb < PCIE_WINDOW + 16; mb++)
        ArcL1[mb] = PA_35_32(PCIE_WINDOW_PA) | SUPERSECTION | SECTION | AP_PRIV_RW | DEVICE | XN;

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
