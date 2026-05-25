/*++ BUILD Version: 0001    // Increment this if a change has global effects

Copyright (c) 1989-1993  Microsoft Corporation

Module Name:

    nt.h

Abstract:

    Top level include file for the NT API.

    ARM32 port: verbatim copy of PUBLIC/SDK/INC/NT.H with an _ARM_ arch branch
    added so ntarm.h (the NTMIPS.H analog) is included. This shadow copy is
    first on the include path; everything else resolves to the real headers.

--*/

#ifndef NT_INCLUDED
#define NT_INCLUDED

//
//  Common definitions
//

#define _CTYPE_DISABLE_MACROS

#include <excpt.h>
#include <stdarg.h>
#include <ntdef.h>

#include <ntstatus.h>
#include <ntkeapi.h>

#ifdef _X86_
#include "nti386.h"
#endif // i386

#ifdef _MIPS_
#include "ntmips.h"
#endif // MIPS

#ifdef _ALPHA_
#include "ntalpha.h"
#endif // _ALPHA_

#ifdef _PPC_
#include "ntppc.h"
#endif // _PPC_

#ifdef _ARM_
#include "ntarm.h"
#endif // _ARM_

//
//  Each NT Component that exports system call APIs to user programs
//  should have its own include file included here.
//

#include <ntseapi.h>
#include <ntobapi.h>
#include <ntimage.h>
#include <ntldr.h>
#include <ntpsapi.h>
#include <ntxcapi.h>
#include <ntlpcapi.h>
#include <ntioapi.h>
#include <ntiolog.h>
#include <ntexapi.h>
#include <ntmmapi.h>
#include <ntregapi.h>
#include <ntelfapi.h>
#include <ntconfig.h>
#include <ntnls.h>

#endif // NT_INCLUDED
