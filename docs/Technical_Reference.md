# Technical reference: VCC

This document lists repository layout, build details, components, tests, and development notes. The [README](../README.md) is the overview for new readers.

VCC is one machine-independent C11 front end (scanner, parser, semantic analysis, TAC
lowering and optimization) feeding per-target code generators. Nine are complete:
RISC-V RV64IMFD/LP64D and RV32IMFD/ILP32D (`genriscv`, see
[Riscv_Backend.md](Riscv_Backend.md)), AArch64 AAPCS64 (`genaarch64`, see
[Aarch64_Backend.md](Aarch64_Backend.md)), ARMv7-A AAPCS-VFP (`genarm32`, see
[Arm32_Backend.md](Arm32_Backend.md)), x86-64 System V (`genx86`, see
[X86_64_Backend.md](X86_64_Backend.md)), AVR ATmega1280 with the avr-gcc ABI (`genavr`, see
[Avr_Backend.md](Avr_Backend.md)), the classic MSP430 with the EABI as GCC has it (`genmsp430`,
see [Msp430_Backend.md](Msp430_Backend.md)), Knuth's MMIX with the MMIXware ABI as GCC has it
(`genmmix`, see [Mmix_Backend.md](Mmix_Backend.md)), WebAssembly wasm32 with clang's C ABI
(`genwasm`, see [Wasm_Backend.md](Wasm_Backend.md)) and BESM-6 (`genbesm`). The examples in
this document use RISC-V.

## Repository layout

```
vcc/
├── ast/            # AST: types, alloc, import/export, YAML, Graphviz, print, clone, compare, free
├── backend/
│   ├── common/     # Shared by every backend: command-line driver (driver.h), CFG + liveness over TAC (flow.h), the book run tests
│   ├── riscv/      # RISC-V codegen: IR (rv.h), register allocation, instruction selection, peephole, tests
│   ├── aarch64/    # AArch64 codegen: IR (a64.h), register allocation, instruction selection, peephole, tests
│   ├── arm32/      # ARM32 codegen: IR (a32.h), register allocation, instruction selection, peephole, tests
│   ├── x86/        # x86-64 codegen: IR (x86.h), register allocation, instruction selection, x87, peephole, tests
│   ├── avr/        # AVR codegen: IR (avr_ir.h), register allocation, two-form selection, peephole, branch relaxation, tests
│   ├── msp430/     # MSP430 codegen: IR (msp_ir.h), register allocation, memory-to-memory selection, peephole, branch relaxation, tests
│   ├── besm6/      # BESM-6 codegen: IR (besm.h, besm6.asdl), three assembler dialects, tests, BESM-6 docs
│   ├── mmix/       # MMIX codegen: IR (mmix_ir.h), register allocation in GCC's fixed model, selection with fusions, peephole, tests
│   └── wasm/       # WebAssembly codegen: IR (wasm_ir.h), stack-code selection, structured control flow, stackify, peephole, local coalescing, tests
├── cc/             # Compiler driver vcc (from v7besm's b6cc), its end-to-end tests
├── cpp/            # C preprocessor (v7 cpp, C11; from v7besm's b6cpp), its conformance tests
├── docs/           # Project documentation (this file)
├── grammar/        # C11 Yacc/Lex/ASDL reference; see docs/C_Grammar.md
├── libc/           # Target runtimes and C11 headers: riscv/ (crt0.o, libc.a, link.ld, include/), common/ (portable C sources, shared headers), besm6/
├── libutil/        # xalloc, wio, string_map, float128, c_escape
├── optimize/       # TAC optimizer: const fold, unreachable code, copy propagation, dead stores
├── parser/         # Recursive-descent parser, nametab; parse driver
├── scanner/        # Hand-written lexer
├── scripts/        # check_headers.sh, googletest.xml (cppcheck), validate_asdl.py
├── semantic/       # symtab, structtab, typetab, typecheck, label_loops, resolve_labels, defer, coroutines, const_convert, target
├── tac/            # TAC IR: alloc, print, free, compare, walk, verify, export/import, YAML, Graphviz
├── third_party/    # googletest/: GoogleTest v1.18.0, vendored
├── translator/     # AST→TAC lowering (translate, expr, stmt); lower driver
├── CMakeLists.txt  # Root CMake project (project name: c-scanner)
├── Makefile        # Convenience: configure, build, test, install
└── LICENSE
```

## Executables

| Program | Built as | Installed as | Reads | Writes |
|---------|----------|--------------|-------|--------|
| `cc` | `build/cc/cc` | `bin/vcc` | C, `.S`/`.s` assembly, objects | objects, executables (drives the others) |
| `cpp` | `build/cpp/cpp` | `bin/vcpp` | C source | preprocessed C |
| `parse` | `build/parse` | `bin/vparse` | preprocessed C | binary AST (`.ast`), YAML, DOT |
| `lower` | `build/lower` | `bin/vlower` | binary AST | binary TAC (`.tac`), YAML, DOT |
| `genriscv` | `build/backend/genriscv` | `bin/vgenriscv64` | binary TAC | RISC-V GNU assembly (`.s`) |
| `genaarch64` | `build/backend/genaarch64` | `bin/vgenaarch64` | binary TAC (`-t aarch64`) | AArch64 GNU assembly (`.s`) |
| `genarm32` | `build/backend/genarm32` | `bin/vgenarm32` | binary TAC (`-t arm32`) | ARM32 unified assembly (`.s`) |
| `genx86` | `build/backend/genx86` | `bin/vgenx86` | binary TAC (`-t x86_64`) | x86-64 AT&T assembly (`.s`) |
| `genavr` | `build/backend/genavr` | `bin/vgenavr` | binary TAC (`-t avr`) | AVR GNU avr-as assembly (`.s`) |
| `genmsp430` | `build/backend/genmsp430` | `bin/vgenmsp430` | binary TAC (`-t msp430`) | MSP430 GNU msp430-as assembly (`.s`) |
| `genmmix` | `build/backend/genmmix` | `bin/vgenmmix` | binary TAC (`-t mmix`) | MMIX GNU mmix-as assembly (`.s`) |
| `genwasm` | `build/backend/genwasm` | `bin/vgenwasm` | binary TAC (`-t wasm32`) | LLVM wasm assembly (`.s`) |
| `genbesm` | `build/backend/genbesm` | `bin/vgenbesm6` | binary TAC | BESM-6 assembly (`.s`, `.mad` or `.bem`) |

`parse` and `lower` are built from the root `CMakeLists.txt`, `cc` and `cpp` from
`cc/CMakeLists.txt` and `cpp/CMakeLists.txt`, the code generators from `backend/CMakeLists.txt`. None of them writes binary output to stdout unless asked: with
no output argument the result goes to a file named after the input with the new suffix;
pass `-` as the output argument for stdout.

A complete RISC-V compilation in the build tree:

```bash
./build/cpp/cpp -t riscv64 -nostdinc -Ilibc/riscv64/include -Ilibc/lp64/include -Ilibc/common/include hello.c hello.i
./build/parse hello.i hello.ast
./build/lower -t riscv64 hello.ast hello.tac
./build/backend/genriscv hello.tac hello.s
```

Linking with `crt0.o`, `libc.a` and `link.ld` and running under qemu is described in
[Riscv_Backend.md](Riscv_Backend.md#running-a-program-by-hand).

### `cc` (driver)

Runs `vcpp` → `vparse` → `vlower` → `vgen<T>` → assembler → linker for the target given
with `-t`, stopping early with `-E`, `-S` or `-c`. The default is the host: `x86_64-linux`
or `aarch64-linux`, assembled and linked by the system's C compiler against glibc, or
`aarch64-darwin` on a Mac with Apple silicon, against libSystem, else `riscv64`. Our passes are taken from the directory `vcc` is in, and headers and libraries from
`../share/vcc/<target>/`, so an installation can be moved. The bare-metal targets'
assemblers and linkers are their GNU binutils, else clang and `ld.lld`, the BESM-6 ones
v7besm's `b6as` and `b6ld`. Each tool can be
overridden with `VCC_CPP`, `VCC_PARSE`, `VCC_LOWER`, `VCC_GEN`, `VCC_AS` or `VCC_LD`. See
[cc/README.md](../cc/README.md).

### `cpp` (preprocessor)

**Input:** C source. **Output:** preprocessed C with `# line` markers (`-P` drops them),
to stdout or the second positional argument. `-t riscv64|besm6` (default `riscv64`)
selects the predefined target macros and the standard include directory
`<prefix>/share/vcc/<target>/include`, searched after the `-I` directories unless
`-nostdinc` is given. The exit status is the error count. See [cpp/README.md](../cpp/README.md).

The build itself (libc, test fixtures) still preprocesses with the system `cc -E`.

### `parse` (parser)

**Input:** one C source file, already preprocessed (by `cpp` or the system `cc -E`).
The standard headers are in `libc/riscv64/include/` (the target's own),
`libc/lp64/include/` (the LP64 data model, shared with AArch64) and
`libc/common/include/` (target-neutral), and are expanded by an external preprocessor
first — use `cpp` or the C compiler's `cc -E`, not a traditional system `cpp`, which
only honors column-1 directives. `# line` markers are consumed, so diagnostics keep original line
numbers. See [Standard_Include_Files.md](Standard_Include_Files.md).

**Output:** one of:

- Binary AST (default): `--ast` or omit format flag; default file extension `.ast`.
- `--yaml` — YAML dump of the AST.
- `--dot` — Graphviz DOT for structure visualization.

**Options** (see `parser/main.c`): `--ast`, `--yaml`, `--dot`, `-v` / `--verbose`, `-D` / `--debug`, `-h` / `--help`.

**Examples:**

```bash
parse input.c                   # → input.ast
parse input.c output.ast
parse --yaml input.c output.yaml
parse --dot input.c output.dot
parse input.c -                 # binary AST to stdout
```

### `lower` (translator driver)

**Input:** binary AST stream as produced by `parse` (opened with `ast_import_open` / `import_external_decl`).

**Processing order** (per top-level declaration): `typecheck_decl` (`typecheck_global_decl` →
`label_loops` → `resolve_labels` → `check_defers`) → `translate` (lowering, `%`-renaming of locals, the
optimizer, and the type verifier when enabled) → emit. After the last declaration,
`translate_unit_end` emits one `extern` toplevel for every name the unit references but
does not define.

**TAC lowering status:** Complete. Arithmetic, control flow, all function call forms (direct and indirect), pointers, arrays, structs/unions, type casts, `_Generic` selection, compound literals, and aggregate local-variable initializers all lower correctly.

**Target.** `-t` / `--target NAME` selects the target descriptor (`semantic/target.c`) that
fixes type sizes, alignment, the signedness of plain `char` and struct layout. The
default is `riscv64`; BESM-6 code must be lowered with `-t besm6`. `lower -h` lists
the known descriptors: `avr`, `msp430`, `arm32`, `aarch64`, `x86_64`, `riscv32`,
`riscv64`, `mmix`, `wasm32`, `besm6`.

**Options** (see `translator/main.c`): `--tac` (default), `--yaml`, `--dot`, `-t`/`--target`,
`--no-unreachable`, `--no-copy-prop`, `--no-dead-store`, `--opt-debug`, `--verify`, `-v`,
`-D`, `-h`.

```bash
lower -t riscv64 input.ast                  # → input.tac
lower -t riscv64 --yaml input.ast -         # YAML TAC to stdout
lower -t riscv64 --no-copy-prop --yaml input.ast -
```

### `genriscv` (RISC-V code generator)

**Input:** binary TAC lowered with `-t riscv64`. **Output:** GNU assembly for clang's
integrated assembler (`--target=riscv64 -march=rv64imfd -mabi=lp64d`); default output
file `input.s`.

**Options:** `--no-regalloc` (keep every variable in memory), `--no-peephole`,
`--frame-pointer` (keep `s0`), `-v`, `-D`, `-h`. The argument parsing, TAC import and
per-toplevel loop are the shared `backend_main()` (`backend/common/driver.c`); a backend
supplies only its flags, output extension and a per-toplevel `codegen` callback.

`genaarch64` (TAC lowered with `-t aarch64`, assembled by `aarch64-none-elf-as`)
takes the same three flags (`--frame-pointer` keeps a frame record in every function), as
does `genarm32` (TAC lowered with `-t arm32`, assembled by
`arm-none-eabi-as -mcpu=cortex-a15 -mfpu=vfpv3-d16 -mfloat-abi=hard`) and `genx86` (TAC
lowered with `-t x86_64`, assembled by `x86_64-elf-as --64`). `genavr` (TAC
lowered with `-t avr`, assembled by `avr-as -mmcu=atmega1280`) takes
`--no-regalloc` and `--no-peephole`, as does `genmsp430` (TAC lowered with `-t msp430`,
assembled by `msp430-elf-as -mcpu=msp430`) and `genmmix` (TAC lowered with `-t mmix`,
assembled by `mmix-knuth-mmixware-as -x -no-predefined-syms`). `genwasm` (TAC lowered
with `-t wasm32`, assembled by `clang --target=wasm32` with the wasm features) takes
`--no-structure`, `--no-regional`, `--no-peephole`, `--no-stackify` and `--no-coalesce`, and `genbesm` uses the same driver too; see [BESM-6 backend](#besm-6-backend-backendbesm6).

### Installation

`make install` runs `cmake --install build`, whose default prefix is `~/.local` (set in
the top-level `CMakeLists.txt`; override with `--prefix DIR`). The executables are renamed
with a `v` prefix only at install time; each target's runtime and headers go to
`share/vcc/<target>/`:

| Path under `~/.local` | Contents |
|-----------------------|----------|
| `bin/` | `vcc`, `vcpp`, `vparse`, `vlower`, `vgenriscv64`, `vgenriscv32`, `vgenaarch64`, `vgenarm32`, `vgenx86`, `vgenavr`, `vgenmsp430`, `vgenmmix`, `vgenwasm`, `vgenbesm6` |
| `share/vcc/riscv64/lib/` | `crt0.o`, `libc.a`, `link.ld` (only when a RISC-V clang and `llvm-ar` were found) |
| `share/vcc/riscv64/include/` | all RISC-V and shared headers, hosted ones included |
| `share/vcc/aarch64/lib/`, `include/` | the same for AArch64 (the runtime only when the clang has an AArch64 target) |
| `share/vcc/arm32/lib/`, `include/` | the same for ARM32 (the runtime only when the clang has an ARM target) |
| `share/vcc/x86_64/lib/`, `include/` | the same for x86-64 (the runtime only when the clang has an x86-64 target) |
| `share/vcc/avr/lib/`, `include/` | the same for AVR (the runtime only when the clang has an AVR target) |
| `share/vcc/msp430/lib/`, `include/` | the same for MSP430, `link.ld` for mspsim (the runtime only when the GNU MSP430 toolchain was found) |
| `share/vcc/mmix/lib/`, `include/` | `crt0.o` and `libc.a` for MMIX, with no linker script (the runtime only when the GNU MMIX toolchain was found) |
| `share/vcc/wasm32/lib/`, `include/` | `crt0.o`, `libc.a` and the node host `run.mjs` for wasm32, with no linker script (the runtime only when a clang with the WebAssembly target, `wasm-ld` and `llvm-ar` were found) |
| `share/vcc/besm6/lib/` | `libc.bin`, `libbem.bin`, `libruntime.a` |
| `share/vcc/besm6/include/` | the C11 freestanding headers and `besm6.h` (the hosted libc comes from [v7besm](https://github.com/besm6/v7besm)) |

## Components

### Scanner (`scanner/`)

Hand-written lexer. Token set follows C11-style tokens for preprocessed source.

| File | Role |
|------|------|
| `scanner.h`, `scanner.c` | Token definitions and lexer |
| `test/tests.cpp` | GoogleTest suite (`scanner-tests`) |

### Parser (`parser/`)

Recursive-descent parser guided by the C11 grammar in `grammar/` (not generated from Yacc). Builds AST nodes and manages a name table for ordinary identifiers.

| File | Role |
|------|------|
| `parser.h`, `parser_internal.h`, `parser.c` | Parser API and top level |
| `decl.c`, `expr.c`, `stmt.c` | Declarations (incl. anonymous struct tag minting), expressions, statements |
| `nametab.c` | Identifier name table |
| `main.c` | `parse` entry: `parse` → `export_ast` / `export_yaml` / `export_dot` |
| `test/fixture.h` | Test helpers |

Parser tests (9 files): `simple_tests.cpp`, `statement_tests.cpp`, `operator_tests.cpp`, `type_tests.cpp`, `struct_tests.cpp`, `declaration_tests.cpp`, `constant_tests.cpp`, `serialize_tests.cpp`, `negative_tests.cpp` → `parser-tests`.

### AST (`ast/`)

AST values are implemented in C (`ast.h` and companion `.c` files). Binary serialization and YAML/DOT export are used by `parse` and by `lower` when importing ASTs.

| File | Role |
|------|------|
| `ast.asdl` | Canonical IR description (not auto-generated into C by the build) |
| `ast.h`, `internal.h`, `tags.h` | Types and internals |
| `ast_alloc.c`, `ast_free.c` | Allocation and free |
| `ast_export.c`, `ast_import.c` | Binary wire format |
| `ast_yaml.c`, `ast_graphviz.c` | YAML and DOT |
| `ast_print.c`, `ast_clone.c`, `ast_compare.c` | Print, clone, compare |

### Semantic analysis (`semantic/`)

| File | Role |
|------|------|
| `semantic.h`, `typecheck.h` | Public headers for the semantic subsystem |
| `symtab.c`, `symtab.h` | Scoped identifier → Symbol map |
| `structtab.c`, `structtab.h` | Scoped struct/union/enum tag → StructDef map |
| `typetab.c`, `typetab.h` | Scoped typedef name → TypeDef map |
| `typecheck.c` | Type checking and name binding (single-pass); `typecheck_decl` entry |
| `expressions.c` | Expression semantic analysis |
| `init_normalize.c` | Initializer normalization: designators, brace elision, braced scalars (see [Initializers](#initializers-designators-and-brace-elision)) |
| `initializers.c` | Initializer consumers: static data (`build_static_init`) and automatic typecheck (`typecheck_init`) |
| `statements.c` | Statement semantic analysis |
| `declarations.c` | Declaration processing |
| `label_loops.c` | Annotates loop/switch statements with break/continue jump targets |
| `resolve_labels.c` | `goto`/label validation per function |
| `defer.c` | `defer` checks: no jump into a block past a defer or a `co_alloca`, into or out of a deferred statement, no `return` inside one; label positions for the translator |
| `coroutines.c` | coroutine checks (every target but BESM-6, `Target.no_coroutines`): `_Coro(Y)` declarations, `yield` and `await` in their coroutine, the `co_*` operations, `_Coro_frame(Y, T)` types; lowered by `translator/coro.c` (see [Coroutines_Internals.md](Coroutines_Internals.md)) |
| `type_utils.c` | Type helpers: `get_size`, `get_alignment`, `is_integer`, etc. |
| `const_convert.c` | Constant-expression evaluation and conversion |
| `target.c`, `target.h` | Target descriptors: type sizes and alignment, plain-`char` signedness, shift semantics |
| `symtab_print.c`, `structtab_print.c`, `typetab_print.c` | Debug printers |

Tests: `symtab_tests.cpp`, `structtab_tests.cpp`, `typetab_tests.cpp`, `typecheck_tests.cpp`, `real_tests.cpp`, `pipeline_tests.cpp`, `init_normalize_tests.cpp`, `label_loops_tests.cpp`, `const_convert_tests.cpp`, `coercion_tests.cpp`, `intrinsics_tests.cpp` → `semantic-tests`.

### Translator (`translator/`)

| File | Role |
|------|------|
| `translate.h`, `translate.c` | Shared helpers, type conversion, top-level entry points, unit begin/end |
| `expr.c` | AST `Expr` → TAC instruction lowering |
| `stmt.c` | AST `Stmt` → TAC instruction lowering; local declaration init |
| `coro.c` | Coroutines: the operations lowered to calls of `libc/common/co.c`, and the split pass that makes a coroutine a state machine over its frame after the optimizer ([Coroutines_Internals.md](Coroutines_Internals.md) §5) |
| `main.c` | `lower` entry: import → semantic passes → translate → emit |
| `test/translate_test.h` | Test fixture helpers shared across translator test files |

Tests: `decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `type_tests.cpp` → `translate-tests`.

### Optimizer (`optimize/`)

Machine-independent TAC passes, run to a fixed point per function by `optimize_function`:
constant folding (`const_fold.c`), unreachable code elimination (`unreachable.c`), copy
propagation (`copy_prop.c`) and dead store elimination (`dead_store.c`, whose liveness
analysis is `liveness.c`, shared with the coroutine split pass), over a CFG
(`cfg.c`) with alias analysis (`alias.c`). See [TAC_Optimization.md](TAC_Optimization.md).

Tests: `const_fold_tests.cpp`, `type_conv_tests.cpp`, `jump_unreachable_tests.cpp`, `copy_prop_tests.cpp`, `dead_store_tests.cpp`, `pipeline_tests.cpp` → `optimizer-tests`.

### TAC (`tac/`)

| File | Role |
|------|------|
| `tacky.asdl` | Canonical TAC description |
| `tac.h` | TAC structs and enums |
| `tac_alloc.c`, `tac_free.c` | Allocation and free |
| `tac_print.c` | Human-readable TAC printing |
| `tac_compare.c` | Structural comparison |
| `tac_walk.c` | Visit every variable name in an instruction |
| `tac_verify.c` | Type verifier (`tac_verify_function`, `tac_verify_program`) |
| `tac_export.c`, `tac_import.c` | Binary wire format (read/write via `wio`) |
| `tags.h` | 4-letter ASCII tag constants for binary wire format; a stream starts with the magic `TAC5` |
| `tac_yaml.c` | YAML listing (debug/test; not re-importable) |
| `tac_graphviz.c` | Graphviz DOT output |

Tests: `binary_tests.cpp`, `yaml_tests.cpp`, `graphviz_tests.cpp`, `verify_tests.cpp` → `tac-tests`.

### Shared backend code (`backend/common/`)

| File | Role |
|------|------|
| `driver.c`, `driver.h` | `backend_main()`: options, TAC import, per-toplevel loop, output file |
| `flow.c`, `flow.h` | Basic blocks, successors, use/def and liveness sets over a function's TAC body |
| `test/backend_test.h` | Fixture shared by the backend test executables |
| `test/book/chapter*_tests*.cpp` | "Writing a C Compiler" run programs, compiled into every backend's test binary |

Tests: `flow_tests.cpp` → `backend-tests`.

### RISC-V backend (`backend/riscv/`)

| File | Role |
|------|------|
| `rv.h`, `rv.c` | IR: functions as instruction lists over virtual and real registers |
| `regalloc.c` | Graph colouring over TAC liveness (`backend/common/flow.c`) |
| `instr.c`, `call.c` | Instruction selection; the LP64D and ILP32D calling conventions (structs, variadics, `long double`) |
| `llong.c` | `long long` in register pairs on riscv32 |
| `frame.c` | Stack slots, value access, prologue/epilogue |
| `peephole.c` | Peephole pass |
| `data.c` | Static data (`.data`, `.rodata`, `.bss`) |
| `emit.c` | GNU assembly output |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genriscv` entry |
| `riscv.asdl`, `riscv.md` | Reference ISA description and its design notes (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suites (`riscv-tests`, and `riscv32-tests` for `--rv32`) |

Per function: register allocation, instruction selection, prologue/epilogue, peephole.
For the frame layout, calls and runtime see [Riscv_Backend.md](Riscv_Backend.md).

### AArch64 backend (`backend/aarch64/`)

| File | Role |
|------|------|
| `a64.h`, `a64.c` | IR: functions as instruction lists over real registers |
| `regalloc.c` | The target side of `backend/common/regalloc.c` |
| `instr.c`, `call.c` | Instruction selection, compare-and-branch fusion; AAPCS64 calls (HFAs, x8 results, variadics, `long double`) |
| `frame.c` | Stack slots, value access, prologue/epilogue (frameless leaves, sp-addressed frames) |
| `peephole.c` | Peephole pass |
| `data.c` | Static data |
| `emit.c` | GNU assembly output |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genaarch64` entry |
| `aarch64.asdl`, `aarch64.md` | Reference ISA description and its design notes (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suite (`aarch64-tests`) |

The argument classification (`tac_aapcs64_class`) lives in `tac/tac_abi.c`, shared with
the semantic pass's `__builtin_va_class`. See [Aarch64_Backend.md](Aarch64_Backend.md).

### ARM32 backend (`backend/arm32/`)

| File | Role |
|------|------|
| `a32.h`, `a32.c` | IR: functions as instruction lists, a condition on every instruction |
| `regalloc.c` | The target side of `backend/common/regalloc.c` (r0 numbered 1 there) |
| `instr.c`, `fp.c`, `llong.c` | Instruction selection, compare-and-branch fusion; floating point; `long long` in register pairs and the RTABI helper calls |
| `call.c` | AAPCS-VFP calls: back-filled `s` registers, HFAs, the r3/stack split, the base standard for variadics |
| `frame.c` | Stack slots, value access, parallel moves, prologue/epilogue (frameless leaves, sp-addressed frames, r11 fallback) |
| `peephole.c` | Peephole pass over register liveness, conditional execution, `ldrd`/`strd` |
| `data.c` | Static data |
| `emit.c` | Unified assembly output, with clang's ABI attributes |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genarm32` entry |
| `arm32.asdl` | Reference ISA description (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suite (`arm32-tests`) |

`tac_aapcs32_class` in `tac/tac_abi.c` classifies the aggregates, sharing
`tac_aapcs64_class`'s code. See [Arm32_Backend.md](Arm32_Backend.md).

### x86-64 backend (`backend/x86/`)

| File | Role |
|------|------|
| `x86.h`, `x86.c` | IR: functions as blocks of two-operand instructions in AT&T order, a width per operand |
| `regalloc.c` | The target side of `backend/common/regalloc.c` |
| `instr.c`, `fp.c`, `x87.c` | Instruction selection, compare-and-branch fusion; SSE `float`/`double`; the x87 `long double` |
| `call.c` | psABI calls: eightbyte classes, all or nothing, MEMORY structs on the stack, `%al` for variadics, parallel moves |
| `frame.c` | Stack slots, value access, struct copies, prologue/epilogue (rsp-addressed frames, the red zone, `--frame-pointer`) |
| `peephole.c` | Peephole pass over register and flag liveness, `cmov` |
| `data.c` | Static data, the x87 `long double` as `.quad` + `.short` |
| `emit.c` | AT&T assembly output, accepted by clang and GNU `as` |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genx86` entry |
| `x86_64.asdl`, `x86_64.md` | Reference ISA description and its notes (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suite (`x86-tests`) |

`tac_sysv64_class` in `tac/tac_abi.c` classes the eightbytes of an aggregate, for the
backend and `__builtin_va_class` alike. See [X86_64_Backend.md](X86_64_Backend.md).

### AVR backend (`backend/avr/`)

| File | Role |
|------|------|
| `avr_ir.h`, `avr_ir.c` | IR: functions as blocks of byte-register instructions, each knowing its size (2 or 4 bytes) |
| `regalloc.c` | The target side of `backend/common/regalloc.c`, the even register pair as unit |
| `instr.c`, `fp.c` | Instruction selection in two forms (naive register blocks, or in the destination's registers with X/Z as scratch), compare-and-branch fusion; binary32 through the libgcc helpers |
| `call.c` | avr-gcc calls: pieces of flattened structures from r25 down, the stack after r8, variadics all on the stack, parallel moves, hints for the allocator |
| `frame.c` | Slots, value access within and past `Y+63`, parallel moves, prologue/epilogue (`rcall .` frames, frameless functions) |
| `peephole.c` | Peephole pass over register and SREG liveness, copy and memory forwarding, tail calls |
| `relax.c` | Branch relaxation, which the toolchain does not do |
| `data.c` | Static data, function addresses as `pm()` |
| `emit.c` | GNU avr-as output, as clang emits it |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genavr` entry |
| `avr.asdl`, `avr.md` | Reference ISA description and its notes (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suite (`avr-tests`) |

The 16-bit `int` and binary32 `double` come from the `avr` descriptor in
`semantic/target.c`. See [Avr_Backend.md](Avr_Backend.md).

### MSP430 backend (`backend/msp430/`)

| File | Role |
|------|------|
| `msp_ir.h`, `msp_ir.c` | IR: functions as blocks of instructions, each knowing its size (2, 4 or 6 bytes) by GNU `as`'s rules |
| `regalloc.c` | The target side of `backend/common/regalloc.c`, the 16-bit register as unit, `r15` kept as scratch |
| `instr.c`, `fp.c` | Instruction selection on operands where they lie (register or memory, either side), compare-and-branch fusion, inline constant multiply; binary32 and binary64 through the helpers |
| `call.c` | GCC's calls: `r12`–`r15`, the split `long`, structures by reference with the callee's copy (or none, when the parameter is only read), variadics from the last named argument on the stack |
| `frame.c` | Slots, value access, parallel moves, copy loops, prologue/epilogue (frameless functions) |
| `peephole.c` | Peephole pass: constant-generator aliases, copy, constant and memory forwarding, dead code over register and SR liveness, loads sunk into their use, `@rN+`, tail calls |
| `relax.c` | Branch relaxation, which the toolchain does not do |
| `data.c` | Static data, a section per variable |
| `emit.c` | GNU msp430-as output, a section per function |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genmsp430` entry |
| `msp430.asdl`, `msp430.md` | Reference ISA description and its notes, MSP430X included (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suite (`msp430-tests`) |

The 16-bit `int`, the unsigned `char`, alignment 2 and the binary64 `double` come from
the `msp430` descriptor in `semantic/target.c`. See [Msp430_Backend.md](Msp430_Backend.md).

### MMIX backend (`backend/mmix/`)

| File | Role |
|------|------|
| `mmix_ir.h`, `mmix_ir.c` | IR: functions as blocks of instructions, every one 4 bytes |
| `regalloc.c` | The target side of `backend/common/regalloc.c` in GCC's fixed model: `$0`–`$13` live across calls, `$14` `rJ`, `$15` the hole, `$16`–`$31` the rest; the compaction (`phys_reg`) moves the upper range down |
| `instr.c`, `fp.c` | Instruction selection: 8-bit immediates, signed division by `div` and a fix-up, compare-and-branch and address fusions, binary64 in hardware with `float` rounded through a slot |
| `call.c` | GCC's calls on the register stack: `pushj`, sixteen arguments in registers, structures right-justified or by reference, results through `$251`, variadics, tail calls |
| `frame.c` | Types, slots, value access, prologue/epilogue (frameless leaves) |
| `peephole.c` | Peephole pass over register liveness: results in place, dead code, copy forwarding, post-increment, conditional sets, jumps, `rJ` dropped when no call is left |
| `data.c` | Static data |
| `emit.c` | GNU mmix-as output, lowercase mnemonics |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genmmix` entry |
| `mmix.asdl`, `mmix.md` | Reference ISA description and its notes (not used by the build) |
| `test/*_tests.cpp` | GoogleTest suite (`mmix-tests`) |

The signed `char` and the big-endian byte order come from the `mmix` descriptor in
`semantic/target.c`. See [Mmix_Backend.md](Mmix_Backend.md).

### WebAssembly backend (`backend/wasm/`)

| File | Role |
|------|------|
| `wasm_ir.h`, `wasm_ir.c` | IR: a function as one flat list of stack instructions, `block`/`loop`/`if`/`end` among them; each instruction's stack effect |
| `frame.c` | Types, clang's signatures (`wasm_pass`, `wasm_sret`), which names are locals and which frame slots, the shadow-stack prologue and epilogue (none without slots) |
| `instr.c` | Selection of stack code: push the operands, compute, pop into the destination; narrow values kept extended; `long double` through the runtime |
| `call.c` | clang's calls: single-scalar structures by value, others by reference to a caller's copy, sret, `long double` as two `i64`, the variadic buffer and `__va_start`, `co_alloca`'s `__builtin_stack_save`/`__builtin_alloca`/`__builtin_stack_restore` in place, `call_indirect` |
| `structure.c` | Structured control flow by Ramsey's translation (reverse postorder, dominators, loop headers, merge nodes; a `JUMP_TABLE` as a `br_table`); an irreducible graph made reducible first by a dispatch node per region with several entries; the whole-function dispatch skeleton as the last fallback |
| `peephole.c` | Rewrites of the finished code: stackify, tees, dead values, tests, offsets folded into accesses, stores merged, dead code and branches removed |
| `locals.c` | Local coalescing: liveness over the structured code, copy-related locals merged, groups coloured |
| `data.c` | Static data, a section per variable |
| `emit.c` | LLVM wasm assembly, every `.functype` at the top of the unit, names the assembler cannot take renamed |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genwasm` entry |
| `test/*_tests.cpp` | GoogleTest suite (`wasm32-tests`) |

The signed `char`, ILP32 and the binary128 `long double` come from the `wasm32`
descriptor in `semantic/target.c`. See [Wasm_Backend.md](Wasm_Backend.md).

**Walkthrough.** For

```c
int scale(int x, int k)
{
    return x * k + 1;
}
```

`lower -t riscv64 --yaml` gives three instructions over two parameters and two
temporaries (`%x * %k → %0`, `%0 + 1 → %1`, `return %1`), and `genriscv` keeps all four
in registers:

```asm
    .text
    .globl  scale
    .p2align 2
    .type   scale, @function
scale:
    mulw    a0, a0, a1
    addiw   a0, a0, 1
    ret
    .size   scale, .-scale
```

With `--no-regalloc` each TAC name gets a 4-byte stack slot instead, and every
instruction loads its operands into `t0`/`t1` and stores its result — useful for reading
instruction selection on its own.

### RISC-V runtime (`libc/riscv64/`, `libc/common/`)

| File | Role |
|------|------|
| `libc/riscv64/crt0.S` | Start-up: stack, call `main`, then `exit` (also built as `crt0-status.o`, which prints `main`'s result, for the book run tests) |
| `libc/riscv64/console.s` | `putbyte` (UART output) and `exit` (stops qemu) |
| `libc/riscv64/malloc.s` | Simple allocator |
| `libc/riscv64/sqrt.s` | `sqrt` and `sqrtf` (`fsqrt.d`, `fsqrt.s`), for both widths; each other target has its own |
| `libc/common/doprnt.c` | The `printf` engine for the byte-addressed IEEE-754 targets |
| `libc/common/float128.c` | binary128 `long double` soft-float (`__addtf3`, `__lttf2`, …), built on `libutil/float128.c` |
| `libc/common/co.c`, `costack.c` | The coroutine runtime, in every target's `libc.a` but BESM-6's and in the hosted targets' `libvcc.a`; `costack.c`, the arena `co_alloca` takes in a function, everywhere but wasm32 ([Coroutines_Internals.md](Coroutines_Internals.md) §5) |
| `libc/lp64/frexp.c`, `ldexp.c`, `modf.c` | Bit-level math for LP64 targets |
| `libc/ilp32/frexp.c`, `ldexp.c`, `modf.c`, `int64.c`, `int64conv.c` | Bit-level math, the `long long` division, and its conversions to and from floating point, for ILP32 targets |
| `libc/riscv64/link.ld` | Linker script for qemu `virt` (load address 0x80000000) |
| `libc/riscv64/include/` | RISC-V's own headers (`stdarg.h`, `stddef.h`, `stdint.h`, `setjmp.h`) |
| `libc/lp64/include/` | LP64 data-model headers shared by riscv64, aarch64, x86-64 and mmix (`float.h`, `inttypes.h`, `limits.h`, `math.h`; x86-64 and mmix have their own `float.h` and `limits.h`) |
| `libc/ilp32/include/` | ILP32 data-model headers shared by riscv32, arm32 and wasm32 (`inttypes.h`, `limits.h`, `math.h`; wasm32 has its own `limits.h`) |
| `libc/aarch64/include/` | AArch64's own headers (`stdarg.h`, `stddef.h`, `stdint.h`, `setjmp.h`) |
| `libc/arm32/include/` | ARM32's own headers (`float.h`, `stdarg.h`, `stddef.h`, `stdint.h`, `setjmp.h`) |
| `libc/x86/include/` | x86-64's own headers (`float.h`, `limits.h`, `stdarg.h`, `stddef.h`, `stdint.h`, `setjmp.h`) |
| `libc/avr/include/` | AVR's own headers (`float.h`, `limits.h`, `math.h`, `setjmp.h`, `stdarg.h`) |
| `libc/msp430/include/` | MSP430's own headers (`float.h`, `limits.h`, `math.h`, `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h`) |
| `libc/mmix/include/` | MMIX's own headers (`float.h`, `limits.h`, `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h`) |
| `libc/wasm32/include/` | wasm32's own headers (`float.h`, `limits.h`, `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h`); its runtime (`crt0.S`, `console.s`, `memory.s`, `sqrt.s`, `main.s`, `malloc.c`, `run.mjs`) is in `libc/wasm32/` |
| `libc/wasm32/braam/` | the `wasm32-braam` runtime, a process of Braam ([Braam.md](Braam.md)): `exports.s` (`crt0.o`), `rt.c`, `sys.c`, `stdio.c`, `malloc.c`, `strerror.c`, `taskbytes.c` (the default `__braam_task_bytes`), the fake kernel `run.mjs`, and `include/` (`braam.h`, `unistd.h`, `fcntl.h`, `errno.h`, `stdio.h`, `stdlib.h`, `signal.h`, `poll.h`, `sys/types.h`, `sys/stat.h`), searched ahead of wasm32's; `scripts/check_braam_abi.py` (the `braam-abi` ctest) compares its numbers with braam-core's; `backend/wasm/test/braam_system.mjs` (the `braam-system` ctest) runs programs on a built braam-core; `docs/examples/notes.c` is the worked example of [Braam_Example.md](Braam_Example.md) |
| `libc/ip16/include/` | 16-bit data-model headers: `inttypes.h`, shared by avr and msp430, and avr's `stddef.h` and `stdint.h` (msp430 has its own, with a `long` `wchar_t`) |
| `libc/common/float32.c` | binary32 soft-float (`__addsf3`, `__ltsf2`, …) for AVR, where `double` is binary32 too, and MSP430 |
| `libc/common/float64.c` | binary64 soft-float (`__adddf3`, `__ltdf2`, `sqrt`, …), correctly rounded, for MSP430 |
| `libc/common/*.c` | Target-neutral C library: `printf`/`sprintf`/`snprintf`, `<string.h>`, `atoi`, `fabs`/`fma`/`fmax`/`fmin`, `puts`/`putchar` |
| `libc/common/include/` | Target-neutral headers, searched after the target's |

The C sources are compiled by VCC itself (`cc -E` → `parse` → `lower -t riscv64` →
`genriscv` → clang as assembler) and archived with `llvm-ar` into
`build/libc/riscv64/libc.a`. Without a RISC-V-capable clang and `llvm-ar` the runtime is
skipped, and the run tests skip themselves.

### BESM-6 backend (`backend/besm6/`)

The second complete backend targets the BESM-6, a 48-bit word-addressed mainframe; it
was used to port [Unix v7 to the BESM-6](https://github.com/besm6/v7besm). `genbesm` emits
one of three assembler dialects: Unix `b6as` (default, `.s`), Madlen (`--madlen`, `.mad`)
and Bemsh (`--bemsh`, `.bem`). Its IR is `Besm_Module` → `Besm_Func` → `Besm_Block` →
`Besm_Instr` (`besm.h`, spec `besm6.asdl`); after instruction selection a peephole pass
and a frame-shrinking pass run. The runtime (`libc.bin`, `libbem.bin`, `libruntime.a`)
lives in `libc/besm6/`. Tests: `backend/besm6/test/*_tests.cpp` → `besm-tests`.

Its documentation lives next to the code:

- [Besm6_Data_Representation.md](../backend/besm6/Besm6_Data_Representation.md) — bit layouts and `sizeof` of every C type
- [Besm6_Calling_Conventions.md](../backend/besm6/Besm6_Calling_Conventions.md) — registers, `b/save`, `b/ret`
- [Besm6_Instruction_Set.md](../backend/besm6/Besm6_Instruction_Set.md) — instruction set reference
- [Besm6_Runtime_Library.md](../backend/besm6/Besm6_Runtime_Library.md) — runtime helper specifications
- [Besm6_Intrinsics.md](../backend/besm6/Besm6_Intrinsics.md) — the `__besm6_*` intrinsics of `<besm6.h>`
- [Peephole_Rewrites.md](../backend/besm6/Peephole_Rewrites.md) — the peephole rewrite catalogue
- [Madlen.md](../backend/besm6/Madlen.md), [Bemsh.md](../backend/besm6/Bemsh.md), [Besm6_Unix_Assembler.md](../backend/besm6/Besm6_Unix_Assembler.md) — the three assembler dialects
- [KOI7_Encoding.md](../backend/besm6/KOI7_Encoding.md) — the character set
- [TODO.md](../backend/besm6/TODO.md) — work plan

### TAC YAML format

`tac_export_yaml()` (`tac/tac_yaml.c`) emits one `- toplevel:` block per call. Indentation is 2 spaces per level. **Not re-importable** — debug/test use only. The samples below are real `lower -t riscv64 --yaml` output; sizes, offsets and the `char` kind depend on the target (plain `char` is unsigned on RISC-V, so it appears as `uchar`).

#### Toplevel kinds

```yaml
- toplevel:
  kind: function
  name: scale
  global: true                # false for static
  type: fn(int, int) -> int   # the function's type, one line (tac_type_str)
  params:                     # omitted when empty
    - param: %x
      type: int
    - param: %k
      type: int
  locals:                     # automatic locals and temporaries; omitted when empty
    - local: %0
      type: int
    - local: %1
      type: int
  body:                       # omitted for prototypes
    - instruction:
      kind: binary
      ...
```

A function may also carry `noret: true` (a `_Noreturn` definition) and `static_locals:`
(block-scope statics, each with `name`, `type`, optional `alignment` and `init_list`).

```yaml
- toplevel:
  kind: static_variable
  name: counter
  global: true
  type:
    kind: int
  init_list:            # omitted when absent (tentative definition)
    - init:
      kind: i32
      value: 42

- toplevel:
  kind: static_constant  # used for string literals
  name: _str0
  type:
    kind: array
    elem_type:
      kind: uchar
    size: 6
  init:
    kind: string
    value: hello
    null_terminated: true

- toplevel:
  kind: extern          # referenced in this unit, defined elsewhere
  name: puts
  type:
    kind: fun_type
    param_types:
      - type:
        kind: pointer
        target:
          kind: uchar
    ret_type:
      kind: int
```

A `static_variable` gets `alignment: N` after its type when `_Alignas` makes it stricter
than the type's; `static_locals:` entries have the same field. An `allocate_local`
carries the alignment of an automatic object, `_Alignas` included.

A `structure` type carries its size and, held by value (not behind a pointer), its
alignment, `union: true` for a union, and its `members:` with byte offsets — what a psABI
needs to classify an aggregate. For `struct point { char tag; long x; int y; }` on
riscv64:

```yaml
  type:
    kind: structure
    tag: point
    size: 24
    alignment: 8
    members:
      - member: tag
        offset: 0
        type:
          kind: uchar
      - member: x
        offset: 8
        type:
          kind: long
      - member: y
        offset: 16
        type:
          kind: int
  init_list:            # struct point origin = { 'o', 1 };
    - init:
      kind: i8
      value: 111
    - init:
      kind: zero
      bytes: 7
    - init:
      kind: i64
      value: 1
    - init:
      kind: zero
      bytes: 8
```

In the one-line spelling it is `struct point(24,8)` (size, alignment).

The `type:` lines, `locals:`, a call's `fun_type:` and the struct layout are the
type annotations; setting `tac_yaml_types = false` leaves them out (the translator and
optimizer test fixtures do, so their expected output shows only the instructions).

#### Values — appear under `src:`, `dst:`, `condition:`, `args:`

```yaml
kind: var
name: %x

kind: constant
const:
  kind: int      # int | long | long_long | uint | ulong | ulong_long
                 # | char | uchar | float | double | long_double
  value: 42      # float/double/long_double use %a (hex float) format; long_double is exact binary128
```

**Variable name convention.** A `var` name encodes its storage class by its first
character, so a backend can classify it from the name alone:

| First char | Meaning | Examples |
|------------|---------|----------|
| `%` + digit | compiler temporary | `%0`, `%1` |
| `%` + letter/`_` | parameter or automatic local | `%x`, `%_buf` |
| letter / `_` / `$` | module-level global, static, string constant, or function | `counter`, `_str0`, `puts` |

Labels are `%`-prefixed too: `%L`+digit from `label_loops`, or a `%`+digit temporary
(`genriscv` renders them as `.LL0`, `.L3`).

The translator establishes this in two steps: `new_temp()` mints temporaries already
percent-prefixed, and a per-function pass (`percent_locals_in_function` in
`translator/translate.c`, run just before the optimizer) prefixes every parameter and
automatic-local name — in the body and in the stored `params`/`locals` lists — with `%`.
Temporaries are minted already `%`-prefixed and typed (`new_typed_temp`), so
`params` + `locals` give the type of every frame-resident name in the body; a name
without `%` is typed by its `static_variable`/`static_constant` toplevel, a static
local, or an `extern` toplevel: one ahead of a function for each block-scope `extern`
or function declaration in it, and one from `translate_unit_end()`, after the last
declaration, for every other name the unit references but does not define. A name
declared `extern` in a block and defined later in the unit has both.

`tac_verify_function` (`tac/tac_verify.c`) checks that every name has a type and that
operand types agree with the operator: conversion classes and directions, equal widths
for arithmetic and copies (a constant carries its value over), one floating kind per
`*_DOUBLE` operation, memory through pointers, `COPY_*_OFFSET` inside the aggregate.
`lower --verify` runs it on every function; a build without `NDEBUG` always does.

**Instructions** (all have `- instruction:` header; fields follow at +2 indent)

| `kind:` | Additional fields |
|---------|-------------------|
| `return` | `src:` val (omitted for void return) |
| `copy` | `volatile: true` (when set) `src:` `dst:` |
| `sign_extend`, `zero_extend`, `truncate` | `src:` `dst:` |
| `int_to_double`, `uint_to_double`, `double_to_int`, `double_to_uint` | `src:` `dst:` |
| `int_to_float`, `uint_to_float`, `float_to_int`, `float_to_uint`, `float_to_double`, `double_to_float` | `src:` `dst:` |
| `int_to_long_double`, `uint_to_long_double`, `long_double_to_int`, `long_double_to_uint`, `long_double_to_double`, `double_to_long_double`, `long_double_to_float`, `float_to_long_double` | `src:` `dst:` |
| `ptr_to_char_ptr`, `char_ptr_to_ptr` | `src:` `dst:` (word ↔ byte pointer; BESM-6) |
| `unary` | `op:` `src:` `dst:` |
| `binary` | `op:` `src1:` `src2:` `dst:` |
| `get_address`, `get_address_byte`, `get_address_decay` | `src:` `dst:` |
| `load`, `load_byte` | `volatile: true` (when set) `src_ptr:` `dst:` |
| `store`, `store_byte` | `volatile: true` (when set) `src:` `dst_ptr:` |
| `add_ptr` | `ptr:` `index:` `scale: N` `dst:` |
| `ptr_diff` | `ptr_a:` `ptr_b:` `dst:` |
| `copy_to_offset`, `copy_byte_to_offset` | `src:` `dst: name` (bare string) `offset: N` |
| `copy_from_offset`, `copy_byte_from_offset` | `src: name` (bare string) `offset: N` `dst:` |
| `allocate_local` | `name:` `size: N` `alignment: N` |
| `jump` | `target: label` |
| `jump_if_zero` | `condition:` `target: label` |
| `jump_if_not_zero` | `condition:` `target: label` |
| `label` | `name: label` |
| `jump_table` | `index:` `targets:` list of `- label` (index i jumps to the i-th) `default: label` (any other index); made only by the coroutine split, for a dispatch of three suspension points or more (wasm32) |
| `fun_call` | `fun_name: f` `indirect: true` (omitted when false) `args:` list of `- val:` (omitted when none) `dst:` (omitted for void) `fun_type:` the callee's type |
| `fun_call_noreturn` | same fields as `fun_call`; a direct call to a `_Noreturn` function |

**Volatile.** Each access to a volatile object is one instruction marked `volatile:
true`, made once and as written. A volatile variable is read only by a volatile `copy`
into a temporary (`read_var` in `translator/expr.c`): an increment, a compound
assignment and a plain use all read it that way, once. It is written by a volatile
`copy` from the value, its initializer included, and the value of an assignment to it is
the value stored, not a second read. The optimizer neither folds a volatile instruction
nor substitutes into it, records no copy from it, and never drops it as dead. In the
backends, the named variable of a volatile `copy` is kept in memory, as an address-taken
one is (`backend/common/flow.c`), so it keeps its value across `longjmp`. Each machine
instruction selected for a volatile access carries `is_volatile`, so the peephole passes
neither delete it as the reload of a store (RISC-V, AArch64, ARM32, x86-64, BESM-6 rule #27) nor
merge it into a pair (`ldp`/`stp`, `ldrd`/`strd`).

Unary ops: `complement`, `complement_unsigned`, `negate`, `negate_unsigned`, `negate_double`, `not`,
`sqrt_double`. The translator emits `sqrt_double` for a call of the C library's
`sqrt(double)` (external, not defined in the unit) on a target whose descriptor has
`hw_sqrt` (RISC-V, AArch64, ARM32, x86-64; not the BESM-6), where square root is one
correctly rounded instruction; constant folding evaluates it for a constant that is
not negative or a NaN (the sign of the NaN a target makes is its own).

Binary ops: `add`, `subtract`, `multiply`, `divide`, `remainder`, `equal`, `not_equal`,
`less_than`, `less_or_equal`, `greater_than`, `greater_or_equal`, `bitwise_and`,
`bitwise_or`, `bitwise_xor`, `left_shift`, `right_shift`; unsigned forms
`add_unsigned`, `subtract_unsigned`, `multiply_unsigned`, `divide_unsigned`,
`remainder_unsigned`, `less_than_unsigned`, `less_or_equal_unsigned`,
`greater_than_unsigned`, `greater_or_equal_unsigned`, `right_shift_logical`; floating
forms `add_double`, `subtract_double`, `multiply_double`, `divide_double`,
`less_than_double`, `less_or_equal_double`, `greater_than_double`,
`greater_or_equal_double`.

Member access on an aggregate local uses `copy_to_offset`/`copy_from_offset`; through a
pointer it is `add_ptr` arithmetic. For `p[i].x` with `p` a `struct point *` on riscv64:

```yaml
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %p
      index:
        kind: var
        name: %5
      scale: 24
      dst:
        kind: var
        name: %6
    - instruction:
      kind: add_ptr
      ptr:
        kind: var
        name: %6
      index:
        kind: constant
        const:
          kind: int
          value: 1
      scale: 8
      dst:
        kind: var
        name: %7
    - instruction:
      kind: load
      src_ptr:
        kind: var
        name: %7
      dst:
        kind: var
        name: %8
```

**Types** (appear under `type:`, `elem_type:`, `target:`, `ret_type:`, `param_types:`, `members:`)

```yaml
kind: int | uint | long | ulong | long_long | ulong_long
     | schar | uchar | short | ushort | float | double | long_double | void

kind: pointer
target:
  kind: ...

kind: array
elem_type:
  kind: ...
size: N

kind: fun_type
param_types:        # omitted when none
  - type:
    kind: ...
ret_type:
  kind: ...
variadic: true      # omitted when false

kind: structure
tag: point
size: N
alignment: N        # and union:, members: — see above
```

**Static init kinds** (list items under `init_list:`, single item under `init:`)

| `kind:` | Fields |
|---------|--------|
| `i8` / `i16` / `i32` / `i64` | `value:` (signed) |
| `u8` / `u16` / `u32` / `u64` | `value:` (unsigned) |
| `float` | `value:` (hex float) |
| `double` | `value:` (hex float) |
| `long_double` | `value:` (hex float, exact binary128) |
| `zero` | `bytes: N` |
| `string` | `value:` `null_terminated: true\|false` |
| `pointer` | `name:` `byte_offset:` (when nonzero) |
| `fat_pointer` | `name:` `byte_offset:` — a `char *` initializer (`const char *greeting = "hello";` gives `name: _str0`, `byte_offset: 0`); a byte-addressed backend emits it like `pointer`, BESM-6 encodes the byte position into the word |

### Grammar (`grammar/`)

Reference grammars and notes. See [grammar/README.md](../grammar/README.md) for the relationship between Yacc, Lex, and ASDL files (`c11.y`, `c11.l`, `c11.asdl`).

### Utilities (`libutil/`)

| Module | Files | Purpose |
|--------|--------|---------|
| **xalloc** | `xalloc.c`, `xalloc.h` | Tracked allocation; `xfree_all`; `xstruniq()` for unique name generation; leak reporting in debug builds |
| **wio** | `wio.c`, `wio.h` | Binary I/O for AST and TAC streams |
| **string_map** | `string_map.c`, `string_map.h` | Map used in symbol and type tables |
| **float128** | `float128.c`, `float128.h` | IEEE binary128 in software: the value of every long double constant, exact on any host (parsing, folding, conversion, formatting); also included by the RISC-V runtime |
| **c_escape** | `c_escape.c`, `c_escape.h` | Decoding of backslash escapes in character and string literals |

Tests: `c_escape_tests.cpp`, `string_map_tests.cpp`, `wio_tests.cpp`, `xalloc_tests.cpp`, `float128_tests.cpp` → `libutil-tests`. `libutil/test/` also holds helpers shared by every test binary: `test_preprocess.h` (runs `cc -E` over test snippets with the target's headers) and `test_chdir.cpp` (each binary `chdir()`s into its build directory at startup).

### Scripts (`scripts/`)

| File | Purpose |
|------|---------|
| `check_headers.sh` | Preprocesses and parses every standard header (the `riscv-headers` and `besm-headers` CTests) |
| `googletest.xml` | cppcheck library hints for GoogleTest macros |
| `validate_asdl.py` | Optional ASDL validation (requires Python package `pyasdl`): `python3 scripts/validate_asdl.py path/to/file.asdl` |

## Language behaviors and extensions

### No identifier shadowing

This compiler intentionally rejects identifier shadowing: a name declared in an inner block
that duplicates any name in an enclosing scope is a compile error. This is a permanent design
decision — `symtab` / `structtab` / `typetab` reject duplicates with `fatal_error`.

### Initializers: designators and brace elision

Designators, brace elision and braced scalars (C11 §6.7.9) are handled by `normalize_init`
(`semantic/init_normalize.c`), with GCC semantics. It rewrites a parser initializer into a
canonical form that `build_static_init` and `typecheck_init` consume by position: an array
has exactly N items, a struct one per member, a union one item (with a `DESIGNATOR_FIELD`
when not the first member), and a NULL item means zero. It also sizes unsized arrays.

A compound literal is an lvalue with its own frame slot; at file scope it becomes an
anonymous static object `_clN`. An automatic aggregate with at least 8 zero stores is
zeroed by a loop, then only its non-zero leaves are stored.

### `defer` and coroutines

Two extensions, spelled with reserved names so that no C program changes meaning:
`_Defer stmt` runs `stmt` when its block is left, on every target; `_Coro(Y)`,
`_Yield`, `_Await`, `_Coro_frame(Y, T)`, `_Coro_ptr(Y, T)` and the `__co_*` operations
make stackless coroutines, on every target but BESM-6 (`Target.no_coroutines`; `cpp`
predefines `__vcc_coroutines__` on the others). `<coro.h>` gives the short names `defer`, `coro`,
`yield`, `await`, `co_frame`, `coro_ptr` and `co_init` … `co_alignof`.
[Coroutines_in_C.md](Coroutines_in_C.md) is the tutorial and has the frame ABI (§10);
[Coroutines_Internals.md](Coroutines_Internals.md) the implementation and the design decisions. The
target `wasm32-braam` builds processes of Braam with them
([Braam.md](Braam.md)).

### `$` in identifiers

`$` is accepted as an identifier character (as in GCC/Clang). A name passes through TAC
unchanged and each backend spells it for its assembler (the BESM-6 Madlen emitter maps
`$` to `/`, so C source can name the runtime's slash-named helpers).

### Multi-character constants

A character constant containing more than one character (e.g. `'ab'`) is implementation-defined
by C11 §6.4.4.1; this compiler packs its bytes GCC-style:

- A byte with bit 7 = 0 is a single ASCII byte.
- A byte with bit 7 = 1 must begin a **valid UTF-8 sequence**; the whole sequence is validated and
  its **raw bytes** are kept verbatim (no codepoint decoding). An invalid lead
  or continuation byte is a fatal error.
- A backslash escape (`'\n'`, `'\xC3'`, `'\303'`) contributes its byte value (low 8 bits) with no
  UTF-8 validation.

The bytes are packed **big-endian, zero-padded from the left** (so `'ab'` → `0x6162`, `'é'` →
`0xC3A9`). The parser types the result by length:

| Packed bytes | Type | Notes |
| --- | --- | --- |
| 1–5 | `int` | |
| 6–8 | `unsigned int` | A deliberate extension: the standard says character constants are `int`. |
| > 8 | — | Fatal error in the parser. |

The value must then fit the target's `int` (or `unsigned int`), or the semantic pass
rejects it ("character constant too long"). With riscv64's 32-bit `int` that allows up to
4 bytes (`'abcd'` → `0x61626364`); BESM-6, with a 41-bit `int` and 48-bit `unsigned`,
allows 5 and 6. The AST integer fields use 64-bit host storage.

A constant of one byte without a prefix (`'\xff'`) is the value of a plain `char`
converted to `int` (C11 §6.4.4.4p10): −1 where plain `char` is signed (x86-64, AVR, MMIX, wasm32),
255 where it is unsigned. `parse` has no target, so it keeps the byte and marks the literal
`LITERAL_CHAR_BYTE`; the semantic pass sign-extends it (`type_char_literal`).

## Build system

- **CMake** minimum 3.10; root project name: `c-scanner`.
- **C** standard: C11; **C++** for tests: C++17.
- **Compiler flags:** `-Wall -Werror -Wshadow` for C and C++, and `-Wno-dangling-else` for C++ (GCC 16 flags an unbraced `if` around a GoogleTest `EXPECT_*`; see root `CMakeLists.txt`).
- **GoogleTest:** v1.18.0 vendored in `third_party/googletest` (the release's `googletest/` directory only, no googlemock), added `EXCLUDE_FROM_ALL`.
- **cppcheck:** If `cppcheck` is found, it is attached to C and C++ targets with project-specific suppressions, `scripts/googletest.xml` for tests and `scripts/cppcheck-c11.xml` for C (cppcheck 2.21 ignores `_Noreturn`, so it is mapped to GCC's attribute).
- **Cross tools:** `scripts/CrossTools.cmake` (`vcc_find_cross`, called by each `libc/<target>/CMakeLists.txt`) looks for the target's GNU binutils by a list of prefixes (`riscv64-unknown-elf`, `aarch64-none-elf`, `arm-none-eabi`, `x86_64-elf` or the host's, `avr`, `msp430-elf`, …) in `PATH`, `~/.local/bin` and Homebrew's directories, and else for a `clang` (also `clang-NN`) that lists the target, `ld.lld` and `llvm-ar`; `-DVCC_CROSS_TOOLS=gnu|llvm` forces one. It sets `<T>_AS` (the assembler command with its flags), `<T>_LD`, `<T>_LDFLAGS`, `<T>_AR` and `<T>_TOOLS_FOUND`, and separately `<T>_CLANG_FOUND`, the tests' reference compiler. `vcc_assemble_crt0` preprocesses `crt0.S` with the C compiler and assembles it. qemu is looked up per target.
- **Makefile:** Creates `build/`, runs `cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo`, delegates `all` to `$(MAKE) -C build`. Targets: `make` (compiler, runtimes, and all test executables), `make test` (builds `all`, but does not run the tests), `make run` (builds `all`, then runs every test via `ctest --test-dir build` — including the textbook chapter tests), `make install` (see [Installation](#installation)), `make clean`, `make debug` (cmake Debug build into `build`).

Common build types: `Debug`, `RelWithDebInfo`, `Release`.

## ASDL and C code

The `.asdl` files (`ast/ast.asdl`, `tac/tacky.asdl`, `grammar/c11.asdl`) describe the intended shape of the AST and TAC. The **CMake build does not generate C headers from ASDL**; `ast.h` and `tac.h` are maintained manually to match those specs. Use `scripts/validate_asdl.py` if you change ASDL and want a quick parse check.

The ISA descriptions under `backend/` (`riscv/riscv.asdl`, `besm6/besm6.asdl`, `x86/x86_64.asdl`, and others) are reference specs as well; `besm6.asdl` is kept in sync with `besm.h`, while the RISC-V backend's own IR (`rv.h`) is a smaller, hand-written subset.

## Testing

Build and run all tests:

```bash
make run
# or
cmake --build build
ctest --test-dir build
```

The test executables are built by the default `make`/`make all` alongside the compiler and
runtimes; `make run` builds everything and then runs ctest. The "Writing a C Compiler" chapter
tests are compiled into these same per-module test binaries (see **Chapter (book) tests**
below), so `make run` runs them too. Test executables and their unit-test sources:

| Executable | Sources (under repo root) |
|------------|---------------------------|
| `cc-tests` | `cc/test/cc_test.cpp` (the driver end to end: every stage, both targets, a staged installation) |
| `cpp-tests` | `cpp/test/test_*.cpp` (C11 conformance, one file per clause, plus the target options) |
| `scanner-tests` | `scanner/test/tests.cpp` |
| `parser-tests` | `parser/test/simple_tests.cpp`, …, `negative_tests.cpp` (9 files) |
| `ast-tests` | `ast/test/clone_tests.cpp` |
| `libutil-tests` | `libutil/test/c_escape_tests.cpp`, `string_map_tests.cpp`, `wio_tests.cpp`, `xalloc_tests.cpp`, `float128_tests.cpp` |
| `tac-tests` | `tac/test/binary_tests.cpp`, `yaml_tests.cpp`, `graphviz_tests.cpp`, `verify_tests.cpp` |
| `semantic-tests` | `semantic/test/*_tests.cpp` (11 unit-test files, listed above) |
| `translate-tests` | `translator/test/decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `defer_tests.cpp`, `coro_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `type_tests.cpp` |
| `optimizer-tests` | `optimize/test/const_fold_tests.cpp`, `type_conv_tests.cpp`, `jump_unreachable_tests.cpp`, `copy_prop_tests.cpp`, `dead_store_tests.cpp`, `pipeline_tests.cpp` |
| `backend-tests` | `backend/common/test/flow_tests.cpp` |
| `riscv-tests` | `backend/riscv/test/*_tests.cpp` (emit, codegen golden assembly, frame, instr, register allocation, peephole, data, qemu run, clang interop, printf/str/mem/math libc, binary128 `long double`) and the book suite |
| `aarch64-tests` | `backend/aarch64/test/*_tests.cpp` (golden assembly, qemu run, clang interop, HFAs, variadics, register allocation, peephole, libc, binary128 `long double`) and the book suite |
| `arm32-tests` | `backend/arm32/test/*_tests.cpp` (golden assembly, qemu run, clang interop, headers against clang's, `long long`, HFAs, variadics, register allocation, frames, peephole, libc) and the book suite |
| `x86-tests` | `backend/x86/test/*_tests.cpp` (golden assembly, also checked by GNU `as`, qemu run, clang interop, headers against clang's, the x87 `long double`, structs, variadics, register allocation, frames, peephole, libc) and the book suite |
| `mmix-tests` | `backend/mmix/test/*_tests.cpp` (golden assembly, runs under Knuth's `mmix`, GCC interop both ways, headers against GCC's, register allocation, peephole, libc also under newlib) and the book suite, compared with GCC's build |
| `wasm32-tests` | `backend/wasm/test/*_tests.cpp` (golden assembly with and without the rewrites, runs under node, clang interop both ways, headers against clang's, structured control flow and its fallback, variadics, libc, binary128 `long double`) and the book suite, compared with clang's build |
| `besm-tests` | `backend/besm6/test/*_tests.cpp` (golden output for the three dialects, run tests under the `dubna` and `b6sim` simulators) and the book suite |

Besides the GoogleTest cases, ctest runs the `riscv-headers`, `aarch64-headers`,
`arm32-headers`, `x86_64-headers`, `mmix-headers`, `wasm32-headers` and `besm-headers` header checks, and their `-cpp` twins that preprocess with our own `cpp`. cppcheck, when installed, runs during the build, not under ctest.

`riscv-tests` runs programs on bare-metal `qemu-system-riscv64`, links VCC code with
clang-compiled code in both directions, and compares every book program's output with
clang's. Tests that need qemu skip themselves when the tools are missing, and those that
compile C with clang when there is no clang with the target; the book programs then
still run and are checked against the book's own results.

Run a single binary (each one `chdir()`s into its own build directory, so it can be
started from anywhere):

```bash
./build/scanner/scanner-tests
./build/parser/parser-tests
./build/ast/ast-tests
./build/libutil/libutil-tests
./build/tac/tac-tests
./build/semantic/semantic-tests
./build/translator/translate-tests
./build/optimize/optimizer-tests
./build/backend/common/backend-tests
./build/backend/riscv/riscv-tests
./build/backend/besm6/besm-tests
```

Run a specific GoogleTest case with `--gtest_filter`, e.g.
`./build/backend/riscv/riscv-tests --gtest_filter='*Interop*'`.

### Chapter (book) tests

The "Writing a C Compiler" chapter tests (`*/test/chapter*_tests.cpp`, and the run programs
shared by every backend in `backend/common/test/book/`) are compiled **into the regular per-module test
binaries** — the chapter sources are listed in the same `add_executable(<module>-tests …)`
as the unit tests. So `parser-tests` contains `chapter1..18_tests.cpp` alongside its unit
tests, and both `riscv-tests` and `besm-tests` contain the book run programs. Each run
program is a `BookTest`, a fixture each backend defines in its own `test/book_test.h`, with
a skip list for programs its target cannot run. There are no separate `*-book-tests`
executables and no ctest `book` label. See [Tests_From_The_Book.md](Tests_From_The_Book.md).

`fatal_error()` (the compiler libraries call it, but its definition lives in the test
executable, not a library) is defined exactly once per binary in a regular unit-test source —
e.g. `parser/test/simple_tests.cpp`, `semantic/test/typecheck_tests.cpp`,
`translator/test/stmt_tests.cpp`, `optimize/test/pipeline_tests.cpp`,
`backend/common/test/flow_tests.cpp`, `backend/riscv/test/emit_tests.cpp`,
`backend/besm6/test/codegen_tests.cpp` — and the chapter sources do not redefine it. The
scanner needs none — it reports lexical errors via its own `lex_error()`/`exit()`.

## Development notes

### Memory

`xalloc` tracks allocations; `xfree_all()` frees everything in bulk at shutdown. With `-D`,
`parse` and `lower` report leaked memory (`xreport_lost_memory`). See
[Memory_Allocation.md](Memory_Allocation.md).

### Debugging

- **`parse -D`:** parser debug, AST pretty-print to stdout before export, import/export/wio debug, `xreport_lost_memory` at end.
- **`lower -D`:** translator debug; prints each imported AST declaration (`print_external_decl`) and each resulting TAC toplevel (`tac_print_toplevel`), then `xreport_lost_memory` at end.
- **`lower --opt-debug`:** traces the optimizer passes; `--no-unreachable`, `--no-copy-prop`, `--no-dead-store` switch single passes off.
- **`genriscv --no-regalloc` / `--no-peephole` / `--frame-pointer`:** show the code without an optimization.

### Visualization

AST DOT export works:

```bash
parse --dot input.c ast.dot
dot -Tpng ast.dot -o ast.png
```

TAC DOT and YAML export work the same way — pass `--dot` or `--yaml` to `lower`:

```bash
lower -t riscv64 --yaml input.ast -        # YAML TAC to stdout
lower -t riscv64 --dot input.ast tac.dot
dot -Tpng tac.dot -o tac.png
```

## References

- **Book:** [Nora Sandler, *Writing a C Compiler*](https://nostarch.com/writing-c-compiler) — pedagogical pipeline similar to this project’s stages.
- **C11 grammar:** Yacc/Lex heritage (e.g. Jeff Lee’s ANSI C grammar) updated toward C11; see `grammar/`.
- **RISC-V:** [RISC-V ELF psABI](https://github.com/riscv-non-isa/riscv-elf-psabi-doc) — the LP64D calling convention `genriscv` follows.
- **AArch64:** [AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst) — the procedure call standard `genaarch64` follows, `va_arg` (appendix B) included.
- **ARM32:** [AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst) — the procedure call standard `genarm32` follows (its VFP variant), and the [RTABI](https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst) helpers `libc/arm32` provides.
- **x86-64:** [System V AMD64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI) — the calling convention `genx86` follows, the eightbyte classification and `va_arg` included.
- **AVR:** [avr-gcc ABI](https://gcc.gnu.org/wiki/avr-gcc) — the calling convention `genavr` follows, as clang implements it; the [AVR instruction set manual](https://ww1.microchip.com/downloads/en/devicedoc/atmel-0856-avr-instruction-set-manual.pdf).
- **MSP430:** [MSP430 Embedded Application Binary Interface](https://www.ti.com/lit/pdf/slaa534) (TI SLAA534) — the ABI `genmsp430` follows, as msp430-elf-gcc implements it; [mspsim](https://github.com/sergev/mspsim), the simulator the programs run on, and its `MSP430_Instruction_Set.md`.
- **MMIX:** [MMIXware](https://www-cs-faculty.stanford.edu/~knuth/mmixware.html) (Knuth, *The Art of Computer Programming*, Volume 1, Fascicle 1) — the architecture, and `mmix`, the simulator the programs run on; GCC's `mmix-knuth-mmixware` port, whose ABI `genmmix` follows.
- **WebAssembly:** the [WebAssembly specification](https://webassembly.github.io/spec/core/); clang's [wasm32 C ABI](https://github.com/WebAssembly/tool-conventions/blob/main/BasicCABI.md) in the tool conventions, which `genwasm` follows; Norman Ramsey, ["Beyond Relooper: recursive translation of unstructured control flow to structured control flow"](https://dl.acm.org/doi/10.1145/3547621) (ICFP 2022), the translation `structure.c` implements.
- **BESM-6:** [v7besm](https://github.com/besm6/v7besm) (Unix v7 on BESM-6), [dubna](https://github.com/besm6/dubna) (Dubna monitor simulator).
