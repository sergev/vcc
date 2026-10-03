# cc — the compiler driver

The C compiler driver of VCC, built as `build/cc/cc` and installed as `bin/vcc`. It is a
**driver**, not a compiler itself: it runs the toolchain one program per stage, as Unix
`cc(1)` always has, so that

```sh
vcc -o hello.elf hello.c
```

does what otherwise takes six commands. It is ported from the
[v7besm](https://github.com/besm6/v7besm) project's `b6cc` (`cmd/cc/`), a C11 rewrite of the
v7 driver, and made target-aware. The native BESM-6 build of that driver and its
documentation stay in v7besm.

## Pipeline

```text
vcpp         preprocess    .c   -> .i
vparse       parse         .i   -> .ast
vlower       lower + opt   .ast -> .tac
vgen<T>      code gen      .tac -> .s
assembler    assemble      .s   -> .o
linker       link          .o   -> a.out
```

| Target | `-t` | Code generator | Assembler | Linker |
| --- | --- | --- | --- | --- |
| RISC-V RV64IMFD/LP64D | `riscv64` (default) | `vgenriscv64` | `clang --target=riscv64 -march=rv64imfd -mabi=lp64d -c` | `ld.lld -T link.ld` |
| RISC-V RV32IMFD/ILP32D | `riscv32` | `vgenriscv32` | `clang --target=riscv32 -march=rv32imfd -mabi=ilp32d -c` | `ld.lld -T link.ld` |
| AArch64 (ARMv8-A, AAPCS64) | `aarch64` | `vgenaarch64` | `clang --target=aarch64-none-elf -c` | `ld.lld -T link.ld` |
| ARM32 (ARMv7-A, AAPCS-VFP) | `arm32` | `vgenarm32` | `clang --target=armv7a-none-eabihf -mcpu=cortex-a15 -mfpu=vfpv3-d16 -c` | `ld.lld -T link.ld` |
| BESM-6 | `besm6` | `vgenbesm6` | `b6as -X` | `b6ld -X -e _start` |

The intermediate files are temporaries in `$TMPDIR` (or `/tmp`), named `vccXXXXXX.<suffix>`
and removed on exit.

## Input files

| Suffix | Handling |
| --- | --- |
| `.c` | full pipeline |
| `.S` | assembly preprocessed first (`vcpp` with `__ASSEMBLER__` defined, then the assembler) |
| `.s` | assembly (assembler only) |
| `.o`, `.a` | passed straight to the linker |

## Options

| Option | Meaning |
| --- | --- |
| `-t NAME`, `-tNAME`, `--target NAME`, `--target=NAME` | Target: `riscv64` (default), `riscv32`, `aarch64`, `arm32` or `besm6` |
| `-c` | Compile and assemble, but do not link |
| `-S` | Compile only; emit assembly (`.s`) |
| `-Smadlen`, `-Sbemsh` | Like `-S`, but emit the BESM-6 Madlen (`.mad`) or Bemsh (`.bemsh`) dialect (`besm6` only) |
| `-E` | Preprocess only; write to `-o` or `.i` |
| `-o file` | Set the output file name (default `a.out` for a link) |
| `-v` | Echo each sub-command before running it |
| `-Dname[=v]`, `-Uname`, `-Ipath` | Passed to the preprocessor (`-D name` is folded into `-Dname`) |
| `-Lpath`, `-lname` | Passed to the linker, after the objects |
| `-T file` | Linker script instead of the standard `link.ld` (not `besm6`) |
| `-nostdinc` | Do not add the target's standard include directory |
| `-nostdlib` | No `crt0.o`, no standard library directory, no implicit libraries |
| `-O`, `-g` | Accepted and ignored: `vlower` always optimizes, and there is no debug info yet |

The last stage is chosen by `-E`, `-S` or `-c`. With none of them, the objects are linked.
Output names derive from the input's base name in the current directory (`src/foo.c` →
`foo.o`). `-o` with `-c`, `-S` or `-E` takes a single input.

The exit status is 0 on success and 1 on any failure. One failing file does not stop
the others from compiling, but it skips the link.

## Where things are found

**The installation is relocatable.** `vcc` finds its own directory (`_NSGetExecutablePath`
on macOS, `/proc/self/exe` on Linux, otherwise `argv[0]`), with symlinks resolved, and
takes everything relative to it:

| What | Where |
| --- | --- |
| `vcpp`, `vparse`, `vlower`, `vgen<T>` | the directory `vcc` is in |
| standard headers | `../share/vcc/<target>/include` |
| `crt0.o`, libraries, `link.ld` | `../share/vcc/<target>/lib` |

`vcc` passes `-nostdinc -I<share>/include` to `vcpp`, so `vcpp`'s own compiled-in include
directory plays no part. The assembler and linker belong to other projects. They are the
clang and `ld.lld` found when the build was configured (RISC-V and ARM), or else whatever
`clang`/`ld.lld`/`b6as`/`b6ld` is on `PATH`.

Each tool can be overridden with an environment variable. This is how the tests run the
driver against the build tree:

| Variable | Tool |
| --- | --- |
| `VCC_CPP`, `VCC_PARSE`, `VCC_LOWER` | the preprocessor, parser and lowerer |
| `VCC_GEN` | the code generator of the selected target |
| `VCC_AS`, `VCC_LD` | the assembler and linker |

## Linking

RISC-V, AArch64 and ARM32:

```text
ld.lld -T <lib>/link.ld -o a.out -L<lib> <lib>/crt0.o objects... -L/-l flags... -lc
```

The result is an ELF for the qemu `virt` machine. It runs with
`qemu-system-riscv64 -M virt -bios none -display none -serial stdio -monitor none -kernel a.out`
(`qemu-system-riscv32` for `riscv32`), or for `aarch64` with
`qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -monitor none -semihosting -kernel a.out`,
and for `arm32` with
`qemu-system-arm -M virt -cpu cortex-a15 -display none -serial stdio -monitor none -semihosting -kernel a.out`;
these two exit with `main`'s result.

BESM-6:

```text
b6ld -X -e _start -o a.out -L<lib> <lib>/crt0.o objects... -L/-l flags... -lc -lruntime
```

`libruntime.a` (the `b$*` helpers) is ours. `crt0.o` and `libc.a` are **v7besm's**, and this
project does not install them, so a BESM-6 link needs them copied or installed into
`share/vcc/besm6/lib`; `vcc` says so when `crt0.o` is missing. The same goes for the hosted
headers (`<stdio.h>`, …), which belong in `share/vcc/besm6/include` or come in with `-I`. The
order of the two archives is a contract: `b6ld` scans an archive once, where it stands, and
libc calls the helpers, never the reverse.

`-nostdlib` drops `-L<lib>`, `crt0.o` and the implicit `-l`s, but keeps the linker
script, since it is the machine's memory map and not a library. Use `-T` to replace it.

## Testing

`cc-tests` ([test/cc_test.cpp](test/cc_test.cpp)) runs the built driver on small sources
in a temporary directory, with the in-tree passes chosen through the `VCC_*` variables:

- preprocessing and target selection
- `-S` for every target and both BESM-6 dialects
- the usage errors
- `-c`, a link and a run under qemu for RISC-V, AArch64 and ARM32
- the BESM-6 link line, checked with a stand-in linker

The `StagedPrefix` cases build a miniature installation (`bin/vcc` plus links to the
passes, `share/vcc/<target>/`) and run it with no overrides. That is what tests the
relocatable lookup. Cases that need clang, `ld.lld`, qemu or `b6as` skip when they are
missing.

```sh
./build/cc/test/cc-tests
```
