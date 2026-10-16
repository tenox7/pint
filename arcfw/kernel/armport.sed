# The ARM port's additions to the genuine NT 3.5 sources and headers, made as the
# build copies them (PRIVATE/ and PUBLIC/ stay pristine): where a machine is
# chosen by name rather than by a RISC gate (riscgate.sed), ARM's branch.

# ntimage.h, winnt.h: the image machine of ARM, Thumb-2 little-endian, as Microsoft C
# for ARM and its linker make it.
/^#define IMAGE_FILE_MACHINE_POWERPC  *0x1F0 /a\
#define IMAGE_FILE_MACHINE_ARMNT             0x1C4   // ARM Thumb-2 Little-Endian

# boot/inc/bldr.h: the image type of each target.
/^#define PPC_IMAGE  0x1f0$/a\
#define ARM_IMAGE  0x1c4
/^#define TARGET_IMAGE PPC_IMAGE$/{
n
n
a\
\
#if defined(_ARM_)\
\
#define TARGET_IMAGE ARM_IMAGE\
\
#endif
}

# mm/modwrite.c: the machine of the crash dump header.
/^        Block\[8\] =$/ {
a #ifdef _ARM_
a IMAGE_FILE_MACHINE_ARMNT;
a #endif
}

# mm/mapview.c: the machine an image section must be built for.
/!= IMAGE_FILE_MACHINE_ALPHA)$/ {
n
a #ifdef _ARM_
a if ((ControlArea->Segment->ImageInformation.Machine != IMAGE_FILE_MACHINE_ARMNT)
a #endif
}

# mm/mmfault.c: a user mode reference to the system address space reaches the
# memory manager on ARM, which separates the spaces by page protection as x86,
# Alpha and PPC do (the MIPS processor raises an address error itself).
s/^#if defined(_X86_) || defined(_ALPHA_) || defined(_PPC_)$/& || defined(_ARM_)/

# io/dumpctl.c: the exception address of the crash dump.
/exception.ExceptionAddress = (PVOID) context->Iar;/ {
a #elif defined(_ARM_)
a exception.ExceptionAddress = (PVOID) context->Pc;
}

# ex/sysinfo.c: SystemProcessorInformation has per-machine processor types and an
# "*** Error ***" trap for any other machine.
/^ *\*\*\* Error \*\*\* Processor Type not defined$/ {
i #ifdef _ARM_
i ProcessorInfo->ProcessorType = PROCESSOR_ARM_CORTEX_A7;
i #else
a #endif
}

# ke/kernldat.c: the TB process-id count and TB size are defined for MIPS only; the
# ARM TB (ke/tbarm.c) has process ids too.
/^\/\/ KeNumberProcessIds - This is a MIPS specific value/,/^ULONG KeNumberTbEntries;/s/^#if defined(_MIPS_)$/#if defined(_MIPS_) || defined(_ARM_)/

# ke/mips/flush.c: as on the R4000, the ARMv7 instruction cache does not see the data
# cache, so sweeping the instruction cache also sweeps the data cache.
s/^#if defined(R4000)$/#if defined(R4000) || defined(_ARM_)/

# ke/wait.c: the generic-C KiSetServerWaitClientEvent (compiled only for machines with
# no assembly fast path - every shipped NT 3.5 machine had one) never declared Queue.
/^    KPRIORITY LongWayBoost = EVENT_PAIR_INCREMENT;$/ a\
    PKQUEUE Queue;

# gdi/gre/rfntobj.hxx: the machines listed are those with an assembler glyph metrics
# cache lookup (CACHEA); ARM has the portable one of cache.cxx.
s/^\(#if defined(_\(X86\|MIPS\)_) || defined(_\(MIPS\|X86\)_) || defined(_ALPHA_) || defined(_PPC_)\) || defined(_ARM_) *$/\1/

# crt/setjmp.h: the ARM jump buffer: the frame of the setjmp, the nonvolatile integer
# registers, the stack pointer, the continuation, the floating status and the
# nonvolatile floating registers.
/All MIPS implementations need _JBLEN of 16/,/^#endif/{
/^#endif/a\
\
#ifdef _M_ARM\
/*\
 * ARM implementations use a _JBLEN of 28 longwords.\
 */\
#define _JBLEN  28\
#define _JBTYPE int\
\
/*\
 * Define jump buffer layout for ARM setjmp/longjmp.\
 */\
\
typedef struct __JUMP_BUFFER {\
    unsigned long Frame;\
    unsigned long R4;\
    unsigned long R5;\
    unsigned long R6;\
    unsigned long R7;\
    unsigned long R8;\
    unsigned long R9;\
    unsigned long R10;\
    unsigned long R11;\
    unsigned long Sp;\
    unsigned long Pc;\
    unsigned long Fpscr;\
    unsigned __int64 D[8];\
} _JUMP_BUFFER;\
\
#endif
}

# ntimage.h, winnt.h: the base relocation of the address a Thumb movw, movt pair
# loads, which Microsoft C for ARM makes for every absolute address in code.
/^#define IMAGE_REL_BASED_MIPS_JMPADDR  *5$/a\
#define IMAGE_REL_BASED_THUMB_MOV32           7

# rtl/ldrreloc.c: apply it.
/^            case IMAGE_REL_BASED_ABSOLUTE :$/i\
            case IMAGE_REL_BASED_THUMB_MOV32 :\
                //\
                // Thumb MOV32 - (32-bits) relocate the address a movw and\
                //      movt instruction pair loads.\
                //\
\
                Temp = (((PUSHORT)FixupVA)[0] & 0xf) << 12 |\
                       (((PUSHORT)FixupVA)[0] >> 10 & 1) << 11 |\
                       (((PUSHORT)FixupVA)[1] >> 12 & 7) << 8 |\
                       (((PUSHORT)FixupVA)[1] & 0xff) |\
                       (((PUSHORT)FixupVA)[2] & 0xf) << 28 |\
                       (((PUSHORT)FixupVA)[2] >> 10 & 1) << 27 |\
                       (((PUSHORT)FixupVA)[3] >> 12 & 7) << 24 |\
                       (((PUSHORT)FixupVA)[3] & 0xff) << 16;\
                Temp += Diff;\
                ((PUSHORT)FixupVA)[0] = (USHORT)((((PUSHORT)FixupVA)[0] & 0xfbf0) |\
                    (Temp >> 12 & 0xf) | (Temp >> 11 & 1) << 10);\
                ((PUSHORT)FixupVA)[1] = (USHORT)((((PUSHORT)FixupVA)[1] & 0x8f00) |\
                    (Temp >> 8 & 7) << 12 | (Temp & 0xff));\
                ((PUSHORT)FixupVA)[2] = (USHORT)((((PUSHORT)FixupVA)[2] & 0xfbf0) |\
                    (Temp >> 28 & 0xf) | (Temp >> 27 & 1) << 10);\
                ((PUSHORT)FixupVA)[3] = (USHORT)((((PUSHORT)FixupVA)[3] & 0x8f00) |\
                    (Temp >> 24 & 7) << 12 | (Temp >> 16 & 0xff));\
                break;\


# base/client/kernel32.src: the forwarders of the unwind routines of a RISC machine
# to ntdll, which the MIPS section gives.
/^#if defined(MIPS)$/{
N
s/^#if defined(MIPS)\n    RtlCaptureContext = NTDLL.RtlCaptureContext$/#if defined(MIPS) || defined(_ARM_)\n    RtlCaptureContext = NTDLL.RtlCaptureContext/
}
