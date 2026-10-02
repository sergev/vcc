# VCC — a retargetable C11 compiler

VCC is a compiler for the C programming language (the 2011 standard, C11), small enough
to read from top to bottom. One machine-independent front end produces a simple
intermediate code; a separate code generator per target turns it into assembly. Adding a
machine means writing one more code generator — the front end, the analyzer and the
optimizer stay as they are.

## Targets

| Target        | Status   | Notes                                                                   |
| ------------- | -------- | ----------------------------------------------------------------------- |
| RISC-V 64     | complete | RV64IMFD, standard LP64D calling convention; links with clang's objects |
| BESM-6        | complete | 48-bit word-addressed mainframe; three assembler dialects               |
| RISC-V 32     | planned  | see [backend/riscv/Plan.md](backend/riscv/Plan.md)                      |
| x86-64, AArch64, ARM32, others | design notes | sketches under [backend/](backend/)                 |

The two working targets could hardly be further apart — a modern byte-addressed RISC
machine and a word-addressed machine with its own floating-point format and character set
— which keeps the front end honest: nothing in it may assume one particular kind of
machine. Each target is described in its own documents (see [Documentation](#documentation)).

## How it works

A compiler is a pipeline. Each stage rewrites the program into a form a little closer to
the machine:

1. **Scanner** — splits the source text into words and symbols (*tokens*).
2. **Parser** — arranges those tokens into a tree that mirrors the structure of the
   program (a *syntax tree*).
3. **Semantic analysis** — checks the meaning: do the types agree, does every name refer to
   something that was declared?
4. **Lowering and optimization** — rewrites the tree into a simple, machine-independent
   list of instructions called *three-address code* (TAC), then improves it: folding
   constants, deleting unreachable code, and removing pointless copies and stores.
5. **Code generation** — turns TAC into assembly for the target machine: register
   allocation, instruction selection, and a *peephole* pass that spots and shortens
   wasteful instruction sequences.

```mermaid
flowchart LR
    Source[C source] --> Scanner --> Parser --> Tree[Syntax tree]
    Tree --> Semantic[Semantic analysis] --> TAC[Three-address code]
    TAC --> Optimizer --> Codegen[Target code generator] --> Asm[Assembly]
```

Stages 1–4 are machine-independent; a target plugs in at stage 5. The only thing the
front end needs to know about a target is a small descriptor, chiefly the sizes and
alignment of the C types.

## The programs

The compiler is not one binary but several, run one after another:

| Program    | Reads         | Writes                      |
| ---------- | ------------- | --------------------------- |
| `parse`    | C source      | a syntax tree (`.ast`)      |
| `lower`    | a syntax tree | three-address code (`.tac`) |
| `genriscv` | TAC           | RISC-V assembly             |
| `genbesm`  | TAC           | BESM-6 assembly             |

`lower` takes the target with `-t` (for example `-t riscv64`), since type sizes and
alignment differ between machines.

Splitting them apart makes each stage easy to inspect on its own: every program can also
print its output as readable YAML text (`--yaml`) or as a diagram for
[Graphviz](https://graphviz.org/) (`--dot`).

There is no preprocessor in this repository. If your program uses `#include` or `#define`,
run it through your system compiler's preprocessor first, pointing it at the target's
headers: `cc -E -nostdinc -Ilibc/riscv/include -Ilibc/common/include prog.c`.

## Getting started

**You need** CMake 3.10 or newer and a C11 compiler. Building the tests also needs a C++17
compiler and, the first time you configure, network access so CMake can download
GoogleTest. The RISC-V runtime and run tests need a RISC-V clang, `ld.lld` and
`qemu-system-riscv64` (on macOS: Homebrew `llvm`, `lld` and `qemu`); without them those
tests are skipped, as are the tests of any other target whose tools are missing.

```bash
make            # build the compiler and the runtime libraries
make run        # build and run the full test suite
make install    # install (see below)
```

Compile a small program by hand and look at each stage:

```bash
./build/parse hello.c hello.ast                 # C source -> syntax tree
./build/lower -t riscv64 hello.ast hello.tac    # tree     -> three-address code
./build/backend/genriscv hello.tac hello.s      # TAC      -> RISC-V assembly
```

How to assemble, link and run the result under qemu is in
[docs/Riscv_Backend.md](docs/Riscv_Backend.md). Other targets work the same way with
their own `-t` and code generator.

To read what happened at any stage, ask for YAML instead:

```bash
./build/parse --yaml hello.c hello.yaml
./build/lower --yaml hello.ast hello.yaml
```

Or draw it as a picture, if you have Graphviz:

```bash
./build/parse --dot hello.c hello.dot
dot -Tpng hello.dot -o hello.png
```

## What gets installed

`make install` puts everything into `~/.local`. (To choose your own location:
`cmake --install build --prefix /opt/vcc`.) The programs get a `v` prefix; each target's
libraries and headers go into their own directory under `share/vcc/`.

| Installed as                   | What it is                                      |
| ------------------------------ | ----------------------------------------------- |
| `bin/vparse`                   | the parser                                      |
| `bin/vlower`                   | the analyzer and optimizer                      |
| `bin/vgenriscv64`              | the RISC-V code generator                       |
| `bin/vgenbesm6`                | the BESM-6 code generator                       |
| `share/vcc/<target>/include/`  | the target's C headers                          |
| `share/vcc/<target>/lib/`      | the target's runtime and C library              |

For RISC-V, `lib/` holds `crt0.o`, `libc.a` and the qemu linker script, and `include/`
every C header. For BESM-6, which has its own operating system with its own C library
(the [v7besm](https://github.com/besm6/v7besm) Unix port), only what describes the
compiler itself is installed: the freestanding C11 headers, the intrinsics header and the
helper routines the generated code calls.

## What the runtime provides

Programs compiled here have a usable C library: `printf`, `sprintf` and `snprintf`;
`puts`, `putchar` and console input; the whole of `<string.h>` and the `mem*` family;
`malloc` and friends; `atoi`; `exit`; math helpers (`fabs`, `fmin`, `fmax`, `fma`,
`modf`, `frexp`, `ldexp`); and working variable arguments (`<stdarg.h>`). On RISC-V,
`long double` is IEEE binary128, computed in software. The portable part of the library
lives in [libc/common/](libc/common/) and is shared by every target.

## Documentation

Start with [docs/Learn_From_This_Project.md](docs/Learn_From_This_Project.md) if you want
the guided tour, or [docs/Technical_Reference.md](docs/Technical_Reference.md) if you want
the map of the source tree.

### About the compiler

| Document                                                           | What it covers                                              |
| ------------------------------------------------------------------ | ----------------------------------------------------------- |
| [docs/Technical_Reference.md](docs/Technical_Reference.md)         | Source layout, every component, the build system, the tests |
| [docs/Learn_From_This_Project.md](docs/Learn_From_This_Project.md) | A walkthrough of how the compiler was built and why         |
| [docs/Tests_From_The_Book.md](docs/Tests_From_The_Book.md)         | How the test suite is organized, for newcomers              |
| [docs/C_Grammar.md](docs/C_Grammar.md)                             | The C grammar, and how it maps onto the hand-written parser |
| [docs/Type_Coercion.md](docs/Type_Coercion.md)                     | C's rules for mixing types in an expression                 |
| [docs/Type_Sizes_Alignment.md](docs/Type_Sizes_Alignment.md)       | Type sizes and alignment, per machine                       |
| [docs/TAC_Optimization.md](docs/TAC_Optimization.md)               | The machine-independent optimizer passes                    |
| [docs/Standard_Include_Files.md](docs/Standard_Include_Files.md)   | The C11 headers shipped with the compiler                   |
| [docs/Memory_Allocation.md](docs/Memory_Allocation.md)             | `xalloc`, the compiler's own allocator                      |
| [docs/String_Map.md](docs/String_Map.md)                           | `string_map`, the key-value store used for symbol tables    |
| [docs/Word_Oriented_IO.md](docs/Word_Oriented_IO.md)               | How the `.ast` and `.tac` binary files are written          |

### RISC-V target

| Document                                       | What it covers                                                   |
| ---------------------------------------------- | ---------------------------------------------------------------- |
| [docs/Riscv_Backend.md](docs/Riscv_Backend.md) | The code generator, frame layout, calls, and running under qemu |

### BESM-6 target

| Document                                                                                 | What it covers                                               |
| ---------------------------------------------------------------------------------------- | ------------------------------------------------------------ |
| [backend/besm6/Besm6_Data_Representation.md](backend/besm6/Besm6_Data_Representation.md) | How every C type is stored in a 48-bit word                  |
| [backend/besm6/Besm6_Instruction_Set.md](backend/besm6/Besm6_Instruction_Set.md)         | The instruction set                                          |
| [backend/besm6/Besm6_Calling_Conventions.md](backend/besm6/Besm6_Calling_Conventions.md) | How functions call each other                                |
| [backend/besm6/Besm6_Runtime_Library.md](backend/besm6/Besm6_Runtime_Library.md)         | The helper routines the generated code calls                 |
| [backend/besm6/Besm6_Intrinsics.md](backend/besm6/Besm6_Intrinsics.md)                   | Reaching machine instructions that C has no way to express   |
| [backend/besm6/Peephole_Rewrites.md](backend/besm6/Peephole_Rewrites.md)                 | The peephole pass and every rewrite it performs              |
| [backend/besm6/KOI7_Encoding.md](backend/besm6/KOI7_Encoding.md)                         | The KOI-7 character set and the conversion the compiler does |
| [backend/besm6/Frexp_Ldexp.md](backend/besm6/Frexp_Ldexp.md)                             | `frexp` and `ldexp` in assembly                              |
| [backend/besm6/Besm6_Unix_Assembler.md](backend/besm6/Besm6_Unix_Assembler.md)           | `b6as`, the Unix assembler (the default dialect)             |
| [backend/besm6/Madlen.md](backend/besm6/Madlen.md)                                       | Madlen, the Dubna monitor assembler                          |
| [backend/besm6/Bemsh.md](backend/besm6/Bemsh.md)                                         | Bemsh, the 1967 autocode with Russian mnemonics              |

## License

MIT. See [LICENSE](LICENSE).

Copyright (c) 2025 Serge Vakulenko
