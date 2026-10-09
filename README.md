# VCC — a retargetable C11 compiler

VCC is a compiler for the C programming language (the 2011 standard, C11), small enough
to read from top to bottom. One machine-independent front end produces a simple
intermediate code; a separate code generator per target turns it into assembly. Adding a
machine means writing one more code generator — the front end, the analyzer and the
optimizer stay as they are.

## Targets

| Target (`-t`)    | Machine and ABI                                                          | Links with      |
| ---------------- | ------------------------------------------------------------------------ | --------------- |
| `x86_64-linux`   | hosted, x86-64 Linux against glibc                                       | the system's    |
| `aarch64-linux`  | hosted, AArch64 Linux against glibc                                      | the system's    |
| `aarch64-darwin` | hosted, macOS on Apple silicon against libSystem, Mach-O                 | the system's    |
| `riscv64`        | RV64IMFD, LP64D                                                          | clang's objects |
| `riscv32`        | RV32IMFD, ILP32D; the same code generator                                | clang's objects |
| `aarch64`        | ARMv8-A, AAPCS64                                                         | clang's objects |
| `arm32`          | ARMv7-A, AAPCS-VFP hard float                                            | clang's objects |
| `x86_64`         | System V psABI, x87 `long double`                                        | clang's objects |
| `wasm32`         | WebAssembly, clang's wasm32 C ABI, a module run under node               | clang's objects |
| `wasm32-braam`   | a process of [Braam](docs/Braam.md), an OS in a browser tab             | clang's objects |
| `avr`            | 8-bit ATmega1280, avr-gcc ABI, 16-bit `int`, binary32 `double`           | clang's objects |
| `msp430`         | 16-bit classic MSP430, GCC's EABI, soft binary64 `double`                | GCC's objects   |
| `mmix`           | Knuth's 64-bit big-endian RISC, GCC's MMIXware ABI, register stack       | GCC's objects   |
| `besm6`          | 48-bit word-addressed mainframe; three assembler dialects                | v7besm's        |

All are complete. `vcc` builds for the machine it runs on by default (one of the three
hosted targets, else `riscv64`); the rest are bare metal, run under a simulator, or for
WebAssembly under node. Targets
this far apart keep the front end honest: nothing in it may assume one kind of machine.

## Two extensions of C

- **`defer`** runs a statement when its block is left, whichever way: `defer
  free(p);` next to the `malloc`. It works on every target.
- **Coroutines**: functions that stop at `yield`, return to their caller and continue
  later, with `await` to call one from another. They work on every target but BESM-6.
  On wasm32 they let a program for [Braam](docs/Braam.md), which may never wait inside
  a call, be written as ordinary C: `n = await read(fd, buf, len);`.

[docs/Coroutines_in_C.md](docs/Coroutines_in_C.md) is the tutorial for both. They
use reserved spellings (`_Defer`, `_Coro`, `_Yield`, `_Await`, …), and the short
names come from `<coro.h>`, so no existing program changes meaning.

## How it works

1. **Preprocessor** (`cpp`) — expands `#include`, `#define` and `#if`.
2. **Parser** (`parse`) — splits the text into tokens and builds a syntax tree.
3. **Analysis, lowering, optimization** (`lower`) — checks types and names, rewrites the
   tree into *three-address code* (TAC), then folds constants and removes dead code,
   redundant copies, stores and loads.
4. **Code generation** (`gen<target>`) — register allocation, instruction selection and a
   peephole pass, out comes assembly.

```mermaid
flowchart LR
    Source[C source] --> Cpp[cpp] --> Parse[parse] --> Lower[lower] --> Gen[gen&lt;target&gt;] --> Asm[Assembly]
```

Only stage 4 is per target; the others need just a small descriptor (type sizes and
alignment, a few predefined macros), chosen with `-t`. Each program can print its output
as YAML (`--yaml`) or as a [Graphviz](https://graphviz.org/) diagram (`--dot`). The
driver `cc` ([cc/README.md](cc/README.md)) runs them all, then the assembler and linker;
it takes the usual `-c`, `-S`, `-E`, `-o`, `-D`, `-I`, `-L` and `-l`. `cpp`
([cpp/README.md](cpp/README.md)) and `cc` come from the
[v7besm](https://github.com/besm6/v7besm) project, modernized to C11.

## Getting started

### Prerequisites

CMake 3.10 or newer and a C11 compiler build the compiler; the hosted targets use that
same system compiler to assemble and link. The tests also need a C++17 compiler
(GoogleTest is vendored in `third_party/`). Each bare-metal target needs its own
tools; without them its runtime is not built and its run tests are skipped.

| Target | Assembler and linker | Simulator | Optional reference compiler |
|---|---|---|---|
| RISC-V 64 and 32 | `riscv64-unknown-elf-` (or `riscv64-elf-`, `riscv64-linux-gnu-`) binutils | `qemu-system-riscv64`, `qemu-system-riscv32` | clang |
| AArch64 | `aarch64-none-elf-` (or `aarch64-elf-`, `aarch64-linux-gnu-`) binutils | `qemu-system-aarch64` | clang |
| ARM32 | `arm-none-eabi-` binutils | `qemu-system-arm` | clang |
| x86-64 | `x86_64-elf-` binutils, or the host's own on x86-64 Linux | `qemu-system-x86_64` | clang |
| AVR | `avr-` binutils | `qemu-system-avr` | `avr-gcc`; clang |
| MSP430 | `msp430-elf-` (or `msp430-unknown-elf-`) binutils | [mspsim](https://github.com/sergev/mspsim) | `msp430-elf-gcc` with newlib; clang |
| WebAssembly | clang with the WebAssembly target, `wasm-ld` and `llvm-ar` (no binutils) | node | clang |
| MMIX | `mmix-knuth-mmixware-` binutils, GCC and newlib | `mmix` from [MMIXware](https://www-cs-faculty.stanford.edu/~knuth/mmix.html) | |
| BESM-6 | `b6as`, `b6ld` from [v7besm](https://github.com/besm6/v7besm) | `b6sim`; `dubna` (with `besmc` for Bemsh) | |

Without binutils, a clang with the target plus `ld.lld` and `llvm-ar` stand in
(`cmake -DVCC_CROSS_TOOLS=gnu|llvm` forces one or the other). The reference compiler
builds the other half of the interoperability tests and a second build of each "Writing a
C Compiler" program, whose output must match ours. `cppcheck`, when installed, checks
every source during the build.

**Debian and Ubuntu:**

```bash
sudo apt install build-essential cmake git cppcheck \
    binutils-riscv64-unknown-elf binutils-aarch64-none-elf binutils-arm-none-eabi \
    binutils-avr binutils-msp430-unknown-elf \
    qemu-system-misc qemu-system-arm qemu-system-x86
sudo apt install qemu-system-riscv        # Debian 13 and later: RISC-V is split out
sudo apt install clang lld llvm gcc-avr   # optional: the reference compilers
sudo apt install clang lld llvm nodejs    # WebAssembly: its only tools
```

Where the `-none-elf` packages are missing, `binutils-riscv64-linux-gnu` and
`binutils-aarch64-linux-gnu` serve as well.

**macOS (Homebrew):** with the Xcode command line tools,

```bash
brew install cmake cppcheck qemu \
    riscv64-elf-binutils aarch64-elf-binutils arm-none-eabi-binutils x86_64-elf-binutils
brew tap osx-cross/avr && brew install avr-binutils
brew install llvm lld                     # optional: the reference compiler
brew install llvm lld node                # WebAssembly: its only tools
```

The MSP430 GCC and newlib and the MMIX toolchain are built from source
([docs/Howto_build_MSP430_GCC.md](docs/Howto_build_MSP430_GCC.md),
[docs/Howto_build_MMIXware.md](docs/Howto_build_MMIXware.md)), as is
[mspsim](https://github.com/sergev/mspsim); install them into `~/.local`, where CMake looks.

### Building and testing

```bash
make            # build the compiler and the runtime libraries
make test       # build the tests too
make run        # build and run all the tests (or: ctest --test-dir build -j8)
make install    # install the compiler and runtime into ~/.local
make self       # build vcc with vcc (into build/self/)
make self-test  # ... and run all the tests against it
```

`make self` installs the compiler into `build/stage/` and builds itself and every
target's runtime again with that `vcc` as the C compiler. Built once more by its own
output, it gives identical objects. It needs a hosted target as the host, at present
macOS on Apple silicon: the Linux headers do not have the POSIX interfaces yet.

### Compiling a program

On the host:

```bash
vcc hello.c -lm
./a.out
```

For a bare-metal target, add `-t` and run the result under its simulator:

| Target    | Compile                                | Run                                                                                                       |
| --------- | -------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `riscv64` | `vcc -t riscv64 -o hello.elf hello.c`  | `qemu-system-riscv64 -M virt -bios none -display none -serial stdio -monitor none -kernel hello.elf`       |
| `riscv32` | `vcc -t riscv32 -o hello.elf hello.c`  | `qemu-system-riscv32 -M virt -bios none -display none -serial stdio -monitor none -kernel hello.elf`       |
| `aarch64` | `vcc -t aarch64 -o hello.elf hello.c`  | `qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -monitor none -semihosting -kernel hello.elf` |
| `arm32`   | `vcc -t arm32 -o hello.elf hello.c`    | `qemu-system-arm -M virt -cpu cortex-a15 -display none -serial stdio -monitor none -semihosting -kernel hello.elf` |
| `x86_64`  | `vcc -t x86_64 -o hello.elf hello.c`   | `qemu-system-x86_64 -M microvm -display none -serial stdio -monitor none -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel hello.elf` |
| `avr`     | `vcc -t avr -o hello.elf hello.c`      | `qemu-system-avr -M arduino-mega -display none -monitor none -serial stdio -serial file:status -bios hello.elf` |
| `msp430`  | `vcc -t msp430 -o hello.elf hello.c`   | `mspsim hello.elf`                                                                                        |
| `mmix`    | `vcc -t mmix -o hello.mmo hello.c`     | `mmix hello.mmo`                                                                                          |
| `wasm32`  | `vcc -t wasm32 -o hello.wasm hello.c`  | `node ~/.local/share/vcc/wasm32/lib/run.mjs hello.wasm`                                                   |
| `wasm32-braam` | `vcc -t wasm32-braam -o cat.wasm cat.c` | `node ~/.local/share/vcc/wasm32-braam/lib/run.mjs cat.wasm`, or `fimport` it into Braam |

Most exit with `main`'s result. x86-64 qemu exits with `(result << 1) | 1`; the AVR's
result is the byte in the file `status`, and its qemu must be stopped with Ctrl-C.

Or run the passes one at a time from the build tree, and look at each stage:

```bash
./build/cpp/cpp -t riscv64 -nostdinc -Ilibc/riscv64/include -Ilibc/lp64/include \
    -Ilibc/common/include hello.c hello.i       # C source -> preprocessed C
./build/parse hello.i hello.ast                 # C        -> syntax tree
./build/lower -t riscv64 hello.ast hello.tac    # tree     -> three-address code
./build/backend/genriscv hello.tac hello.s      # TAC      -> RISC-V assembly
./build/lower --yaml hello.ast hello.yaml       # any stage as YAML (or --dot)
```

Each target's backend document gives its header directories and how to assemble, link
and run by hand.

## What gets installed

`make install` puts everything into `~/.local` (or elsewhere:
`cmake --install build --prefix /opt/vcc`):

- `bin/vcc`, `bin/vcpp`, `bin/vparse`, `bin/vlower` — the driver and the shared passes;
- `bin/vgenriscv64`, `vgenriscv32`, `vgenaarch64`, `vgenarm32`, `vgenx86`, `vgenavr`,
  `vgenmsp430`, `vgenmmix`, `vgenwasm`, `vgenbesm6` — the code generators;
- `share/vcc/<target>/include/` and `lib/` — each target's C headers and runtime.

A bare-metal target gets `crt0.o`, `libc.a` and, for qemu and mspsim, a linker script;
WebAssembly gets `run.mjs`, the node host its programs run under, and `wasm32-braam`
a stand-in for Braam's kernel by the same name. A
hosted one gets headers matching the system's C library (our parser cannot read the
system's own) and, on Linux, a small `libvcc.a`. BESM-6,
whose C library belongs to the [v7besm](https://github.com/besm6/v7besm) Unix port, gets
only what describes the compiler: the freestanding headers, the intrinsics header and the
helper routines generated code calls.

## What the runtime provides

The bare-metal targets have a usable C library: `printf` and friends, console I/O,
`<string.h>`, `malloc`, `atoi`, `exit`, the common math helpers (with `sqrt` but on BESM-6
and AVR), `<stdarg.h>`, and `setjmp`/`longjmp` on x86-64, AVR, MSP430 and MMIX. Where the
hardware lacks them, `long double`, `long long` and floating point are computed in
software; the details are in each backend's document. The portable part lives in
[libc/common/](libc/common/), what the 64-bit, 32-bit and 16-bit targets share in
[libc/lp64/](libc/lp64/), [libc/ilp32/](libc/ilp32/) and [libc/ip16/](libc/ip16/), and
the rest in a directory per target under [libc/](libc/).

## Documentation

- [docs/Technical_Reference.md](docs/Technical_Reference.md) — start here: the source
  layout, every component, the build system, the tests
- [docs/Tests_From_The_Book.md](docs/Tests_From_The_Book.md) — how the test suite is organized
- [docs/C_Grammar.md](docs/C_Grammar.md) — the C grammar and the hand-written parser
- [docs/Coroutines_in_C.md](docs/Coroutines_in_C.md) — `defer` and coroutines, a tutorial
- [docs/Braam.md](docs/Braam.md), [docs/Braam_Example.md](docs/Braam_Example.md) — C
  programs for Braam
- [docs/Type_Coercion.md](docs/Type_Coercion.md),
  [docs/Type_Sizes_Alignment.md](docs/Type_Sizes_Alignment.md) — types
- [docs/TAC_Optimization.md](docs/TAC_Optimization.md) — the optimizer
- [docs/Standard_Include_Files.md](docs/Standard_Include_Files.md) — the C11 headers
- [docs/Memory_Allocation.md](docs/Memory_Allocation.md),
  [docs/String_Map.md](docs/String_Map.md),
  [docs/Word_Oriented_IO.md](docs/Word_Oriented_IO.md) — internal libraries
- Backends: [RISC-V](docs/Riscv_Backend.md), [AArch64](docs/Aarch64_Backend.md) (also the
  hosted Linux and macOS targets), [ARM32](docs/Arm32_Backend.md),
  [x86-64](docs/X86_64_Backend.md) (also hosted Linux), [AVR](docs/Avr_Backend.md),
  [MSP430](docs/Msp430_Backend.md), [MMIX](docs/Mmix_Backend.md),
  [WebAssembly](docs/Wasm_Backend.md), [BESM-6](backend/besm6/Besm6_Calling_Conventions.md)

## License

MIT. See [LICENSE](LICENSE).

Copyright (c) 2025-2026 Serge Vakulenko
