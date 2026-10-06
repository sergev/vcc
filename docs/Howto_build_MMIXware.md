# How to build GCC for MMIX from source

This guide builds a C cross-compiler for Donald Knuth's MMIX on a Mac (Apple
Silicon, macOS). You end up with:
- `mmix-knuth-mmixware-gcc`, the assembler, the linker and newlib's C library, all
  installed in `~/.local`;
- Knuth's own simulator `mmix`, from Homebrew.

It was tested in October 2026 with binutils 2.47, GCC 16.2.0, newlib 4.6.0,
MMIXware 20160804 and macOS 27. Expect about 15–20 minutes of build time on an
8-core machine. The unpacked sources take about 2 GB and the build trees 1 GB.

## 1. What you are building, and why there are three steps

A *cross-compiler* runs on your Mac but produces code for another CPU, here
MMIX. The GNU target name is **`mmix-knuth-mmixware`**, and every tool gets it
as a prefix: `mmix-knuth-mmixware-gcc`, `mmix-knuth-mmixware-ld` and so on.
(`--target=mmix` is accepted too and means the same.)

A working toolchain has four parts:

| Part | Package | What it gives you |
|---|---|---|
| Assembler, linker, binary tools | **binutils** | `mmix-knuth-mmixware-as`, `-ld`, `-objdump`, `-nm`, … |
| Compiler | **GCC** | `mmix-knuth-mmixware-gcc`, plus `libgcc` and the startup files `crti.o`/`crtn.o` |
| C library | **newlib** | `printf`, `strcpy`, `malloc`, `sqrt`, and the simulator system calls |
| Simulator | **MMIXware** (Homebrew) | `mmix`, which runs the programs; also `mmixal`, Knuth's assembler |

GCC needs a C library, and the C library must be compiled by GCC. For MMIX the
circle is easy to break, because GCC's MMIX support needs nothing from the C
library to build its own runtime:

1. **binutils**
2. **GCC**: the compiler and `libgcc`, configured `--with-newlib --without-headers`
3. **newlib**, compiled by that GCC

No second GCC build is needed: the compiler from step 2 finds newlib's headers
and libraries in the same prefix as soon as step 3 installs them. (The MSP430
guide needs a fourth step; MMIX does not.)

All steps install into the same place (the *prefix*). Each later step finds the
earlier ones there.

## 2. Prepare

### Tools and libraries

You need the Xcode command-line tools (they provide `clang` and the macOS SDK)
and Homebrew:

```sh
xcode-select --install       # skip if already installed
brew install gmp mpfr libmpc zstd make mmix
```

- **GMP, MPFR and MPC:** maths libraries that GCC itself uses at compile time.
- **`zstd`:** compresses GCC's LTO data.
- **`make`:** GNU make 4, installed as `gmake`. Apple's `make` is 3.81, which mostly works,
  but `gmake` is safer for GCC.
- **`mmix`:** the MMIXware simulator and `mmixal`.

**Optional:** `brew install texinfo`. It provides `makeinfo`, which builds the
manuals. Without it, newlib's build fails unless you pass `MAKEINFO=true` as
shown below, which skips the manuals.

ISL is not needed. Without it GCC has no Graphite loop optimizations, which
ordinary `-O2`/`-O3` code does not use.

### Directories and variables

Use one working directory for sources and builds. Never build inside a source
tree: GCC refuses to, and the others misbehave. Set these variables in the
terminal you build in; every command below uses them:

```sh
export WORK=~/Project/Mmixware     # sources and build trees
export PREFIX=~/.local             # where the toolchain is installed
export PATH=$PREFIX/bin:$PATH      # so later steps find earlier tools
mkdir -p $WORK/src $WORK/build
```

The tools land in `$PREFIX/bin`. Make sure that directory is in your `PATH`
permanently too (add the `export PATH` line to `~/.zshrc` or `~/.bash_profile`).

## 3. Download the sources

```sh
cd $WORK/src
curl -LO https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz
curl -LO https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz
curl -LO https://sourceware.org/pub/newlib/newlib-4.6.0.20260123.tar.gz
for f in *.tar.*; do tar xf $f; done
```

To use newer versions, look at the directory listings at
<https://ftp.gnu.org/gnu/binutils/>, <https://ftp.gnu.org/gnu/gcc/> and
<https://sourceware.org/pub/newlib/>, then adjust the names everywhere below.
Before you do, check that the new GCC still has `gcc/config/mmix/` and lists
`mmix-knuth-mmixware` in `gcc/config.gcc`; MMIX is a little-used port.

## 4. Step 1: binutils

```sh
mkdir -p $WORK/build/binutils && cd $WORK/build/binutils
../../src/binutils-2.47/configure --target=mmix-knuth-mmixware --prefix=$PREFIX \
    --disable-nls --disable-werror --disable-gdb --disable-gprofng --with-system-zlib
gmake -j8
gmake install
```

| Option | Meaning |
|---|---|
| `--target=mmix-knuth-mmixware` | Build tools for MMIX |
| `--prefix=$PREFIX` | Install into `$PREFIX/bin`, `$PREFIX/mmix-knuth-mmixware`, … |
| `--disable-nls` | No translated messages; English only, faster build |
| `--disable-werror` | Do not stop on compiler warnings (new compilers warn more) |
| `--disable-gdb --disable-gprofng` | Skip the debugger and profiler, which are not needed |
| `--with-system-zlib` | Use macOS's zlib rather than the bundled copy |

`-j8` runs 8 compile jobs in parallel. Use your number of CPU cores
(`sysctl -n hw.ncpu`).

Check it: `mmix-knuth-mmixware-as --version` should print the version.

## 5. Step 2: GCC

```sh
mkdir -p $WORK/build/gcc && cd $WORK/build/gcc
../../src/gcc-16.2.0/configure --target=mmix-knuth-mmixware --prefix=$PREFIX \
    --enable-languages=c --with-newlib --without-headers \
    --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic \
    --disable-decimal-float \
    --with-gmp=/opt/homebrew --with-zstd=/opt/homebrew
gmake -j8 all-gcc
gmake install-gcc
gmake -j8 all-target-libgcc
gmake install-target-libgcc
```

| Option | Meaning |
|---|---|
| `--enable-languages=c` | C only |
| `--with-newlib` | The C library will be newlib |
| `--without-headers` | There is no C library yet; don't look for its headers |
| `--disable-shared --disable-threads` | The simulator has no shared libraries and no threads |
| `--disable-libssp/-libquadmath/-libgomp/-libatomic` | Skip runtime libraries that are not needed here |
| `--disable-decimal-float` | No `_Decimal64` support, a smaller `libgcc` |
| `--with-gmp=/opt/homebrew` | Where Homebrew put GMP. MPFR and MPC are found in the same place, since Homebrew links all three into `/opt/homebrew` |
| `--with-zstd=/opt/homebrew` | Where Homebrew put zstd |

`all-gcc` builds only the compiler and `all-target-libgcc` its helper
library. A plain `gmake` would also try the rest of the target libraries, which
are disabled or need a C library.

`libgcc` is built twice, once per *multilib* (see §8). Both land in
`$PREFIX/lib/gcc/mmix-knuth-mmixware/16.2.0/`, the second one under `gnuabi/`.

During the build you may see `clang++: warning: argument unused during
compilation: '-pie'` and `ld: warning: ignoring duplicate libraries`. Both are
harmless.

Check it: `mmix-knuth-mmixware-gcc --version`.

## 6. Step 3: newlib

```sh
mkdir -p $WORK/build/newlib && cd $WORK/build/newlib
../../src/newlib-4.6.0.20260123/configure --target=mmix-knuth-mmixware --prefix=$PREFIX
gmake -j8 MAKEINFO=true
gmake install MAKEINFO=true
```

No options beyond the target are needed. Unlike a microcontroller, the MMIX
simulator has plenty of memory, so the full newlib (with floating-point
`printf`) is the right choice.

The system calls (`write`, `read`, `open`, `exit`, …) are newlib's
`libc/sys/mmixware`, which talks to the simulator through `trap` instructions:
`Fopen`, `Fread`, `Fwrite`, `Halt` and so on. So no board support package is
needed.

`MAKEINFO=true` matters here: without `makeinfo`, newlib's `libgloss` fails
on its porting manual (see §9).

## 7. Test it

Write a small program, `hello.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void)
{
    char *p = malloc(32);
    strcpy(p, "MMIX");
    printf("Hello from %s! sqrt(2)=%.6f %lld\n", p, sqrt(2.0), 1LL << 62);
    free(p);
    return 3;
}
```

Compile, link and run it:

```sh
mmix-knuth-mmixware-gcc -O2 hello.c -o hello -lm
mmix hello
echo $?
```

It prints `Hello from MMIX! sqrt(2)=1.414214 4611686018427387904`, and the
exit status is 3.

- **No linker script is needed.** The linker's default places code at 0x100
  and data at `Data_Segment` (0x2000000000000000), and `crti.o` sets up the
  stack.
- **The output is already in Knuth's `.mmo` format,** which is what `mmix` loads,
  because the linker's default emulation is `mmo`. To get an ELF for
  `mmix-knuth-mmixware-objdump -d`, add `-Wl,--oformat=elf64-mmix`.
- **The exit status** is `main`'s result. newlib's `_exit` puts it in `$255` and
  executes `trap 0,Halt,0`, and `mmix` returns it to the shell.

Other useful commands:

```sh
mmix-knuth-mmixware-gcc -O2 -S hello.c       # assembly listing hello.s
mmix -s hello                                # statistics: instructions, mems, oops
mmix -t100 hello                             # trace the first 100 instructions
mmix -i hello                                # interactive simulator
```

`mmix-knuth-mmixware-gcc -print-multi-lib` lists the two library variants:

| Options | ABI | Arguments |
|---|---|---|
| *(none)* | MMIXware (`__MMIX_ABI_MMIXWARE__`) | In the register stack: the callee's `$0`, `$1`, … |
| `-mabi=gnu` | GNU (`__MMIX_ABI_GNU__`) | In the global registers `$231`… |

Both run under `mmix`. Use the same `-mabi` for every compile and link command.

## 8. Notes on the generated code

- **Data model:** LP64, a signed `char`, big-endian, and `long double` the same
  binary64 as `double`.
- **Assembler flags:** GCC calls the assembler with `-no-predefined-syms -x`.
  The `-x` lets the assembler and linker expand out-of-range branches, `geta`
  and `pushj`, and lets the linker allocate global base registers for
  addressing data (`.MMIX.reg_contents` in the ELF output).
- **Registers:** the startup code `crti.o` sets `rG` to 32, so a function has at
  most 32 local registers, `$0`–`$31`.
- **Assembly syntax:** GNU `as` accepts both GCC's mmixal-compatible output and
  ordinary GNU syntax (`name:` labels, lowercase mnemonics, `.quad`). Knuth's
  `mmixal` cannot link separate objects, so it is no use with GCC's output.

## 9. Problems you may hit

**`makeinfo is missing` / `porting.info Error 127`** while building newlib.
Add `MAKEINFO=true` to both `gmake` commands, or `brew install texinfo`. The
build can be resumed: just re-run `gmake` with the option.

**`configure: WARNING: unrecognized options`.** You mistyped an option;
`configure` warns and carries on without it. Check the first lines of the
output.

**`Bad object file! (Try running MMOtype.)`** from `mmix`. You linked with
`--oformat=elf64-mmix`; `mmix` loads only `.mmo`. Link again without it.

**The program exits with status 0 but printed nothing.** A jump into zeroed
memory executes `trap 0,0,0`, the halt, so a crash can look like `exit(0)`.
Trace it with `mmix -t1000` or `mmix -i`.

**A step failed halfway.** Fix the cause, delete that step's build directory
(`rm -rf $WORK/build/…`) and start the step again from `configure`. For a
failure in the docs only (`MAKEINFO`), simply re-running `gmake` with the
missing option is enough.

## 10. Clean up

Once newlib is installed, the build trees are no longer needed:

```sh
rm -rf $WORK/build $WORK/src
```

The installed toolchain lives in:

- `$PREFIX/bin/mmix-knuth-mmixware-*`: the programs
- `$PREFIX/mmix-knuth-mmixware/`: newlib's headers and libraries (`lib/` and
  `lib/gnuabi/`), and the linker scripts (`lib/ldscripts/`)
- `$PREFIX/lib/gcc/mmix-knuth-mmixware/` and
  `$PREFIX/libexec/gcc/mmix-knuth-mmixware/`: compiler internals, `libgcc.a`,
  `crti.o`/`crtn.o`
- `/opt/homebrew/bin/mmix`, `mmixal`: the simulator and Knuth's assembler
