//
// Stand-in NT kernel for the ARM32 / Raspberry Pi 2 port - the "tiny bit of a
// kernel, enough to say hello world or less" that the OS loader hands off to.
//
// This is NOT the real ntoskrnl - NT 3.5 was never built for ARM, so there is no
// kernel to load. It is the minimal image that proves the loader's endgame: the
// loader reads this PE off the (FAT) Arc disk via the real BOOT/LIB PE loader
// (peldr.c BlLoadImage), maps it, and enters it at KiSystemStartup with r0 = the
// LOADER_PARAMETER_BLOCK - the same contract KE/MIPS/X4START.S + INITKR.C use.
//
// To keep it dependency-free it talks straight to the PL011 the loader already
// initialized (the JAZZ-video / HalDisplayString console path is the next
// milestone, not hello-world). It mirrors KiInitializeKernel's first acts: save
// the loader block, then prove the handoff carried real data across by walking
// the memory-descriptor list the loader built and echoing a passed-in string.
//
// Freestanding ARMv7, no libc, no .bss (the one global is non-zero-initialized so
// it lands in .data; mkpe.py loads no zero-fill region). Runs MMU-off at its link
// address 0x01001000, on the kernel stack BlSetupForNt allocated.
//

typedef unsigned int u32;

#define UART0   0x3F201000u
#define UART_DR (*(volatile u32 *)(UART0 + 0x00))
#define UART_FR (*(volatile u32 *)(UART0 + 0x18))
#define FR_TXFF 0x20u

static void kputc(int c)
{
    while (UART_FR & FR_TXFF)
        ;
    UART_DR = (u32)(unsigned char)c;
}

static void kputs(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            kputc('\r');
        kputc(*s);
    }
}

static void kputhex(u32 v)
{
    static const char digits[] = "0123456789abcdef";
    int i;
    kputs("0x");
    for (i = 28; i >= 0; i -= 4)
        kputc(digits[(v >> i) & 0xf]);
}

//
// Just the prefix of LOADER_PARAMETER_BLOCK (arc.h) that we read. ARM is ILP32 /
// little-endian like the loader, so the layout matches byte-for-byte; KernelStack
// is at offset 24 (three 8-byte LIST_ENTRYs precede it) - the same offset
// start.S loads sp from.
//
typedef struct _LE {
    struct _LE *Flink;
    struct _LE *Blink;
} LE;

typedef struct _LPB {
    LE    LoadOrderListHead;
    LE    MemoryDescriptorListHead;
    LE    BootDriverListHead;
    u32   KernelStack;
    u32   Prcb;
    u32   Process;
    u32   Thread;
    u32   RegistryLength;
    void *RegistryBase;
    void *ConfigurationRoot;
    char *ArcBootDeviceName;
    char *ArcHalDeviceName;
    char *NtBootPathName;
    char *NtHalPathName;
    char *LoadOptions;
} LPB;

//
// KiInitializeKernel's "save the address of the loader parameter block" (INITKR.C).
// Initialized non-zero so it is emitted in .data, keeping the image .bss-free.
//
void *KeLoaderBlock = (void *)0xffffffffu;

void
KiSystemStartupC(void *LoaderBlock)
{
    LPB *lpb = (LPB *)LoaderBlock;
    LE *e;
    int descriptors = 0;

    KeLoaderBlock = LoaderBlock;

    kputs("\n\n*** NTOSKRNL (ARM32 / Raspberry Pi 2) - KiSystemStartup ***\n");
    kputs("Reached the kernel via the OS loader's PE-load + handoff.\n\n");

    kputs("  LoaderBlock = ");
    kputhex((u32)(unsigned long)LoaderBlock);
    kputc('\n');

    kputs("  KernelStack = ");
    kputhex(lpb->KernelStack);
    kputs("  (sp set here by start.S)\n");

    //
    // Walk the memory-descriptor list the loader built (BlMemoryInitialize). A
    // non-zero count across the handoff proves the loader block's lists arrived
    // intact - the data contract, not just the control transfer.
    //
    for (e = lpb->MemoryDescriptorListHead.Flink;
         e != &lpb->MemoryDescriptorListHead;
         e = e->Flink) {
        if (++descriptors > 4096)
            break;
    }
    kputs("  memory descriptors handed over: ");
    kputhex((u32)descriptors);
    kputc('\n');

    if (lpb->LoadOptions) {
        kputs("  LoadOptions = \"");
        kputs(lpb->LoadOptions);
        kputs("\"\n");
    }

    kputs("\nhello world\n\n");
    kputs("NTOSKRNL: nothing more to do yet - halting.\n");
}
