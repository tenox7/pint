/* CPU Architecture Constants */
#define PROCESSOR_ARCHITECTURE_INTEL       0
#define PROCESSOR_ARCHITECTURE_MIPS        1
#define PROCESSOR_ARCHITECTURE_ALPHA       2
#define PROCESSOR_ARCHITECTURE_PPC         3
#define PROCESSOR_ARCHITECTURE_SHX         4
#define PROCESSOR_ARCHITECTURE_ARM         5
#define PROCESSOR_ARCHITECTURE_IA64        6
#define PROCESSOR_ARCHITECTURE_ALPHA64     7
#define PROCESSOR_ARCHITECTURE_MSIL        8
#define PROCESSOR_ARCHITECTURE_AMD64       9 
#define PROCESSOR_ARCHITECTURE_IA32_ON_WIN64 10
#define PROCESSOR_ARCHITECTURE_NEUTRAL     11
#define PROCESSOR_ARCHITECTURE_ARM64       12
#define PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64 13
#define PROCESSOR_ARCHITECTURE_IA32_ON_ARM64   14
#define PROCESSOR_ARCHITECTURE_UNKNOWN     0xFFFF

/* CPU Architecture Name Strings Array */
static char* ProcessorArchitectureNames[] = {
    "x86",                /* PROCESSOR_ARCHITECTURE_INTEL */
    "MIPS",               /* PROCESSOR_ARCHITECTURE_MIPS */
    "Alpha",              /* PROCESSOR_ARCHITECTURE_ALPHA */
    "PowerPC",            /* PROCESSOR_ARCHITECTURE_PPC */
    "SHX",                /* PROCESSOR_ARCHITECTURE_SHX */
    "ARM",                /* PROCESSOR_ARCHITECTURE_ARM */
    "IA64",               /* PROCESSOR_ARCHITECTURE_IA64 */
    "Alpha64",            /* PROCESSOR_ARCHITECTURE_ALPHA64 */
    "MSIL",               /* PROCESSOR_ARCHITECTURE_MSIL */
    "x64",                /* PROCESSOR_ARCHITECTURE_AMD64 */
    "IA32 on Win64",      /* PROCESSOR_ARCHITECTURE_IA32_ON_WIN64 */
    "Neutral",            /* PROCESSOR_ARCHITECTURE_NEUTRAL */
    "ARM64",              /* PROCESSOR_ARCHITECTURE_ARM64 */
    "ARM32 on Win64",     /* PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64 */
    "IA32 on ARM64"       /* PROCESSOR_ARCHITECTURE_IA32_ON_ARM64 */
};

/* For unknown architecture (0xFFFF) */
#define PROCESSOR_ARCHITECTURE_STR_UNKNOWN "Unknown"

/* The architecture of the processor. NT 3.5 has no wProcessorArchitecture (NT 3.51
 * put it where dwOemId is): it is that of dwProcessorType */
static WORD processorArchitecture(SYSTEM_INFO *sysInfo) {
    switch (sysInfo->dwProcessorType) {
    case PROCESSOR_INTEL_386:
    case PROCESSOR_INTEL_486:
    case PROCESSOR_INTEL_PENTIUM:
        return PROCESSOR_ARCHITECTURE_INTEL;
    case PROCESSOR_MIPS_R4000:
        return PROCESSOR_ARCHITECTURE_MIPS;
    case PROCESSOR_ALPHA_21064:
        return PROCESSOR_ARCHITECTURE_ALPHA;
    case PROCESSOR_PPC_601:
    case PROCESSOR_PPC_603:
    case PROCESSOR_PPC_604:
    case PROCESSOR_PPC_620:
        return PROCESSOR_ARCHITECTURE_PPC;
    case PROCESSOR_ARM_CORTEX_A7:
    case PROCESSOR_ARM_CORTEX_A53:
    case PROCESSOR_ARM_CORTEX_A72:
        return PROCESSOR_ARCHITECTURE_ARM;
    }
    return PROCESSOR_ARCHITECTURE_UNKNOWN;
}
