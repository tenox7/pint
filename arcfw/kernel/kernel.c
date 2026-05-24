//
// Stand-in NT kernel for the ARM32 / Raspberry Pi 2 port - the "tiny bit of a
// kernel, enough to say hello world" the OS loader hands off to.
//
// This is NOT a full ntoskrnl - NT 3.5 was never built for ARM, so there is no
// kernel to load, and the real KiInitializeKernel -> ExpInitializeExecutive chain
// pulls in the entire executive (PCR/PRCB/Mm/Ob/Ps/...). What IS real here is the
// output path: kernel.c reaches the kernel exactly as KE/MIPS/X4START.S does (PE
// loaded by the OS loader's peldr.c, entered at KiSystemStartup with r0 = the
// LOADER_PARAMETER_BLOCK), then prints "hello world" on the HDMI framebuffer through
// the genuine NT HAL routine HalDisplayString (arcfw/kernel/jxdisp.c, ported from
// NTHALS/.../JXDISP.C) using the font + framebuffer the OS loader supplied.
//
// Two output sinks, kept distinct:
//   - PL011 serial: a debug breadcrumb (the KdPrint/kernel-debugger analog) that
//     proves the handoff carried real data across - the loader block pointer, the
//     kernel stack, the memory-descriptor list, the passed LoadOptions. Visible on
//     GPIO14/15 and to the headless QEMU serial check.
//   - HDMI framebuffer: "hello world" rendered by the real HAL display path.
//
// Freestanding ARMv7, no libc. Zero-initialized globals (the HAL display state in
// jxdisp.c) are folded into .data by kernel.ld so the image still carries no .bss
// zero-fill region (mkpe.py emits none). Runs MMU-off at its link address 0x01001000,
// on the kernel stack BlSetupForNt allocated.
//

#include "kernel.h"

#define UART0   0x3F201000u
#define UART_DR (*(volatile ULONG *)(UART0 + 0x00))
#define UART_FR (*(volatile ULONG *)(UART0 + 0x18))
#define FR_TXFF 0x20u

static void kputc(int c)
{
    while (UART_FR & FR_TXFF)
        ;
    UART_DR = (ULONG)(unsigned char)c;
}

static void kputs(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            kputc('\r');
        kputc(*s);
    }
}

static void kputhex(ULONG v)
{
    static const char digits[] = "0123456789abcdef";
    int i;
    kputs("0x");
    for (i = 28; i >= 0; i -= 4)
        kputc(digits[(v >> i) & 0xf]);
}

//
// KiInitializeKernel's "save the address of the loader parameter block" (INITKR.C).
//
PVOID KeLoaderBlock;

void
KiSystemStartupC(void *LoaderBlockPtr)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock = (PLOADER_PARAMETER_BLOCK)LoaderBlockPtr;
    PLIST_ENTRY head, e;
    int descriptors = 0;

    KeLoaderBlock = LoaderBlockPtr;

    //
    // Serial breadcrumb: confirm the handoff and the integrity of the data the
    // loader block points at, before touching the framebuffer.
    //
    kputs("\n\n*** NTOSKRNL (ARM32 / Raspberry Pi 2) - KiSystemStartup ***\n");
    kputs("Reached the kernel via the OS loader's PE-load + handoff.\n\n");

    kputs("  LoaderBlock = ");
    kputhex((ULONG)(unsigned long)LoaderBlock);
    kputc('\n');

    kputs("  KernelStack = ");
    kputhex(LoaderBlock->KernelStack);
    kputs("  (sp set here by start.S)\n");

    //
    // Walk the memory-descriptor list the loader built (BlMemoryInitialize). A
    // non-zero count across the handoff proves the loader block's lists arrived
    // intact - the data contract, not just the control transfer.
    //
    head = &LoaderBlock->MemoryDescriptorListHead;
    for (e = head->Flink; e != head; e = e->Flink) {
        if (++descriptors > 4096)
            break;
    }
    kputs("  memory descriptors handed over: ");
    kputhex((ULONG)descriptors);
    kputc('\n');

    if (LoaderBlock->LoadOptions) {
        kputs("  LoadOptions = \"");
        kputs((const char *)LoaderBlock->LoadOptions);
        kputs("\"\n");
    }

    kputs("  OemFontFile = ");
    kputhex((ULONG)(unsigned long)LoaderBlock->OemFontFile);
    kputs("   FrameBuffer = ");
    kputhex(LoaderBlock->Arm.FrameBuffer);
    kputc('\n');

    //
    // Real NT HAL display path -> HDMI. HalpInitializeDisplay0 takes the font from
    // LoaderBlock->OemFontFile and the framebuffer geometry from the loader block,
    // then HalDisplayString renders through the ported HalpOutputCharacter glyph
    // blitter (jxdisp.c). This is the "hello world" the user sees on the monitor.
    //
    if (HalpInitializeDisplay0(LoaderBlock)) {
        HalDisplayString((PUCHAR)"Microsoft (R) Windows NT (TM)  -  ARM32 / Raspberry Pi 2\n");
        HalDisplayString((PUCHAR)"OS Loader handed off to NTOSKRNL via KiSystemStartup.\n");
        HalDisplayString((PUCHAR)"\n");
        HalDisplayString((PUCHAR)"hello world\n");
        kputs("\n  HalDisplayString: rendered \"hello world\" to the framebuffer (HDMI).\n");
    } else {
        kputs("\n  HalpInitializeDisplay0: no framebuffer/font from loader - serial only.\n");
    }

    kputs("\nhello world\n\n");
    kputs("NTOSKRNL: nothing more to do yet - halting.\n");
}
