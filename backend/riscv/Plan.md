# RISC-V backend — development plan

A second backend, developed in this tree, whose job is twofold: produce a working
RV64 code generator, and in doing so prove where the frontend/backend boundary
really lies. Every place where the shared code turns out to assume BESM-6 is fixed
in the shared code, not worked around in `backend/riscv/`.

Step IDs are stable: a finished step is marked done, never renumbered.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| ISA | RV64IMFD (`-march=rv64imfd`), no C extension | 64-bit regs hold every C scalar but `long double`; M gives native mul/div; the assembler may still compress |
| ABI | LP64D psABI, `riscv64` descriptor already in `semantic/target.c` | Link-compatible with clang output, which becomes our test oracle |
| Output | GNU assembler syntax (`.s`) | Accepted by both clang/llvm-mc and binutils; we write no assembler or object format |
| Toolchain | Homebrew LLVM `clang --target=riscv64` (assemble), `ld.lld` (link), `llvm-ar` | Present on this machine; Apple's `/usr/bin/clang` has no RISC-V target |
| Run environment | `qemu-system-riscv64 -M virt -bios none`, bare metal | Runs on macOS (qemu-user does not exist there). Output through the ns16550 UART at `0x10000000`, exit status through the SiFive test finisher at `0x100000` |
| Backend IR | Small hand-written `Rv_Instr` list over virtual registers | `riscv.asdl` stays a reference spec, like the other `.asdl` files; the IR covers only what we emit |
| Executable | `genriscv` (`backend/riscv/`), library `riscv` | Mirrors `genbesm`/`besm` |

Verified 2026-09-30: a hand-written `_start`/`main` assembled with Homebrew
clang, linked by `ld.lld` at `0x80000000`, ran under qemu and printed over the UART.

## What the frontend provides

The frontend work (decoupling, typed TAC) is done; see
[Technical_Reference.md](../../docs/Technical_Reference.md) for the format. The backend
can rely on:

- a type for every name: `params` + `locals` of a function, its `type`, a call's
  `fun_type`, and `static_variable`/`static_constant`/`extern` toplevels;
- struct types with size, alignment and members (by value);
- aggregate copies in chunks of the alignment, at most one word;
- pointer arithmetic already scaled to bytes (`ADD_PTR`, divided differences);
- a struct return wider than 16 bytes lowered to a hidden first-argument pointer;
  narrower returns and all struct arguments come whole, for the backend to classify;
- `tac_verify_program` to check imported TAC, and `lower --verify`;
- the shared driver (`backend/common/driver.c`), test fixture
  (`backend/common/test/backend_test.h`) and book suite
  (`backend/common/test/book/`, `BookTest` with a per-backend skip list).

The backend skeleton is in place: `genriscv`, the bare-metal runtime in `libc/riscv/`,
and `riscv-tests` with `CompileToRiscv`, `CompileAndRunRiscv` and `CompileAndRunBook`
(and the shared book suite).

Instruction selection (R5–R12) is done: every TAC variable lives in a stack slot,
and all of ch. 1–20 run. The 72 book programs whose results depend on integer
widths or sizes are shared in their generic LP64 form; BESM-6 runs its own versions
from `backend/besm6/test/book_besm6_tests.cpp`.

psABI conformance (R13–R16) is done: the full LP64D calling convention including
FP-register structs, variadic functions, interop tests against clang in both
directions, and every book program compared with clang.

The runtime library and headers (R17–R19) are done: target-neutral libc sources
and headers are shared from `libc/common/`, the data-model headers are per target,
and `printf` with the string, memory and math routines runs on RISC-V.

## Phase 6 — code quality

- **R20. Liveness and CFG over TAC** in `backend/common/`, usable by any backend.
  *Done:* `flow.c` (blocks, successors, live-in/out, per-instruction step; tested
  by `backend-tests`).
- **R21. Register allocation** — graph colouring with coalescing (book ch. 20),
  callee-saved `s1`–`s11` and `fs0`–`fs11` saved only when used; spill to the R5
  slots. The ch. 20 tests pass. *Done:* `regalloc.c` over the R20 liveness (Briggs
  coalescing of copies, optimistic spilling, costs weighted by loop depth);
  narrow integers in registers are kept extended, and arguments and results are
  extended by the declared type as the psABI asks. `genriscv --no-regalloc` keeps
  everything in memory, for the selection tests.
- **R22. Peephole pass** for what allocation leaves: redundant moves, branch over
  jump, jump to next label, `li`+op into immediate forms. *Done:* `peephole.c`, also
  the zero register for a zero constant, scratch moves folded, a doubleword
  reload after its store, a byte load's mask; the RISC-V libc shrinks by 17%.
  `genriscv --no-peephole` skips it. A leaf function that needs no stack has no
  frame at all.

## Phase 7 — `long double`

- **R23. binary128 `long double`.** The psABI makes it 16 bytes, passed in an
  integer register pair. Implement `__addtf3`, `__multf3`, `__divtf3`,
  `__subtf3`, comparisons and conversions in C, compiled by our compiler, in the
  RISC-V runtime. Until this step `long double` operations are a clear
  `fatal_error`, not a miscompile.

## Phase 8 — finishing

- **R24. Install.** `genriscv`, the RISC-V runtime and headers under
  `share/riscv/`, mirroring `make install` for BESM-6.
- **R25. Documentation.** A short `docs/Riscv_Backend.md` (decisions, frame
  layout, how to run a program by hand under qemu), README and CLAUDE.md updated:
  the project is no longer "complete, maintenance only".
- **R26. Optional: Linux user-mode.** A second crt0 over Linux syscalls for
  `qemu-riscv64` on Linux hosts.

## Risks

- **BESM-6 assumptions still hidden in the translator.** Two surfaced late (wide
  struct values read through memory, unscaled pointer arithmetic). Mitigation: the
  TAC verifier, RISC-V run tests, and the BESM-6 tests as a regression net.
- **qemu on macOS is system-mode only**: the harness owns its crt0/linker script;
  no libc from the host toolchain is used.
- **The book suite was BESM-6-adapted** (`putch`, 41-bit values). A program that
  cannot be shared is generic in the suite, with a BESM-6 version beside the
  BESM-6 tests, so neither target's expectations are weakened.

## Open questions

1. `long double`: binary128 per psABI (recommended, R23), or 64-bit `double` as a
   non-conforming shortcut?
2. Install names for the RISC-V tools (`genriscv` → ?).
3. Whether `backend/common/` code (driver, liveness) should already be a separate
   CMake library with a documented API, in preparation for the eventual
   repository split.
