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

## Phase 2 — backend skeleton

- **R1. Skeleton.** *Done.* `backend/riscv/` with `CMakeLists.txt`, `rv.h` (IR: function,
  block, instruction over virtual registers), `codegen.c`, `emit.c`, its own
  `main.c` on the shared driver, and `genriscv`. Emits `.text`/`.globl`/labels.
- **R2. Runtime stub.** `libc/riscv/`: `crt0.s` (set `sp`, clear `.bss`, call
  `main`, pass its result to the finisher), `link.ld`, and `putbyte`/`flush`/`exit`
  over the UART and finisher. Assembled with clang, archived with `llvm-ar`.
- **R3. Run harness.** `backend/riscv/test/` fixture with `CompileToRiscv` (golden
  assembly) and `CompileAndRunRiscv` (assemble, link with crt0 + runtime, run qemu
  with `-display none -serial stdio -monitor none` and a timeout, decode the
  finisher's exit status), plus its `book_test.h` for the shared `BookTest` suite
  (skip list, per-target expectations such as chapter 17 `sizeof`). CMake finds a
  RISC-V-capable clang (hint `/opt/homebrew/opt/llvm/bin`), `ld.lld`, `llvm-ar` and
  qemu; tests guard with `SKIP_IF_NO_RISCV_TOOLS()` so `make run` stays green
  without them.
- **R4. First program.** `int main(void) { return 2; }` runs and returns 2.

## Phase 3 — instruction selection, book order

Naive and correct first: every TAC variable lives in a stack slot; each
instruction loads operands into scratch registers, computes, stores. Each step is
done when its book chapters pass through `CompileAndRunBook` and a few golden
assembly tests pin the selected instructions.

- **R5. Frame.** Slot layout from typed symbols (size/alignment), `ALLOCATE_LOCAL`,
  prologue/epilogue (`ra`, `s0` frame pointer, 16-byte `sp` alignment).
  Large-offset handling beyond the 12-bit immediate.
- **R6. Integer ops** (ch. 2–4, 11, 12): unary, binary, comparisons via
  `slt`/`sltu`/`xor`+`seqz`, `W`-suffixed 32-bit ops with re-extension, shifts,
  `mul`/`div`/`rem` and their unsigned forms, width conversions.
- **R7. Control flow** (ch. 5–8): labels, jumps, `beqz`/`bnez`, switch lowering
  as emitted by the frontend.
- **R8. Calls, simple ABI** (ch. 9): integer/pointer args in `a0`–`a7`, the rest on
  the stack, result in `a0`; direct and indirect (`jalr`) calls;
  `FUN_CALL_NORETURN`.
- **R9. Globals and static data** (ch. 10): `.data`/`.bss`/`.rodata`, all
  `Tac_StaticInit` kinds, address materialisation with `la` (medany code model),
  static locals with the existing `name$N` uniqueness, using a legal symbol
  spelling.
- **R10. Floating point** (ch. 13): `float`/`double` in `fa0`–`fa7`, all
  conversions (`fcvt.*` with `rtz` for C truncation), comparisons into integer
  registers, FP constants from `.rodata`.
- **R11. Pointers, arrays, chars, strings** (ch. 14–16): `LOAD`/`STORE` by width,
  `ADD_PTR`, and the byte-pointer TAC kinds as plain operations: `GET_ADDRESS_BYTE`/
  `GET_ADDRESS_DECAY` = address, `LOAD_BYTE` = `lb`/`lbu`, `PTR_DIFF` = `sub`,
  `PTR_TO_CHAR_PTR`/`CHAR_PTR_TO_PTR` = copy.
- **R12. Structs** (ch. 17–18): member access via `COPY_*_OFFSET`, whole-aggregate
  copies, by-value and returned structs (whole, or through the hidden pointer).

## Phase 4 — psABI conformance

- **R13. Full LP64D argument classification.** Structs ≤ 16 bytes in up to two
  registers, using FP registers for float-member structs; larger by reference;
  sret in `a0`. Variadic arguments always in integer registers.
- **R14. `<stdarg.h>`.** A variadic callee saves `a0`–`a7` into the 64-byte save
  area directly below the incoming stack arguments, so the arguments are
  contiguous and `va_list` can stay a plain pointer stepping 8 bytes, as in the
  BESM-6 header.
- **R15. Interop tests.** Link our objects against clang-compiled objects in both
  directions (we call clang code, clang code calls us) over a table of signatures:
  mixed int/FP, small/large structs, variadics.
- **R16. Differential testing.** Compile the same programs with clang for RV64,
  run both under qemu, compare output.

## Phase 5 — runtime library and headers

- **R17. Portable libc shared.** Move the target-neutral sources of
  `libc/besm6/*.c` to `libc/common/` after checking each for BESM-6 assumptions;
  compile them with our toolchain for both targets. The RISC-V library is
  `libc/riscv` leaves + common sources.
- **R18. Headers.** Freestanding headers for LP64 (`limits.h`, `stdint.h`,
  `float.h`, `stdarg.h`, `stddef.h`) in `libc/riscv/include/`; hosted headers shared
  from a common include dir where they are target-neutral. A `riscv-headers`
  CTest like `besm-headers`.
- **R19. `printf` and friends run on RISC-V**: port the BESM-6
  `printf_tests`/`str_tests`/`mem_tests`/`math_tests` run tests.

## Phase 6 — code quality

- **R20. Liveness and CFG over TAC** in `backend/common/`, usable by any backend.
- **R21. Register allocation** — graph colouring with coalescing (book ch. 20),
  callee-saved `s1`–`s11` and `fs0`–`fs11` saved only when used; spill to the R5
  slots. The ch. 20 tests pass.
- **R22. Peephole pass** for what allocation leaves: redundant moves, branch over
  jump, jump to next label, `li`+op into immediate forms.

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
- **The book suite is BESM-6-adapted** (`putch`, chapter 17 `sizeof`). Per-target
  expectations must not weaken the BESM-6 tests.

## Open questions

1. `long double`: binary128 per psABI (recommended, R23), or 64-bit `double` as a
   non-conforming shortcut?
2. Install names for the RISC-V tools (`genriscv` → ?).
3. Whether `backend/common/` code (driver, liveness) should already be a separate
   CMake library with a documented API, in preparation for the eventual
   repository split.
