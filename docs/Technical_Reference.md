# Technical reference: BESM-6 C compiler

This document lists repository layout, build details, components, tests, and development notes. The [README](../README.md) is the overview for new readers.

The project is complete: the compiler was used to port [Unix v7 to the BESM-6](https://github.com/besm6/v7besm). The frontend and the BESM-6 backend are finished; no other machine backend was implemented.

## Repository layout

```
c-compiler/
├── ast/            # AST: types, alloc, import/export, YAML, Graphviz, print, clone, compare, free
├── backend/
│   ├── common/     # Shared by every backend: the command-line driver (driver.h)
│   ├── besm6/      # BESM-6 codegen: IR (besm.h, besm6.asdl), b6as/Madlen/Bemsh emitters, tests
│   ├── x86/        # x86_64: never implemented — x86_64.asdl, notes, TODO.md
│   └── ...         # aarch64/, arm32/ — ISA ASDL specs, never implemented; riscv/ — in progress (Plan.md)
├── docs/           # Project documentation (this file)
├── grammar/        # C11 Yacc/Lex/ASDL reference; see docs/C_Grammar.md
├── libc/           # Target C runtime + C11 headers: besm6/{include, madlen (libc.bin), unix (libruntime.a, libc0.a, crt0.o)}
├── libutil/        # xalloc, wio, string_map
├── parser/         # Recursive-descent parser, nametab; parse driver
├── scanner/        # Hand-written lexer
├── scripts/        # googletest.xml (cppcheck), validate_asdl.py
├── semantic/       # symtab, structtab, typetab, typecheck, label_loops, const_convert, target
├── tac/            # TAC IR: alloc, print, free, compare, export/import, YAML, Graphviz
├── translator/     # AST→TAC lowering (translate, expr, stmt); lower driver
├── CMakeLists.txt  # Root CMake project (project name: c-scanner)
├── Makefile        # Convenience: mkdir build, cmake, make test
└── LICENSE
```

## Executables: `parse` and `lower`

Both tools are built from the root `CMakeLists.txt`. Install or run them from the build directory (e.g. `./build/parse`).

### `parse` (parser)

**Input:** one C source file (preprocessor is not implemented; feed the compiler preprocessed C if you rely on `#include` / `#define`). The C11 standard headers ship in `libc/besm6/include/` and are consumed via an external preprocessor first — use the compiler's `cc -E` (not a traditional `cpp`, which only honors column-1 directives); see [Standard_Include_Files.md](Standard_Include_Files.md).

**Output:** one of:

- Binary AST (default): `--ast` or omit format flag; file extension often `.ast`.
- `--yaml` — YAML dump of the AST.
- `--dot` — Graphviz DOT for structure visualization.

**Options** (see `parser/main.c`): `--ast`, `--yaml`, `--dot`, `-v` / `--verbose`, `-D` / `--debug`, `-h` / `--help`.

**Examples:**

```bash
parse input.c output.ast
parse --yaml input.c output.yaml
parse --dot input.c output.dot
parse input.c -                 # stdout
```

### `lower` (translator driver)

**Input:** binary AST stream as produced by `parse` (opened with `ast_import_open` / `import_external_decl`).

**Processing order** (per top-level declaration): `resolve` → `typecheck_global_decl` → `label_loops` → `translate` → emit.

**TAC lowering status:** Complete. Arithmetic, control flow, all function call forms (direct and indirect), pointers, arrays, structs/unions, type casts, `_Generic` selection, compound literals, and aggregate local-variable initializers all lower correctly.

**Options:** `--tac`, `--yaml`, `--dot`, `-v`, `-D`, `-h` (see `translator/main.c`).

**Debug (`-D`):** enables translator/import/export/wio debug flags and, when TAC exists, could print TAC via `print_tac_toplevel`; also prints imported AST with `print_external_decl` before analysis.

## Components

### Scanner (`scanner/`)

Hand-written lexer. Token set follows C11-style tokens for preprocessed source.

| File | Role |
|------|------|
| `scanner.h`, `scanner.c` | Token definitions and lexer |
| `tests.cpp` | GoogleTest suite (`scanner-tests`) |

### Parser (`parser/`)

Recursive-descent parser guided by the C11 grammar in `grammar/` (not generated from Yacc). Builds AST nodes and manages a name table for ordinary identifiers.

| File | Role |
|------|------|
| `parser.h`, `parser.c` | Parser API and implementation |
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
| `semantic.h` | Umbrella public header for the semantic subsystem |
| `symtab.c`, `symtab.h` | Scoped identifier → Symbol map |
| `structtab.c`, `structtab.h` | Scoped struct/union/enum tag → StructDef map |
| `typetab.c`, `typetab.h` | Scoped typedef name → TypeDef map |
| `typecheck.c` | Type checking and name binding (single-pass) |
| `expressions.c` | Expression semantic analysis |
| `init_normalize.c` | Initializer normalization: designators, brace elision, braced scalars (see [Initializers](#initializers-designators-and-brace-elision)) |
| `initializers.c` | Initializer consumers: static data (`build_static_init`) and automatic typecheck (`typecheck_init`) |
| `statements.c` | Statement semantic analysis |
| `declarations.c` | Declaration processing |
| `label_loops.c` | Annotates loop/switch statements with break/continue jump targets |
| `type_utils.c` | Type helpers: `get_size`, `get_alignment`, `is_integer`, etc. |
| `const_convert.c` | Constant-expression evaluation and conversion |
| `target.c`, `target.h` | Target architecture parameterization (type sizes, alignment) |
| `symtab_print.c` | Debug printer for symtab entries |
| `structtab_print.c` | Debug printer for structtab entries |
| `typetab_print.c` | Debug printer for typetab entries |

Tests: `symtab_tests.cpp`, `structtab_tests.cpp`, `typetab_tests.cpp`, `typecheck_tests.cpp`, `real_tests.cpp`, `pipeline_tests.cpp`, `label_loops_tests.cpp`, `const_convert_tests.cpp`, `coercion_tests.cpp`, `init_normalize_tests.cpp` → `semantic-tests`.

### Translator (`translator/`)

| File | Role |
|------|------|
| `translate.h`, `translate.c` | Shared helpers, type conversion, top-level entry points |
| `test/translate_test.h` | Test fixture helpers shared across translator test files |
| `expr.c` | AST `Expr` → TAC instruction lowering |
| `stmt.c` | AST `Stmt` → TAC instruction lowering; local declaration init |
| `main.c` | `lower` entry: import → semantic passes → translate → emit |

Tests: `decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp` → `translate-tests`.

### TAC (`tac/`)

| File | Role |
|------|------|
| `tacky.asdl` | Canonical TAC description |
| `tac.h` | TAC structs and enums |
| `tac_alloc.c`, `tac_free.c` | Allocation and free |
| `tac_print.c` | Human-readable TAC printing |
| `tac_compare.c` | Structural comparison |
| `tac_export.c`, `tac_import.c` | Binary wire format (read/write via `wio`) |
| `tags.h` | 4-letter ASCII tag constants for binary wire format (`TAC2`) |
| `tac_yaml.c` | YAML listing (debug/test; not re-importable) |
| `tac_graphviz.c` | Graphviz DOT output |

### BESM-6 backend (`backend/besm6/`)

| File | Role |
|------|------|
| `besm6.asdl` | Canonical ISA description: instructions, addressing modes, calling conventions |
| `besm.h` | C structs for BESM-6 IR (`Besm_Module`, `Besm_Func`, `Besm_Block`, `Besm_Instr`, `Besm_DataSection`, `Besm_DataItem`) |
| `abi.h`, `internal.h` | Target ABI constants (sizes, INT-format bridge) and backend-internal declarations |
| `besm_alloc.c` | Allocation functions (`besm_new_*`) |
| `besm_free.c` | Deallocation functions (`besm_free_*`) |
| `codegen.c` | Top-level program/function codegen driver |
| `frame.c`, `frame.h` | Frame allocation: stack slots for parameters, locals, and aggregates |
| `static.c` | Static data/constant lowering (integers, strings, pointers, floats/doubles) |
| `instr.c` | TAC → BESM-6 instruction selection |
| `emit.c` | Instruction-emit helpers (`emit_xta`, `emit_atx`, `emit_arith_val`, …) |
| `emit_madlen.c` | Madlen assembly emitter (`emit_madlen_module`, `emit_madlen_func`, etc.) |
| `utf8_to_koi7.c`, `utf8_to_koi7.h` | UTF-8 → KOI7 string conversion for static string data |
| `test/*_tests.cpp` | GoogleTest suite (`besm-tests`) — see the test list below |

IR hierarchy: `Besm_Module` → `Besm_Func` (calling convention: `BESM6_C` or `INTERNAL`) → `Besm_Block` → `Besm_Instr` (8 instruction categories: mem, arith, log, exp, reg, mod, branch, extra). Data lives in `Besm_DataSection` → `Besm_DataItem` (8 kinds: Int, Real, Oct, Log, Bss, Equ, Ref, String).

Frame allocation (`frame.c`) assigns a stack slot to every TAC name beginning with `%`
(parameters and automatic locals — see the variable name convention below); any other
referenced name is a module-level global, accessed via `,utc, name` and pre-declared with
a `,subp,` directive. An `ALLOCATE_LOCAL` aggregate slot is never treated as a temporary,
even under a temporary's name (e.g. a compound literal), since it is read through its address.

On BESM-6, `float` and `double` are the same 48-bit native floating-point word, so both C
types map to one representation. After instruction selection a peephole-optimization pass
(`besm_peephole`) runs on the `Besm_Instr` list between selection and Madlen emission,
removing the store/reload, mode-register (`ntr`), compare/branch, and branch/label residue
that one-node-at-a-time selection leaves behind; a post-peephole frame-slot reclamation pass
then shrinks the stack frame to the slots still in use. See
[Peephole_Rewrites.md](Peephole_Rewrites.md) for the catalogue of rewrites.

### TAC YAML format

`tac_export_yaml()` (`tac/tac_yaml.c`) emits one `- toplevel:` block per call. Indentation is 2 spaces per level. **Not re-importable** — debug/test use only.

#### Toplevel kinds

```yaml
- toplevel:
  kind: function
  name: f
  global: true          # false for static
  params:               # omitted when empty
    - param: x
  body:                 # omitted for prototypes
    - instruction:
      kind: ...

- toplevel:
  kind: static_variable
  name: g
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
      kind: char
    size: 6
  init:
    kind: string
    value: hello
    null_terminated: true
```

#### Values — appear under `src:`, `dst:`, `condition:`, `args:`

```yaml
kind: var
name: %x

kind: constant
const:
  kind: int      # int | long | long_long | uint | ulong | ulong_long
                 # | char | uchar | float | double | long_double
  value: 42      # float/double/long_double use %a (hex float) format
```

**Variable name convention.** A `var` name encodes its storage class by its first
character, so a backend can classify it from the name alone (the in-memory `locals`
list is not serialized):

| First char | Meaning | Examples |
|------------|---------|----------|
| `%` + digit | compiler temporary | `%0`, `%1` |
| `%` + letter/`_` | parameter or automatic local | `%x`, `%_buf` |
| letter / `_` / `$` | module-level global, static, string constant, or function | `g`, `_str0`, `printf` |

The translator establishes this in two steps: `new_temp()` mints temporaries already
percent-prefixed, and a per-function pass (`percent_locals_in_function` in
`translator/translate.c`, run just before the optimizer) prefixes every parameter and
automatic-local name — in the body and in the stored `params`/`locals` lists — with `%`.
The BESM-6 frame allocator (`backend/besm6/frame.c`) then assigns a stack slot to any
`%`-prefixed name and treats every other referenced name as an external global.

**Instructions** (all have `- instruction:` header; fields follow at +2 indent)

| `kind:` | Additional fields |
|---------|-------------------|
| `return` | `src:` val (omitted for void return) |
| `copy` | `src:` `dst:` |
| `sign_extend` | `src:` `dst:` |
| `zero_extend` | `src:` `dst:` |
| `truncate` | `src:` `dst:` |
| `int_to_double` | `src:` `dst:` |
| `uint_to_double` | `src:` `dst:` |
| `double_to_int` | `src:` `dst:` |
| `double_to_uint` | `src:` `dst:` |
| `unary` | `op:` `src:` `dst:` |
| `binary` | `op:` `src1:` `src2:` `dst:` |
| `get_address` | `src:` `dst:` |
| `load` | `src_ptr:` `dst:` |
| `store` | `src:` `dst_ptr:` |
| `add_ptr` | `ptr:` `index:` `scale: N` `dst:` |
| `ptr_diff` | `ptr_a:` `ptr_b:` `dst:` |
| `copy_to_offset` | `src:` `dst: name` (bare string) `offset: N` |
| `copy_from_offset` | `src: name` (bare string) `offset: N` `dst:` |
| `jump` | `target: label` |
| `jump_if_zero` | `condition:` `target: label` |
| `jump_if_not_zero` | `condition:` `target: label` |
| `label` | `name: label` |
| `fun_call` | `fun_name: f` `indirect: true` (omitted when false) `args:` list (omitted when none) `dst:` (omitted for void) |
| `fun_call_noreturn` | same fields as `fun_call`; a direct call to a `_Noreturn` function — the BESM-6 backend tail-jumps to it and drops the dead post-call path |

Unary ops: `complement`, `negate`, `not`.
Binary ops: `add`, `subtract`, `multiply`, `divide`, `remainder`, `equal`, `not_equal`, `less_than`, `less_or_equal`, `greater_than`, `greater_or_equal`, `bitwise_and`, `bitwise_or`, `bitwise_xor`, `left_shift`, `right_shift`.

**Types** (appear under `type:`, `elem_type:`, `target:`, `ret_type:`, `param_types:`)

```yaml
kind: int | uint | long | ulong | long_long | ulong_long
     | char | schar | uchar | short | ushort | float | double | long_double | void

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

kind: structure
tag: MyStruct
```

**Static init kinds** (list items under `init_list:`, single item under `init:`)

| `kind:` | Fields |
|---------|--------|
| `i8` / `i16` / `i32` / `i64` | `value:` (signed) |
| `u8` / `u16` / `u32` / `u64` | `value:` (unsigned) |
| `float` | `value:` (hex float) |
| `double` | `value:` (hex float) |
| `long_double` | `value:` (hex float) |
| `zero` | `bytes: N` |
| `string` | `value:` `null_terminated: true\|false` |
| `pointer` | `name:` |

### Grammar (`grammar/`)

Reference grammars and notes. See [grammar/README.md](../grammar/README.md) for the relationship between Yacc, Lex, and ASDL files (`c11.y`, `c11.l`, `c11.asdl`).

### Utilities (`libutil/`)

| Module | Files | Purpose |
|--------|--------|---------|
| **xalloc** | `xalloc.c`, `xalloc.h`, `xalloc_tests.cpp` | Tracked allocation; `xfree_all`; `xstruniq()` for unique name generation; leak reporting in debug builds |
| **wio** | `wio.c`, `wio.h` | Binary I/O for AST and TAC streams |
| **string_map** | `string_map.c`, `string_map.h` | Map used in symbol and type tables |

Tests: `string_map_tests.cpp`, `wio_tests.cpp`, `xalloc_tests.cpp` → `libutil-tests`.

### Scripts (`scripts/`)

| File | Purpose |
|------|---------|
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

`$` is accepted as an identifier character (as in GCC/Clang). The BESM-6 backend sanitizes
`$` to `/`, so a C name like `b$tout` becomes the Madlen symbol `b/tout`, letting C source
reference slash-named assembly helpers in the runtime library.

### Multi-character constants

A character constant containing more than one character (e.g. `'ab'`) is implementation-defined
by C11 §6.4.4.1; this compiler packs its bytes GCC-style:

- A byte with bit 7 = 0 is a single ASCII byte.
- A byte with bit 7 = 1 must begin a **valid UTF-8 sequence**; the whole sequence is validated and
  its **raw bytes** are kept verbatim (no codepoint decoding, no KOI7 conversion). An invalid lead
  or continuation byte is a fatal error.
- A backslash escape (`'\n'`, `'\xC3'`, `'\303'`) contributes its byte value (low 8 bits) with no
  UTF-8 validation.

The bytes are packed **big-endian, zero-padded from the left** (so `'ab'` → `0x6162`, `'é'` →
`0xC3A9`). The result type depends on length:

| Packed bytes | Type | Notes |
| --- | --- | --- |
| 1–5 | `int` | |
| 6–8 | `unsigned int` | A deliberate extension: the standard says character constants are `int`. |
| > 8 | — | Fatal error in the parser. |

A constant wider than 32 bits must also fit the target's `int` (or `unsigned int`), or the
semantic pass rejects it. On BESM-6 that allows up to 5 bytes as `int` (41 bits) and 6 as
`unsigned int` (48 bits). The AST integer fields use 64-bit host storage.

## Build system

- **CMake** minimum 3.10; root project name: `c-scanner`.
- **C** standard: C11; **C++** for tests: C++17.
- **Compiler flags:** `-Wall -Werror -Wshadow` for C++ (see root `CMakeLists.txt`).
- **GoogleTest:** FetchContent, tag `v1.15.2`, `BUILD_GMOCK=OFF`.
- **cppcheck:** If `cppcheck` is found, it is attached to C and C++ targets with project-specific suppressions and `scripts/googletest.xml` for tests.
- **Makefile:** Creates `build/`, runs `cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo`, delegates `all` to `$(MAKE) -C build`. Targets: `make` (compiler, runtime, and all test executables), `make test` (builds `all`, but does not run the tests), `make run` (builds `all`, then runs every test via `ctest --test-dir build` — including the textbook chapter tests), `make clean`, `make debug` (cmake Debug build into `build`).

Common build types: `Debug`, `RelWithDebInfo`, `Release`.

## ASDL and C code

The `.asdl` files (`ast/ast.asdl`, `tac/tacky.asdl`, `grammar/c11.asdl`) describe the intended shape of the AST and TAC. The **CMake build does not generate C headers from ASDL**; `ast.h` and `tac.h` are maintained manually to match those specs. Use `scripts/validate_asdl.py` if you change ASDL and want a quick parse check.

Backend ISA descriptions (`backend/besm6/besm6.asdl`, `backend/x86/x86_64.asdl`, and others under `backend/`) follow the same pattern — canonical specs maintained in sync with the hand-written C headers (`besm.h`, etc.).

## Testing

Build and run all tests:

```bash
make run
# or
cmake --build build
ctest --test-dir build
```

The test executables are built by the default `make`/`make all` alongside the compiler and
runtime; `make run` builds everything and then runs ctest. The "Writing a C Compiler" chapter
tests are compiled into these same per-module test binaries (see **Chapter (book) tests**
below), so `make run` runs them too. Test executables and their unit-test sources:

| Executable | Sources (under repo root) |
|------------|---------------------------|
| `scanner-tests` | `scanner/test/tests.cpp` |
| `parser-tests` | `parser/test/simple_tests.cpp`, …, `serialize_tests.cpp` (9 files) |
| `ast-tests` | `ast/test/clone_tests.cpp` |
| `libutil-tests` | `libutil/test/string_map_tests.cpp`, `wio_tests.cpp`, `xalloc_tests.cpp` |
| `tac-tests` | `tac/test/yaml_tests.cpp`, `graphviz_tests.cpp`, `binary_tests.cpp` |
| `semantic-tests` | `semantic/test/symtab_tests.cpp`, `structtab_tests.cpp`, `typetab_tests.cpp`, `typecheck_tests.cpp`, `real_tests.cpp`, `pipeline_tests.cpp`, `label_loops_tests.cpp`, `const_convert_tests.cpp`, `coercion_tests.cpp` |
| `besm-tests` | `backend/besm6/test/codegen_tests.cpp`, `arith_tests.cpp`, `convert_tests.cpp`, `copy_tests.cpp`, `flow_tests.cpp`, `frame_tests.cpp`, `init_tests.cpp`, `label_tests.cpp`, `ptr_tests.cpp`, `run_tests.cpp`, `struct_tests.cpp`, `unary_tests.cpp` |
| `translate-tests` | `translator/test/decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp` |

Run a single binary from `build/`:

```bash
./build/libutil-tests
./build/scanner/scanner-tests
./build/parser/parser-tests
./build/ast/ast-tests
./build/tac/tac-tests
./build/semantic/semantic-tests
./build/translator/translate-tests
./build/backend/besm6/besm-tests
```

### Chapter (book) tests

The "Writing a C Compiler" chapter tests (`*/test/chapter*_tests.cpp`, and the run programs
shared by every backend in `backend/common/test/book/`) are compiled **into the regular per-module test
binaries** — the chapter sources are listed in the same `add_executable(<module>-tests …)`
as the unit tests. So `parser-tests` contains `chapter1..18_tests.cpp` alongside its unit
tests, `besm-tests` contains `chapter1..20_tests.cpp`, etc. There are no separate
`*-book-tests` executables and no ctest `book` label; `make test` builds and runs them with
everything else.

`fatal_error()` (the compiler libraries call it, but its definition lives in the test
executable, not a library) is defined exactly once per binary in a regular unit-test source —
`parser/test/simple_tests.cpp`, `semantic/test/typecheck_tests.cpp`, `optimize/test/pipeline_tests.cpp`,
`backend/besm6/test/codegen_tests.cpp` — and the chapter sources do not redefine it. The scanner
needs none — it reports lexical errors via its own `lex_error()`/`exit()`.

## Development notes

### Memory

`xalloc` tracks allocations; `xfree_all()` frees everything in bulk at shutdown. Debug paths can report leaks (see `xalloc` usage with `-D` in the tools).

### Debugging

- **`parse -D`:** parser debug, AST pretty-print to stdout before export, import/export/wio debug, `xreport_lost_memory` at end.
- **`lower -D`:** translator debug, AST print on import, import/export/wio debug; prints TAC with `print_tac_toplevel` after lowering.

### Visualization

AST DOT export works:

```bash
parse --dot input.c ast.dot
dot -Tpng ast.dot -o ast.png
```

TAC DOT and YAML export work the same way — pass `--dot` or `--yaml` to `lower`:

```bash
lower --yaml input.ast -        # YAML TAC to stdout
lower --dot input.ast tac.dot
dot -Tpng tac.dot -o tac.png
```

## References

- **Book:** [Nora Sandler, *Writing a C Compiler*](https://nostarch.com/writing-c-compiler) — pedagogical pipeline similar to this project’s stages.
- **C11 grammar:** Yacc/Lex heritage (e.g. Jeff Lee’s ANSI C grammar) updated toward C11; see `grammar/`.
- **Unix v7 on BESM-6:** [v7besm](https://github.com/besm6/v7besm)
- **Dubna monitor:** [dubna](https://github.com/besm6/dubna)
