/*++

Module Name:

    psxarm.h

Abstract:

    The machine dependent definitions of the POSIX subsystem for ARM: the
    child of a fork returns from the fork system call with the value the
    client tests in r0, the integer return register; and the start address
    of a thread as its context holds it, the program counter without the
    Thumb bit, which is the T bit of the status register.

--*/

#define PSX_FORK_RETURN 0x7777

#define SetPsxForkReturn(c) \
        (c.R0 = PSX_FORK_RETURN)

#define PSX_CPSR_THUMB 0x20

//
// The address a call of the start of the thread of context c takes.
//

#define PsxContextStartAddress(c) \
        ((PVOID)((c)->Pc | (((c)->Cpsr & PSX_CPSR_THUMB) ? 1 : 0)))

//
// Start the thread of context c at the address a of a function.
//

#define PsxSetContextStartAddress(c, a) \
        ((c)->Pc = (ULONG)(a) & ~1, \
         (c)->Cpsr = ((ULONG)(a) & 1) ? ((c)->Cpsr | PSX_CPSR_THUMB) : ((c)->Cpsr & ~PSX_CPSR_THUMB))
