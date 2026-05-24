#ifndef _NT_ARM_SHIM_
#define _NT_ARM_SHIM_

//
// Neutralize MSVC-only keywords/decorations so the original NT headers and
// loader sources compile under the freestanding arm-linux-gnueabihf GNU stack.
// NTDEF.H already guards most of these on _MSC_VER / _M_*; these are defensive
// catch-alls for raw tokens elsewhere in the tree.
//

#ifndef __cdecl
#define __cdecl
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __fastcall
#define __fastcall
#endif
#ifndef _cdecl
#define _cdecl
#endif
#ifndef _stdcall
#define _stdcall
#endif
#ifndef _fastcall
#define _fastcall
#endif
#ifndef __declspec
#define __declspec(x)
#endif

//
// MSVC 64-bit type. NTDEF.H typedefs LONGLONG/ULONGLONG from __int64 on every
// non-MIDL, non-_M_IX86 build (which includes us). Map it to the GNU spelling;
// "long long" is 64-bit under the ARM EABI, matching the original LONGLONG.
//
#ifndef __int64
#define __int64 long long
#endif

#endif // _NT_ARM_SHIM_
