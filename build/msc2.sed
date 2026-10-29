# The C and C++ Microsoft C 2.0 accepted and the compiler of today does not, as
# the build copies the sources and headers.

# A member function named without a call (gre/pdevobj.cxx): the 1994 compiler
# warned of the missing argument list and called nothing.
s/^\([[:space:]]*\)bDisableHalftone;/\1\&PDEVOBJ::bDisableHalftone;/

# A function of C linkage without a return type, int (gre/textobj.cxx).
s/^extern "C" \([A-Za-z_][A-Za-z_0-9]*\)(/extern "C" int \1(/

# The iostreams: the address of a protected member of a virtual base by its
# qualified name (the address of the member, without parentheses a pointer to
# member), and a temporary of a class whose constructor is protected (here a
# default ios, assigned to reset the ios of a stream: a class derived from ios
# makes it).
s/(&istream::bp!=&ostream::bp)/(\&(istream::bp)!=\&(ostream::bp))/
s/this->ios::operator=(ios());/{ class _ios_default : public ios { public: _ios_default() { } } _Default; this->ios::operator=(_Default); }/

# A parameter of a prototype named by an undeclared type (wowshlp.h, LPSZ): the
# 1994 compiler took it as a name; the string it is, LPSTR.
s/(LPSZ, WORD)/(LPSTR, WORD)/

# An int assigned to an enumeration (midl20\codegen\pickle.cxx): the 1994 compiler
# converted it.
s/pTEInfo->Flags = HasImplicitHandle() ? TYPE_ENCODE_WITH_IMPL_HANDLE : 0;/pTEInfo->Flags = (TYPE_ENCODE_FLAGS)(HasImplicitHandle() ? TYPE_ENCODE_WITH_IMPL_HANDLE : 0);/

# A string literal followed by a macro of a string (midl20\codegen): C++ of today reads
# the name as the suffix of a user-defined literal (also winsock2\dll\winsock2).
s/"\(FC_FIELD_PAD_MACRO\|LENGTH_VAR_NAME\|NOTIFY_SUFFIX\|PSTUB_MESSAGE_PAR_NAME\|RPC_C_INT_INFO_TYPE_NAME\|RPC_S_INT_INFO_TYPE_NAME\|RPC_STATUS_VAR_NAME\|SEQUENCE_KEY_DIGITS_STRING\|STUB_MESSAGE_VAR_NAME\|USER_MARSHAL_ROUTINE_TABLE_TYPE\|VLIST_VAR_NAME\|XMIT_AS_ROUTINE_TABLE_TYPE\)/" \1/g

# A member function named without a call as a condition (regedit/regdata.cxx, the
# share disposition of a resource; OLE's com\rot\crothint.hxx, the hint table of the
# running object table): the 1994 compiler took its address, never zero.
s/if( \([A-Za-z]*\)->\(IsResourceShare[A-Za-z]*\) )/if( \&IO_DESCRIPTOR::\2 )/
s/^    if (InitOK)$/    if (\&CCliRotHintTable::InitOK)/

# The operator new and delete of a module, static inline (shell\shelldll\shellprv.h, comdlg's
# filenew.cpp): the 1996 compiler gave each source its own; the compiler of today refuses
# them static (C2323) and makes them, inline, the one pair of the image, the same functions.
s/^static inline \(void[ *]*__cdecl operator \(new\|delete\)(\)/inline \1/

# A function type of parameters of a type not declared (gina\winlogon\logoff.h: LPRASCONNW of
# ras.h, which its sources include after it, if at all; pnp\umpnpmgr\revent.c: PCTSTR, which
# NT 4.0 has not; setup\setupapi\inflogcf.c: LOG_CONFIG, which no header declares): the 1996
# compiler took them as names, a type of no prototype, as it still is of HRASCONN of the next
# one in logoff.h; the compiler of today refuses the type names among them (C2122). No prototype.
s/^(APIENTRY \* PRASENUMCONNECTIONSW)( LPRASCONNW, LPDWORD, LPDWORD );$/(APIENTRY * PRASENUMCONNECTIONSW)();/
s/^typedef DWORD    (WINAPI \*FP_ADDTAGTOGROUPORDERLISTENTRY)(PCTSTR, DWORD, BOOL);$/typedef DWORD    (WINAPI *FP_ADDTAGTOGROUPORDERLISTENTRY)();/
s/^    IN DWORD    (\*CallbackFunc)(LOG_CONFIG,PINFCONTEXT),$/    IN DWORD    (*CallbackFunc)(),/

# A table of functions declared to return a const number (midl20\front\lex.cxx):
# the 1996 compiler ignored the qualifier of a returned number.
s/^const static token_t (\*action\[\])(void) = {$/static token_t (*action[])(void) = {/

# A local returned before it is set (ke\kiinit.c): KeInitSystem sets its status only
# on x86 (KiInitMachineDependent), and on the RISC machines the 1996 compilers returned
# what the register held; the compiler of today rejects the read (C4700). Nothing else
# it initializes can fail: TRUE.
/^KeInitSystem ($/,/^}$/s/^    BOOLEAN Initialized;$/    BOOLEAN Initialized = TRUE;/

# A local read before it is set (ntuser\kernel\loadbits.c): DestroyEmptyCursorObject tries
# the cache of the current process once, and never clears the flag of that first; the
# compiler of today rejects the read (C4700). Not tried yet: FALSE.
/^VOID DestroyEmptyCursorObject($/,/^}$/s/^    BOOL    fTriedThisProcessCache;$/    BOOL    fTriedThisProcessCache = FALSE;/

# A local read before it is set (lsa\server\dbinit.c): LsapDbInitializeServer first asks
# GetComputerNameW for the length of the name, with a size of 0 and the buffer of a string it
# sets only after; with the name in the registry that call does not touch the buffer. The
# compiler of today rejects the read (C4700). The string is empty: no buffer.
/^LsapDbInitializeServer($/,/^}$/s/^    UNICODE_STRING ComputerName;$/    UNICODE_STRING ComputerName = { 0, 0, NULL };/

# A local read before it is set (shell\comdlg\color.c): InitScreenCoords would take the first
# basic colors from a list of the display driver while i < nDriverColors, but sets that count
# to 0 and never fetches the list; the compiler of today rejects the read (C4700). No list: NULL.
/^BOOL InitScreenCoords($/,/^}$/s/^    DWORD FAR \*lpDriverRGB;$/    DWORD FAR *lpDriverRGB = NULL;/

# A local passed before it is set (setup\setupapi\inflogcf.c): pSetupProcessLogConfigSection
# hands its log configuration to pSetupProcessConfigPriority, which does not use it, before
# CM_Add_Empty_Log_Conf makes it; the compiler of today rejects the read (C4700). None yet: 0.
/^pSetupProcessLogConfigSection($/,/^}$/s/^    LOG_CONF LogConfig;$/    LOG_CONF LogConfig = 0;/

# A local stepped before it is set (rasprint\rasddui\readres.c): bGetPaperSources steps a pointer
# through the paper sources of the GPC data with its loop, and never reads it: the sources come
# from aFMBin, which GetResPtrs fills. The compiler of today rejects the read (C4700). Nothing: NULL.
/^bGetPaperSources($/,/^}$/s/^    short \*psrc;    /    short *psrc = NULL;/

# A local read before it is set (shell\control\joy\joycpl.c): regUserValsFini deletes the user
# values for the Reset button through a key it never opens; the RegDeleteValue of the 1996
# compilers failed on what the stack held, as it does on no key. The compiler of today rejects
# the read (C4700). No key: NULL.
/^static void regUserValsFini( LPJOYDATA pjd )$/,/^}/s/^    HKEY\thkey;$/    HKEY\thkey = NULL;/

# A local read before it is set (sfm\atalk\driver\zip.c): atalkZipHandleQuery keeps the zone
# count of the network before as it counts the next one, also before the first, whose count it
# then takes for both; the compiler of today rejects the read (C4700). None yet: 0.
/^atalkZipHandleQuery($/,/^}$/s/^\tULONG\t\t\ti, CurrNumZones, PrevNumZones, TotalNetCnt;$/\tULONG\t\t\ti, CurrNumZones = 0, PrevNumZones, TotalNetCnt;/

# A constructor declared again by its bare name at file scope, far to the right
# (shell\control\devapps\pcmcia\pcminfo\winrange.cpp): the 1996 compiler accepted it and made
# nothing of it, the compiler of today rejects it (C2059). Nothing.
s/^ \{20,\}WINLOOKRANGE ();$//

# A field read before it is set (ntgdi\gre\ddraw.cxx): NtGdiDdBlt lets a blt with DDBLT_DDFX
# down to the driver only for the effect DDBLTFX_NOTEARING, but does not copy the effects
# from the caller as it copies the other fields it supports; the compiler of today rejects
# the read (C4700). They are copied too.
/^NtGdiDdBlt($/,/^}$/s/^\(        BltData\.bltFX\.ddckSrcColorkey = pBltData->bltFX\.ddckSrcColorkey;\)$/\1\
        BltData.bltFX.dwDDFX          = pBltData->bltFX.dwDDFX;/

# An assignment read in the same expression (ntgdi\halftone\ht\htmath.c, U32xU32_U64): the
# 1996 compilers set xT before they compared it, the compiler of today compares first
# (C4700). The assignment first.
s/^\(            \)(QH)=(DWORD)(((xT)=(x1)+(x2)) >> 16) + (x3) +/\1(xT)=(x1)+(x2); (QH)=(DWORD)((xT) >> 16) + (x3) +/

# A field written through before it is set (net\ui\common\src\lmobj\lmobj\lmouser.cxx):
# LOCAL_USER::I_GetInfo, connecting to ADMIN$ of a share-level server, clears the local device
# name of a use_info_1 as LAN Manager 2 had it, an array; in NT it is a pointer, which the 1996
# compilers left as the stack had it; the compiler of today rejects the read (C4700). No local
# device: NULL.
/^APIERR LOCAL_USER::I_GetInfo()$/,/^}$/s/^\(        \)ui1\.ui1_local \[0\] = TCH('\\0');\(.*\)$/\1ui1.ui1_local = NULL;\2/

# A local read before it is set (srv\smbbulk.c): RestartPrepareBulkMdlWrite, retrying a failed
# compressed write uncompressed, takes what the fast path wrote off a length it sets only on
# success and does not read after (the request goes by the length of the work context); the
# compiler of today rejects the read (C4700). Not set: 0.
/^RestartPrepareBulkMdlWrite ($/,/^}/s/^    ULONG writeLength;$/    ULONG writeLength = 0;/

# Calls of two overloads (shell\fontfldr\fontext: instfls.cpp, lstrfns.cpp, pfont.cpp): the
# font folder defines its own StrCpyN and StrCmpN (lstrfns.cpp), which shlwapi.h turns into
# C++ overloads of lstrcpynW and StrCmpNW. With an int or WORD count Visual C++ 4.2 calls the
# SDK's (tested: it does not weigh the const of their string pointers against the count); the
# compiler of today finds these calls ambiguous (C2666). A const string: the SDK's function.
s/^    lstrcpyn( lpDest, lpSrc, len + 1 );$/    lstrcpyn( lpDest, (LPCTSTR) lpSrc, len + 1 );/
s/^              && StrCmpN( lpFirst, lpSrch, wLen );$/              \&\& StrCmpN( (LPCTSTR) lpFirst, lpSrch, wLen );/
s/^        if( !StrCmpN( lpLast, lpSrch, wLen ) &&$/        if( !StrCmpN( (LPCTSTR) lpLast, lpSrch, wLen ) \&\&/
s/^    lstrcpyn( szAtom, lpStr, n+1 );$/    lstrcpyn( szAtom, (LPCTSTR) lpStr, n+1 );/

# A local stepped before it is set (tdi\nbf\nbfdrvr.c): NbfInitializeOneDeviceContext counts
# its opens up and down in a local it never sets and never reads (it returns 1 or 0); the
# compiler of today rejects the read (C4700). From 0.
/^NbfInitializeOneDeviceContext($/,/^}$/s/^    UINT SuccessfulOpens;$/    UINT SuccessfulOpens = 0;/

# A local returned before it is set (tdi\isn\ipx\address.c, as in NT 3.5): IpxOpenAddressM
# returns its status when no address file can be allocated, before anything sets it; the
# compiler of today rejects the read (C4700). The status of its other allocation failure:
# STATUS_INSUFFICIENT_RESOURCES.
/^IpxOpenAddressM($/,/^}$/{
/^    AddressFile = IpxCreateAddressFile (Device);$/,/^    }$/s/^        return status;$/        return STATUS_INSUFFICIENT_RESOURCES;/
}

# A local passed before it is set (clipbook\datasrv\callback.c, and the copy in dde.c, which
# is not built): MarkShare first asks
# NDdeGetShareSecurity for the size of a share's security with a size of 0 and the descriptor
# it allocates only after (a unique pointer in the IDL: no data goes either way); the compiler
# of today rejects the read (C4700). None yet: NULL.
/^BOOL MarkShare($/,/^}/s/^PSECURITY_DESCRIPTOR pSDShare;$/PSECURITY_DESCRIPTOR pSDShare = NULL;/

# A stray + at the end of a case label (logonsrv\server\ssiapi.c, NlPackSingleDelta): the
# statement after it, Status = ..., became +Status = ...; the 1996 compilers took the unary plus
# of a variable for the variable, the compiler of today refuses the assignment (C2106). None.
s/^\(    case ChangeGroupMembership:\)[[:space:]]*+$/\1/

# An empty wide character constant (ncpa1.1\ncpa1.1\ident.cpp and netsetup\wjoin.cpp, the
# Initialize of their domain and workgroup names): the 1996 compiler made it 0, the compiler
# of today rejects it (C2137). The null character.
s/^\(        psz\(Domain\|Workgroup\)\[0\] = \)L'';$/\1L'\\0';/

# The data of #pragma data_seg(".CRT$X?") (the run-time: its initializer tables and their bounds,
# cinitexe.c __xc_a ... __xc_z) was in writable sections, as Microsoft C 2.0 put the C++
# initializers (.CRT$XCU); the compiler of today makes those read-only, and the link keeps
# sections of other attributes apart: the bounds then held none and no static constructor ran
# (ole32, rpcss). Read-only for all of them, on the same line.
s/^[[:space:]]*#[[:space:]]*pragma[[:space:]]\{1,\}data_seg[[:space:]]*([[:space:]]*\("\.CRT[$]X[A-Z]*"\)[[:space:]]*)/__pragma(section(\1, long, read)) __pragma(data_seg(\1))/

# A section name of #pragma data_seg in TEXT() (rover\filesync\syncui\port32.h DATASEG_READONLY):
# with UNICODE a wide string, which the 1996 compiler took as the name and the compiler of today
# rejects (C2002). The narrow string.
s/^\(#define[[:space:]]\{1,\}DATASEG_[A-Z]*[[:space:]]\{1,\}\)TEXT(\("[^"]*"\))/\1\2/

# The types of class templates not declared before them (rpc\locator\newservr\data\abstract.hxx:
# ULONG of windows.h and SkipStatus of skiplist.hxx, which come after it): the 1996 compiler read
# a template when it was instantiated, the compiler of today reads it where it is (C3646). The
# type ULONG is, and the enumeration by its name.
s/^\(\tvirtual \)ULONG\( size() = 0;\)$/\1unsigned long\2/
s/^\(\tvirtual \)\(SkipStatus insert(Data \*) = 0;\)$/\1enum \2/

# The static members of a class template defined with a parameter of another kind than the
# template's (rpc\locator\newservr\data\util.hxx: TCResourceCounted, of a char *, its counts with
# an int index): the 1996 compiler took it for the template's parameter, the compiler of today
# refuses the definitions (C3860). The parameter of the template.
/^template <int index>$/{
N
s/^template <int index>\nULONG TCResourceCounted<index>::/template <char * name>\nULONG TCResourceCounted<name>::/
}

# A friend of a class template, a class template named before it is declared (rpc\locator\newservr\data:
# the iterators of its safe and guarded link and skip lists): the 1996 compiler read the template when
# it was instantiated, the compiler of today refuses the name where it is (C3856). The iterator
# template declared the friend there.
s/^\([[:space:]]*\)friend class \(TC\(Safe\|Guarded\)\(Link\|Skip\)ListIterator\)<Data>;$/\1template <class> friend class \2;/

# The headers of class templates that use the classes of a header they do not include
# (rpc\locator\newservr\data\linklist.hxx and skiplist.hxx: the critical sections of mutex.hxx,
# which "must be included before the template can be instantiated"): the 1996 compiler read a
# template when it was instantiated, the compiler of today reads it where it is (C2143). The
# header included with that of their interfaces.
/^#include <abstract\.hxx>$/a\
#include <mutex.hxx>

# A destructor of a class template declared in it by its qualified name (rpc\locator\newservr\data:
# the guarded link and skip list iterators): the 1996 compiler took the qualification of its own
# class, the compiler of today reads one in a class as an override of an interface (C3254). None.
s/^\t\(TCGuarded\(Link\|Skip\)ListIterator\)::~\1() {$/\t~\1() {/

# A member function of a class template defined in it and again after it, alike (rpc\locator\
# newservr\data\skiplist.hxx, TCGuardedSkipList::remove): the 1996 compiler took the two, the
# compiler of today refuses the second (C2995). The one in the class declared there.
/^\tData \* remove(IOrderedItem \* I) $/,/^\t}$/{
s/^\(\tData \* remove(IOrderedItem \* I)\) $/\1;/
t
d
}

# The static members of a class template that only a checked build declares, defined in every build
# (rpc\locator\newservr\server\objects.hxx, the handle counts of CCompleteHandle): the 1996 compiler
# read the definitions only for a template it instantiated, the compiler of today reads them where they
# are (C2039). The counts declared in every build, as static members, which take no room in the objects.
/^class CCompleteHandle : public CNSHandle<ItemType> {$/,/^};$/{
s/^\tstatic ULONG ulHandleCount;$/#endif\n&/
s/^\tstatic ULONG ulHandleNo;$/&\n#if DBG/
}

# A macro of two parameters given three, the second empty (utils\untfs\src\mftfile.cxx: max(x, /* y */,
# 0)): the 1996 preprocessor dropped the empty argument and took max(x, 0) (tested with Visual C++ 4.2),
# the compiler of today takes the empty one (C2059). The empty argument dropped.
s|^\([[:space:]]*/\* MFT_BITMAP_INITIAL_SIZE/ClusterSize \*/\), 0);$|\1 0);|

# A member function named without a call (windows\win4help\ftsrch\tokenc.cpp: m_clsf.Initial;): the 1996
# compiler made nothing of it (tested with Visual C++ 4.2), the compiler of today refuses it (C3867). None.
s/^\([[:space:]]*\)m_clsf\.Initial;$/\1;/

# A member function named without a call in a condition (windows\spooler\printui\setup.hxx: return
# MGenericProp::bValid && _PortsLV.bValid();): the 1996 compiler took its address, never null (tested
# with Visual C++ 4.2), the compiler of today refuses it (C3867). Its address.
s/^\([[:space:]]*return \)MGenericProp::bValid && _PortsLV\.bValid();$/\1\&MGenericProp::bValid \&\& _PortsLV.bValid();/

# A function pointer of no parameters called with arguments (windows\win4help\winhelp\binding.c, the
# macros of the DLLs a help file binds: VOIDPROC called with up to 16): the 1996 compiler passed them (its
# warning C4087 turned off there), the compiler of today refuses the call (C2197). Declared without a
# parameter list, as the calls take it.
s/^typedef LONG (STDCALL \*VOIDPROC)(VOID);$/typedef LONG (STDCALL *VOIDPROC)();/
