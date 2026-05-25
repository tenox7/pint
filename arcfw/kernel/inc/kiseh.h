/*++

Module Name:  kiseh.h

Abstract:

    Genuine structured-exception handling for the ARM32 NT port, backed by a
    setjmp/longjmp frame chain (the ReactOS-PSEH approach, faithful semantics -
    NOT the if(1)/if(0) neuter in excpt.h). No ARM32 compiler codegens MSVC SEH,
    so executive files that need real fault recovery are converted to the explicit
    macros below; see ARM32/TOOLCHAIN-FINDINGS.md for the decision.

        _SEH_TRY
            <guarded body>
        _SEH_EXCEPT(<filter>)
            <handler>                 // runs iff filter == EXCEPTION_EXECUTE_HANDLER
        _SEH_END_EXCEPT

        _SEH_TRY_FINALLY
            <guarded body>
        _SEH_FINALLY
            <termination handler>     // runs on BOTH normal and abnormal exit
        _SEH_END_FINALLY

        _SEH_LEAVE;                    // jump to the end of the guarded body

    A raise (ExRaiseStatus, or a CPU fault delivered through ke/trap.S) walks the
    frame chain: each _SEH_EXCEPT evaluates its filter; _SEH_FINALLY frames in the
    unwound range run with AbnormalTermination() == TRUE; an unhandled raise ends
    in KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED) - NT's real outcome.

    Limitations (documented; sufficient for kernel bring-up): no
    EXCEPTION_CONTINUE_EXECUTION (the setjmp model cannot resume the faulting
    instruction - a continue-execution filter is treated as continue-search); the
    captured ContextRecord is minimal (code only). The frame chain head is a
    uniprocessor global; it moves to the PCR with SMP.

--*/

#ifndef _KISEH_H_
#define _KISEH_H_

#include <excpt.h>      /* EXCEPTION_DISPOSITION, EXCEPTION_EXECUTE_HANDLER, KiSehExceptionCode */

/* r4-r11, sp, lr - the AAPCS callee-saved set plus the stack/return, saved by
   KiSehSetjmp and restored by KiSehLongjmp (ke/seh.S). */
typedef struct _KSEH_JMPBUF {
    unsigned long Reg[10];
} KSEH_JMPBUF;

typedef struct _KSEH_FRAME {
    struct _KSEH_FRAME *Prev;
    KSEH_JMPBUF Buf;
    unsigned long Code;     /* exception code delivered to this frame */
    int Kind;               /* 0 = try/except, 1 = try/finally */
    int Abnormal;           /* finally: reached via unwind (vs normal fall-through) */
} KSEH_FRAME;

int  KiSehSetjmp(KSEH_JMPBUF *Buf) __attribute__((returns_twice));
void KiSehLongjmp(KSEH_JMPBUF *Buf, int Value) __attribute__((noreturn));
void KiSehRaise(unsigned long Code) __attribute__((noreturn));

extern KSEH_FRAME *KiSehTopFrame;   /* uniprocessor frame-chain head (-> PCR with SMP) */

/* The guarded body is wrapped in do{}while(0) so _SEH_LEAVE can be `break`
   (no per-site label, so nesting is safe); the source braces keep the rest
   balanced. KiSehSetjmp==0 is the body pass; a longjmp returns non-zero into
   the else arm (filter for except, abnormal-set for finally). */
#define _SEH_TRY \
    { KSEH_FRAME _sf; _sf.Kind = 0; _sf.Abnormal = 0; _sf.Code = 0; \
      _sf.Prev = KiSehTopFrame; KiSehTopFrame = &_sf; \
      if (KiSehSetjmp(&_sf.Buf) == 0) { do {

#define _SEH_EXCEPT(filter) \
          } while (0); KiSehTopFrame = _sf.Prev; \
      } else { \
          KiSehTopFrame = _sf.Prev; \
          if ((filter) == EXCEPTION_EXECUTE_HANDLER) {

#define _SEH_END_EXCEPT \
          } else KiSehRaise(_sf.Code); } }

#define _SEH_TRY_FINALLY \
    { KSEH_FRAME _ff; _ff.Kind = 1; _ff.Abnormal = 0; _ff.Code = 0; \
      _ff.Prev = KiSehTopFrame; KiSehTopFrame = &_ff; \
      if (KiSehSetjmp(&_ff.Buf) == 0) { do {

#define _SEH_FINALLY \
          } while (0); KiSehTopFrame = _ff.Prev; \
      } else { _ff.Abnormal = 1; KiSehTopFrame = _ff.Prev; } \
      {

#define _SEH_END_FINALLY \
      } if (_ff.Abnormal) KiSehRaise(_ff.Code); }

/* leave the guarded body of the nearest _SEH_TRY[_FINALLY]; handler/finally
   still run. break exits the do{}while(0) wrapping the body. */
#define _SEH_LEAVE   break

/* In a converted file, AbnormalTermination() must read the live finally frame
   (excpt.h's neuter hard-codes 0). Valid only inside a _SEH_FINALLY block. */
#undef  AbnormalTermination
#define AbnormalTermination()   (_ff.Abnormal)

#endif /* _KISEH_H_ */
