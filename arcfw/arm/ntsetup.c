//
// ARM analog of BOOT/LIB/{MIPS,ALPHA}/NTSETUP.C - the per-architecture setup the
// OS loader does between loading the kernel image and transferring control to it
// (osloader.c:779 BlSetupForNt). Replaces the stubs.c placeholder.
//
// The MIPS/Alpha versions build kernel page tables and allocate the idle thread's
// kernel stack, panic stack, PCR, and PDR pages. The kernel runs MMU-off (no page
// tables yet - KSEG0_BASE == 0, bldr.h) and the one thing it needs is a stack:
// KE/MIPS/X4START.S loads sp from LoaderBlock->KernelStack, and so does our start.S.
// We allocate that; PCR/PDR/panic-stack/page-tables follow as the kernel grows.
//
#include "bldr.h"

//
// The kernel's HAL console (arcfw/kernel/jxdisp.c) needs the OEM font + the VideoCore
// framebuffer geometry via the loader block. Set here, in the arch setup BlOsLoader calls
// at osloader.c:779, which has no notion of our ARM framebuffer.
//
extern unsigned int *fb_base;
extern unsigned int fb_width, fb_height, fb_pitch, fb_order;
extern unsigned char _binary_font_fon_start[];

//
// Idle-thread kernel stack size. MIPS/Alpha use KERNEL_STACK_SIZE from the kernel
// headers (not in our shim set); 16 KiB (4 pages) matches their order of magnitude.
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

    //
    // Hand the kernel its console: the OEM font (OemFontFile, the same field the MIPS/
    // Alpha loader fills) and the VideoCore framebuffer geometry (the ARM_LOADER_BLOCK
    // fields). jxdisp.c's HalpInitializeDisplay0 reads these; without them it falls back
    // to serial-only.
    //
    // PREFER the OEM HAL font the OS Loader already loaded (BlLoadOemHalFont -> vgaoem.fon,
    // set into OemFontFile) - the RISC/Jazz contract. Fall back to the embedded font only
    // when none was loaded (a boot path that skips the hive/NLS/font load leaves it NULL).
    //
    if (LoaderBlock->OemFontFile == NULL) {
        LoaderBlock->OemFontFile = (PVOID)_binary_font_fon_start;
    }
    LoaderBlock->u.Arm.FrameBuffer = (ULONG)(unsigned long)fb_base;
    LoaderBlock->u.Arm.FrameBufferWidth = fb_width;
    LoaderBlock->u.Arm.FrameBufferHeight = fb_height;
    LoaderBlock->u.Arm.FrameBufferPitch = fb_pitch;
    LoaderBlock->u.Arm.FrameBufferPixelOrder = fb_order;

    return ESUCCESS;
}
