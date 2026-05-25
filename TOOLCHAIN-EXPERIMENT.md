# Toolchain experiment — a "true port" compiler for NT 3.5 → ARM32 (the SEH wall)

**For:** a subagent running experiments. **Goal:** decide which toolchain lets us compile NT 3.5's
**unmodified MSVC C source** for **ARM32**, especially the structured exception handling
(`__try` / `__except` / `__finally` / `__leave`) the executive uses pervasively — ideally emitting
**PE/COFF** (NT's native object format). Report findings + a recommendation; **do not change the port yet.**

---

## Why this matters (context)

- Phase 1 (ARC firmware emulator + NT OS Loader) and the KE/ARM kernel-arch glue are done; the real
  `KiInitializeKernel` runs on ARM/QEMU. The next layer is the **portable executive** (Ex/Mm/Ob/Ps/Io/Se/Cm),
  which is genuinely portable C — EXCEPT it uses MSVC SEH in ~93 files.
- Our current compiler, **`arm-linux-gnueabihf-gcc`** (in Docker image `arc-rpi-build:latest`), compiles the
  portable code fine but **cannot compile `__try`/`__except`** — those are MSVC language extensions. This is
  THE blocker for "compile the executive as-is."
- We refuse the cheap shortcut (`#define try if(1)` / `#define except(x) if(0)`): it compiles but silently
  breaks fault recovery and probing. We want a **real** mechanism.
- NT source (READ-ONLY, never modify): `/Volumes/Data/Software/sources/windows/nt35/PRIVATE/NTOS`.
  `try`/`except`/`finally`/`leave` `#define` to `__try`/`__except`/`__finally`/`__leave` in
  `PUBLIC/SDK/INC/CRT/EXCPT.H`. SEH-heavy dirs: `PRIVATE/NTOS/{EX,MM,OB,PS,IO,SE,CONFIG}`.
- **Important framing:** there was NEVER an NT 3.5 ARM compiler or ABI (ARM became an NT target only ~2012,
  Windows-on-ARM / WinRT, which is **Thumb-2 + the Windows ARM ABI**). So "true port" does **not** mean
  period-accuracy — it means *compile the unmodified C (incl. SEH) with a real MSVC-compatible compiler for
  ARM*. Any ARM target is a modern ABI.
- The **SEH runtime** (`RtlDispatchException`, `RtlVirtualUnwind`, the language-specific handler) is OUR code
  regardless of compiler; the compiler only decides the **unwind-table format** we must then support. Note
  which format each toolchain emits.
- Bonus prize: a real PE/COFF toolchain removes our `arcfw/kernel/mkpe.py` hand-built-PE hack (binutils has
  no PE backend).

**Work in `/tmp` (or a scratch dir). Do not modify `PRIVATE/`, `PUBLIC/`, or the `ARM32/` tree.** Docker is
available; QEMU not needed for this. The kernel header-farm method (for compiling real NT files against the
full `ntos.h` chain) is in `ARM32/arcfw/kernel/make-ktest.sh` — reuse it if you compile real NT sources.

---

## The standard SEH test file

Use this as the gate (write to `/tmp/seh_test.c`). It exercises `__try`/`__except` with a filter
expression, `__finally`, and nesting — representative of NT executive code:

```c
typedef unsigned long ULONG;
typedef long LONG;
#define EXCEPTION_EXECUTE_HANDLER  1
#define EXCEPTION_CONTINUE_SEARCH  0

ULONG ExpProbeRead(ULONG *Address) {
    ULONG value = 0;
    __try { value = *Address; }
    __except (EXCEPTION_EXECUTE_HANDLER) { value = 0xDEADBEEF; }
    return value;
}

LONG ExpFilter(ULONG code) {
    return (code == 0xC0000005) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

ULONG ExpWithFinally(ULONG *p) {
    ULONG r = 0;
    __try {
        __try { r = *p; }
        __finally { r += 1; }
    } __except (ExpFilter(0xC0000005)) { r = 0xBADC0DE; }
    return r;
}
```

"Accepts SEH" = compiles with no syntax error AND emits an object (inspect with `llvm-objdump`/`objdump`/
`file`). Bonus: note whether SEH unwind data is emitted (`.pdata`/`.xdata`, or ARM `.pdata` SEH tables).

---

## Experiments (priority order)

### EXP 1 — clang targeting `*-windows-msvc` ARM (cross-platform; do this FIRST, runs in Docker now)

clang implements MSVC SEH via `-fms-extensions` and can emit PE/COFF. Get a recent clang (try image
`silkeh/clang:latest`, else `apt-get install -y clang lld llvm` in a `debian:bookworm` container).

For each triple `thumbv7-unknown-windows-msvc`, `armv7-unknown-windows-msvc`, `arm-pc-windows-msvc`:
```sh
clang --target=<triple> -fms-extensions -fms-compatibility -ffreestanding -O1 -c /tmp/seh_test.c -o /tmp/t.o
file /tmp/t.o ; llvm-objdump -h /tmp/t.o   # COFF/PE? ARM/Thumb? .pdata/.xdata?
llvm-objdump -d /tmp/t.o | head -60        # did it actually codegen SEH (not just parse)?
```
- Does it ACCEPT `__try`/`__except`/`__finally`? Does it CODEGEN (real unwind), or warn that SEH is
  unsupported for the target? (clang's SEH is solid on x64/arm64; **32-bit ARM-Windows is the open question —
  verify, don't assume.**)
- If 32-bit ARM-Windows SEH is incomplete, ALSO record: does `aarch64-unknown-windows-msvc` work fully? (a
  64-bit ARM port is a fallback worth knowing about.)
- Then try a REAL NT file: pick a small executive file using try/except
  (`grep -rl '__try\|^    try {' /work/PRIVATE/NTOS/EX/*.C` inside the kernel-header-farm Docker from
  make-ktest.sh) and compile it against the chain with clang's windows-msvc triple. Report error classes.
- **Report:** clang version; which triple(s) accept+codegen SEH for 32-bit ARM; object format; unwind
  sections; whether a real NT EX file compiles; representative errors.

### EXP 2 — gcc / MinGW: does any gcc-family toolchain do MSVC `__try`?

- Confirm our `arm-linux-gnueabihf-gcc` rejects `__try` (it will) — capture the exact error.
- Investigate whether ANY gcc/MinGW accepts the MSVC `__try`/`__except` **syntax**. (MinGW is gcc underneath;
  mingw-w64 has SEH *unwinding* for x64 via libgcc, but does NOT implement the `__try`/`__except` *keywords* —
  verify and document precisely. Check `-fseh-exceptions`, `__SEH__`, any ARM relevance.)
- Is there a "true ARM MinGW" at all? (MinGW targets Windows x86/x64; a 32-bit-ARM-Windows gcc is unusual —
  find out if one exists and whether it does SEH.)
- **Report:** definitive yes/no on gcc-family + MSVC `__try` syntax, with evidence.

### EXP 3 — Substitute `__try`/`__except` with a REAL mechanism (PSEH / setjmp), not the shortcut

The user asked: can we substitute SEH with something else? The known-good answer is **ReactOS PSEH**
("Portable Structured Exception Handling") — ReactOS compiles an entire NT-clone kernel's SEH code with gcc
using `_SEH2_TRY` / `_SEH2_EXCEPT` / `_SEH2_FINALLY` macros backed by a real frame-based/`__builtin_setjmp`
mechanism (runs the filter, dispatches to the handler, unwinds `__finally`). This is the faithful
"substitute" path (genuine semantics — NOT `#define try if(1)`).
- Research how PSEH (PSEH2/PSEH3) works and what it requires. KEY question: does it need **source changes**
  (`__try` → `_SEH2_TRY` + a `_SEH2_END;` terminator), or can it be driven by macros over the existing
  `try`/`except` spellings (via `EXCPT.H`)? Estimate the substitution effort across ~93 NT files.
- Assess a minimal `__builtin_setjmp`-based `__try`/`__except`/`__finally` macro layer: can it faithfully
  evaluate the filter expression, support `GetExceptionCode`/`GetExceptionInformation`, run `__finally` on
  normal AND abnormal exit, and `__leave`? Where does it fall short of real MSVC SEH (async exceptions, exact
  unwind)? For a KERNEL, how much fidelity is actually required?
- Note: this path keeps our **existing gcc/ARM toolchain** (no PE/COFF, no Thumb switch) — its cost is the
  source substitution + the runtime, not a new compiler.
- **Report:** is PSEH adaptable to NT 3.5 source for ARM-gcc? what substitution is required? fidelity vs.
  effort? Could the substitution be mostly macro-driven from `EXCPT.H` (minimizing per-file edits)?

### EXP 4 — MSVC ARM (`cl.exe -arm`) — the truest; NEEDS the user's remote ARM32 MSVC (RSH/SSH)

The user can provide RSH/SSH access to a Windows host with **VS2012–2015 ARM** (`cl.exe`/`link.exe`, the
WinRT/WoA toolset) — the **real Microsoft compiler**: native SEH, real PE/COFF. **You probably cannot run
this yet** (access pending). Prepare the plan + commands; run only if access is provided:
```
cl.exe /c /arm /Od /GS- seh_test.c            # does it accept the SEH? object format?
```
- The hard part is impedance: a 2012–2015 C++ compiler vs. 1994 NT C source (old pragmas/intrinsics, K&R-isms,
  the old SEH/unwind model, `_asm`). Compile `seh_test.c` first, then a real NT EX file (expect many
  header/pragma errors — categorize them). Note: Thumb-2 + Windows-ARM ABI.
- **Report (when access lands):** does `cl.exe -arm` compile NT's SEH source? error classes? object format?
  is the 1994-source-vs-2015-compiler gap manageable?

---

## Deliverable

Write findings to **`/tmp/toolchain-findings.md`** AND return a concise summary. Include a comparison table:

| Toolchain | Accepts MSVC `__try`/`__except`? | Codegens SEH for ARM32? | Emits PE/COFF? | Runs in our Docker? | Source changes needed? | Unwind format |
|---|---|---|---|---|---|---|
| clang `*-windows-msvc` (armv7/thumbv7) | | | | | | |
| clang `aarch64-windows-msvc` (fallback) | | | | | | |
| arm-linux-gnueabihf-gcc (current) | no | n/a | no | yes | — | — |
| gcc/MinGW (any) | | | | | | |
| PSEH / setjmp substitution (gcc) | via macros | n/a (gcc) | no | yes | yes (`__try`→macro) | own |
| MSVC `cl.exe -arm` (remote) | | | | no (remote) | | |

End with a **recommendation**: the truest path that's practically buildable, and the trade-offs (PE/COFF +
real SEH vs. setup cost; Thumb-2/WoA-ABI shift vs. staying gcc/ARM-mode; per-file source substitution vs. a
new compiler). Flag anything that changes the loader/kernel ABI (ARM-mode→Thumb-2, AAPCS→Windows-ARM).
