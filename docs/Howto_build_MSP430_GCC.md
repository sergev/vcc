# How to build GCC for MSP430 from source

This guide builds a C cross-compiler for the Texas Instruments MSP430
microcontrollers on Debian or Ubuntu Linux. You end up with
`msp430-elf-gcc`, the assembler, the linker and a small C library, all
reachable from `~/.local/bin`.

It was tested in October 2026 on Debian forky/sid (x86-64) with the
distribution's binutils 2.47 for MSP430, GCC 16.2.0 and newlib 4.6.0. Ubuntu
has the same package names. Each build step takes a few minutes on a 16-core
machine. The sources and build trees need about 6 GB of disk.

## 1. What you are building, and why there are four steps

A *cross-compiler* runs on your PC but produces code for another CPU, here
the MSP430. The target name is **`msp430-elf`**, and every tool gets it as a
prefix: `msp430-elf-gcc`, `msp430-elf-ld` and so on.

A working toolchain has three parts:

| Part | Package | What it gives you |
|---|---|---|
| Assembler, linker, binary tools | **binutils** | `msp430-elf-as`, `-ld`, `-objdump`, `-size`, … |
| Compiler | **GCC** | `msp430-elf-gcc`, plus `libgcc` (helper routines) |
| C library | **newlib** (and its **libgloss**) | `printf`, `strcpy`, `malloc`, startup code, linker scripts |

Debian packages binutils for the MSP430, so you install it with `apt`.
There is no package for GCC or newlib on the MSP430, so you build those
two from source.

GCC and newlib depend on each other in a circle: GCC needs the C library's
headers, and the C library must be compiled by GCC. You break the circle by
building GCC twice:

1. **binutils**: installed from the package
2. **GCC, stage 1**: a bare C compiler that needs no C library
3. **newlib**, compiled by the stage-1 GCC
4. **GCC, final**: rebuilt so that it knows about newlib

All four end up in the same place (the *prefix*). Each later step finds
the earlier ones there.

## 2. Prepare

### Packages

```sh
sudo apt install build-essential flex bison texinfo curl xz-utils \
    libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev \
    binutils-msp430-unknown-elf
```

GMP, MPFR and MPC are maths libraries that GCC itself uses at compile time.
`texinfo` provides `makeinfo`, which builds the manuals. Without it the build
fails, unless you add `MAKEINFO=true` to every `configure` and `make` command,
which skips the manuals.

### Directories and variables

Use one working directory for sources and builds. Never build inside a source
tree: GCC refuses to, and the others misbehave. Set these variables in the
terminal you build in; every command below uses them:

```sh
export WORK=~/msp430-gcc           # sources and build trees
export PREFIX=~/.local             # where the toolchain is installed
export PATH=$PREFIX/bin:$PATH      # so later steps find earlier tools
mkdir -p $WORK && cd $WORK
```

The tools land in `$PREFIX/bin`. Make sure that directory is in your `PATH`
permanently too. Ubuntu's and Debian's default `~/.profile` adds
`~/.local/bin` once it exists; otherwise add the `export PATH` line to
`~/.bashrc`.

## 3. Download the sources

```sh
cd $WORK
curl -LO https://sourceware.org/pub/gcc/releases/gcc-16.2.0/gcc-16.2.0.tar.xz
curl -LO https://sourceware.org/pub/newlib/newlib-4.6.0.20260123.tar.gz
for f in *.tar.*; do tar xf $f; done
```

GCC is also at <https://ftp.gnu.org/gnu/gcc/>, but that server can be slow
or unreachable. sourceware.org has the same files.

To use newer versions, look at the directory listings at
<https://sourceware.org/pub/gcc/releases/> and
<https://sourceware.org/pub/newlib/>, then adjust the names everywhere below.

## 4. Step 1: binutils

The `binutils-msp430-unknown-elf` package installed in §2 names the target
`msp430-unknown-elf`, not `msp430-elf`. Both are the same target, but GCC
configured for `msp430-elf` looks for `msp430-elf-as` and for
`$PREFIX/msp430-elf/bin/as`. So link the package's tools under those names.
This gives the same layout that building binutils yourself would install:

```sh
mkdir -p $PREFIX/bin $PREFIX/msp430-elf/bin
for f in /usr/bin/msp430-unknown-elf-*; do
    ln -sf $f $PREFIX/bin/msp430-elf-${f#/usr/bin/msp430-unknown-elf-}
done
for f in /usr/msp430-unknown-elf/bin/*; do
    ln -sf $f $PREFIX/msp430-elf/bin/$(basename $f)
done
```

Check it: `msp430-elf-as --version` should print the version.

## 5. Step 2: GCC, stage 1

```sh
mkdir -p $WORK/build-gcc1 && cd $WORK/build-gcc1
../gcc-16.2.0/configure --target=msp430-elf --prefix=$PREFIX \
    --enable-languages=c --without-headers --with-newlib \
    --with-system-zlib --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libquadmath --disable-libgomp
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
```

| Option | Meaning |
|---|---|
| `--target=msp430-elf` | Build a compiler for the MSP430 |
| `--prefix=$PREFIX` | Install into `$PREFIX/bin`, `$PREFIX/lib/gcc`, … |
| `--enable-languages=c` | C only. C++ does not build for MSP430 (see §9) |
| `--without-headers` | There is no C library yet; don't look for its headers |
| `--with-newlib` | The C library will be newlib |
| `--with-system-zlib` | Use the system zlib (`zlib1g-dev`) instead of GCC's bundled copy |
| `--disable-nls` | No translated messages; English only, faster build |
| `--disable-shared --disable-threads` | A microcontroller has no shared libraries and no threads |
| `--disable-libssp/-libquadmath/-libgomp` | Skip runtime libraries that make no sense on the MSP430 |

`-j$(nproc)` runs as many compile jobs in parallel as you have CPU cores.

`all-gcc all-target-libgcc` builds only the compiler and its helper library.
A full `make` would fail at this stage, because the rest needs a C library.

Check it: `msp430-elf-gcc --version`, and
`msp430-elf-gcc -print-prog-name=as` should print a path ending in
`msp430-elf/bin/as`.

## 6. Step 3: newlib

```sh
mkdir -p $WORK/build-newlib && cd $WORK/build-newlib
../newlib-4.6.0.20260123/configure --target=msp430-elf --prefix=$PREFIX \
    --disable-newlib-supplied-syscalls --enable-newlib-reent-small \
    --disable-newlib-fseek-optimization --disable-newlib-wide-orient \
    --enable-newlib-nano-formatted-io --disable-newlib-io-float \
    --enable-newlib-nano-malloc --disable-newlib-unbuf-stream-opt \
    --enable-lite-exit --enable-newlib-global-atexit --disable-nls \
    CFLAGS_FOR_TARGET="-g -gdwarf-4 -O2"
make -j$(nproc)
make install
```

These are the options TI uses for its own MSP430 toolchain. Together they
make the library as small as possible, since the chips have only kilobytes of
memory. The most noticeable effects:

- `--enable-newlib-nano-formatted-io`: a compact `printf`.
- `--disable-newlib-io-float`: no floating point in `printf`/`scanf`; `%f`,
  `%e` and `%g` don't work. If you need them, rebuild newlib without this
  option and link with `-u _printf_float` (several KB of extra code).
- `--enable-newlib-nano-malloc`: a small, simple `malloc`.
- `--enable-lite-exit`: a smaller `exit()`.

`CFLAGS_FOR_TARGET` replaces the default `-g -O2` so that the library's
debug information is DWARF 4 instead of GCC 16's default DWARF 5. With
DWARF 5, linking for `-mlarge` fails (see §9).

The build compiles the library once per *multilib* (CPU and memory-model
variant, see §8), so it takes a while.

## 7. Step 4: GCC, final

The same as stage 1, minus `--without-headers`, plus the same
`CFLAGS_FOR_TARGET` as newlib (for `libgcc`), in a fresh build directory:

```sh
mkdir -p $WORK/build-gcc2 && cd $WORK/build-gcc2
../gcc-16.2.0/configure --target=msp430-elf --prefix=$PREFIX \
    --enable-languages=c --with-newlib \
    --with-system-zlib --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libquadmath --disable-libgomp \
    CFLAGS_FOR_TARGET="-g -gdwarf-4 -O2"
make -j$(nproc)
make install
```

This overwrites the stage-1 compiler with the final one.

A real failure stops the build with `*** [...] Error` and a non-zero exit
status. Messages without that are harmless probes.

## 8. Test it

Write a small program, `t.c`:

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[16];
    strcpy(buf, "hello");
    printf("%s %d\n", buf, (int)sizeof(int));
    return 0;
}
```

Compiling (`-c`, `-S`) needs no extra options:

```sh
msp430-elf-gcc -O2 -c t.c            # object file t.o
msp430-elf-gcc -O2 -S t.c            # assembly listing t.s
```

**Linking needs a linker script**, which describes the chip's memory map.
newlib installed two generic ones for the simulator:

```sh
msp430-elf-gcc -O2 t.c -T msp430-sim.ld -o t.elf                 # MSP430X, small model
msp430-elf-gcc -O2 -mlarge t.c -T msp430xl-sim.ld -o t.elf       # MSP430X, large model
msp430-elf-gcc -O2 -mcpu=msp430 t.c -T msp430-sim.ld -o t.elf    # original MSP430
msp430-elf-size t.elf
```

The `-mcpu`/`-mlarge` options pick the *multilib*, the library variant
compiled for that CPU and memory model. `msp430-elf-gcc -print-multi-lib`
lists them all:

| Options | CPU | Pointers / code reach |
|---|---|---|
| *(none)* | MSP430X | 16-bit, 64 KB |
| `-mcpu=msp430` | original MSP430 (no 430X instructions) | 16-bit, 64 KB |
| `-mlarge` | MSP430X | 20-bit, 1 MB |

The linker may warn `LOAD segment with RWX permissions`. That is normal for
these simple scripts. Its messages are signed `msp430-unknown-elf-ld`, the
package's name for the same linker.

## 9. Problems you may hit

**`undefined reference to 'end'`** when linking without `-T`. There is no
default memory map, so pass a linker script (see §8).

**`final size of uleb128 value at offset … in .debug_loclists … exceeds
available space`** when linking with `-mlarge`. Linker relaxation shrinks the
code after GCC has written DWARF 5 location lists, and the MSP430 linker
cannot resize them. This happens with libraries built without the
`CFLAGS_FOR_TARGET="-g -gdwarf-4 -O2"` of §6 and §7: rebuild them with it.
If your own `-mlarge` code compiled with `-g` hits it, compile with
`-gdwarf-4` too, or link with `-Wl,--no-relax`.

**`cannot open linker script file msp430f5529.ld`** or `could not locate MCU
data file 'devices.csv'` when you use `-mmcu=<chip>`. The per-chip linker
scripts and headers (`msp430.h`, `msp430f5529.ld`, …) are not part of GCC.
They are in TI's free "MSP430 GCC support files" archive, from the
MSP430-GCC-OPENSOURCE page on ti.com. Unpack it and add
`-I<dir>/include -L<dir>/include` to the command line. (Debian's `msp430mcu`
package holds the same kind of files for the old `msp430` GCC 4 port; they
don't fit this GCC.)

**`makeinfo is missing` / `porting.info Error 127`.** Install `texinfo`, or
add `MAKEINFO=true` to the `configure` and `make` commands.

**`curl: (28) Failed to connect to ftp.gnu.org`.** Download from
sourceware.org, as in §3.

**`internal compiler error: in extract_constrain_insn` in `fs_path.cc`.** You
enabled C++ (`--enable-languages=c,c++`). The MSP430 code generator crashes
while compiling the C++ library. This happens with both GCC 15.3 and 16.2.
Build C only.

**`Error: file was compiled for the 430 ISA but the 430X ISA is selected`.**
The CPU options of different files or libraries don't match. Use the same
`-mcpu`/`-mlarge` options for every compile and link command.

**A step failed halfway.** Fix the cause, delete that step's build directory
(`rm -rf $WORK/build-…`) and start the step again from `configure`. For a
failure in the docs only (`MAKEINFO`), simply re-running `make` with the
missing option is enough.

## 10. Clean up

Once `make install` of the final GCC has succeeded, the work directory is no
longer needed:

```sh
rm -rf $WORK
```

The installed toolchain lives in:

- `$PREFIX/bin/msp430-elf-*`: the programs (the binutils ones are links into
  `/usr/bin`; removing the `binutils-msp430-unknown-elf` package breaks them)
- `$PREFIX/msp430-elf/`: newlib headers, libraries, linker scripts, and the
  links to the binutils programs GCC runs
- `$PREFIX/lib/gcc/msp430-elf/` and `$PREFIX/libexec/gcc/msp430-elf/`: compiler internals
