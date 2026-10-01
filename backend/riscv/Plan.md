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

## The main gap: TAC is untyped

A TAC variable is a bare name. BESM-6 never needed more, since every scalar is one
48-bit word. RISC-V needs, for every operand:

- **width**: `addw` vs `add`, `lw` vs `ld`, sign/zero extension after 32-bit ops;
- **int vs float vs double**: which register file, `fadd.s` vs `fadd.d`
  (`TAC_BINARY_ADD_DOUBLE` is used for all three FP types);
- **signedness of narrow loads**: `lb` vs `lbu`;
- **aggregate size/layout**: struct copies and psABI argument classification;
- **the callee's type at a call**: which arguments are variadic (a variadic
  `double` travels in an integer register), and the signature of an indirect call.

Phase 1 adds this. It is the prerequisite for everything else.

## Phase 0 — groundwork (decoupling)

- **D1. TAC audit for `riscv64`.** Run the whole test corpus (chapter sources,
  translator fixtures) through `lower -t riscv64` and check for crashes or BESM-6
  assumptions: word-sized chunks, `INIT_POINTER` "multiple of 6",
  `INIT_FAT_POINTER` "word*6 + byte_from_MSB" encoding, `TAC_TYPE_STRUCTURE.size`
  "word count", 6-byte character-constant limit in `parser/expr.c`. Output: a list
  of defects, each fixed under D2 or T-steps.
  *Done.* All 766 programs embedded in `besm-tests` that lower for `besm6` also
  lower for `riscv64`. Defects found:
  - static `int`/`unsigned`/`long`/word `_Bool` and integer-to-pointer
    initializers always use the 64-bit `I64`/`U64` slot (`const_convert.c`,
    `initializers.c`), wrong where the type is 4 bytes → D2;
  - a static `char *p = &c` adds byte offset 5 (the BESM-6 low byte of a char's
    one-word cell) on every target (`initializers.c`) → D2;
  - character constants are capped at 6 bytes and typed `int` up to 5 bytes, the
    BESM-6 widths, in the parser, which does not know the target → D2;
  - `TAC_TYPE_STRUCTURE.size` is in bytes, but documented as a word count → D2;
  - aggregate copies in pointer-size chunks → T5; one-pointer sret threshold → T6.
- **D2. Fix the small leaks found by D1.** Character-constant length from the target
  descriptor; TAC comments and field docs stated per target, not in BESM-6 units.
  *Done.* Static integer slots follow the object's size (`new_static_init_int`);
  `&c` adds the low-byte offset only on a word-addressed target
  (`target_word_addressed()`); the parser packs up to 8 bytes and the semantic pass
  rejects a character constant that does not fit the target's `int`.
- **D3. Generic backend driver.** Split `backend/main.c` into a shared
  `backend/common/driver.c` (argument parsing, TAC import, toplevel loop, output
  file) and a per-backend descriptor: name, default extension, extra options,
  `codegen_toplevel` callback. `genbesm` keeps its exact CLI and output.
  *Done.* `backend_main()` with a `Backend` of flags, `output_ext` and `codegen`
  callbacks; `backend/besm6/main.c` is the BESM-6 descriptor.
- **D4. Target-specific intrinsics hook.** Move the `__besm6_*` immediate-argument
  table out of `semantic/expressions.c` behind a per-target table in the target
  descriptor, so RISC-V can add its own (`__riscv_csrr`, …) later without editing
  the semantic pass.
  *Done.* `Target.immediate_args` (`semantic/target.h`); only `besm6` has one.
- **D5. Shared test utilities.** Move the target-neutral parts of
  `backend/besm6/test/codegen_test.h` (`RunExternalProgram`, `RunTool`, `ReadFile`,
  `tool_available`, the in-process parse/lower front half) into
  `libutil/test/` or `backend/common/test/`. Rename the `BESM6_CPP` /
  `BESM6_INCLUDE_DIR` test defines to target-neutral names with a per-target
  include dir.
  *Done.* `libutil/test/test_tools.h` (process/file helpers, `FlockGuard`),
  `backend/common/test/backend_test.h` (`BackendTest`: target selection,
  `CompileToTac`, `ScratchPath`); the defines are now `TEST_CPP`/`TEST_INCLUDE_DIR`.
- **D6. Shared book conformance suite.** Extract the "Writing a C Compiler" run
  programs from `backend/besm6/test/chapter*_tests.cpp` into a target-neutral form
  (the source and the host-`cc` expected result) driven by a per-backend
  `CompileAndRunBook`. BESM-6-specific rewrites (`putch` for `putchar`, etc.) become
  per-target exclusion or substitution lists. `besm-tests` must pass unchanged.

`make run` stays green after every D-step.

## Phase 1 — typed TAC

- **T1. Design.** A per-function symbol list `{name, Tac_Type}` covering
  parameters, locals and temporaries (serialized, unlike today's `locals`); a new
  toplevel kind for referenced-but-undefined externals (`extern` objects, called
  functions) with their types; a `Tac_Type *fun_type` on `FUN_CALL`. Bump the
  stream magic to `TAC3`. Update `tac/tacky.asdl`, `tac.h`, export/import, YAML, DOT.
- **T2. Typed temporaries in the translator.** Every `new_var_val`/`new_temp` site
  (about 55) records the type of the value it holds. The translator already knows
  it at each site; this is plumbing.
- **T3. Optimizer keeps types consistent.** Any pass that creates or renames a
  variable updates the symbol list; `percent_locals_in_function` renames the
  symbol list along with the body.
- **T4. Struct layout in TAC types.** `TAC_TYPE_STRUCTURE` carries byte size,
  alignment, and member `(offset, type)` list — enough for psABI classification.
- **T5. Aggregate copy granularity.** `gen_aggregate_assign`/`gen_struct_assign`
  copy in pointer-size chunks, which over-copies a 12-byte, 4-aligned struct on a
  64-bit target. Copy in chunks of the aggregate's alignment, or emit a tail of
  narrower copies.
- **T6. By-value struct threshold.** `type_is_byval_sret` uses one pointer size; the
  psABI passes up to 2×XLEN in registers. Make the threshold (and whether to lower
  to sret in the frontend at all) a target property.
- **T7. Verifier.** A `tac_verify` check, run in debug builds and by tests: every
  variable has a type, operand types agree with the operator.

BESM-6 ignores the new information; its tests and generated code stay identical.

## Phase 2 — backend skeleton

- **R1. Skeleton.** `backend/riscv/` with `CMakeLists.txt`, `rv.h` (IR: function,
  block, instruction over virtual registers), `codegen.c`, `emit.c`, its own
  `main.c` on the D3 driver, and `genriscv`. Emits `.text`/`.globl`/labels.
- **R2. Runtime stub.** `libc/riscv/`: `crt0.s` (set `sp`, clear `.bss`, call
  `main`, pass its result to the finisher), `link.ld`, and `putbyte`/`flush`/`exit`
  over the UART and finisher. Assembled with clang, archived with `llvm-ar`.
- **R3. Run harness.** `backend/riscv/test/` fixture with `CompileToRiscv` (golden
  assembly) and `CompileAndRunRiscv` (assemble, link with crt0 + runtime, run qemu
  with `-display none -serial stdio -monitor none` and a timeout, decode the
  finisher's exit status), plus the D6 `CompileAndRunBook`. CMake finds a
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
  copies, by-value and returned structs with the internal ABI from T6.

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

- **Typed TAC touches everything upstream.** Mitigation: T7's verifier, and the
  BESM-6 golden tests as a regression net — BESM-6 output must not change.
- **BESM-6 assumptions hidden in the translator** beyond those already known (word
  chunking, sret threshold). D1 exists to find them before the backend depends on
  them.
- **qemu on macOS is system-mode only**: the harness owns its crt0/linker script;
  no libc from the host toolchain is used.
- **The book suite is BESM-6-tuned today.** D6 must not weaken the BESM-6 tests
  while making them shareable.

## Open questions

1. `long double`: binary128 per psABI (recommended, R23), or 64-bit `double` as a
   non-conforming shortcut?
2. Install names for the RISC-V tools (`genriscv` → ?).
3. Whether `backend/common/` code (driver, liveness) should already be a separate
   CMake library with a documented API, in preparation for the eventual
   repository split.
