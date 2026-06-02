/*++

Copyright (c) 2026

Module Name:

    exglobals.c

Abstract:

    Executive data globals that ntoskrnl provides and the portable executive
    references, but whose defining files (mostly ntos/init/* and the NLS data
    blobs) are not yet in the build. They are the data half of the executive
    link closure (exec-link-probe.sh). Defined here with bring-up stub values;
    each is replaced by its genuine definition when its owning file compiles
    (the init orchestration, the loaded NLS tables, the real pool/Ps locks).

    Types match the real extern declarations (compiled against the NT headers,
    so a mismatch fails loudly). The few whose type is uncertain without their
    private subsystem header (e.g. PspProcessLockMutex) and the MM PTE globals
    (NoAccessPte / DemandZeroPde, which need MMPTE) are deferred, not guessed.

Environment:

    Kernel mode.

--*/

#include "ki.h"

//
// Executive / Nt globals (ntos/init).
//

ULONG NtGlobalFlag = 0;
ULONG NtBuildNumber = 782;                  // NT 3.5 build 782 (stub; init sets the real form)
PUCHAR NtSystemPath = NULL;
STRING NtSystemPathString = { 0, 0, NULL };

//
// Configuration Manager version globals (ntos/config/cmdat).
//

ULONG CmNtGlobalFlag = 0;
ULONG CmNtCSDVersion = 0;
UNICODE_STRING CmVersionString = { 0, 0, NULL };
UNICODE_STRING CmCSDVersionString = { 0, 0, NULL };

//
// Process manager globals (ntos/ps).
//

LCID PsDefaultThreadLocaleId = 0;
LCID PsDefaultSystemLocaleId = 0;
ULONG PsMinimumWorkingSet = 20;
ULONG PsMaximumWorkingSet = 45;
KSPIN_LOCK PsLoadedModuleSpinLock;
BOOLEAN PsWatchEnabled = FALSE;
FAST_MUTEX PspProcessLockMutex;             // ntrtl/ps decl confirms FAST_MUTEX

//
// Locks the executive expects ntoskrnl to provide (uniprocessor: a zeroed word).
//

KSPIN_LOCK ExpLuidLock;
KSPIN_LOCK PoolTraceLock;
KSPIN_LOCK NonPagedPoolLock;
KSPIN_LOCK LpcpLock;
KSPIN_LOCK PspEventPairLock;

//
// Debugger state (ntos/init / kd).
//

BOOLEAN KdDebuggerEnabled = FALSE;

//
// NLS state + table pointers (the real tables are loaded from the NLS files;
// NULL until then - code paths that dereference them run after NLS init).
//

BOOLEAN *NlsMbCodePageTag = NULL;       // ntrtl.h declares these as BOOLEAN* (into NLS data)
BOOLEAN *NlsMbOemCodePageTag = NULL;
PUSHORT NlsLeadByteInfo = NULL;
PUSHORT Nls844UnicodeUpcaseTable = NULL;
PUSHORT Nls844UnicodeLowercaseTable = NULL;
