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
| x86-64        | complete | System V psABI (`-t x86_64`), x87 `long double`; links with clang's objects |
| AVR           | complete | 8-bit ATmega1280, avr-gcc ABI (`-t avr`), 16-bit `int`, binary32 `double`; links with clang's objects |
| MSP430        | complete | 16-bit classic MSP430, MSP430 EABI as GCC has it (`-t msp430`), soft binary64 `double`; links with GCC's objects |
| MMIX          | complete | Knuth's 64-bit big-endian RISC, MMIXware ABI as GCC has it (`-t mmix`), register stack; links with GCC's objects |
| BESM-6        | complete | 48-bit word-addressed mainframe; three assembler dialects               |

The working targets could hardly be further apart — modern byte-addressed RISC machines,
a two-operand CISC with an 80-bit `long double`, an 8-bit microcontroller with a 16-bit
`int`, a 16-bit memory-to-memory microcontroller with a software `double`, a big-endian
machine whose calls rename a stack of registers, and a word-addressed machine with its own floating-point format and character set —
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
| `genx86`   | TAC           | x86-64 assembly             |
| `genavr`   | TAC           | AVR assembly                |
| `genmsp430` | TAC          | MSP430 assembly             |
| `genmmix`  | TAC           | MMIX assembly               |
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
chain: `vcc hello.c` preprocesses, compiles, assembles and links a program for the
machine it runs on (`-t x86_64-linux` or `-t aarch64-linux`, linked against glibc by the
system's C compiler, or `-t aarch64-darwin` on a Mac with Apple silicon, against
libSystem), or with `-t` for a bare-metal target, links with
the target's GNU binutils (`riscv64-unknown-elf-as`/`-ld`, `aarch64-none-elf-`,
`arm-none-eabi-`, `x86_64-elf-` or the host's, `avr-`, `msp430-elf-`,
`mmix-knuth-mmixware-`), or with clang and `ld.lld` where there are no binutils for the
target (`-t besm6`: `b6as`/`b6ld`). It accepts the usual `-c`, `-S`, `-E`, `-o`, `-D`,
`-I`, `-L` and `-l`.

## Getting started

### Prerequisites

The compiler itself needs only CMake 3.10 or newer and a C11 compiler. The tests also need
a C++17 compiler and, the first time you configure, network access, since CMake downloads
GoogleTest. Each target's runtime library and run tests need more tools, listed below;
without them the runtime of that target is not built and its run tests are skipped.

| Target | Assembler and linker | Simulator | Optional reference compiler |
|---|---|---|---|
| RISC-V 64 and 32 | `riscv64-unknown-elf-` (or `riscv64-elf-`, `riscv64-linux-gnu-`) binutils | `qemu-system-riscv64`, `qemu-system-riscv32` | clang |
| AArch64 | `aarch64-none-elf-` (or `aarch64-elf-`, `aarch64-linux-gnu-`) binutils | `qemu-system-aarch64` | clang |
| ARM32 | `arm-none-eabi-` binutils | `qemu-system-arm` | clang |
| x86-64 | `x86_64-elf-` binutils, or the host's own on x86-64 Linux | `qemu-system-x86_64` | clang |
| AVR | `avr-` binutils | `qemu-system-avr` | clang |
| MSP430 | `msp430-elf-` (or `msp430-unknown-elf-`) binutils | [mspsim](https://github.com/sergev/mspsim) | `msp430-elf-gcc` with newlib; clang |
| MMIX | `mmix-knuth-mmixware-` binutils, GCC and newlib | Knuth's `mmix` from [MMIXware](https://www-cs-faculty.stanford.edu/~knuth/mmix.html) | |
| BESM-6 | `b6as`, `b6ld` from [v7besm](https://github.com/besm6/v7besm) | `b6sim` from v7besm; `dubna` (with `besmc` for Bemsh) | |

Where no binutils are found, a clang with the target, `ld.lld` and `llvm-ar` assemble,
link and archive instead (`cmake -DVCC_CROSS_TOOLS=gnu|llvm` forces one or the other).
The *reference compiler* builds the other half of the interoperability tests and, for the
"Writing a C Compiler" programs, a second build whose output must match ours; without it
those comparisons are skipped. `cppcheck`, when installed, checks every source during
the build.

**Debian and Ubuntu:**

```bash
sudo apt install build-essential cmake git cppcheck \
    binutils-riscv64-unknown-elf binutils-aarch64-none-elf binutils-arm-none-eabi \
    binutils-avr binutils-msp430-unknown-elf \
    qemu-system-misc qemu-system-arm qemu-system-x86
sudo apt install qemu-system-riscv        # Debian 13 and later: RISC-V is split out
sudo apt install clang lld llvm           # optional: the reference compiler
```

x86-64 uses the host's binutils on an x86-64 machine (`binutils-x86-64-linux-gnu`
elsewhere). Where a distribution lacks the `-none-elf` packages,
`binutils-riscv64-linux-gnu` and `binutils-aarch64-linux-gnu` serve as well. The MSP430
GCC and newlib, and the whole MMIX toolchain, are built from source:
see [docs/Howto_build_MSP430_GCC.md](docs/Howto_build_MSP430_GCC.md) and
[docs/Howto_build_MMIXware.md](docs/Howto_build_MMIXware.md). Build mspsim from
[its repository](https://github.com/sergev/mspsim). Install all three into `~/.local`:
CMake looks in `~/.local/bin`.

**macOS (Homebrew):**

```bash
brew install cmake cppcheck qemu \
    riscv64-elf-binutils aarch64-elf-binutils arm-none-eabi-binutils x86_64-elf-binutils
brew tap osx-cross/avr && brew install avr-binutils
brew install llvm lld                     # optional: the reference compiler
```

There is no Homebrew formula for the MSP430 and MMIX toolchains: build them from source
as above.

### Building and testing

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

After `make install`, the driver does all of that, and the assembling and linking too.
By default it builds for the machine it runs on: on x86-64 or AArch64 Linux, an
executable linked against glibc (on another host the default is `riscv64`):

```bash
vcc hello.c -lm
./a.out
```

For RISC-V, add `-t riscv64` and run it under qemu:

```bash
vcc -t riscv64 -o hello.elf hello.c
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
[docs/Arm32_Backend.md](docs/Arm32_Backend.md).

For x86-64, add `-t x86_64` and run it under `qemu-system-x86_64`'s `microvm` machine:

```bash
vcc -t x86_64 -o hello-x86.elf hello.c
qemu-system-x86_64 -M microvm -display none -serial stdio -monitor none \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel hello-x86.elf
```

qemu exits with `(main's result << 1) | 1`. By hand, it is `cpp -t x86_64` with
`libc/x86/include` (then `libc/lp64/include` and `libc/common/include`), `lower -t x86_64`
and `genx86`; see [docs/X86_64_Backend.md](docs/X86_64_Backend.md).

For AVR, add `-t avr` and run it under `qemu-system-avr`'s `arduino-mega` machine:

```bash
vcc -t avr -o hello-avr.elf hello.c
qemu-system-avr -M arduino-mega -display none -monitor none \
    -serial stdio -serial file:status -bios hello-avr.elf
```

`main`'s result is the byte in the file `status`; qemu does not exit by itself, so stop it
with Ctrl-C. By hand, it is `cpp -t avr` with `libc/avr/include` (then
`libc/ip16/include` and `libc/common/include`), `lower -t avr` and `genavr`; see
[docs/Avr_Backend.md](docs/Avr_Backend.md).

For MSP430, add `-t msp430` and run it under mspsim:

```bash
vcc -t msp430 -o hello-msp430.elf hello.c
mspsim hello-msp430.elf
```

mspsim exits with `main`'s result. By hand, it is `cpp -t msp430` with
`libc/msp430/include` (then `libc/ip16/include` and `libc/common/include`),
`lower -t msp430` and `genmsp430`; see [docs/Msp430_Backend.md](docs/Msp430_Backend.md).

For MMIX, add `-t mmix` and run it under Knuth's simulator:

```bash
vcc -t mmix -o hello.mmo hello.c
mmix hello.mmo
```

mmix exits with `main`'s result. By hand, it is `cpp -t mmix` with `libc/mmix/include`
(then `libc/lp64/include` and `libc/common/include`), `lower -t mmix` and `genmmix`; see
[docs/Mmix_Backend.md](docs/Mmix_Backend.md).
BESM-6 works the same way with `-t besm6` and its own code generator.

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
| `bin/vgenx86`                  | the x86-64 code generator                       |
| `bin/vgenavr`                  | the AVR code generator                          |
| `bin/vgenmsp430`               | the MSP430 code generator                       |
| `bin/vgenmmix`                 | the MMIX code generator                         |
| `bin/vgenbesm6`                | the BESM-6 code generator                       |
| `share/vcc/<target>/include/`  | the target's C headers                          |
| `share/vcc/<target>/lib/`      | the target's runtime and C library              |

For RISC-V, ARM, x86-64, AVR and MSP430, `lib/` holds `crt0.o`, `libc.a` and the linker script
for qemu (for mspsim on MSP430), and `include/` every C header. MMIX has the same, but for
the linker script: the linker's own suits `mmix`. For BESM-6, which has its own operating system with its own C library
(the [v7besm](https://github.com/besm6/v7besm) Unix port), only what describes the
compiler itself is installed: the freestanding C11 headers, the intrinsics header and the
helper routines the generated code calls.

## What the runtime provides

Programs compiled here have a usable C library: `printf`, `sprintf` and `snprintf`;
`puts`, `putchar` and console input; the whole of `<string.h>` and the `mem*` family;
`malloc` and friends; `atoi`; `exit`; math helpers (`fabs`, `fmin`, `fmax`, `fma`,
`modf`, `frexp`, `ldexp`; `sqrt` and `sqrtf` on all but the BESM-6 and AVR); and working variable arguments (`<stdarg.h>`). On RISC-V and
AArch64, `long double` is IEEE binary128, computed in software. On 32-bit RISC-V and ARM32, `long long`
is computed inline in register pairs, with division and the conversions to and from
floating point in the runtime (the routines clang's code calls too); on ARM32,
`long double` is a `double`. On x86-64 it is the x87 80-bit format, computed by the x87,
and there is `setjmp`/`longjmp`. On AVR, `int` is 16 bits, `float` and `double` are both
binary32, computed in software, and there is `setjmp`/`longjmp` too. On MSP430, `int` is
16 bits, `float` is binary32 and `double` binary64, both computed in software, with
`setjmp`/`longjmp`. On MMIX, `float` and `double` are computed in hardware, as binary64,
and so are 64-bit multiply and divide, so the runtime has no helper routines; there is
`setjmp`/`longjmp`. The portable part of
the library lives in [libc/common/](libc/common/) and is shared by every target, and
[libc/lp64/](libc/lp64/) holds what the 64-bit targets share, [libc/ilp32/](libc/ilp32/)
what the 32-bit targets share; each target has its own
directory for the rest ([libc/riscv64/](libc/riscv64/), [libc/riscv32/](libc/riscv32/),
[libc/aarch64/](libc/aarch64/), [libc/arm32/](libc/arm32/), [libc/x86/](libc/x86/),
[libc/avr/](libc/avr/), [libc/msp430/](libc/msp430/), [libc/mmix/](libc/mmix/)).

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

### x86-64 target

| Document                                         | What it covers                                                       |
| ------------------------------------------------ | -------------------------------------------------------------------- |
| [docs/X86_64_Backend.md](docs/X86_64_Backend.md) | The code generator, psABI calls and variadics, the x87 `long double`, frames, and running under qemu |

### AVR target

| Document                                   | What it covers                                                                  |
| ------------------------------------------ | ------------------------------------------------------------------------------- |
| [docs/Avr_Backend.md](docs/Avr_Backend.md) | The code generator, the 16-bit data model, frames and `Y+63`, branch relaxation, calls, the runtime's helper contracts, and running under qemu |

### MSP430 target

| Document                                         | What it covers                                                                  |
| ------------------------------------------------ | ------------------------------------------------------------------------------- |
| [docs/Msp430_Backend.md](docs/Msp430_Backend.md) | The code generator, memory-to-memory selection, frames, branch relaxation, GCC's calls with structures by reference, the soft binary64 and the helper contracts, and running under mspsim |

### MMIX target

| Document                                     | What it covers                                                                  |
| -------------------------------------------- | ------------------------------------------------------------------------------- |
| [docs/Mmix_Backend.md](docs/Mmix_Backend.md) | The code generator, the register stack and GCC's fixed register model, frames, GCC's calls, structures and variadics, big-endian notes, signed division, and running under `mmix` |

## License

MIT. See [LICENSE](LICENSE).

Copyright (c) 2025-2026 Serge Vakulenko
