# Toolchain findings — compiling NT 3.5's MSVC SEH for ARM32 (the executive wall)

**Date:** 2026-05-25 · **Author:** subagent (experiments run in Docker on this host)
**Question:** which toolchain lets us compile NT 3.5's unmodified MSVC C executive — especially
`try`/`except`/`finally`/`leave` (SEH) — for the ARM32 Raspberry Pi 2 target, ideally emitting PE/COFF.

> **DECISION (2026-05-25, ratified by tenox): Path 1 — the `__builtin_setjmp` PSEH layer on
> `arm-linux-gnueabihf-gcc`.** Keep ARMv7/AAPCS/ARM-mode and the entire existing port; add an
> `EXCPT.H` `_ARM_` branch + a setjmp-based SEH runtime hooked into `ke/trap.S`. Stage it:
> zero-edit neuter to compile the executive first, convert hot probe paths to real fault recovery
> later. `cl.exe /arm` (Thumb-2, real PE/COFF) and the aarch64/Pi-3 pivot were considered and
> deferred — see the recommendation + table below for why.
>
> **IMPLEMENTED + QEMU-verified (2026-05-25).** The `_ARM_` neuter (`arcfw/inc/excpt.h`) compiles
> unmodified executive SEH files (6/6 sampled). The real setjmp runtime (`arcfw/kernel/inc/kiseh.h`,
> `ke/seh.c`, `ke/seh.S`) + the `ke/trap.S` data/prefetch-abort wiring pass an in-kernel self-test on
> `raspi2b`: software raise (`ExRaiseStatus`+filter), hardware data-abort recovery, nested try/finally
> with correct `AbnormalTermination`, and `__leave` — frame chain balanced, kernel resumes (clock ticks
> on). See "Implementation" at the end of this file.

---


## TL;DR recommendation

**For the actual target (BCM2836 / Cortex-A7 / QEMU `raspi2b` = 32-bit ARMv7-A only), no off-the-shelf
free compiler codegens MSVC SEH.** clang explicitly refuses it on 32-bit ARM; gcc has no `__try` at all.
The two real options are:

1. **RECOMMENDED — a `__builtin_setjmp` SEH layer, keep `arm-linux-gnueabihf-gcc` (ARMv7/AAPCS/ARM-mode).**
   Proven here: a small PSEH-style runtime gives *faithful* semantics (filter eval, nested
   try/finally, `__finally` on normal+abnormal exit, `AbnormalTermination`, `__leave`). It keeps the
   **entire existing port** (loader + KE/ARM + trap/clock layer) — **no ABI break, no new compiler, runs in
   the current Docker**. The SEH runtime is ours regardless of toolchain and plugs straight into the
   existing `ke/trap.S` data-abort path. **Cost:** one `EXCPT.H` `_ARM_` branch + the runtime, **plus** a
   per-construct terminator (a real cost — see EXP 3; can be staged via a zero-edit neuter first).

2. **TRUEST for 32-bit, but heavy — MSVC `cl.exe /arm` (remote, pending).** The *only* toolchain that
   natively codegens 32-bit-ARM SEH (Thumb-2, as shipped in Windows RT). Gives real PE/COFF too (kills
   `mkpe.py`). **But** it shifts the whole port from ARM-mode/AAPCS → **Thumb-2 + Windows-ARM ABI**, needs
   remote access, and faces 1994-source-vs-2015-compiler impedance. A strategic pivot, not a drop-in.

**Do NOT pivot to clang `aarch64-windows-msvc` for SEH** unless you also abandon the Pi 2: clang's real
ARM SEH is **64-bit only**, and the Pi 2 (Cortex-A7) has no AArch64 execution state. That path means
re-targeting to Pi 3 / `raspi3b` and rewriting the entire ARMv7 arch layer for ARMv8/AArch64.

---

## Comparison table

| Toolchain | Accepts MSVC `try`/`except`? | Codegens SEH for **ARM32**? | Emits PE/COFF? | Runs in our Docker? | Source changes? | Unwind format | Runs on Pi 2 (A7)? |
|---|---|---|---|---|---|---|---|
| clang `*-windows-msvc` **armv7/thumbv7** | **NO** — hard error | **NO** (LLVM has no ARM32-Win SEH) | yes (COFF) | yes (`silkeh/clang`) | — | — | yes |
| clang `aarch64-windows-msvc` (fallback) | **YES** | n/a (it's 64-bit) — **real ARM64 SEH** | **yes** (ARM64 COFF + `.pdata`/`.xdata`) | yes | none for SEH | MSVC ARM64 | **NO** (needs ARMv8) |
| `arm-linux-gnueabihf-gcc` (current) | no (`__try` undeclared) | n/a | no | yes | — | — | yes |
| gcc / MinGW (any flavor) | **NO** (no `__try` keyword in any gcc) | no | x64/aarch64 yes; arm32 no | — | — | — | — |
| llvm-mingw (`*-w64-windows-gnu`) armv7 | **NO** (same LLVM refusal) | **NO** | yes | yes | — | — | yes |
| **`__builtin_setjmp` PSEH layer (gcc)** | **via 1 header** (`EXCPT.H`) | n/a (portable C) — **faithful semantics, proven** | no (stays ELF→`mkpe.py`) | **yes** | **yes — per-construct terminator** | ours (frame list) | **yes** |
| **MSVC `cl.exe /arm` (remote)** | **YES** (native) | **YES** — the only one | **yes** (Thumb-2 PE/COFF) | no (remote) | pragma/intrinsic impedance | MSVC ARM (Thumb-2) | **yes** (Thumb-2) |

---

## The decisive context finding (shapes everything)

NT's executive **never writes `__try` directly** — it writes lowercase `try`/`except`/`finally`/`leave`,
which `PUBLIC/SDK/INC/CRT/EXCPT.H` maps per-arch. Measured in `PRIVATE/NTOS`:

- **440 files use `try {`; only 1 uses raw `__try`.** → the SEH *spelling/implementation is centralized in
  one header*. Swapping the SEH mechanism is **~1 header edit**, not 440.
- `EXCPT.H` maps: x86 → `__try`/…; **MIPS/Alpha → `__builtin_try`/`__builtin_except`/`__builtin_finally`**
  (the RISC compilers' *built-ins*, not `__try`!). **There is no `_ARM_` branch**, so for our build
  `try`/`except`/`finally`/`leave` currently expand to **nothing** → bare-identifier syntax errors.
  *We own that branch* and decide what they become.
- Filter/finally complexity is real (so a faithful substitute can't cut corners): **293** files call
  `GetExceptionCode`, **130** `GetExceptionInformation`, **121** `AbnormalTermination`, 4 use `leave`.

**SEH is the *sole* language blocker.** Proof: the project's own header farm (`make-ktest.sh`) already
compiles the full `ntos.h` chain for `_ARM_`. With `exp.h` added and `DBG=1`, the real executive file
`EX/LUID.C` compiles and the **only** errors are `'try' undeclared` + its downstream `expected ';'` — every
other construct (headers, types, intrinsics) resolves. (One cosmetic warning from an unused x86 `_asm`
macro in `ntrtl.h`, already handled by the project's `_ARM_` int64 branches.)

---

## EXP 1 — clang `*-windows-msvc` ARM (image `silkeh/clang:latest`, **clang 21.1.8**)

Test: gate file `seh_test.c` (`__try`/`__except` w/ filter, `__finally`, nesting),
`clang --target=<triple> -fms-extensions -fms-compatibility -ffreestanding -O1 -c`.

| triple | result |
|---|---|
| `thumbv7-unknown-windows-msvc` | **`error: SEH '__try' is not supported on this target`** |
| `armv7-unknown-windows-msvc`   | **same error** |
| `arm-pc-windows-msvc`          | **same error** |
| `armv7-w64-windows-gnu` (llvm-mingw) | **same error** (clang underneath) |
| `aarch64-unknown-windows-msvc` | **OK** — ARM64 COFF, real `.pdata`+`.xdata` |
| `aarch64-w64-windows-gnu` (llvm-mingw) | **OK** — ARM64 COFF, real `.pdata`+`.xdata` |

- **No flag rescues 32-bit ARM.** `-fasync-exceptions` → still "SEH not supported"; `-fseh-exceptions` →
  `error: invalid exception model 'seh' for target 'thumbv7-...'`. This is a hard LLVM front-end gate: LLVM
  implements Windows SEH only for x64, x86, and **arm64** — never 32-bit ARM (even though Windows RT shipped
  it). The user's links (llvm-mingw #82, mingw-woarm64, Linaro MinGW) are all about *AArch64* Windows — they
  do **not** add 32-bit-ARM SEH.
- **aarch64 SEH is genuine, not parsed-and-dropped.** Disassembly shows real MSVC SEH funclets:
  `?filt$0@0@ExpWithFinally@@` (filter), `?fin$0@0@ExpWithFinally@@` (finally), `$ehgcr` continuation, plus
  populated `.xdata`/`.pdata`. Identical in shape to MSVC.
- **Real NT file:** not run under aarch64-msvc because the result is moot for this hardware (see below) and
  the source-impedance it would surface is the same class `cl.exe` faces (EXP 4).

**Verdict:** clang gives real MSVC SEH **only on 64-bit ARM**. Unusable on Cortex-A7.

### The hardware constraint that gates the aarch64 option

The stated target is **BCM2836 (Pi 2) = 4× Cortex-A7 = ARMv7-A, AArch32 only**; QEMU `raspi2b` models exactly
this. **Cortex-A7 cannot execute AArch64.** So clang's aarch64 SEH can't run on the target at all. Using it
means re-targeting to **Pi 3 / BCM2837 / Cortex-A53 (`raspi3b`)** and re-porting the whole arch layer
(`start.S`, `ke/*.S`, `ntarm.h`/`arm.h`, traps, MMU) from ARMv7/AArch32 to ARMv8/AArch64 — a different
register file, exception model, page-table format, and ABI. That discards the existing 32-bit KE/ARM work.

---

## EXP 2 — gcc / MinGW: does any gcc-family compiler do MSVC `__try`?

- **`arm-linux-gnueabihf-gcc 12.2.0` (our `arc-rpi-build`): rejects `__try`** — `'__try' undeclared`, then a
  parse error. No `__SEH__` macro; `-fseh-exceptions` is unrecognized. As expected.
- **No gcc-family front-end parses the `__try`/`__except` *keywords* on any target.** mingw-w64 gcc has SEH
  *unwinding runtime* for **x86_64** (libgcc `_GCC_specific_handler`, `.pdata`/`.xdata`), and its `<excpt.h>`
  ships inline-asm scope macros `__try1`/`__except1` plus `__C_specific_handler` — **but those are manual
  asm frame macros, not the language keywords**, and cannot wrap unmodified NT `try { } except() { }`.
- **"ARM MinGW"** exists only as **llvm-mingw** (clang) and the **woarm64/Linaro GNU** toolchains, and both
  are **AArch64**. The clang flavor inherits the 32-bit-ARM SEH refusal (EXP 1). A GNU (gcc) AArch64-Windows
  toolchain would give PE/COFF but still **no `__try`** (gcc front-end limitation).
- **`__builtin_setjmp`/`__builtin_longjmp` compile fine on `arm-linux-gnueabihf-gcc`** — the primitive for
  EXP 3 is available on the real target. (clang on this host rejects `__builtin_setjmp`, so the EXP-3 *demo*
  used portable `setjmp`; the mechanism is identical and the lighter builtin is what we'd use on gcc/ARM.)

**Verdict:** definitive **no** for gcc-family + MSVC `__try`. But it *does* provide the setjmp primitive.

---

## EXP 3 — a real substitute: `__builtin_setjmp` PSEH-style layer (not the `if(1)/if(0)` neuter)

**Built and ran a faithful runtime** (`seh_rt.{h,c}` + `tst_pseh.c`, run natively in-container). A per-frame
registration list (the analog of the kernel's exception-registration chain) + setjmp/longjmp, with a
`SehRaise` that models `KiDispatchException`: walks frames, evaluates the filter in the faulting context,
runs intervening `__finally` blocks during unwind, and dispatches to the handler. **Output (all correct):**

```
1. probe(NULL)=deadbeef (want deadbeef)  probe(&42)=42 (want 42)
2. fault:   r=badc0de (want badc0de)  finallyRan=1  abnormal=1 (want 1,1)
3. nofault: r=8       (want 8)        finallyRan=1  abnormal=0 (want 1,0)
```

So a setjmp substitute can **faithfully** support: handler dispatch on fault, filter evaluation
(`GetExceptionCode`), **nested** try/finally inside try/except, `__finally` on both normal and abnormal exit,
correct `AbnormalTermination`, and `__leave`. This is genuine semantics, **not** the rejected
`#define try if(1)` neuter.

### ReactOS PSEH for reference
ReactOS compiles an NT-clone kernel's SEH under gcc via `_SEH2_TRY`/`_SEH2_EXCEPT`/`_SEH2_FINALLY` +
`_SEH2_END;`. **PSEH2** = GCC nested functions (filter/handler lifted to funclets) + a registration frame;
**PSEH3** = a setjmp/trampoline variant reducing nested-function reliance. Both are real frame-based
mechanisms — and **both require source changes**: `try`→`_SEH2_TRY` *and a `_SEH2_END;` terminator per
construct*. ReactOS's ARM PSEH support historically lagged (often the neuter), which is *not* what we want.

### The catch I proved: it **cannot** be a zero-edit, pure-`EXCPT.H` swap
Terminator-free macros over NT's exact `try { } except() { }` spelling **fail to compile** — the `try` macro
must open a brace (`if(setjmp(...)){`) that has no matching close without a trailing token, so the function's
braces unbalance (gcc: `expected declaration or statement at end of input`). A faithful (or even *compilable*)
substitute therefore needs a **terminator token after every construct**, exactly like ReactOS's `_SEH2_END;`.

**Edit surface if going full-PSEH** (whole `PRIVATE/NTOS`): **2423 `try` sites / 1353 `except` / 1090
`finally` across 440 files**. The executive bring-up subset (EX/MM/OB/PS/IO/SE/CONFIG/RTL): ~**596 try-sites
across ~110 files**:

| dir | try-sites | files |   | dir | try-sites | files |
|---|---|---|---|---|---|---|
| EX | 92 | 20 | | IO | 79 | 16 |
| MM | 88 | 22 | | SE | 85 | 12 |
| OB | 39 | 7  | | CONFIG | 33 | 6 |
| PS | 78 | 10 | | RTL | 102 | 17 |

### Staging that avoids the up-front edit cost (the pragmatic play)
Because the mapping is one header, you can **stage** it:
- **Phase 0 (zero edits):** an `EXCPT.H` `_ARM_` branch that neuters (`try`→`if(1)`, `except(f)`→`if(0)`,
  `finally`→run-inline, `leave`→`goto`). Compiles & links all 440 files **immediately**; runs every
  non-faulting path correctly. The only thing missing is *fault recovery* — acceptable for early bring-up of
  a research kernel that isn't yet handling untrusted faults.
- **Phase 1 (targeted real SEH):** flip the same header to the `_SEH_` macros and convert the **hot probe
  paths first** (Mm probe/`MmProbeAndLockPages`, Ob handle-table, Io buffered-I/O capture) — a few dozen
  sites — to real fault recovery, leaving the rest neutered until needed. The runtime hooks into the
  existing `ke/trap.S` data-abort handler (already routes to `KeBugCheckEx`) — redirect "fault inside a
  registered try" to the frame walk instead of bugcheck.

This keeps gcc/ARMv7/AAPCS/ARM-mode, no PE/COFF change, no remote dependency — and never pays the full
2423-edit bill unless/until fidelity demands it.

---

## EXP 4 — MSVC `cl.exe /arm` (remote, access pending) — the plan

**Why it's the truest for 32-bit:** Windows RT (2012) shipped on 32-bit ARM (Cortex-A9, Thumb-2) **with full
SEH**, so MSVC's ARM back-end *does* codegen 32-bit-ARM SEH + ARM unwind tables — the **only** toolchain that
does. It also emits real PE/COFF (removes `mkpe.py`). Run when SSH/RSH to a VS2012–2015 ARM host lands:

```bat
cl.exe /c /arm /Od /GS- seh_test.c        :: 1) gate: does it accept the SEH? object format?
dumpbin /headers /section:.pdata t.obj    ::    confirm Thumb-2 + .pdata/.xdata
:: 2) then a real file via a header farm (1994 source) — expect impedance:
cl.exe /c /arm /Od /GS- /D_ARM_ /I<farm> EX\LUID.C
```
**Expected impedance (categorize, don't fight blindly):** old `#pragma`s, `_asm` blocks (x86-only paths —
already `_ARM_`-gated in this project), K&R-isms, `__int64`/intrinsic differences, and the **ABI shift**
(Thumb-2 + Windows-ARM calling convention vs the port's current ARM-mode AAPCS — every `.S` file and the
loader↔kernel handoff contract changes).

**Flag for the user:** adopting `cl.exe /arm` is an **ABI/ISA migration** (ARM-mode→Thumb-2,
AAPCS→Windows-ARM) of the entire arcfw/kernel, plus a build-system move off gcc/Docker. High fidelity, high
cost. It is, however, the one path that yields real-compiler SEH **on the actual Pi 2 CPU** (unlike the
aarch64-clang route, which can't run there at all).

---

## Bottom line / what changes the ABI

- **No ABI change, keep everything, available now:** setjmp PSEH layer on gcc/ARMv7. ← recommended, staged.
- **ABI change (ARM-mode→Thumb-2, AAPCS→Windows-ARM), real 32-bit SEH, real PE/COFF:** `cl.exe /arm`, remote.
- **Hardware + ABI change (Pi 2→Pi 3, AArch32→AArch64):** clang `aarch64-windows-msvc` — only if abandoning
  the Cortex-A7 target.

The **SEH dispatch runtime** (`RtlDispatchException`/`RtlUnwind` analog + the ARM data-abort→raise plumbing)
is **ours to write regardless** of compiler — the compiler only dictates the unwind-table format we consume.
With the setjmp layer, that format is ours (a frame list); with `cl.exe`, it's the MSVC ARM `.pdata`/`.xdata`.

### Reproduce
Artifacts in `/tmp/tc/`: `seh_test.c` (gate), `seh_rt.{h,c}` + `tst_pseh.c` (working PSEH demo),
`nt_try.c` + `excpt_arm.h` (terminator-free failure case). Images: `silkeh/clang:latest` (EXP 1),
`arc-rpi-build:latest` (EXP 2/3 on the real gcc). Header farm: `ARM32/arcfw/kernel/make-ktest.sh`.

---

## Implementation (Path 1, landed 2026-05-25, QEMU-verified)

**Two-tier design** (the staging from the recommendation):

- **Tier 0 — neuter (zero source edits): `arcfw/inc/excpt.h`.** A copy of `EXCPT.H` with an `_ARM_` branch.
  Because it is `-I`'d ahead of the build's header farm, it supersedes the farmed `EXCPT.H` for the kernel
  build (the loader never includes `excpt.h`). It maps the keywords *terminator-free* so unmodified NT
  source compiles:
  `try{B}except(F){H}` → `do{B}while(0); if(0){H}` · `try{B}finally{Fin}` → `do{B}while(0); {Fin}` ·
  `leave` → `break`. The `do/while(0)` gives `leave` a target without a trailing token; the source's own
  braces stay balanced. Verified: **6/6 sampled executive files** (`EX/{LUID,SYSENV,MUTANT,SEMPHORE,
  EVENTPR,EVENT}.C`, `DBG=1`) compile. Loses fault recovery + abnormal-finally (the neuter's known cost).

- **Tier 1 — real fault recovery (explicit macros): `arcfw/kernel/inc/kiseh.h` + `ke/seh.c` + `ke/seh.S`.**
  A setjmp frame chain (the ReactOS-PSEH mechanism). Files needing genuine recovery use
  `_SEH_TRY / _SEH_EXCEPT(filter) / _SEH_END_EXCEPT` and `_SEH_TRY_FINALLY / _SEH_FINALLY /
  _SEH_END_FINALLY` (+ `_SEH_LEAVE`). `KiSehSetjmp/KiSehLongjmp` (ke/seh.S) save/restore r4-r11+sp+lr;
  `KiSehRaise` (ke/seh.c) walks the chain; `ExRaiseStatus` now routes here.

- **Hardware faults: `ke/trap.S`.** The data- and prefetch-abort entries call `KiArmDispatchFault`
  (returns the nearest registered frame's jmpbuf or NULL); on a hit they `cpsie a, #0x13` (SVC mode,
  re-enable async aborts) and tail-call `KiSehLongjmp` to resume at the `_SEH_TRY`. No frame → the
  existing `KiArmTrapReport` bugcheck path. Status is parked in callee-saved r4-r6 so it survives the call.

**Self-test** (`KI_SEH_SELFTEST` in `ke/kearm.c`, runs at the end of `KiArmReportInitialized`) — all pass
on `qemu-system-arm -M raspi2b`; the kernel then resumes and the clock keeps ticking:
```
1. ExRaiseStatus -> filter -> handler  v=0x0000c0de   OK
2. data abort -> handler               v=0x0000fa17   OK
3. fault thru finally -> except        v=0x0000f1a1 finallyRan=1 abnormal=1   OK
4. read + leave + normal finally       v=0xabcd1234 reached=0 finallyRan=1 abnormal=0   OK
frame chain head after tests = 0x00000000   OK   (balanced)
```
Faults are forced with an unaligned load (MMU-off ⇒ strongly-ordered ⇒ unaligned LDR always aborts — the
project's documented gotcha), the odd address derived at run time so the compiler can't elide it.

**Known limitations (documented, fine for bring-up):** no `EXCEPTION_CONTINUE_EXECUTION` (setjmp can't
resume the faulting instruction — treated as continue-search); minimal `ContextRecord` (code only); the
frame-chain head is a uniprocessor global (`KiSehTopFrame`, → PCR with SMP). **Next:** convert the hot
probe paths (Mm `MmProbeAndLockPages`, Ob handle table, Io buffered I/O) from neuter to `_SEH_` as the
executive is brought up; everything else stays neutered until fidelity demands otherwise.
