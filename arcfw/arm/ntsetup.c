//
// ARM analog of BOOT/LIB/{MIPS,ALPHA}/NTSETUP.C - the per-architecture setup the
// OS loader does between loading the kernel image and transferring control to it
// (osloader.c:779 BlSetupForNt). Replaces the stubs.c placeholder.
//
// The MIPS/Alpha versions build kernel page tables and allocate the idle thread's
// kernel stack, panic stack, PCR, and PDR pages. Our stand-in kernel runs MMU-off
// (no page tables yet - KSEG0_BASE == 0, bldr.h) and only a "hello world" kernel,
// so the one thing it genuinely needs is a stack: KE/MIPS/X4START.S loads sp from
// LoaderBlock->KernelStack, and so does our start.S. We allocate that and nothing
// more; PCR/PDR/panic-stack/page-tables arrive with a real kernel.
//
#include "bldr.h"

//
// Idle-thread kernel stack size. MIPS/Alpha use KERNEL_STACK_SIZE from the kernel
// headers (not in our shim set); 16 KiB (4 pages) matches their order of magnitude
// and is ample for the stand-in kernel.
//
#define KERNEL_STACK_SIZE 0x4000

ARC_STATUS
BlSetupForNt(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )
{
    ARC_STATUS Status;
    ULONG KernelPage;

    //
    // Allocate kernel stack pages for the boot processor's idle thread, exactly as
    // MIPS NTSETUP.C does. BasePage 0 means "anywhere free". KernelStack points at
    // the TOP of the region (high address) because the stack grows down; start.S
    // sets sp to it. With KSEG0_BASE == 0 (MMU off) this is a usable physical
    // address.
    //
    Status = BlAllocateDescriptor(LoaderStartupKernelStack,
                                  0,
                                  KERNEL_STACK_SIZE >> PAGE_SHIFT,
                                  &KernelPage);

    if (Status != ESUCCESS) {
        return Status;
    }

    LoaderBlock->KernelStack =
        (KSEG0_BASE | (KernelPage << PAGE_SHIFT)) + KERNEL_STACK_SIZE;

    return ESUCCESS;
}
