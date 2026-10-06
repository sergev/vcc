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
| x86-64 (SysV psABI) | `x86_64` | `vgenx86` | `clang --target=x86_64-none-elf -c` | `ld.lld -T link.ld` |
| AVR (ATmega1280, avr-gcc ABI) | `avr` | `vgenavr` | `clang --target=avr -mmcu=atmega1280 -c` | `ld.lld -T link.ld` |
| MSP430 (classic, MSPABI) | `msp430` | `vgenmsp430` | `msp430-elf-as -mcpu=msp430` | `msp430-elf-ld --gc-sections -T link.ld` |
| MMIX (MMIXware ABI) | `mmix` | `vgenmmix` | `mmix-knuth-mmixware-as -x -no-predefined-syms` | `mmix-knuth-mmixware-ld --defsym=__.MMIX.start..text=0x100` |
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
| `-t NAME`, `-tNAME`, `--target NAME`, `--target=NAME` | Target: `riscv64` (default), `riscv32`, `aarch64`, `arm32`, `x86_64`, `avr`, `msp430`, `mmix` or `besm6` |
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
clang and `ld.lld` found when the build was configured (the GNU `msp430-elf-as` and
`msp430-elf-ld` for the MSP430, `mmix-knuth-mmixware-as` and `-ld` for MMIX; nothing for
the BESM-6), or else whatever `clang`/`ld.lld`/`msp430-elf-as`/`msp430-elf-ld`/
`mmix-knuth-mmixware-as`/`mmix-knuth-mmixware-ld`/`b6as`/`b6ld` is on `PATH`.

Each tool can be overridden with an environment variable. This is how the tests run the
driver against the build tree:

| Variable | Tool |
| --- | --- |
| `VCC_CPP`, `VCC_PARSE`, `VCC_LOWER` | the preprocessor, parser and lowerer |
| `VCC_GEN` | the code generator of the selected target |
| `VCC_AS`, `VCC_LD` | the assembler and linker; split into words at blanks, so they may carry arguments |

## Linking

RISC-V, AArch64, ARM32, x86-64 and AVR:

```text
ld.lld -T <lib>/link.ld -o a.out -L<lib> <lib>/crt0.o objects... -L/-l flags... -lc
```

The result is an ELF for the qemu `virt` machine (`microvm` for x86-64, `arduino-mega` for AVR). It runs with
`qemu-system-riscv64 -M virt -bios none -display none -serial stdio -monitor none -kernel a.out`
(`qemu-system-riscv32` for `riscv32`), or for `aarch64` with
`qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -monitor none -semihosting -kernel a.out`,
and for `arm32` with
`qemu-system-arm -M virt -cpu cortex-a15 -display none -serial stdio -monitor none -semihosting -kernel a.out`;
these two exit with `main`'s result. For `x86_64`:
`qemu-system-x86_64 -M microvm -display none -serial stdio -monitor none -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel a.out`;
`exit` writes the status byte to the debug console (add `-debugcon file:status` to keep
it), and the exit device then stops qemu with status `(main's result << 1) | 1`.

For `avr` the ELF is for qemu's `arduino-mega` (an ATmega1280):
`qemu-system-avr -M arduino-mega -display none -monitor none -serial stdio -serial file:status -bios a.out`.
Nothing on the machine can stop qemu: `exit` writes `main`'s result as one byte to
USART1 (the `status` file) and waits, so qemu is stopped by hand, or by a script once the
byte has arrived. For a real board, `llvm-objcopy -O ihex a.out a.hex` makes the Intel HEX
image a flasher takes.

MSP430:

```text
msp430-elf-ld --gc-sections -T <lib>/link.ld -o a.out -L<lib> <lib>/crt0.o objects... -L/-l flags... -lc [libgcc.a]
```

`vgenmsp430` gives every function and variable a section of its own, so `--gc-sections`
keeps only what is reached: `printf("%d")` would be 53 KB without it. GCC's `libgcc.a`
comes last when it was found at configure time (`msp430-elf-gcc -mcpu=msp430
-print-libgcc-file-name`) and still exists: our `libc.a` has every helper our own code
calls, but objects compiled by GCC may call more (`__clzhi2` for `__builtin_clz`).

The ELF is for the classic MSP430 with 48 KB of ROM at 0x4000 and 15.5 KB of RAM at 0x0200, and runs with
[mspsim](https://github.com/sergev/mspsim): `mspsim a.out` prints the UART output and
exits with `main`'s result (`-q` drops the banner, `-t` traces, `-g` starts in the
debugger). `msp430-elf-objcopy -O ihex a.out a.hex` makes the Intel HEX image a flasher
takes, and mspsim runs that too.

clang's assembler and `ld.lld` can stand in for the GNU binutils:

```sh
VCC_AS="clang --target=msp430 -c" VCC_LD="ld.lld -n" vcc -t msp430 hello.c
```

`-n` keeps `ld.lld` from placing the ELF headers in a loaded segment, where they would
land on the peripheral area. clang warns that it does not use `-mcpu=msp430`.

MMIX:

```text
mmix-knuth-mmixware-ld --defsym=__.MMIX.start..text=0x100 -o a.out -L<lib> <lib>/crt0.o objects... -L/-l flags... -lc [libgcc.a]
```

These are the flags GCC passes: the assembler's `-x` lets the assembler and the linker
expand a branch, `geta`, `pushj` or `jmp` that is out of range and allocate the base
registers; the linker uses its own script and puts the text at 0x100, where the loader
expects it. GCC's `libgcc.a` comes last when it was found at configure time
(`mmix-knuth-mmixware-gcc -print-libgcc-file-name`), for objects compiled by GCC
(`__clzdi2` for `__builtin_clzl`); our own code calls no helper.

The output is Knuth's `.mmo` object format, which his simulator runs directly:
`mmix a.out` prints the program's output and exits with `main`'s result, which our
`exit` also reports as `[exit N]` on the standard error (`-q` drops the simulator's own
messages, `-s` prints the instruction, oop and mem counts, `-t` traces, `-i` starts in
the interactive mode). `objdump` cannot read a `.mmo`; to
disassemble, link the same objects once more with `--oformat elf64-mmix`:

```sh
vcc -t mmix -v -o a.out hello.c        # shows the link line
mmix-knuth-mmixware-ld --oformat elf64-mmix --defsym=__.MMIX.start..text=0x100 -o a.elf ...
mmix-knuth-mmixware-objdump -d a.elf
```

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
script, since it is the machine's memory map and not a library. Use `-T` to replace it
(or, for MMIX, to give one where the linker's own is used).

## Testing

`cc-tests` ([test/cc_test.cpp](test/cc_test.cpp)) runs the built driver on small sources
in a temporary directory, with the in-tree passes chosen through the `VCC_*` variables:

- preprocessing and target selection
- `-S` for every target and both BESM-6 dialects
- the usage errors
- `-c`, a link and a run under qemu for RISC-V, AArch64, ARM32, x86-64 and AVR, and under
  mspsim for the MSP430 (its Intel HEX as well, and with clang and `ld.lld` through
  `VCC_AS`/`VCC_LD`), and under Knuth's `mmix` for MMIX
- the BESM-6 link line, checked with a stand-in linker

The `StagedPrefix` cases build a miniature installation (`bin/vcc` plus links to the
passes, `share/vcc/<target>/`) and run it with no overrides. That is what tests the
relocatable lookup. Cases that need clang, `ld.lld`, qemu, the GNU MSP430 or MMIX
binutils, mspsim, `mmix` or `b6as` skip when they are missing.

```sh
./build/cc/test/cc-tests
```
