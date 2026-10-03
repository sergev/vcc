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
| RISC-V 32     | complete | RV32IMFD, ILP32D (`-t riscv32`); the same code generator                |
| AArch64       | complete | ARMv8-A, standard AAPCS64 calling convention (`-t aarch64`); links with clang's objects |
| ARM32         | complete | ARMv7-A, AAPCS-VFP hard-float calling convention (`-t arm32`); links with clang's objects |
| BESM-6        | complete | 48-bit word-addressed mainframe; three assembler dialects               |
| x86-64, others | design notes | sketches under [backend/](backend/)                          |

The working targets could hardly be further apart — modern byte-addressed RISC machines
and a word-addressed machine with its own floating-point format and character set —
which keeps the front end honest: nothing in it may assume one particular kind of
machine. Each target is described in its own documents (see [Documentation](#documentation)).

## How it works

A compiler is a pipeline. Each stage rewrites the program into a form a little closer to
the machine:

1. **Preprocessor** — expands `#include`, `#define` and `#if` (the C preprocessor, `cpp`).
2. **Scanner** — splits the source text into words and symbols (*tokens*).
3. **Parser** — arranges those tokens into a tree that mirrors the structure of the
   program (a *syntax tree*).
4. **Semantic analysis** — checks the meaning: do the types agree, does every name refer to
   something that was declared?
5. **Lowering and optimization** — rewrites the tree into a simple, machine-independent
   list of instructions called *three-address code* (TAC), then improves it: folding
   constants, deleting unreachable code, and removing pointless copies and stores.
6. **Code generation** — turns TAC into assembly for the target machine: register
   allocation, instruction selection, and a *peephole* pass that spots and shortens
   wasteful instruction sequences.

```mermaid
flowchart LR
    Source[C source] --> Cpp[Preprocessor] --> Scanner --> Parser --> Tree[Syntax tree]
    Tree --> Semantic[Semantic analysis] --> TAC[Three-address code]
    TAC --> Optimizer --> Codegen[Target code generator] --> Asm[Assembly]
```

Stages 1–5 are machine-independent (the preprocessor only predefines a few target
macros); a target plugs in at stage 6. The only thing the front end needs to know about
a target is a small descriptor, chiefly the sizes and alignment of the C types.

## The programs

The compiler is not one binary but several, run one after another:

| Program    | Reads         | Writes                      |
| ---------- | ------------- | --------------------------- |
| `cc`       | C, assembly, objects | runs the programs below, then the assembler and linker |
| `cpp`      | C source      | preprocessed C (`.i`)       |
| `parse`    | preprocessed C | a syntax tree (`.ast`)     |
| `lower`    | a syntax tree | three-address code (`.tac`) |
| `genriscv` | TAC           | RISC-V assembly             |
| `genaarch64` | TAC         | AArch64 assembly            |
| `genarm32` | TAC           | ARM32 assembly              |
| `genbesm`  | TAC           | BESM-6 assembly             |

`cpp` and `lower` take the target with `-t` (for example `-t riscv64`): `cpp` for the
predefined macros and the standard header directory, `lower` because type sizes and
alignment differ between machines.

Splitting them apart makes each stage easy to inspect on its own: every program can also
print its output as readable YAML text (`--yaml`) or as a diagram for
[Graphviz](https://graphviz.org/) (`--dot`).

The preprocessor ([cpp/README.md](cpp/README.md)) descends from the Unix v7 `cpp`,
modernized to C11 in the [v7besm](https://github.com/besm6/v7besm) project. In the build
tree, point it at the source headers:
`build/cpp/cpp -t riscv64 -nostdinc -Ilibc/riscv64/include -Ilibc/lp64/include -Ilibc/common/include prog.c prog.i`.
Installed, `vcpp -t riscv64` finds them by itself. (The system `cc -E` works too.)

The driver ([cc/README.md](cc/README.md)), ported from v7besm's `b6cc`, runs the whole
chain: `vcc -o hello.elf hello.c` preprocesses, compiles, assembles with clang and links
with `ld.lld` for RISC-V and ARM (`-t besm6`: `b6as`/`b6ld`). It accepts the usual `-c`, `-S`, `-E`,
`-o`, `-D`, `-I`, `-L` and `-l`.

## Getting started

**You need** CMake 3.10 or newer and a C11 compiler. Building the tests also needs a C++17
compiler and, the first time you configure, network access so CMake can download
GoogleTest. The RISC-V and ARM runtimes and run tests need a clang with those
targets, `ld.lld`, and `qemu-system-riscv64`, `qemu-system-riscv32`,
`qemu-system-aarch64` and `qemu-system-arm` (on macOS: Homebrew `llvm`, `lld` and `qemu`); without them those tests are skipped, as are the tests of any other target whose
tools are missing.

```bash
make            # build the compiler and the runtime libraries
make run        # build and run the full test suite (or: ctest --test-dir build -j8)
make install    # install (see below)
```

Compile a small program by hand and look at each stage:

```bash
./build/cpp/cpp -nostdinc -Ilibc/riscv64/include -Ilibc/lp64/include -Ilibc/common/include \
    hello.c hello.i                             # C source -> preprocessed C
./build/parse hello.i hello.ast                 # C        -> syntax tree
./build/lower -t riscv64 hello.ast hello.tac    # tree     -> three-address code
./build/backend/genriscv hello.tac hello.s      # TAC      -> RISC-V assembly
```

After `make install`, the driver does all of that, and the assembling and linking too:

```bash
vcc -o hello.elf hello.c
qemu-system-riscv64 -M virt -bios none -display none -serial stdio -monitor none \
    -kernel hello.elf
```

For 32-bit RISC-V, add `-t riscv32` and run it under `qemu-system-riscv32`:

```bash
vcc -t riscv32 -o hello32.elf hello.c
qemu-system-riscv32 -M virt -bios none -display none -serial stdio -monitor none \
    -kernel hello32.elf
```

By hand, the 32-bit chain is the same with `cpp -t riscv32` and `libc/riscv32/include`
(then `libc/ilp32/include` in place of `libc/lp64/include`), `lower -t riscv32` and `genriscv --rv32`. How to assemble, link and run the result is in
[docs/Riscv_Backend.md](docs/Riscv_Backend.md).

For AArch64, add `-t aarch64` and run it under `qemu-system-aarch64`, which exits with
`main`'s result:

```bash
vcc -t aarch64 -o hello64.elf hello.c
qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -monitor none \
    -semihosting -kernel hello64.elf
```

By hand, it is `cpp -t aarch64` with `libc/aarch64/include` (then `libc/lp64/include` and
`libc/common/include`), `lower -t aarch64` and `genaarch64`; see
[docs/Aarch64_Backend.md](docs/Aarch64_Backend.md).

For 32-bit ARM, add `-t arm32` and run it under `qemu-system-arm`, which also exits with
`main`'s result:

```bash
vcc -t arm32 -o hello-arm.elf hello.c
qemu-system-arm -M virt -cpu cortex-a15 -display none -serial stdio -monitor none \
    -semihosting -kernel hello-arm.elf
```

By hand, it is `cpp -t arm32` with `libc/arm32/include` (then `libc/ilp32/include` and
`libc/common/include`), `lower -t arm32` and `genarm32`; see
[docs/Arm32_Backend.md](docs/Arm32_Backend.md). BESM-6 works the same way with
`-t besm6` and its own code generator.

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
| `bin/vcc`                      | the compiler driver                             |
| `bin/vcpp`                     | the preprocessor                                |
| `bin/vparse`                   | the parser                                      |
| `bin/vlower`                   | the analyzer and optimizer                      |
| `bin/vgenriscv64`              | the RISC-V code generator                       |
| `bin/vgenriscv32`              | the same, for 32-bit RISC-V                     |
| `bin/vgenaarch64`              | the AArch64 code generator                      |
| `bin/vgenarm32`                | the ARM32 code generator                        |
| `bin/vgenbesm6`                | the BESM-6 code generator                       |
| `share/vcc/<target>/include/`  | the target's C headers                          |
| `share/vcc/<target>/lib/`      | the target's runtime and C library              |

For RISC-V and ARM, `lib/` holds `crt0.o`, `libc.a` and the qemu linker script, and
`include/` every C header. For BESM-6, which has its own operating system with its own C library
(the [v7besm](https://github.com/besm6/v7besm) Unix port), only what describes the
compiler itself is installed: the freestanding C11 headers, the intrinsics header and the
helper routines the generated code calls.

## What the runtime provides

Programs compiled here have a usable C library: `printf`, `sprintf` and `snprintf`;
`puts`, `putchar` and console input; the whole of `<string.h>` and the `mem*` family;
`malloc` and friends; `atoi`; `exit`; math helpers (`fabs`, `fmin`, `fmax`, `fma`,
`modf`, `frexp`, `ldexp`); and working variable arguments (`<stdarg.h>`). On RISC-V and
AArch64, `long double` is IEEE binary128, computed in software. On 32-bit RISC-V and ARM32, `long long`
is computed inline in register pairs, with division and the conversions to and from
floating point in the runtime (the routines clang's code calls too); on ARM32,
`long double` is a `double`. The portable part of
the library lives in [libc/common/](libc/common/) and is shared by every target, and
[libc/lp64/](libc/lp64/) holds what riscv64 and aarch64 share, [libc/ilp32/](libc/ilp32/)
what the 32-bit targets share; each target has its own
directory for the rest ([libc/riscv64/](libc/riscv64/), [libc/riscv32/](libc/riscv32/),
[libc/aarch64/](libc/aarch64/), [libc/arm32/](libc/arm32/)).

## Documentation

Start with [docs/Technical_Reference.md](docs/Technical_Reference.md) for the map of the
source tree.

### About the compiler

| Document                                                         | What it covers                                              |
| ---------------------------------------------------------------- | ----------------------------------------------------------- |
| [docs/Technical_Reference.md](docs/Technical_Reference.md)       | Source layout, every component, the build system, the tests |
| [docs/Tests_From_The_Book.md](docs/Tests_From_The_Book.md)       | How the test suite is organized, for newcomers              |
| [docs/C_Grammar.md](docs/C_Grammar.md)                           | The C grammar, and how it maps onto the hand-written parser |
| [docs/Type_Coercion.md](docs/Type_Coercion.md)                   | C's rules for mixing types in an expression                 |
| [docs/Type_Sizes_Alignment.md](docs/Type_Sizes_Alignment.md)     | Type sizes and alignment, per machine                       |
| [docs/TAC_Optimization.md](docs/TAC_Optimization.md)             | The machine-independent optimizer passes                    |
| [docs/Standard_Include_Files.md](docs/Standard_Include_Files.md) | The C11 headers shipped with the compiler                   |
| [docs/Memory_Allocation.md](docs/Memory_Allocation.md)           | `xalloc`, the compiler's own allocator                      |
| [docs/String_Map.md](docs/String_Map.md)                         | `string_map`, the key-value store used for symbol tables    |
| [docs/Word_Oriented_IO.md](docs/Word_Oriented_IO.md)             | How the `.ast` and `.tac` binary files are written          |

### RISC-V target

| Document                                       | What it covers                                                   |
| ---------------------------------------------- | ---------------------------------------------------------------- |
| [docs/Riscv_Backend.md](docs/Riscv_Backend.md) | The code generator for both widths, frame layout, calls, and running under qemu |

### AArch64 target

| Document                                           | What it covers                                                     |
| -------------------------------------------------- | ------------------------------------------------------------------ |
| [docs/Aarch64_Backend.md](docs/Aarch64_Backend.md) | The code generator, AAPCS64 calls and variadics, frames, and running under qemu |

### ARM32 target

| Document                                       | What it covers                                                        |
| ---------------------------------------------- | --------------------------------------------------------------------- |
| [docs/Arm32_Backend.md](docs/Arm32_Backend.md) | The code generator, AAPCS-VFP calls and variadics, frames, and running under qemu |

## License

MIT. See [LICENSE](LICENSE).

Copyright (c) 2025-2026 Serge Vakulenko
