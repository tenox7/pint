# The ARM port's additions to the genuine NT sources and headers, made as the
# build copies them (private/ and public/ stay pristine): where a machine is
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

# mm/mmfault.c: a user mode reference to the system address space reaches the
# memory manager on ARM, which separates the spaces by page protection as x86,
# Alpha and PPC do (the MIPS processor raises an address error itself).
s/^#if defined(_X86_) || defined(_ALPHA_) || defined(_PPC_)$/& || defined(_ARM_)/

# config/cmdat3.c: the Session Manager value EnableAlignmentFaultExceptions, which
# makes misaligned references raise exceptions rather than be emulated (unless the
# thread or process asks for emulation) on Alpha and PPC, and on ARM (ke/exceptn.c).
s/^#if defined(_ALPHA_) || defined(_PPC_)$/& || defined(_ARM_)/

# mm/mi.h: the memory manager header of each machine; that of ARM is the port's
# kernel/mm/miarm.h (its include named flat, as incfix.sed leaves the others).
/^#include "mippc\.h"$/a\
\
#elif defined(_ARM_)\
#include "miarm.h"

# kd/kdapi.c: the image machine the debugger is told.
/^    m->u.GetVersion.MachineType = IMAGE_FILE_MACHINE_POWERPC;$/a\
#elif defined(_M_ARM)\
    m->u.GetVersion.MachineType = IMAGE_FILE_MACHINE_ARMNT;

# OLE's com\dcomrem\channelb.cxx and com\class\alocdbg.cxx, apimon: the image machine of
# the stack walk (StackWalk) of the checked build.
/^#define MACHINE_TYPE  IMAGE_FILE_MACHINE_POWERPC$/a\
#elif defined(_M_ARM)\
#define MACHINE_TYPE  IMAGE_FILE_MACHINE_ARMNT

# io/dumpctl.c: the exception address of the crash dump.
/exception.ExceptionAddress = (PVOID) context->Iar;/ {
a #elif defined(_ARM_)
a exception.ExceptionAddress = (PVOID) context->Pc;
}

# ke/kernldat.c: the TB process-id count and TB size are defined for MIPS only; the
# ARM TB (ke/tbarm.c) has process ids too.
/^\/\/ KeNumberProcessIds - This is a MIPS specific value/,/^ULONG KeNumberTbEntries;/s/^#if defined(_MIPS_)$/#if defined(_MIPS_) || defined(_ARM_)/

# ke.h, ke/ki.h, ke/kernldat.c, mm/procsup.c: the process ids of the PowerPC, which a
# master id and sequence assign for all processors, are those of the ARM ASIDs
# (ke/tbarm.c): the id and sequence of a process, and a new id for a process swapped
# in again (its page directory is a new page).
/^    ULONG DirectoryTableBase\[2\];$/,/^    KAFFINITY ActiveProcessors;$/s/^#if defined(_PPC_)$/& || defined(_ARM_)/
/^extern CALL_PERFORMANCE_DATA KiWaitSingleCallData;$/,/^extern ULONG KiMasterPid;$/s/^#if defined(_PPC_)$/& || defined(_ARM_)/
/^\/\/ KiMasterPid - This is the master PID/,/^ULONG KiMasterPid;$/s/^#if defined(_PPC_)$/& || defined(_ARM_)/
/^        OutProcess->Vm.WorkingSetSize = 0;$/,/^        Process->ProcessSequence = 0;$/s/^#if defined(_PPC_)$/& || defined(_ARM_)/

# ke.h: the raise and lower IRQL routines a driver imports: on ARM kernel routines,
# as on the PowerPC HAL routines.
/^#define KeRaiseIrql(NewIrql, OldIrql) \*(OldIrql) = KeSwapIrql(NewIrql)$/,/^#elif defined(_PPC_)$/s/^#elif defined(_PPC_)$/& || defined(_ARM_)/

# ex.h, ex/lookasid.c: the 64-bit compare exchange and the sequenced singly linked
# lists of ARM are those of MIPS and Alpha, which need no lock (LDREXD/STREXD), and so
# are the paged lookaside routines, which ex.h gives those machines inline.
s/^#if defined(_ALPHA_) || defined(_MIPS_)$/& || defined(_ARM_)/
/^#if defined(_MIPS_) || defined(_ALPHA_)$/{
N
N
s/^\(#if defined(_MIPS_) || defined(_ALPHA_)\)\(\n\n#define ExInterlockedPopEntrySList(Head, Lock) \\\)$/\1 || defined(_ARM_)\2/
}
s/^#if !defined(_MIPS_) && !defined(_ALPHA_)$/& \&\& !defined(_ARM_)/

# ntexapi.h, winnt.h: the processor architecture of ARM, the number later versions of
# NT give it.
/^#define PROCESSOR_ARCHITECTURE_PPC   3$/a\
#define PROCESSOR_ARCHITECTURE_ARM   5

# init/init.c: the image machine of ARM, which the memory manager admits for images
# (SharedUserData), and its 64-bit compare exchange.
/^        SharedUserData->ImageNumberHigh = IMAGE_FILE_MACHINE_POWERPC;$/a\
\
#elif defined(_ARM_)\
\
        SharedUserData->ImageNumberLow = IMAGE_FILE_MACHINE_ARMNT;\
        SharedUserData->ImageNumberHigh = IMAGE_FILE_MACHINE_ARMNT;\
        SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE_DOUBLE] = TRUE;

# gdi/gre/rfntobj.hxx: the machines listed are those with an assembler glyph metrics
# cache lookup (CACHEA); ARM has the portable one of cache.cxx.
s/^\(#if defined(_\(X86\|MIPS\)_) || defined(_\(MIPS\|X86\)_) || defined(_ALPHA_) || defined(_PPC_)\) || defined(_ARM_) *$/\1/

# crt/setjmp.h: the ARM jump buffer, a machine branch after that of the PowerPC: the
# frame of the setjmp, the nonvolatile integer registers, the stack pointer, the
# continuation, the floating status and the nonvolatile floating registers.
/Define jump buffer layout for PowerPC setjmp\/longjmp/,/^} _JUMP_BUFFER;/{
/^} _JUMP_BUFFER;/a\
\
#elif defined(_M_ARM)\
\
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
} _JUMP_BUFFER;
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

# base/client/process.c: GetSystemInfo's processor type of each machine. ARM's is its
# core, the part number of the MIDR (PROCESSOR_ARM_CORTEX_A7, _A53, _A72), which the
# kernel gives as the processor level (ke/initkr.c), as PINT 3.5 gave it.
/^        lpSystemInfo->dwProcessorType = 604;  \/\/ backward compatibility$/{
n
a\
    else\
    if (ProcessorInfo.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) {\
        lpSystemInfo->dwProcessorType = ProcessorInfo.ProcessorLevel;\
        }
}
