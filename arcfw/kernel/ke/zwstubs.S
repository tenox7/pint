@ ke/zwstubs.S - kernel-mode Zw -> Nt system-service stubs (generated).
@
@ In kernel mode ZwXxx and NtXxx differ only in the previous-mode the service
@ sees (Zw forces KernelMode). Until the system-service trap layer sets that,
@ each ZwXxx tail-branches to its NtXxx: args (r0-r3 + stack) and the return
@ value pass straight through, so the stub is signature-agnostic. This resolves
@ the Zw half of the executive link closure. Previous-mode override is a later
@ refinement (most ExpInitializeExecutive Zw calls already run in KernelMode).
@
@ Generated from the Zw link-closure list (exec-link-probe.sh / EXEC-LINK-TODO.txt).

.syntax unified
.arm
.text
.global ZwAcceptConnectPort
ZwAcceptConnectPort: b NtAcceptConnectPort
.global ZwAllocateVirtualMemory
ZwAllocateVirtualMemory: b NtAllocateVirtualMemory
.global ZwClose
ZwClose: b NtClose
.global ZwCompleteConnectPort
ZwCompleteConnectPort: b NtCompleteConnectPort
.global ZwConnectPort
ZwConnectPort: b NtConnectPort
.global ZwCreateEvent
ZwCreateEvent: b NtCreateEvent
.global ZwCreateFile
ZwCreateFile: b NtCreateFile
.global ZwCreateKey
ZwCreateKey: b NtCreateKey
.global ZwCreatePort
ZwCreatePort: b NtCreatePort
.global ZwCreateProcess
ZwCreateProcess: b NtCreateProcess
.global ZwCreateSection
ZwCreateSection: b NtCreateSection
.global ZwCreateThread
ZwCreateThread: b NtCreateThread
.global ZwDeleteValueKey
ZwDeleteValueKey: b NtDeleteValueKey
.global ZwDuplicateObject
ZwDuplicateObject: b NtDuplicateObject
.global ZwEnumerateKey
ZwEnumerateKey: b NtEnumerateKey
.global ZwEnumerateValueKey
ZwEnumerateValueKey: b NtEnumerateValueKey
.global ZwFlushBuffersFile
ZwFlushBuffersFile: b NtFlushBuffersFile
.global ZwFlushKey
ZwFlushKey: b NtFlushKey
.global ZwFreeVirtualMemory
ZwFreeVirtualMemory: b NtFreeVirtualMemory
.global ZwFsControlFile
ZwFsControlFile: b NtFsControlFile
.global ZwInitializeRegistry
ZwInitializeRegistry: b NtInitializeRegistry
.global ZwListenPort
ZwListenPort: b NtListenPort
.global ZwMapViewOfSection
ZwMapViewOfSection: b NtMapViewOfSection
.global ZwOpenFile
ZwOpenFile: b NtOpenFile
.global ZwOpenKey
ZwOpenKey: b NtOpenKey
.global ZwOpenProcess
ZwOpenProcess: b NtOpenProcess
.global ZwOpenProcessToken
ZwOpenProcessToken: b NtOpenProcessToken
.global ZwOpenSymbolicLinkObject
ZwOpenSymbolicLinkObject: b NtOpenSymbolicLinkObject
.global ZwOpenThreadToken
ZwOpenThreadToken: b NtOpenThreadToken
.global ZwProtectVirtualMemory
ZwProtectVirtualMemory: b NtProtectVirtualMemory
.global ZwQueryInformationProcess
ZwQueryInformationProcess: b NtQueryInformationProcess
.global ZwQueryInformationToken
ZwQueryInformationToken: b NtQueryInformationToken
.global ZwQueryKey
ZwQueryKey: b NtQueryKey
.global ZwQueryObject
ZwQueryObject: b NtQueryObject
.global ZwQuerySection
ZwQuerySection: b NtQuerySection
.global ZwQuerySymbolicLinkObject
ZwQuerySymbolicLinkObject: b NtQuerySymbolicLinkObject
.global ZwQuerySystemInformation
ZwQuerySystemInformation: b NtQuerySystemInformation
.global ZwQueryValueKey
ZwQueryValueKey: b NtQueryValueKey
.global ZwQueryVirtualMemory
ZwQueryVirtualMemory: b NtQueryVirtualMemory
.global ZwQueryVolumeInformationFile
ZwQueryVolumeInformationFile: b NtQueryVolumeInformationFile
.global ZwRaiseHardError
ZwRaiseHardError: b NtRaiseHardError
.global ZwReadFile
ZwReadFile: b NtReadFile
.global ZwReplyWaitReceivePort
ZwReplyWaitReceivePort: b NtReplyWaitReceivePort
.global ZwRequestWaitReplyPort
ZwRequestWaitReplyPort: b NtRequestWaitReplyPort
.global ZwRestoreKey
ZwRestoreKey: b NtRestoreKey
.global ZwSaveKey
ZwSaveKey: b NtSaveKey
.global ZwSetInformationFile
ZwSetInformationFile: b NtSetInformationFile
.global ZwSetInformationThread
ZwSetInformationThread: b NtSetInformationThread
.global ZwSetSecurityObject
ZwSetSecurityObject: b NtSetSecurityObject
.global ZwSetSystemTime
ZwSetSystemTime: b NtSetSystemTime
.global ZwSetTimerResolution
ZwSetTimerResolution: b NtSetTimerResolution
.global ZwSetValueKey
ZwSetValueKey: b NtSetValueKey
.global ZwShutdownSystem
ZwShutdownSystem: b NtShutdownSystem
.global ZwUnloadDriver
ZwUnloadDriver: b NtUnloadDriver
.global ZwUnmapViewOfSection
ZwUnmapViewOfSection: b NtUnmapViewOfSection
.global ZwWaitForSingleObject
ZwWaitForSingleObject: b NtWaitForSingleObject
.global ZwWriteFile
ZwWriteFile: b NtWriteFile
.global ZwWriteVirtualMemory
ZwWriteVirtualMemory: b NtWriteVirtualMemory

.section .note.GNU-stack,"",%progbits
