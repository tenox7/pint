/***
*excpt.h - exception values, types, and the SEH keyword mapping (ARM32 port)
*
*	Derived from PUBLIC/SDK/INC/CRT/EXCPT.H (Microsoft, 1990-1993). The only
*	change is a new `defined(_ARM_)` branch in the two arch ladders, added the
*	same way NT added each new architecture. Per the project rule, this is a
*	copy under arcfw/inc/ (never edit PUBLIC/); it is -I'd ahead of the build's
*	header farm, so it supersedes the farmed EXCPT.H for the _ARM_ kernel build.
*	(The loader build never includes excpt.h, so this file is kernel-only.)
*
*Purpose:
*	NT's portable executive writes the lowercase macros try/except/finally/leave
*	(EXCPT.H maps them per-arch: x86 -> __try, MIPS/Alpha -> __builtin_try). There
*	is no ARM32 compiler that codegens MSVC SEH (clang refuses 32-bit ARM; gcc has
*	no __try), so the _ARM_ branch maps them to a *neuter*: the try body runs, the
*	handler is dead, finally runs on normal exit, leave exits the try. This lets
*	all ~440 executive files compile unmodified under arm-linux-gnueabihf-gcc.
*	What it does NOT do is recover from a fault or run finally on an abnormal exit.
*	Files that need genuine fault recovery are converted to the explicit _SEH_*
*	macros + the setjmp runtime in kiseh.h / ke/seh.c (see TOOLCHAIN-FINDINGS.md).
*
****/

#ifndef _INC_EXCPT

#ifdef __cplusplus
extern "C" {
#endif

#if   ( (_MSC_VER >= 800) && (_M_IX86 >= 300) )
#define _CRTAPI1 __cdecl
#define _CRTAPI2 __cdecl
#else
#define _CRTAPI1
#define _CRTAPI2
#endif


/*
 * Exception disposition return values.
 */
typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind
} EXCEPTION_DISPOSITION;


/*
 * Prototype for the language-specific SEH support handler.
 */

#ifdef	_M_IX86

struct _EXCEPTION_RECORD;
struct _CONTEXT;

EXCEPTION_DISPOSITION _CRTAPI2 _except_handler (
	struct _EXCEPTION_RECORD *ExceptionRecord,
	void * EstablisherFrame,
	struct _CONTEXT *ContextRecord,
	void * DispatcherContext
	);

#elif defined(_M_MRX000) || defined(_M_ALPHA)

typedef struct _EXCEPTION_POINTERS *Exception_info_ptr;
struct _EXCEPTION_RECORD;
struct _CONTEXT;
struct _DISPATCHER_CONTEXT;

EXCEPTION_DISPOSITION __C_specific_handler (
	struct _EXCEPTION_RECORD *ExceptionRecord,
	void *EstablisherFrame,
	struct _CONTEXT *ContextRecord,
	struct _DISPATCHER_CONTEXT *DispatcherContext
	);

#elif defined(_ARM_)

/*
 * ARM32: the language-specific handler shape mirrors the RISC ports (a stub in
 * ke/seh.c satisfies the link; the setjmp dispatcher in kiseh.h is what actually
 * unwinds). The runtime accessors below back GetExceptionCode and
 * GetExceptionInformation for BOTH the neuter (where they sit in dead handler
 * code) and the real _SEH_ macros.
 */

typedef struct _EXCEPTION_POINTERS *Exception_info_ptr;
struct _EXCEPTION_RECORD;
struct _CONTEXT;
struct _DISPATCHER_CONTEXT;

EXCEPTION_DISPOSITION __C_specific_handler (
	struct _EXCEPTION_RECORD *ExceptionRecord,
	void *EstablisherFrame,
	struct _CONTEXT *ContextRecord,
	struct _DISPATCHER_CONTEXT *DispatcherContext
	);

unsigned long KiSehExceptionCode(void);
void *        KiSehExceptionInformation(void);

#endif


/*
 * Keywords and intrinsics for SEH.
 */

#if	( _MSC_VER >= 800 )
#define try				__try
#define except				__except
#define finally 			__finally
#define leave				__leave
#define GetExceptionCode() (_exception_code())
#define exception_code()   (_exception_code())
#define GetExceptionInformation() ((struct _EXCEPTION_POINTERS *)_exception_info())
#define exception_info()          ((struct _EXCEPTION_POINTERS *)_exception_info())
#define AbnormalTermination()  (_abnormal_termination())
#define abnormal_termination() (_abnormal_termination())

unsigned long _CRTAPI1 _exception_code(void);
void *	      _CRTAPI1 _exception_info(void);
int	      _CRTAPI1 _abnormal_termination(void);

#elif defined(_M_MRX000) || defined(_M_ALPHA)
#define try				__builtin_try
#define except				__builtin_except
#define finally 			__builtin_finally
#define leave				__builtin_leave
#define GetExceptionCode()		__exception_code
#define exception_code()		__exception_code
#define GetExceptionInformation()       (struct _EXCEPTION_POINTERS *)__exception_info
#define exception_info()                (struct _EXCEPTION_POINTERS *)__exception_info
#define AbnormalTermination()		__abnormal_termination
#define abnormal_termination()		__abnormal_termination

extern unsigned long __exception_code;
extern int	     __exception_info;
extern int	     __abnormal_termination;

#elif defined(_ARM_)

/*
 * ARM32 neuter (terminator-free, so unmodified NT source compiles):
 *
 *   try { B } except(F) { H }   ->  do { B } while(0); if(0) { H }
 *   try { B } finally  { Fin }  ->  do { B } while(0);       { Fin }
 *   leave;                      ->  break;            (exits the do-while = the try)
 *
 * The do/while(0) gives `leave` a target without needing a trailing token, and
 * the source's own braces keep the nesting balanced. The except() filter F is a
 * macro argument that is dropped (the handler is unreachable in the neuter), so
 * a filter calling GetException* is never emitted; GetException* appearing inside
 * a (dead) handler body still compiles via the accessors above.
 *
 * AbnormalTermination() is 0 here because a neuter finally only ever runs on the
 * normal path. Real fault recovery + abnormal finally are the _SEH_* macros.
 */
#define try				do
#define except(filter)			while(0); if(0)
#define finally				while(0);
#define leave				break

/* the one file that spells the keywords raw maps the same way. */
#define __try				do
#define __except(filter)		while(0); if(0)
#define __finally			while(0);
#define __leave				break

#define GetExceptionCode()		(KiSehExceptionCode())
#define exception_code()		(KiSehExceptionCode())
#define GetExceptionInformation()	((struct _EXCEPTION_POINTERS *)KiSehExceptionInformation())
#define exception_info()		((struct _EXCEPTION_POINTERS *)KiSehExceptionInformation())
#define AbnormalTermination()		(0)
#define abnormal_termination()		(0)

#endif


/*
 * Legal values for the expression in except().
 */

#define EXCEPTION_EXECUTE_HANDLER	 1
#define EXCEPTION_CONTINUE_SEARCH	 0
#define EXCEPTION_CONTINUE_EXECUTION	-1


#ifdef __cplusplus
}
#endif

#define _INC_EXCPT
#endif	/* _INC_EXCPT */
