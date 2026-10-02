# Technical reference: VCC

This document lists repository layout, build details, components, tests, and development notes. The [README](../README.md) is the overview for new readers.

VCC is one machine-independent C11 front end (scanner, parser, semantic analysis, TAC
lowering and optimization) feeding per-target code generators. Two are complete:
RISC-V RV64IMFD/LP64D (`genriscv`, see [Riscv_Backend.md](Riscv_Backend.md)) and
BESM-6 (`genbesm`). The examples in this document use RISC-V; the other directories
under `backend/` hold design notes only.

## Repository layout

```
vcc/
├── ast/            # AST: types, alloc, import/export, YAML, Graphviz, print, clone, compare, free
├── backend/
│   ├── common/     # Shared by every backend: command-line driver (driver.h), CFG + liveness over TAC (flow.h), the book run tests
│   ├── riscv/      # RISC-V codegen: IR (rv.h), register allocation, instruction selection, peephole, tests
│   ├── besm6/      # BESM-6 codegen: IR (besm.h, besm6.asdl), three assembler dialects, tests, BESM-6 docs
│   └── ...         # x86/, aarch64/, arm32/, avr/, mmix/, msp430/ — ISA ASDL specs and notes, not implemented
├── cpp/            # C preprocessor (v7 cpp, C11; from v7besm's b6cpp), its conformance tests
├── docs/           # Project documentation (this file)
├── grammar/        # C11 Yacc/Lex/ASDL reference; see docs/C_Grammar.md
├── libc/           # Target runtimes and C11 headers: riscv/ (crt0.o, libc.a, link.ld, include/), common/ (portable C sources, shared headers), besm6/
├── libutil/        # xalloc, wio, string_map, float128, c_escape
├── optimize/       # TAC optimizer: const fold, unreachable code, copy propagation, dead stores
├── parser/         # Recursive-descent parser, nametab; parse driver
├── scanner/        # Hand-written lexer
├── scripts/        # check_headers.sh, googletest.xml (cppcheck), validate_asdl.py
├── semantic/       # symtab, structtab, typetab, typecheck, label_loops, resolve_labels, const_convert, target
├── tac/            # TAC IR: alloc, print, free, compare, walk, verify, export/import, YAML, Graphviz
├── translator/     # AST→TAC lowering (translate, expr, stmt); lower driver
├── CMakeLists.txt  # Root CMake project (project name: c-scanner)
├── Makefile        # Convenience: configure, build, test, install
└── LICENSE
```

## Executables

| Program | Built as | Installed as | Reads | Writes |
|---------|----------|--------------|-------|--------|
| `cpp` | `build/cpp/cpp` | `bin/vcpp` | C source | preprocessed C |
| `parse` | `build/parse` | `bin/vparse` | preprocessed C | binary AST (`.ast`), YAML, DOT |
| `lower` | `build/lower` | `bin/vlower` | binary AST | binary TAC (`.tac`), YAML, DOT |
| `genriscv` | `build/backend/genriscv` | `bin/vgenriscv64` | binary TAC | RISC-V GNU assembly (`.s`) |
| `genbesm` | `build/backend/genbesm` | `bin/vgenbesm6` | binary TAC | BESM-6 assembly (`.s`, `.mad` or `.bem`) |

`parse` and `lower` are built from the root `CMakeLists.txt`, `cpp` from
`cpp/CMakeLists.txt`, the code generators from `backend/CMakeLists.txt`. None of them writes binary output to stdout unless asked: with
no output argument the result goes to a file named after the input with the new suffix;
pass `-` as the output argument for stdout.

A complete RISC-V compilation in the build tree:

```bash
./build/cpp/cpp -t riscv64 -nostdinc -Ilibc/riscv/include -Ilibc/common/include hello.c hello.i
./build/parse hello.i hello.ast
./build/lower -t riscv64 hello.ast hello.tac
./build/backend/genriscv hello.tac hello.s
```

Linking with `crt0.o`, `libc.a` and `link.ld` and running under qemu is described in
[Riscv_Backend.md](Riscv_Backend.md#running-a-program-by-hand).

### `cpp` (preprocessor)

**Input:** C source. **Output:** preprocessed C with `# line` markers (`-P` drops them),
to stdout or the second positional argument. `-t riscv64|besm6` (default `riscv64`)
selects the predefined target macros and the standard include directory
`<prefix>/share/vcc/<target>/include`, searched after the `-I` directories unless
`-nostdinc` is given. The exit status is the error count. See [cpp/README.md](../cpp/README.md).

The build itself (libc, test fixtures) still preprocesses with the system `cc -E`.

### `parse` (parser)

**Input:** one C source file, already preprocessed (by `cpp` or the system `cc -E`).
The standard headers are in `libc/riscv/include/` (data-model dependent) and
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
`label_loops` → `resolve_labels`) → `translate` (lowering, `%`-renaming of locals, the
optimizer, and the type verifier when enabled) → emit. After the last declaration,
`translate_unit_end` emits one `extern` toplevel for every name the unit references but
does not define.

**TAC lowering status:** Complete. Arithmetic, control flow, all function call forms (direct and indirect), pointers, arrays, structs/unions, type casts, `_Generic` selection, compound literals, and aggregate local-variable initializers all lower correctly.

**Target.** `-t` / `--target NAME` selects the target descriptor (`semantic/target.c`) that
fixes type sizes, alignment, the signedness of plain `char` and struct layout. The
default is `riscv64`; BESM-6 code must be lowered with `-t besm6`. `lower -h` lists
the known descriptors: `avr`, `msp430`, `arm32`, `aarch64`, `x86_64`, `riscv32`,
`riscv64`, `mmix`, `besm6` (only `riscv64` and `besm6` have a code generator).

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

`genbesm` uses the same driver; see [BESM-6 backend](#besm-6-backend-backendbesm6).

### Installation

`make install` runs `cmake --install build`, whose default prefix is `~/.local` (set in
the top-level `CMakeLists.txt`; override with `--prefix DIR`). The executables are renamed
with a `v` prefix only at install time; each target's runtime and headers go to
`share/vcc/<target>/`:

| Path under `~/.local` | Contents |
|-----------------------|----------|
| `bin/` | `vcpp`, `vparse`, `vlower`, `vgenriscv64`, `vgenbesm6` |
| `share/vcc/riscv64/lib/` | `crt0.o`, `libc.a`, `link.ld` (only when a RISC-V clang and `llvm-ar` were found) |
| `share/vcc/riscv64/include/` | all RISC-V and shared headers, hosted ones included |
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
| `main.c` | `lower` entry: import → semantic passes → translate → emit |
| `test/translate_test.h` | Test fixture helpers shared across translator test files |

Tests: `decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `type_tests.cpp` → `translate-tests`.

### Optimizer (`optimize/`)

Machine-independent TAC passes, run to a fixed point per function by `optimize_function`:
constant folding (`const_fold.c`), unreachable code elimination (`unreachable.c`), copy
propagation (`copy_prop.c`) and dead store elimination (`dead_store.c`), over a CFG
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
| `instr.c`, `call.c` | Instruction selection; the LP64D calling convention (structs, variadics, `long double`) |
| `frame.c` | Stack slots, value access, prologue/epilogue |
| `peephole.c` | Peephole pass |
| `data.c` | Static data (`.data`, `.rodata`, `.bss`) |
| `emit.c` | GNU assembly output |
| `codegen.c`, `codegen.h`, `internal.h` | Per-function driver |
| `main.c` | `genriscv` entry |
| `riscv.asdl`, `riscv.md` | Reference ISA description and its design notes (not used by the build) |
| `Plan.md` | Plan for the riscv32 target |
| `test/*_tests.cpp` | GoogleTest suite (`riscv-tests`) |

Per function: register allocation, instruction selection, prologue/epilogue, peephole.
For the frame layout, calls and runtime see [Riscv_Backend.md](Riscv_Backend.md).

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

### RISC-V runtime (`libc/riscv/`, `libc/common/`)

| File | Role |
|------|------|
| `libc/riscv/crt0.S` | Start-up: stack, call `main`, then `exit` (also built as `crt0-status.o`, which prints `main`'s result, for the book run tests) |
| `libc/riscv/console.s` | `putbyte` (UART output) and `exit` (stops qemu) |
| `libc/riscv/malloc.s` | Simple allocator |
| `libc/riscv/doprnt.c`, `frexp.c`, `ldexp.c`, `modf.c` | Target-specific C routines |
| `libc/riscv/float128.c` | binary128 `long double` soft-float (`__addtf3`, `__lttf2`, …), built on `libutil/float128.c` |
| `libc/riscv/link.ld` | Linker script for qemu `virt` (load address 0x80000000) |
| `libc/riscv/include/` | Data-model-dependent headers (`float.h`, `limits.h`, `stdint.h`, `stdarg.h`, …) |
| `libc/common/*.c` | Target-neutral C library: `printf`/`sprintf`/`snprintf`, `<string.h>`, `atoi`, `fabs`/`fma`/`fmax`/`fmin`, `puts`/`putchar` |
| `libc/common/include/` | Target-neutral headers, searched after the target's |

The C sources are compiled by VCC itself (`cc -E` → `parse` → `lower -t riscv64` →
`genriscv` → clang as assembler) and archived with `llvm-ar` into
`build/libc/riscv/libc.a`. Without a RISC-V-capable clang and `llvm-ar` the runtime is
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
| `fun_call` | `fun_name: f` `indirect: true` (omitted when false) `args:` list of `- val:` (omitted when none) `dst:` (omitted for void) `fun_type:` the callee's type |
| `fun_call_noreturn` | same fields as `fun_call`; a direct call to a `_Noreturn` function |

Unary ops: `complement`, `complement_unsigned`, `negate`, `negate_unsigned`, `negate_double`, `not`.

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

## Build system

- **CMake** minimum 3.10; root project name: `c-scanner`.
- **C** standard: C11; **C++** for tests: C++17.
- **Compiler flags:** `-Wall -Werror -Wshadow` for C and C++ (see root `CMakeLists.txt`).
- **GoogleTest:** FetchContent, tag `v1.15.2`, `BUILD_GMOCK=OFF`.
- **cppcheck:** If `cppcheck` is found, it is attached to C and C++ targets with project-specific suppressions and `scripts/googletest.xml` for tests.
- **RISC-V tools:** `libc/riscv/CMakeLists.txt` looks for a `clang` that lists `riscv64` among its targets (Homebrew's LLVM first), `llvm-ar`, `ld.lld` and `qemu-system-riscv64`. clang and `llvm-ar` are needed to build the runtime; `ld.lld` and qemu to run programs. On macOS: `brew install llvm lld qemu`.
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
| `cpp-tests` | `cpp/test/test_*.cpp` (C11 conformance, one file per clause, plus the target options) |
| `scanner-tests` | `scanner/test/tests.cpp` |
| `parser-tests` | `parser/test/simple_tests.cpp`, …, `negative_tests.cpp` (9 files) |
| `ast-tests` | `ast/test/clone_tests.cpp` |
| `libutil-tests` | `libutil/test/c_escape_tests.cpp`, `string_map_tests.cpp`, `wio_tests.cpp`, `xalloc_tests.cpp`, `float128_tests.cpp` |
| `tac-tests` | `tac/test/binary_tests.cpp`, `yaml_tests.cpp`, `graphviz_tests.cpp`, `verify_tests.cpp` |
| `semantic-tests` | `semantic/test/*_tests.cpp` (11 unit-test files, listed above) |
| `translate-tests` | `translator/test/decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `type_tests.cpp` |
| `optimizer-tests` | `optimize/test/const_fold_tests.cpp`, `type_conv_tests.cpp`, `jump_unreachable_tests.cpp`, `copy_prop_tests.cpp`, `dead_store_tests.cpp`, `pipeline_tests.cpp` |
| `backend-tests` | `backend/common/test/flow_tests.cpp` |
| `riscv-tests` | `backend/riscv/test/*_tests.cpp` (emit, codegen golden assembly, frame, instr, register allocation, peephole, data, qemu run, clang interop, printf/str/mem/math libc, binary128 `long double`) and the book suite |
| `besm-tests` | `backend/besm6/test/*_tests.cpp` (golden output for the three dialects, run tests under the `dubna` and `b6sim` simulators) and the book suite |

Besides the GoogleTest cases, ctest runs the `riscv-headers` and `besm-headers`
header checks, and their `-cpp` twins that preprocess with our own `cpp`. cppcheck, when installed, runs during the build, not under ctest.

`riscv-tests` runs programs on bare-metal `qemu-system-riscv64`, links VCC code with
clang-compiled code in both directions, and compares every book program's output with
clang's. Tests that need qemu or clang skip themselves when the tools are missing.

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
- **BESM-6:** [v7besm](https://github.com/besm6/v7besm) (Unix v7 on BESM-6), [dubna](https://github.com/besm6/dubna) (Dubna monitor simulator).
