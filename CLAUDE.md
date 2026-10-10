# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Project status: active, nine targets.** One C11 frontend feeds nine backends: BESM-6
(`genbesm`; used to port [Unix v7 to the BESM-6](https://github.com/besm6/v7besm)),
RISC-V RV64IMFD/LP64D and RV32IMFD/ILP32D (`genriscv`, `--rv32`; bare-metal qemu,
link-compatible with clang — see [docs/Riscv_Backend.md](docs/Riscv_Backend.md)),
AArch64 ARMv8-A/AAPCS64 (`genaarch64`; bare-metal qemu with semihosting, link-compatible
with clang — see [docs/Aarch64_Backend.md](docs/Aarch64_Backend.md)), ARM32
ARMv7-A/AAPCS-VFP (`genarm32`; ARM state, bare-metal qemu with semihosting,
link-compatible with clang's `armv7a-none-eabihf` — see
[docs/Arm32_Backend.md](docs/Arm32_Backend.md)), x86-64 System V psABI (`genx86`;
bare-metal qemu `microvm`, x87 `long double`, link-compatible with clang's
`x86_64-none-elf` — see [docs/X86_64_Backend.md](docs/X86_64_Backend.md)), and AVR, the
8-bit ATmega1280 with the avr-gcc ABI (`genavr`; 16-bit `int`, binary32 `double`,
bare-metal qemu `arduino-mega`, link-compatible with clang's `--target=avr
-mmcu=atmega1280` — see [docs/Avr_Backend.md](docs/Avr_Backend.md)), and MSP430, the classic
16-bit TI MSP430 with the MSP430 EABI as GCC implements it (`genmsp430`; 16-bit `int`,
unsigned `char`, alignment 2, a soft binary64 `double`, every structure argument by
reference; bare-metal `mspsim`, link-compatible with `msp430-elf-gcc -mcpu=msp430`, its
`libgcc.a` and newlib — see [docs/Msp430_Backend.md](docs/Msp430_Backend.md)), and MMIX,
Knuth's 64-bit big-endian RISC with the MMIXware ABI as GCC implements it (`genmmix`; LP64,
signed `char`, the register stack, binary64 in hardware with `float` only in memory; Knuth's
`mmix` simulator, link-compatible with `mmix-knuth-mmixware-gcc`, its `libgcc.a` and newlib —
see [docs/Mmix_Backend.md](docs/Mmix_Backend.md)), and WebAssembly wasm32 with clang's C ABI
(`genwasm`; ILP32, signed `char`, binary128 `long double`, Braam's features
`-mreference-types -mbulk-memory -msign-ext -mmutable-globals -mnontrapping-fptoint` in
`WASM32_FEATURES`; LLVM wasm assembly assembled by clang and linked by `wasm-ld`, run
under node with `libc/wasm32/run.mjs` as the host of `env.putch`/`env.exit`;
link-compatible with clang — see [docs/Wasm_Backend.md](docs/Wasm_Backend.md)), with a
second target on the same code generator, `wasm32-braam`: a process of Braam
(`lower -t wasm32-braam` is wasm32 with the `braam` bit, `main` a coroutine
`coro(braam_call *) int main(int, char **)`; linked with the memory imported and no
entry, the driver appending the `braam` section; the runtime in `libc/wasm32/braam`,
run under node by its fake kernel `run.mjs` — see
[docs/Braam.md](docs/Braam.md)).
Two **hosted** targets reuse the x86-64 and AArch64 code generators: `x86_64-linux` and
`aarch64-linux` (`vgenx86`/`vgenaarch64 --linux`, which adds `.note.GNU-stack`; `lower`
aliases them to the bare-metal descriptors), assembled and linked by the system C compiler
(`cc -no-pie … -lvcc`) against glibc, with our glibc-compatible headers from `libc/linux/`
(our parser cannot read glibc's) and `libvcc.a` holding `__va_arg` and the coroutine
runtime (`libc/common/co.c`). A third hosted
target, `aarch64-darwin` (macOS on Apple silicon), has a descriptor of its own (signed
`char`, `long double` = `double`, `__builtin_va_class` = `tac_apple64_class`) and
`vgenaarch64 --darwin`: Mach-O (`_` names, `L` labels, `@PAGE`/`@PAGEOFF`), the GOT for
every name the unit does not define, and Apple's arm64 calls (named stack arguments at
their natural size, every variadic one on the stack, `va_list` a `char *`); it links with
`cc` (a PIE, `-lvcc`, its `libvcc.a` only the coroutine runtime) against libSystem, with
the headers of `libc/darwin/` (see
[docs/Aarch64_Backend.md](docs/Aarch64_Backend.md#hosted-macos)).
**`vcc`'s default target is the host** (`HOST_TARGET` in `cc/cc.c`: one of those three,
else `riscv64`); `cpp` and `lower` keep `riscv64` as theirs. `cc-tests` `HostedHeadersAgreeWithSystem` compares
header layouts and constants with the system compiler's.
Run the tests with `ctest -j8` (or `make run`): it is much faster than the binaries.
`scripts/bench_msp430.sh` prints mspsim cycles and code size of `bench/msp430/*.c`, ours
against `msp430-elf-gcc -O2`; `scripts/bench_mmix.sh` prints `mmix -s` instructions, oops
and mems of `bench/mmix/*.c`, ours against `mmix-knuth-mmixware-gcc -O2`;
`scripts/bench_wasm.sh` prints the Code section bytes of C files (by default the book
programs `wasm32-tests` leaves in `build/backend/wasm`), ours with and without the
rewrites against clang `-O2` and `-Os`; `scripts/bench_aarch64.sh` prints the text bytes
of `bench/msp430/*.c` and `libc/common/*.c` on the hosted AArch64 (macOS or Linux), ours
with and without the peephole pass against `cc -Os -fno-inline` and `-O2`.
A shared-code
change must keep every backend's tests green, and must not change BESM-6 output except
to fix a bug.

## Build & Test

```sh
make              # configure + build the compiler & runtime (RelWithDebInfo) into ./build/, no tests
make test         # build the compiler, runtime and all unit tests (incl. textbook chapter tests)
make run          # make test, then ctest --progress
make install      # build + install the compiler & runtime (see below)
make self         # build vcc with vcc: stage 1 installed into build/stage, stage 2 in build/self
make self-test    # make self, then build and run the tests against stage 2
make debug        # build with Debug flags
make clean        # remove ./build/
```

**`make install`** builds the compiler and runtime (not the tests), then installs the artifacts via `cmake --install`
to `~/.local`: `cc` → `bin/vcc` (the compiler driver, `cc/`), `cpp` → `bin/vcpp` (the C preprocessor, `cpp/`), `parse` → `bin/vparse`, `lower` → `bin/vlower`, `genbesm` →
`bin/vgenbesm6`, and the three runtime libraries
`libc.bin` / `libbem.bin` / `libruntime.a` → `share/vcc/besm6/lib/`, plus the twelve
**compiler-owned headers** → `share/vcc/besm6/include/` (the directory `b6cc` appends to every
compilation): the C11 freestanding subset (C11 §4 — `float.h`, `iso646.h`, `limits.h`,
`stdalign.h`, `stdarg.h`, `stdbool.h`, `stddef.h`, `stdint.h`, `stdnoreturn.h`) plus
`besm6.h`, `coro.h` (the short names `defer` and those of the coroutines) and `alloca.h`. Those describe the compiler itself — its
data model, its `<stdarg.h>` ABI, its intrinsics, its extensions — so they must ship with it (`install(FILES …)` in `libc/besm6/CMakeLists.txt`,
an explicit list rather than a directory glob, because the split *is* the ownership
boundary). **Deliberately not installed:** the Unix `libc0.a`, `crt0.o`, and the *hosted*
C11 headers (`stdio.h`, `string.h`, `stdlib.h`, `math.h`, …) — the sibling **v7besm**
project owns the authoritative hosted `libc.a` and `crt0.o`, and ships the headers that go
with them, so this repo ships only what v7besm cannot supply: `libruntime.a` (the `b$*`
helpers the code generator emits calls to) and the freestanding/intrinsics headers. The
hosted headers and `libc0.a` still build and are used in-tree (test fixtures, the
`besm-headers` test, the Unix run harnesses).
For RISC-V it installs `genriscv` → `bin/vgenriscv64`, the runtime `crt0.o`, `libc.a`
and the qemu `virt` linker script `link.ld` → `share/vcc/riscv64/lib/`, and *all* the RISC-V and
shared headers, hosted ones included, → `share/vcc/riscv64/include/` (no other project supplies a
RISC-V libc); the runtime only when the RISC-V binutils (or a clang with ld.lld/llvm-ar) were found. The same for riscv32:
`genriscv` again as `bin/vgenriscv32` (the `32` in its name selects RV32, as `--rv32` does),
the `libc/riscv32` runtime and headers → `share/vcc/riscv32/`. And for AArch64: `genaarch64` →
`bin/vgenaarch64`, the `libc/aarch64` runtime → `share/vcc/aarch64/lib/` (when the AArch64
binutils or clang were found) and the AArch64, LP64 and shared headers → `share/vcc/aarch64/include/`.
The same for ARM32: `genarm32` → `bin/vgenarm32`, the `libc/arm32` runtime →
`share/vcc/arm32/lib/` (when the ARM binutils or clang were found) and the ARM32, ILP32 and shared
headers → `share/vcc/arm32/include/`. And for x86-64: `genx86` → `bin/vgenx86`, the
`libc/x86` runtime → `share/vcc/x86_64/lib/` (when x86-64 binutils or clang were found) and the
x86-64, LP64 and shared headers → `share/vcc/x86_64/include/` (x86-64's own `float.h` and
`limits.h` in place of the LP64 ones). And for AVR: `genavr` → `bin/vgenavr`, the
`libc/avr` runtime → `share/vcc/avr/lib/` (when the AVR binutils or clang were found) and the AVR
and shared headers → `share/vcc/avr/include/`. And for MSP430: `genmsp430` → `bin/vgenmsp430`,
the `libc/msp430` runtime (`crt0.o`, `libc.a` and the mspsim `link.ld`) →
`share/vcc/msp430/lib/` (when the MSP430 binutils were found) and the MSP430, `ip16` (but
for the `stddef.h`/`stdint.h` MSP430 has its own) and shared headers →
`share/vcc/msp430/include/`. And for MMIX: `genmmix` → `bin/vgenmmix`, the `libc/mmix`
runtime (`crt0.o` and `libc.a`; no linker script, the linker's own serves) →
`share/vcc/mmix/lib/` (when the GNU MMIX toolchain was found) and the MMIX, `lp64` (but for the
`float.h`/`limits.h` MMIX has its own) and shared headers → `share/vcc/mmix/include/`.
And for WebAssembly: `genwasm` → `bin/vgenwasm`, the `libc/wasm32` runtime (`crt0.o`,
`libc.a`, no linker script, and the node host `run.mjs`) → `share/vcc/wasm32/lib/` (when a
clang with the WebAssembly target, `wasm-ld` and `llvm-ar` were found) and the wasm32,
`ilp32` (but for the `limits.h` wasm32 has its own) and shared headers →
`share/vcc/wasm32/include/`. And `wasm32-braam`: its `crt0.o` (the exports), `libc.a` and
the fake kernel `run.mjs` → `share/vcc/wasm32-braam/lib/`, and `libc/wasm32/braam/include`
merged ahead of wasm32's headers → `share/vcc/wasm32-braam/include/` (the build stages the
same tree in `build/share/vcc/wasm32-braam`, where `build/cc/cc` finds it).
And for the hosted targets (`libc/linux/CMakeLists.txt`): `share/vcc/x86_64-linux/` and
`share/vcc/aarch64-linux/`, `lib/libvcc.a` (built only where a C compiler for that Linux
exists: the build's own on a matching host, else `<triple>-gcc`) and `include/` merged from
`libc/linux/<arch>/include`, `libc/linux/include`, the architecture's, LP64 and shared
headers, the first of a name winning; and `share/vcc/aarch64-darwin/`
(`libc/darwin/CMakeLists.txt`), `include/` from `libc/darwin/include`, AArch64's, LP64
and shared headers, and `lib/libvcc.a` (the coroutine runtime, built on macOS on Apple
silicon).
The default prefix `~/.local` is set in the top-level `CMakeLists.txt` (unless
`CMAKE_INSTALL_PREFIX` is given; `cmake --install build --prefix DIR` also overrides it); the
binaries are renamed (`v` prefix) only at install time via
`install(PROGRAMS … RENAME)`, so the in-tree build outputs (`build/parse`, `build/lower`,
`build/backend/genbesm`, `build/cpp/cpp`, `build/cc/cc`) keep their original names.
`vcc` is relocatable: it runs the passes from its own directory and takes headers and
libraries from `../share/vcc/<target>/` (see [cc/README.md](cc/README.md)).

**`make self`** configures `build/self` with `-DCMAKE_C_COMPILER=build/stage/bin/vcc` (an
unknown compiler to CMake, so the driver ignores the GCC options CMake passes: `-W…`,
`-f…`, `-std=`, `-arch`, `-isysroot`), which also makes `vcc -E` the `SystemCpp`. The
hosted targets then keep the system `cc` (`VCC_HOST_CC`). It works on macOS and Linux
(the `libc/darwin` and `libc/linux` headers carry the POSIX interfaces the sources use:
`unistd.h`, `fcntl.h`, `getopt.h`, `sys/stat.h`, `sys/wait.h`, and `mach-o/dyld.h` on
macOS; on Linux `sys/types.h` and `sys/stat.h` are per architecture, glibc's
`struct stat` differing). CMake does not rebuild `build/self` when stage 1 changes:
`rm -rf build/self` first. The sources stay compilable by it: no GCC builtins outside `#ifdef __GNUC__`
(`libutil/bitops.h`), and no local named like a libc function (no shadowing).
Stage 2 rebuilt by itself gives identical objects.

**Tests are not part of `all`.** A plain `make`/`make all` builds only the compiler and
runtime (the passes, the driver and every target's libraries), which is all `make install`
needs. The top-level `CMakeLists.txt` walks every directory at the end and marks each
`*-tests` target `EXCLUDE_FROM_ALL`, making it a dependency of the custom target `tests`
(not `test`, which CMake reserves for ctest); GoogleTest itself (vendored in `third_party/googletest`) is added
`EXCLUDE_FROM_ALL`, so it is built only for them. `make test` builds `all tests`; `make run`
depends on `make test` and then runs `ctest --test-dir build --progress`. A new test
executable needs nothing more than a name ending in `-tests`.

**The "Writing a C Compiler" chapter tests are compiled into the regular test binaries.**
The chapter sources (`*/test/chapter*_tests.cpp`, and the backend run programs in
`backend/common/test/book/chapter*_tests.cpp`) are listed in the same
`add_executable(<module>-tests …)` as the unit tests, so e.g. `parser-tests` and `besm-tests`
contain both. The book run programs are shared by every backend: each test is a `BookTest`,
a fixture each backend defines in its own `test/book_test.h` (with a skip list for programs
its target cannot run). There are no separate `*-book-tests` executables and no ctest
`book` label. `fatal_error()` (the libraries call it, but it is defined in the test
executable) is defined exactly once per binary in a regular unit-test source
(`parser/test/simple_tests.cpp`, `semantic/test/typecheck_tests.cpp`, `optimize/test/pipeline_tests.cpp`,
`backend/besm6/test/codegen_tests.cpp`); the chapter sources do **not** redefine it. The scanner
needs none — it uses its own `lex_error()`/`exit()`.

Run a single test binary directly (semantic and translator tests live in subdirectories):
```sh
./build/cc/test/cc-tests
./build/cpp/test/cpp-tests
./build/ast/ast-tests
./build/parser-tests
./build/tac/tac-tests
./build/semantic/semantic-tests
./build/translator/translate-tests
./build/backend/besm6/besm-tests
./build/optimize/optimizer-tests
```

Run a specific GoogleTest case:
```sh
./build/parser-tests --gtest_filter="*ExprTest*"
./build/semantic/semantic-tests --gtest_filter="PipelineTest.*"
```

**Run tests from the build directory** to avoid polluting the source tree with temporary
files that GoogleTest writes during test discovery:
```sh
cd build/semantic && ./semantic-tests
ctest --test-dir build -R "Typecheck|Pipeline"
```

**Runtime library (`libc.bin`).** The runtime routines cover a substantial hosted libc
subset: I/O (`printf`/`sprintf`/`snprintf` over the shared `doprnt` pointer-walk engine,
`puts`, `putchar`, `getch`, `putch`, `putbyte`, `flush`), all of `<string.h>`
(`strlen`/`strcpy`/`strncpy`/`strcat`/`strncat`/`strcmp`/`strncmp`/`strchr`/`strrchr`/`strstr`/`strtok`/`strerror`
and the `mem*` family `memcpy`/`memmove`/`memset`/`memcmp`/`memchr`), the dynamic allocator
(`malloc`/`calloc`/`realloc`/`free` — **Unix `libc0.a` only**: the heap lives between the
linker's `end` symbol and the b6sim stack base, which the Dubna monitor does not provide,
so `libc/besm6/CMakeLists.txt` drops `malloc` from `LIBC_C_MADLEN_SOURCES` and it is in
neither `libc.bin` nor `libbem.bin`), `atoi`, and the math helpers
(`fabs`/`fmin`/`fmax`/`fma`/`modf`, plus the frameless `frexp`/`ldexp`). The C sources are
split by Dubna dependency: the **portable** routines (everything above the I/O leaves — the
format engine, allocator, string/mem/math) are shared by every BESM-6 assembler backend —
the target-neutral ones in `libc/common/*.c` (also compiled for RISC-V), the BESM-6-specific
`doprnt`/`modf`/`malloc`/`putch` in `libc/besm6/*.c` — while only the three Dubna-monitor **leaves** — `putbyte` (owns the
KOI7 stdout buffer), `flush` (`b/tout`), `getch` (`moncard_`/`monread_`) — stay in
`libc/besm6/madlen/*.c` alongside the hand-written Madlen helpers; the sibling
`libc/besm6/unix` target reimplements those three over Unix v7 syscalls (`write.s`/`read.s`
extracode leaves, plus a `crt0.s` startup that calls `int main`) and archives the result with
`b6ar`/`b6ranlib` into **two** archives for the `b6as`/`b6ld`/`b6sim` path (+ a standalone
`crt0.o`): `libruntime.a`, holding only the 37 `b$*` compiler helpers (`b_*.s`), and
`libc0.a`, holding the minimal C library — the portable routines, the `putbyte`/`flush`/`getch`
leaves, the `write.s`/`read.s` syscall leaves, and the hand-written standard-C assembly
`exit.s`/`frexp.s`/`ldexp.s`. The split exists because only `libruntime.a` is installed; v7besm
owns the real `libc.a`/`crt0.o`/headers. Link order is always `crt0.o`, the program object,
`libc0.a`, `libruntime.a` — libc0 calls the helpers, never the reverse (the only intra-helper
edges are `b_udiv.s` → `b$div`/`b$mod`).
All `.c` are compiled by our own toolchain (`parse → lower → genbesm --madlen` → `.madlen`) and
assembled with the Madlen helpers (`b_*.madlen`, `b_tout`, `exit`, `frexp`, `ldexp`) into
`libc.bin` — see `libc/besm6/CMakeLists.txt` (`LIBC_C_PORTABLE` / `LIBC_C_DUBNA` /
`LIBC_MADLEN`) and `libc/besm6/unix/CMakeLists.txt` (`UNIX_RUNTIME_HELPERS` /
`UNIX_ASM_LIBC`, the `besm6_unix_archive` macro) for the Unix archives. The
original B sources have been removed; the runtime is now C plus Madlen only. Runtime-helper
names use `$` as their special separator (e.g. `b$tout`, `b$ret`, `b$save`) — this is the
**canonical IR form** carried unchanged from the scanner through TAC into the `Besm_Module`;
each backend renders it per-dialect: the Madlen emitter lowers `$`→`/` (so `b$tout` → `b/tout`,
matching the `.madlen` helper symbols), while the Unix (`b6as`) emitter keeps `$` (`b6as`
accepts `$` in names). An `extern T name[]` array emits no TAC top-level; a later
reference decays the array to its address via `GET_ADDRESS`, which self-declares the
external name (SUBP), so cross-module array indexing needs no special array-ness record.

**The BESM-6 tests (`besm-tests`) can be run from any directory.** At startup every test
binary `chdir()`s into its own build directory (a GoogleTest global environment compiled in
via `libutil/test/test_chdir.cpp` and the `test_chdir_to_bindir()` CMake helper, keyed off a
per-target `TEST_BINARY_DIR` define), so `besm-tests` always lands in `build/backend/besm6`
— the directory that holds the assembled runtime library `libc.bin`, which the Dubna
simulator job links from the current working directory. (Before this, running from elsewhere
used a stale or missing library and the run tests reported `ERROR`.) The same chdir keeps
manual runs of every other test binary from littering the source tree with their scratch
files (`<TestName>.c`, `.ast`, `.dub`, `.lst`); under `ctest`/`make run` it is a no-op
because ctest already runs each binary in its build directory. Either of these works:
```sh
cd build/backend/besm6 && ./besm-tests
./build/backend/besm6/besm-tests          # also fine — it chdir()s itself
```

**Debugging a failing run with Dubna instruction tracing.** A `CompileAndRun` test leaves
its job file `<TestName>.dub` in `build/backend/besm6`. Re-run it under the simulator with
full instruction/register tracing via `-d c`:

```sh
cd build/backend/besm6
dubna -d c RemainderRun.dub > RemainderRun.trace
```

The trace lists every instruction with the effective address, the memory word read/written,
and the resulting `ACC` / `RAU` (mode register R) / index-register values, e.g.:

```
01065 R: 00 037 0000 ntr
      RAU = 00
01066 L: 16 015 0014 aox 14(16)
      Memory Read [01101] = 6400 0000 0000 0000
      ACC = 6400 0000 0000 0002
      RAU = 04
```

Search the trace for a routine label (e.g. `B/MOD`, `B/DIV`) to follow a runtime helper and
inspect the accumulator/exponent at each step — the fastest way to localize a wrong
mode-bit, exponent, or addressing error in hand-written Madlen. To build a focused job by
hand: assemble a `.mad` with `genbesm --madlen` (Unix is the default dialect, so the flag is
required here), wrap it with the `*name/*disc/*file:libc,40/*assem
… *library:40/*execute/*end file` boilerplate (see `codegen_test.h` `CompileAndRun`), and
run `dubna [-d c] job.dub`.

**Running a program on the Unix (`b6as`) path.** Alongside the Madlen/`dubna` `CompileAndRun`,
`codegen_test.h` provides `CompileAndRunUnix` — the Unix-dialect run harness (`besm-tests`
`CodegenTest.UnixRun*`, `test/unix_run_tests.cpp`). It compiles in-process via `CompileToUnix`
(`genbesm --unix`), assembles with `b6as`, links with `b6ld` — **`crt0.o` first**, then the
program object, then `libc0.a` and `libruntime.a` (`b6ld` takes the entry point from the first
object's first text word) — and runs the linked `b.out` under the `b6sim` simulator via
`RunExternalProgram`,
returning the captured stdout. `b6sim` traps the Unix v7 syscalls onto the host, so a program's
`write(1,…)` lands straight on stdout — no `.lst`/`≠` scraping (unlike the `dubna` path). The
Unix `crt0` calls `int main(void)` (not the Madlen libc's `void program()`), so these test
programs define `main()`. All external tools (`b6as`/`b6ld`/`b6sim`, in the sibling `v7besm`
tree) resolve by bare name on `PATH`; `crt0.o`, `libc0.a`, and `libruntime.a` are staged
next to `besm-tests` in
`build/backend/besm6/`. Tests using it guard with `SKIP_IF_NO_UNIX_RUN_TOOLS()` so `make run`
stays green where the toolchain is absent. To reproduce by hand:
`b6sim build/backend/besm6/<TestName>.b6` (add `-d irm` / `--trace=FILE` for tracing).

**Running a program on the Bemsh (`*bemsh`) path.** `codegen_test.h` also provides
`CompileAndRunBemsh` — the Bemsh-dialect `dubna` run harness (`besm-tests` `CodegenTest.Bemsh*Run`,
`test/bemsh_run_tests.cpp`). It compiles in-process via `CompileToBemsh` (`genbesm --bemsh`), wraps
the output in a `*bemsh` job linking the Bemsh runtime `libbem.bin` on library 40 (model:
`backend/besm6/tmp/bemsh.dub`, entry `*main progra`), runs `dubna`, and scrapes the same `.lst`
`≠`/`----` framing as the Madlen path — both share the `ExtractDubnaOutput` helper. genbesm
`--bemsh` wraps each module in its own `ввд$$$…кнц$$$` Macro-Bemsh deck (the translator processes
one module per deck). Unlike Madlen's auto-declaring `,call,`, Bemsh's `пв` needs an explicit
`внешн` per call target (spliced in by `codegen.c` for the Bemsh dialect) and must carry the return
register — `пв name(13)`; code labels are emitted as a labeled `ноп` (not `экв *`, which captures
the wrong half-cell address). `libbem.bin` holds the hand-written helpers (task B4) plus the full
compiled portable C libc (task B5): `printf`/`doprnt`, `sprintf`/`snprintf`, the `str*`/`mem*`
families, the `fabs`/`fmax`/`fmin`/`fma`/`modf` math, `atoi`, and the stdout chain
`putbyte`/`flush`/`putchar`/`putch`/`puts` — the same `LIBC_C_PORTABLE` set as the Madlen `libc.bin`
minus `malloc` (needs the Unix heap map) and `getch` (input, deferred). So these tests use `void
program()` with the same hosted-libc surface as the Madlen path; the printf/string/float run tests
are in `test/bemsh_printf_tests.cpp`. Bringing up that surface surfaced (and fixed) B4 helper bugs —
a missing base-register load (Madlen `,base,` loads the register, Bemsh `УПОТР` only declares it, so
each based helper needs an explicit `уиа _NAME(14)`) and `экв *` code labels (now labeled `ноп`) —
plus emitter literal bugs (Madlen-form `=377`/`=:64` literals → Bemsh `=в'377'`/`=в'6400000000000000'`,
and a type-Е mantissa overflow on 2^40 → octal bit-pattern fallback). To reproduce by hand:
`dubna [-d rime] build/backend/besm6/<TestName>.dub`.

**Target standard headers (`libc/besm6/include/`, `libc/riscv64/include/`, `libc/riscv32/include/`, `libc/aarch64/include/`, `libc/arm32/include/`, `libc/x86/include/`, `libc/avr/include/`, `libc/msp430/include/`, `libc/mmix/include/`, `libc/wasm32/include/`, `libc/lp64/include/`, `libc/ilp32/include/`, `libc/ip16/include/`, `libc/common/include/`).**
C11 standard-library headers: each target's directory holds the headers that depend on its
data model (`float.h`, `limits.h`, `stdint.h`, `inttypes.h`, `stddef.h`, `stdarg.h`, `math.h`,
`setjmp.h`; BESM-6 also `besm6.h`, `malloc.h`) — except that riscv64 and aarch64 share
`float.h`/`inttypes.h`/`limits.h`/`math.h` in `libc/lp64/include/` (x86-64 takes
`inttypes.h`/`math.h` from there, with its own `float.h` for the x87 `long double` and
`limits.h` for the signed `char`), and riscv32, arm32 and wasm32 (which has its own
`limits.h`, for the signed `char`)
`inttypes.h`/`limits.h`/`math.h` in `libc/ilp32/include/`, searched second
(`wchar_t` keeps `stddef.h`/`stdint.h` apart, and `long double` the ILP32 `float.h`); AVR and MSP430
share the 16-bit `inttypes.h` in `libc/ip16/include/`, whose `stddef.h`/`stdint.h` are AVR's
(MSP430's `wchar_t` is msp430-elf-gcc's `long`; plain `char` keeps `limits.h` apart, and
`double` `float.h`) — and `libc/common/include/` the target-neutral
rest, searched last (the freestanding subset is complete; the hosted subset declares the
few implemented libc routines plus future ones — see `libc/besm6/include/README.md`).
`parse` has no preprocessor, so these are consumed by a preprocessor first: our own
`cpp` (`cpp/`, installed as `vcpp`; `-t besm6|riscv64` selects the target macros and the
installed `share/vcc/<target>/include`, `-nostdinc` drops it) or, as the build itself still
does (`SystemCpp`), the C compiler's `cc -E` — not a traditional standalone `cpp`: a traditional `cpp`
(e.g. Apple's `/usr/bin/cpp`) only recognizes a `#` directive in column 1, so indented
`#include` lines silently fail to expand. No `-P` is needed — `parse`'s scanner consumes
`# line` markers and keeping them preserves original line numbers in diagnostics:
`cc -E -nostdinc -Ilibc/besm6/include -Ilibc/common/include prog.c | parse -`. The
`besm-headers`, `riscv-headers`, `aarch64-headers`, `arm32-headers`, `x86_64-headers`, `avr-headers`, `msp430-headers`, `mmix-headers` and `wasm32-headers` CTests (`scripts/check_headers.sh`, run under `make run`)
preprocess and parse every header to catch syntax errors; their `besm-headers-cpp`/
`riscv-headers-cpp` twins do the same through our `cpp` (`CPPFLAGS=-t<target>`). The unit-test fixtures preprocess
their C snippets automatically via `libutil/test/test_preprocess.h` (using the CMake
`TEST_CPP`/`TEST_INCLUDE_DIR`/`TEST_COMMON_INCLUDE_DIR` defines, plus the optional
`TEST_MODEL_INCLUDE_DIR`: `libc/lp64/include` for riscv64, aarch64 and x86_64, `libc/ilp32/include`
for riscv32 and arm32, none for avr), so
tests `#include <stdio.h>` instead of hand-declaring libc routines. `<stdarg.h>` is
functional (BESM-6: word-pointer `va_list`, covered by `stdarg_tests.cpp`; RISC-V: a byte
pointer over the register save area, covered by the RISC-V run and interop tests; AArch64:
the AAPCS64 `va_list` structure, `va_arg` through the runtime's `__va_arg` given the
argument class from the `__builtin_va_class(T)` keyword; ARM32: clang's
`struct __va_list`, a pointer walk over r0–r3 pushed below the stack arguments; x86-64:
clang's `__va_list_tag[1]` over the 176-byte register save area, `va_arg` through
`__va_arg` given the eightbyte classes from `__builtin_va_class(T)`; AVR: a `char *` walk
over the stack, where a variadic callee takes every argument).

Static analysis: when `cppcheck` is installed, CMake attaches it to every C and C++ target
(`CMAKE_C_CPPCHECK`/`CMAKE_CXX_CPPCHECK` in the top-level `CMakeLists.txt`), so it runs as
part of the build and any finding fails the build. There is no separate ctest for it.
For C it loads `scripts/cppcheck-c11.xml`, which maps `_Noreturn` to GCC's attribute: cppcheck
2.21 ignores `_Noreturn`, and reports false null dereferences after a `fatal_error()`.

Try the compiler tools:
```sh
./build/parse input.c               # parse → binary AST to input.ast (NOT stdout)
./build/parse input.c --yaml        # parse → YAML AST
./build/parse input.c --dot         # parse → Graphviz DOT
./build/parse -D input.c            # debug: parser trace + AST dump + leak report

# IMPORTANT: parse and lower do NOT write binary output to stdout — they create a new
# file with an .ast / .tac suffix next to the input.  To emit to stdout (e.g. to pipe or
# redirect), pass "-" as the output argument:
./build/parse input.c -          > /tmp/input.ast   # "-" = binary AST to stdout
./build/parse input.c               # → input.ast (default file output)
./build/lower /tmp/input.ast              # → binary TAC to /tmp/input.tac (default target: riscv64)
./build/lower --yaml /tmp/input.ast -     # → YAML TAC to stdout ("-" = stdout)
./build/lower -t x86_64 /tmp/input.ast -  # → TAC with x86_64 type sizes/offsets
./build/lower -D /tmp/input.ast           # debug: translator trace

# genbesm needs TAC lowered for its target: ./build/lower -t besm6 /tmp/input.ast
# genbesm defaults to the Unix (b6as) dialect; the output file's extension follows the
# dialect (.s for Unix, .mad for Madlen, .bem for Bemsh) when no output name is given.
./build/backend/genbesm /tmp/input.tac              # → Unix b6as assembly to /tmp/input.s
./build/backend/genbesm --madlen /tmp/input.tac     # → Madlen assembly to /tmp/input.mad
./build/backend/genbesm /tmp/input.tac out.s        # explicit output filename
```

Compiler flags in use: `-Wall -Werror -Wshadow` — all warnings are errors (plus
`-Wno-dangling-else` for C++: GCC 16 flags an unbraced `if` around a GoogleTest `EXPECT_*`).
C is compiled as C11 (`CMAKE_C_STANDARD 11`): GCC 15's default C23 makes `alignas` a keyword.

**Cross tools** (`scripts/CrossTools.cmake`, `vcc_find_cross`, called by each `libc/<target>/CMakeLists.txt`):
GNU binutils first, by a list of prefixes (`riscv64-unknown-elf`, `aarch64-none-elf`, `arm-none-eabi`,
`x86_64-elf`/`x86_64-linux-gnu`/the host's, `avr`, `msp430-elf`/`msp430-unknown-elf`); else clang +
ld.lld + llvm-ar (`-DVCC_CROSS_TOOLS=gnu|llvm` forces one). wasm32 has no binutils:
`LLVM_ONLY` and `LD wasm-ld` give it clang + wasm-ld + llvm-ar alone. It sets `<T>_AS` (command with flags),
`<T>_LD`/`<T>_LDFLAGS`, `<T>_AR`, `<T>_TOOLS_FOUND`, and separately `<T>_CLANG_FOUND`: clang is only
the tests' optional *reference compiler* (interop, the book comparison, `HeadersAgreeWithClang`),
guarded by `SKIP_IF_NO_<T>_CLANG()` (for the AVR book programs `avr-gcc` takes its place when found); `msp430-elf-gcc` likewise by `SKIP_IF_NO_MSP430_GCC()`. The test
fixtures take the assembler as `<T>_ASSEMBLER` (one blank-separated string, `cross_tools()` in
`qemu_test.h`), and `vcc`'s target table must use the same flags (`cc-tests` checks the `-v` echo).

## Architecture

This is a multi-platform C11 compiler. The shared frontend emits TAC; machine backends under `backend/` consume TAC and emit target assembly: BESM-6, RISC-V, AArch64, ARM32, x86-64, AVR, MSP430, MMIX and WebAssembly. The pipeline:

```
[vcc] drives the whole chain, then the assembler and linker (<target>-as + <target>-ld GNU binutils, else clang + ld.lld | clang + wasm-ld | b6as + b6ld):

Source (.c)
  → [cpp]        Macro expansion, #include, #if → preprocessed C (`-t` target macros)
  → [parse]      Scanner → Parser → AST (binary/YAML/DOT)
  → [lower]      Typecheck → Translate → Optimize → TAC (binary/YAML/DOT)
  → [genbesm]    Frame alloc → Instruction select → Unix b6as assembly (.s, default)
                                                  → Madlen assembly (.mad, --madlen)
                                                  → Bemsh autocode (.bem, --bemsh)
  → [genriscv]   Register alloc → Instruction select → Frame → Peephole → GNU assembly (.s)
                 (TAC lowered with `lower -t riscv64`)
  → [genaarch64] Register alloc → Instruction select → Frame → Peephole → GNU assembly (.s)
                 (TAC lowered with `lower -t aarch64`)
  → [genarm32]   Register alloc → Instruction select → Frame → Peephole → unified assembly (.s)
                 (TAC lowered with `lower -t arm32`)
  → [genx86]     Register alloc → Instruction select → Peephole → Frame → AT&T assembly (.s)
                 (TAC lowered with `lower -t x86_64`)
  → [genavr]     Register alloc → Instruction select → Peephole → Frame → Branch relaxation
                 → GNU avr-as assembly (.s)   (TAC lowered with `lower -t avr`)
  → [genmsp430]  Register alloc → Instruction select → Peephole → Frame → Branch relaxation
                 → GNU msp430-as assembly (.s)   (TAC lowered with `lower -t msp430`;
                 assembled and linked by msp430-elf-as/-ld, --gc-sections)
  → [genmmix]    Register alloc → Frame → Instruction select → Prologue/epilogue → Peephole
                 → GNU mmix-as assembly (.s)   (TAC lowered with `lower -t mmix`;
                 assembled with mmix-knuth-mmixware-as -x, linked by -ld into a .mmo)
  → [genwasm]    Frame → Instruction select within Structured control flow → Stackify
                 → Peephole → Local coalescing → LLVM wasm assembly (.s)   (TAC lowered
                 with `lower -t wasm32`; assembled by clang --target=wasm32, linked by
                 wasm-ld into a module run under node)
```

**`parse`** (`parser/main.c`): Lexes and parses a C source file, outputs a binary AST stream (via `wio`) to stdout, or `--yaml`/`--dot` for human-readable forms.

**`lower`** (`translator/main.c`): Reads the binary AST, runs semantic analysis and TAC lowering, then runs the TAC optimizer, and outputs TAC. Lowering is complete. The optimizer runs five passes in a loop until a round changes nothing (compared by the list's YAML spelling, since the passes rewrite in place; at most 64 rounds): constant folding, unreachable code elimination, common-subexpression elimination (pure operations, memory reads, and store-to-load forwarding), copy propagation, and dead store elimination. At that fixed point, induction-variable strength reduction (`optimize/ivsr.c`: a pointer stepped through the array in place of `v[j]`, the loop test against an end pointer, the dead index dropped) runs, and the loop goes on if it changed anything; the translator lowers `while`/`for` tested at the bottom behind a guard (loop rotation), and constant folding mirrors a constant-first comparison. These loop optimizations are off for BESM-6 (`Target.no_loop_opt`), whose code they must not change. Optimizer flags: `--no-unreachable`, `--no-cse`, `--no-copy-prop`, `--no-dead-store`, `--no-ivsr`, `--no-loop-rotate`, `--opt-debug`, `--opt-max-iter N` (also `VCC_OPT_MAX_ITER`, read by the backend test fixtures too — the way to bisect a miscompile to the round that introduces it); and for the translator `--no-shared-cleanup` (every exit lowers its own copy of a `defer` cleanup, `translate_shared_cleanup`) and `--no-cond-jumps` (`&&`/`||`/`!` in a condition lowered as a 0/1 value tested again, and every rotated loop guarded by a copy of its test, `translate_cond_jumps`; otherwise conditions are jumps, `gen_cond_jump`, and a loop whose condition is not simple, `is_simple_cond`, is entered at its test; both off for BESM-6). The TAC YAML format is documented in [docs/Technical_Reference.md](docs/Technical_Reference.md).

### Compiler phases

| Phase | Location | Status |
|---|---|---|
| Compiler driver | `cc/` | Complete (ported from v7besm `cmd/cc` (b6cc); `-t riscv64|riscv32|aarch64|arm32|x86_64|avr|msp430|mmix|wasm32|besm6`, `-E/-S/-c`, `-Smadlen/-Sbemsh`, assembles and links with the target's GNU binutils (`riscv64-unknown-elf-`, `aarch64-none-elf-`, `arm-none-eabi-`, `x86_64-elf-` or the host's, `avr-`; else clang and ld.lld), msp430-elf-ld (`--gc-sections`, then GCC's `libgcc.a` when found), mmix-knuth-mmixware-ld (text at 0x100, the linker's own script, a `.mmo`, then GCC's `libgcc.a` when found), clang and wasm-ld for wasm32 (a module for node, no script) or b6ld; finds the passes beside itself and `../share/vcc/<target>`; tool overrides `VCC_CPP`/`VCC_PARSE`/`VCC_LOWER`/`VCC_GEN`/`VCC_AS`/`VCC_LD` (the last two split at blanks, so `VCC_AS="clang --target=msp430 -c"` works); the BESM-6 `crt0.o`/`libc.a` are v7besm's, not installed here; see [cc/README.md](cc/README.md)) |
| Preprocessor | `cpp/` | Complete (Reiser v7 cpp modernized to C11, ported from v7besm `cmd/cpp` (b6cpp); adds `-t`/`--target` and `-nostdinc`; the `#ifdef besm6` size profile in `defs.h` is kept for diffability with v7besm, where it builds natively; see [cpp/README.md](cpp/README.md)) |
| Lexer | `scanner/` | Complete |
| Parser | `parser/` | Complete |
| AST | `ast/` | Complete (alloc/free/export/import/yaml/graphviz/clone/compare) |
| Type checking | `semantic/typecheck.c` | Complete |
| Loop labeling | `semantic/label_loops.c` | Complete |
| `defer` (vcc extension) | `semantic/defer.c`, `translator/stmt.c` | Complete on every target: `_Defer stmt` (`defer` from `<coro.h>`) runs `stmt` when its block is left — fall-off, `return` (the value taken first), `break`, `continue`, `goto` out — innermost block first, last registered first; the unbraced substatement of an `if`/`switch`/loop is a block of its own. `semantic/defer.c` rejects a jump into a block past a defer (`goto`, `case`, `default`), a jump into or out of a deferred statement, and `return` inside one; `label_loops` makes a deferred statement a barrier to `break`/`continue`. The translator keeps a stack of blocks (`TacScope`) with their exit actions and lowers a deferred statement afresh on every exit, relabelled each time (`label_loops_stmt`); an exit whose cleanup is large (and a coroutine's destroy path, from a smaller size) shares instead one copy lowered at the block's end, a chain picked by a "where next" variable (`leave`/`emit_chain` in `translator/stmt.c`; not on BESM-6, and not under `lower --no-shared-cleanup`). A `co_alloca` counts as a defer for the jump checks |
| Coroutines (vcc extension) | `semantic/coroutines.c`, `translator/coro.c`, `libc/common/co.c` | Complete on every target but BESM-6 ([docs/Coroutines_in_C.md](docs/Coroutines_in_C.md), the tutorial, its §10 the frame ABI; [docs/Coroutines_Internals.md](docs/Coroutines_Internals.md), the implementation, the alternatives rejected and the open questions): generators, `await` in both forms over a task's arena, `co_alloca`, cancel and destroy; `_Coro_ptr(Y, T)` (`coro_ptr`, a pointer to a struct `__co_desc` like a frame type's; a coroutine taking `(void)` or `(void *)` converts to it, its value the address of `f$co`, then {size, align, init, resume}, the init of a `(void)` one the thunk `f$initp`; `co_init`/`co_alloca`/`co_sizeof`/`co_alignof` and `await p(arg)` take one); an arena `await` calling `g$resume` directly; the dispatch a chain of compares, on wasm32 (`Target.jump_tables`) a `JUMP_TABLE` from three suspension points (`br_table` in `backend/wasm/structure.c`, a trampoline for an entry rerouted through a regional dispatch node), its labels `%co.resumek.f` carrying the coroutine's name; the lint for frames in automatic storage (`warning:` on stderr when one outlives its storage or may be left suspended). The Braam target `wasm32-braam` ([docs/Braam.md](docs/Braam.md), its §7 the internals; the worked example [docs/Braam_Example.md](docs/Braam_Example.md)): the runtime `libc/wasm32/braam`, `stdio.h` on files and stdin, `stat` and the path calls, signals (`sig_catch`/`sig_take`, `EINTR`), tasks (`braam_spawn` over a table of `BRAAM_TASKS`), `braam_yield` and `poll`, the fake kernel `run.mjs`, and the `braam-system` ctest on a built braam-core. Syntax: `_Coro(Y)` (`FUNC_SPEC_CORO` with its yield type, `Symbol.u.func.coro`/`yield_type`), `_Coro_frame(Y, T)` (a `struct __co_frame` never defined, carrying `Y`/`T` in `struct_t.frame_yield`/`frame_result` and compared by them), `_Yield [expr]` (operand a relational expression), `_Await expr`, and `__co_init` … `__co_alignof` (`EXPR_CO_OP`); short names in `<coro.h>`; every rule of [docs/Coroutines_in_C.md](docs/Coroutines_in_C.md) section 7 checked; BESM-6 alone has `Target.no_coroutines` (`cpp` predefines `__vcc_coroutines__` on every other target). Lowering in two stages: the translator makes `f$resume(fp, params)` with `yield` a store of the value and an opaque call of `__coro_suspend(fp)` (and a destroy branch running every block's exit actions), `return` a store of the result and the state DONE, the operations calls of the runtime (`__coro_setup`/`__coro_resume`/`__coro_done`/`__coro_value`/`__coro_result`); after the optimizer the split pass (`coro_split`) moves into the frame what lives across a suspension (`optimize/liveness.c`), every object in memory and the parameters, makes each suspension a return and a label behind a dispatch on the state, and adds `f$init` (stores the arguments) and the descriptor `f$co` = {size, align} (clang's wasm assembler cannot write the absolute symbols first planned); then a second optimizer round. The frame header (`unsigned` state and flags, then the resume, task, top and limit pointers, laid out by the target: 24 bytes on ILP32, 40 on LP64, 12 on AVR and MSP430; `co_header_size`) is the runtime's `struct co_header`; the descriptor's words are `size_t`, and the frame's homes take the target's alignments (`type_align`). `co_alloca` and `alloca` (`<alloca.h>`) in a function take the stack (on BESM-6 `alloca` alone, expanded by `intrinsics.c` as r15 raised by the words of the size, given back by `b/ret`) through `__builtin_stack_save`/`__builtin_alloca`/`__builtin_stack_restore`, expanded inline by the backend's `call.c` (such a function always has a frame, on x86-64 from `rbp`, on AArch64 from x29, on RISC-V from s0, on ARM32 from r11, on AVR from Y, on MSP430 from r4, on MMIX from `$253`), and is released at the end of its block as an exit action (`EXIT_CO_RELEASE`); in a coroutine it takes the task's arena instead (`__coro_push`/`__coro_pop`, the frame of the root's storage past the root frame, LIFO). `await` (`gen_await`) is the loop of the tutorial's equivalence, in TAC: resume the sub-frame, copy its value up and suspend, forward the signal, on destroy destroy the sub-frame first; the arena form pushes the callee's frame (`__coro_setup` with the awaiter as parent, so it joins the task) and pops it after reading the result; `CO_TRAP_NO_SPACE` names the coroutine that did not fit |
| `alloca` (`<alloca.h>`) | `semantic/coroutines.c` (`check_alloca_call`), `translator/coro.c` (`gen_alloca`), each backend's `call.c` (BESM-6: `intrinsics.c`) | Complete on every target ([docs/Standard_Include_Files.md](docs/Standard_Include_Files.md) for the user, each `docs/*_Backend.md`'s "alloca" section for the code): `alloca(n)` is `__builtin_alloca(n)`, called only directly and not in a coroutine; lowered to a `FUN_CALL` of it with the raw size, expanded in place by the backend (with `__builtin_stack_save`/`__builtin_stack_restore`, which `co_alloca` uses), never counted as a call (the register allocator's `inline_call` hook). A function calling one always has a frame pointer (x86-64 `rbp`, AArch64 x29, RISC-V s0, ARM32 r11, AVR Y, MSP430 r4, MMIX `$253`, wasm32 its frame local; MSP430 and MMIX got a `--frame-pointer` mode for it, which `VCC_MSP430_FRAME_POINTER`/`VCC_MMIX_FRAME_POINTER` force on their whole test suite); the memory sits above the outgoing-argument area, which `reserve_outgoing` (or `layout_frame`) knows before selection, and the epilogue gives it back by restoring sp from the frame pointer (on AVR before popping the registers saved below Y). BESM-6 raises r15 by the words of the size and its `b/ret` gives them back. Tests: `backend/common/test/alloca/alloca_run_tests.cpp` in every backend binary but BESM-6's (`longjmp` included where the runtime has `setjmp`), `backend/besm6/test/alloca_tests.cpp`, `AllocaLeaf`/`AllocaAboveOutgoing` goldens, interop with the reference compiler both ways |
| Const conversion | `semantic/const_convert.c` | Complete |
| AST → TAC lowering | `translator/translate.c`, `expr.c`, `stmt.c` | Complete |
| TAC optimizer | `optimize/` | Complete (const fold, unreachable elim, CSE over pure operations and memory reads with store-to-load forwarding, copy prop, dead store elim; iterated to a fixed point; then induction-variable strength reduction with test replacement, with loop rotation in the translator, both off for BESM-6) |
| BESM-6 code gen | `backend/besm6/` | Complete (frame alloc, static data, block-scope static locals (captured per function at typecheck via `static_locals_add`, carried on `Tac_TopLevel.function.static_locals`, and emitted by `besm_emit_static_locals` as a module-local labeled datum inside the owning function's own `,name,`/`,end,` module after the code — no SUBP; same-named statics stay distinct by a TU-wide `name$N` suffix on later occurrences (`$`→`/` in Madlen, kept in Unix) so the flat Unix object has no duplicate label; address-initialized pointer static locals (`static char *p = "ABC";`, `static int *q = &g;`) label their first `BESM_DATA_Z00` address word through the dedicated `Besm_Instr.label` field — its `name` already holds the referenced symbol — and a string literal used only by a static-local initializer is emitted as a top-level static constant via `emit_referenced_string_constants` in `translator/translate.c`; multi-dimensional char arrays are a flat byte blob packed 6/word with rows contiguous (no per-row padding): the static path's `char_array_log_items` flattens the init list (NULL-safe for empty-string rows, which round-trip through `.tac` as a NULL `val`), the automatic path's `gen_char_array_string_init` (`translator/stmt.c`) emits per-byte stores with zero-fill for each string row, indexing decays a char-innermost array of any rank to a fat byte pointer (`is_byte_pointer`/`is_fat_pointer` look through array element types) and the subscript/`+`/`++` paths multiply the index by the row's C size so it byte-addresses at ADD_PTR scale 1, and static/extern-local arrays are recorded for the decay too — char data keeps its source (ASCII) encoding while the static path repacks to KOI-7, so tests use uppercase Latin to keep both equal, see [backend/besm6/KOI7_Encoding.md](backend/besm6/KOI7_Encoding.md)), UTF-8→KOI7, main entry, global variable access, COPY/GET_ADDRESS/LOAD/STORE (GET_ADDRESS of a global emits a lone `14 ,vtm, name`: VTM is a Format-2 instruction like UTC, so its own 15-bit address field takes the relocatable label directly and `M[14] = offset + C` needs no `utc name` ahead of it — a local at a nonzero frame offset still needs `utc reg,off` + `14 ,vtm, 0`, since its index register can only ride in the UTC's register field, VTM's own being the destination; `BESM_SHAPE_IMMR` in both emitters therefore renders `instr->name` when set; word deref via WTC: `wtc` loads the pointer word's address into the C address-modifier register, then a bare `xta`/`atx` reads/writes `mem[C]` — no index register r1; STORE loads the source first since C resets after the next instruction)/BINARY (incl. shifts, unsigned add via b/uadd, unsigned sub via b/usub, multiply via b/mul, unsigned multiply via b/umul, divide via b/div, unsigned divide via b/udiv, remainder via b/mod, unsigned remainder via b/umod, constant strength reduction for power-of-two operands (multiply → `asn` left shift, signed masked back to 41 bits via `aax`; unsigned divide → `asn` right shift; unsigned remainder → `aax` low-bit mask; signed divide/remainder stay on b/div/b/mod), FP add/sub/mul/div inline via a+x/a-x/a*x/a/x with NTR-bracketed normalization, FP comparisons via b/flt/b/fle/b/fgt/b/fge ordering helpers with FP ==/!= reusing b/eq/b/ne)/UNARY negate (int/unsigned/FP)/UNARY complement/UNARY not/FUN_CALL (direct call by label via `,call,`; indirect call through a function-pointer frame slot via `wtc` of the slot then a bare `13 ,vjm, 0` — VJM jumps to offset+C, so C carries the target address; a function name used as a value decays to its label address through GET_ADDRESS, and `(*fp)(…)` strips the function-pointer deref to call through the pointer directly; a direct call to a `_Noreturn` callee is a tail `,uj,` (FUN_CALL_NORETURN) with the callee declared SUBP — unlike `,call,`, a `,uj,` to an undefined name is an assembler error — and a parameterless `_Noreturn` function definition itself drops the `b/save0` prologue and the dead `b/ret` epilogue, keeping only `,ntr, 7` plus `15 ,mtj, 7` when it has autos/temps; the definition's `_Noreturn`-ness rides on the serialized `Tac_TopLevel.function.noret` flag)/RETURN/LABEL/JUMP/JUMP_IF_ZERO/JUMP_IF_NOT_ZERO/integer width conversions (TRUNCATE/ZERO_EXTEND/SIGN_EXTEND)/int↔FP conversions (signed INT_TO_DOUBLE/INT_TO_FLOAT inline via INT-format+normalize, UINT_TO_DOUBLE/UINT_TO_FLOAT via b/utod, DOUBLE_TO_INT/FLOAT_TO_INT via b/dtoi, DOUBLE_TO_UINT/FLOAT_TO_UINT via b/dtou, FLOAT_TO_DOUBLE/DOUBLE_TO_FLOAT copies)/ADD_PTR (word pointer & array index scaling; the base is always a pointer value — the translator decays an array rvalue to its address via GET_ADDRESS before the ADD_PTR, so no array-vs-pointer disambiguation is needed in the backend; char*/void* byte arithmetic via scale=1 — constant ±1 through b/pinc/b/pdec, other byte deltas through b/padd's floored divide by 6)/COPY_TO_OFFSET/COPY_FROM_OFFSET (aggregate member access — word-aligned members, plus packed char members via the COPY_BYTE_TO_OFFSET/COPY_BYTE_FROM_OFFSET kinds: byte extract for reads, b/stb RMW for writes)/ALLOCATE_LOCAL (contiguous multi-word frame slots for local arrays & structs)/fat-pointer char access (char*/void* byte LOAD inline via WTC/XTA/ASX/AAX, byte STORE via b/stb RMW helper, GET_ADDRESS_BYTE of a char sets the fat marker, int*↔char* casts via PTR_TO_CHAR_PTR/CHAR_PTR_TO_PTR; byte access uses the dedicated LOAD_BYTE/STORE_BYTE/GET_ADDRESS_BYTE kinds; char-array indexing & string/array decay to a fat pointer at offset_enc 5 via GET_ADDRESS_DECAY + static FAT_POINTER init; frontend routes char* +/-/+=/-=/++/-- to ADD_PTR scale=1)/PTR_DIFF (char*−char* difference → ptrdiff_t byte count via b/pdiff: decode both fat pointers to absolute byte positions word*6+(5−offset_enc) and subtract) done; peephole pass (`peephole.c`, run in `codegen_function` before emission) with rule #27 redundant-reload elimination — matched on a `Loc` (frame slot / global + word offset / dereference through a named pointer), not a raw `(reg,off)` pair, because a `utc`/`wtc` and the instruction after it form an atomic **C group** whose consumer addresses `mem[C]` and whose `(reg,addr)` fields name no slot; the sweep steps cursor-by-group, so a match splices out setter and consumer together (2 nodes, or 3 for `utc gp`+`wtc`+`xta` through a global pointer) and it is structurally impossible to strand a setter; this catches `g.x = 7; return g.x;` and `*p = x; return *p;`, neither of which TAC copy propagation can reach; no memory-clobber analysis is needed because memory is only ever written from A, so a store cannot falsify "A mirrors L" — see [backend/besm6/Peephole_Rewrites.md](backend/besm6/Peephole_Rewrites.md) §5.9 — rule #28 dead temp-store elimination — drops an `atx` to a `%`-temporary slot whose value is never re-read before overwrite/block end; restricted to never-aliased temporaries via `frame_slot_is_temp`, with a single-basic-block guard for temporaries live across an edge; an `ALLOCATE_LOCAL` aggregate slot is never a temporary, even under a `%`+digit name (e.g. a compound literal), since it is read through its address — and rule #29 NTR mode coalescing — tracks the mode register R (seeded to 7 after `b/save`), deletes any `ntr n` whose operand equals the current R, and drops a dead `ntr` overwritten by a later `ntr` before any R-dependent use, so consecutive FP ops keep R=0 and restore to 7 once at the end, and rule #30 compare→branch fusion — a relational helper's 0/1 result feeding a `JUMP_IF_ZERO`/`JUMP_IF_NOT_ZERO` is consumed directly by the `uza`/`u1a`; needs no dedicated rule, it is the emergent product of #27+#28 given the runtime helpers' logical-ω exit contract — and rule #31 branch/label cleanup — drops a `uj` whose target is the immediately following label, deletes instructions between an unconditional `uj` and the next label as unreachable (which also collapses the duplicate `uj b/ret` the RETURN+epilogue emit; a `stop` is deliberately *not* such a terminator — the halt is resumable from the console, so the code after it is live), and inverts a conditional that only skips an unconditional jump (`uza L`/`uj M`/`L:` ⇒ `u1a M`); these three need list look-ahead/mutation so they run directly in the sweep, not via the `rule_table`, and rule #32 I/O address folding — the `xts`/`15 wtc`/`ext` trailer `emit_io_op` emits for a computed `ext`/`mod`/extracode address is collapsed two ways: #32(a) deletes a constant addend (`a+x =N`, or the unsigned `xts =N`+`,call, b/uadd` pair) and moves N into the instruction's own address field — exact despite the 48-bit C-level addition, since the `wtc` keeps only bits 15:1 and truncation commutes with addition — and #32(b) drops the push/pop round-trip when the address was merely loaded out of a frame slot or a global, retyping the `xts` to the plain `xta` word load and retargeting the `wtc` onto that location (`wtc` is Format 2, so a global needs no `utc` escape); the two compose across sweeps, so `__besm6_ext(x + 1, w)` goes from six nodes to `xta w`/`wtc x`/`ext 1`; these also run in the sweep, and cannot fire on ordinary code since only those three kinds carry the trailer — see [backend/besm6/Peephole_Rewrites.md](backend/besm6/Peephole_Rewrites.md) §5.10 — and rule #33 ω fixup before a conditional branch — `uza`/`u1a` recompute ω at branch time from A and the ω-group field of R (bits 5–3), which every accumulator op rewrites, so a truth test means `A = 0?` only under the *logical* group; instruction selection always loads the condition with an ω-logical `xta` right before the branch, but #27+#28 delete that store/reload whenever A already holds the value, exposing the group its producer left — additive for `if (x - y)` (which then reads as `if (x - y >= 0)`: the silent miscompile of `backend/besm6/tmp/BUG.md`, found in v7 `sort(1)`), multiplicative for `arx`; the pass therefore tracks the ω group (`omega_after` is the per-opcode table of backend/besm6/Besm6_Instruction_Set.md §4 transcribed, with a CALL/VJM classified logical by the helpers' exit contract and `b/ret`'s `stx`/`sti`, an extracode and a read-address `ext`/`mod` likewise) and splices a bare `,aex,` — A ^= memory word 0, so A and the R suppress bits are untouched and the group becomes logical, the cheapest of the logical ops — in front of any `uza`/`u1a` whose group is not already logical; it is the one rule that *inserts* a node, so like #31/#32 it lives in the sweep, and it is what lets `__besm6_arx` drop its own unconditional correction — see [backend/besm6/Peephole_Rewrites.md](backend/besm6/Peephole_Rewrites.md) §5.11); post-peephole frame shrink (`used_auto_words` / `remove_instr` in [codegen.c](codegen.c), run in `codegen_function` after `besm_peephole`) reclaims auto slots the peephole pass left unreferenced — it shrinks the prologue `utm 15` stack extension to the auto words still in use (dropping the `utm` entirely when none remain), reclaiming only `%`+digit temporaries whose slot is no longer referenced while always keeping named locals and aggregates reserved (their address may be taken, and `&x` of a slot-0 local emits `ita 7` with the register number in the address field, which a plain reg==REG_AUTO scan would miss); the twelve `<besm6.h>` **intrinsics** ([backend/besm6/Besm6_Intrinsics.md](backend/besm6/Besm6_Intrinsics.md)) — an intrinsic *is* a call in the IR (declared as an ordinary prototype, so the front end checks arity and coerces arguments; `tac/`/`optimize/`/`ast/`/`translator/` are untouched, and `dead_store` already treats a FUN_CALL as never-dead, which is the "never eliminable" contract Tier 1 needs), and `codegen_intrinsic` ([intrinsics.c](backend/besm6/intrinsics.c)) intercepts every `__besm6_`-prefixed FUN_CALL at the top of `instr.c`'s FUN_CALL case and emits the machine instruction inline instead of a `,call,` — **every** one of them must be intercepted, since all twelve collide under Madlen's 8-char truncation and one left to fall through would silently alias another rather than fail to link (hence the trailing `fatal_error`, not a `return false`); the five Tier-2 bit ops (`apx`/`aux`/`acx`/`anx`/`arx`) take the inline A-op-X binop shape and emit no ω correction of their own — `arx` is the one of the five leaving *multiplicative* ω, and peephole rule #33 supplies the `,aex,` exactly where a branch would read it; Tier 1 (`ext` 033, `mod` 002 — kinds `BESM_IO_EXT`/`BESM_IO_MOD`, *not* the `BESM_MOD_*` C-register group) and Tier 3 (the extracode, `BESM_IO_EXTRACODE`, `BESM_SHAPE_SPECIAL` since its mnemonic *is* its opcode) share their addressing (`emit_io_op`), which never uses an index register: a constant address ≤ `07777` becomes the Format-1 offset field, a constant ≤ `077777` rides a `,utc, N` (Format 2, 15-bit field) into the C register, and anything else goes through the stack — the address is computed into A, `,xts,` pushes it while loading the accumulator operand, and a stack-mode `15 ,wtc,` (`V=0`, `M=017`) pops it back into C, so push and pop are adjacent and balanced and only A and the stack are touched; the accumulator is loaded *first* in the C modes, since a C setter must be the instruction immediately before the io; an extracode still sets `M[016]`, so it clobbers r14; all three kinds are `is_block_boundary` (a read address switches R to logical; the monitor's handler runs arbitrary code) while `BESM_BRANCH_STOP` is deliberately *not* a rule-#31(b) terminator — the halt is resumable, so `__besm6_stop` is not `_Noreturn` and the code after it is live; `semantic/expressions.c` constant-folds and range-checks the one argument that is an opcode rather than a value (`__besm6_extracode`'s `op`, 050..077), per the BESM-6 descriptor's `Target.immediate_args` table; the PSW trio (`__besm6_getpsw`/`setpsw`/`maskpsw`) is the mode word, machine register 021 — `ita 021` / `xta`+`ati 021` / a `vtm` whose **modifier register is 0**, which is what makes it a PSW write (БлП/БлЗ/БлПр from the address field, all three at once, masked, A and ω untouched) instead of an index-register load; the mask is a 15-bit address-field immediate so it must be a compile-time constant, checked at instruction selection like the halt code; these three add **no** instruction kind, no peephole rule and no `semantic/` change — the kinds already exist and are already modelled (`state_step` defaults to clobbering A, all three are R-independent, none is a block boundary, and the auto-slot scans gate on `reg == REG_AUTO` so `ita 17` is never read as frame slot 17) — and they cannot be run-tested: dubna and b6sim both compute `M[Aex & 017]` (so `ati 021` would clobber the ABI-preserved M[1]) and both repurpose a register-0 `vtm` as their instruction-trace toggle; per-dialect spelling diverges (Madlen `,ext,`/`,mod,`/`,33,` raw octal — Madlen names no halt — /`,*74,`/`,24,` raw octal for the mode write, since Madlen rejects `,vtm,`/`,utm,` with a zero modifier as "ошибка в модификаторе" and takes only 1..15 there; Bemsh `увв`/`рег`/`стоп`/`э74`/`уиа`; b6as `ext`/`mod`/`stop`/`$74`/`vtm`), and every numeric address field is decimal in all three, so `__besm6_ext(04031, …)` emits `,ext, 2073` and `__besm6_maskpsw(02003)` emits `,24, 1027` |
| RISC-V code gen | `backend/riscv/` | Complete: RV64IMFD, LP64D psABI incl. structs and variadics, and RV32IMFD/ILP32D (`--rv32`; `long long` in register pairs, `llong.c`), graph-colouring register allocation over TAC liveness (`backend/common/regalloc.c` over `backend/common/flow.c`, shared with future backends), peephole, sp-addressed frames, binary128 `long double` via `libc/common/float128.c`; see [docs/Riscv_Backend.md](docs/Riscv_Backend.md) |
| AArch64 code gen | `backend/aarch64/` | Complete: ARMv8-A, AAPCS64 incl. homogeneous float aggregates, structs through x8 and variadics (the AAPCS64 `va_list`; `tac_aapcs64_class` in `tac/tac_abi.c` classifies arguments for the backend and `__builtin_va_class` alike), register allocation on `backend/common/regalloc.c`, compare-and-branch fusion, peephole over register liveness across blocks (a call reads only the argument registers `call.c` records on it; post/pre-indexed addressing, `cinc`/`cneg`, `tbz`, equality sets and range checks as one unsigned compare, constant diamonds as `cset`, jump threading, dead values, word copies as q-register `ldp`/`stp`, blocks ending alike sharing their tail, and the bit-field instructions `ubfx`/`sbfx`/`bfi`/`ubfiz`), frameless leaves and sp-addressed frames (x30 in the record's place beside a lone saved register, `sp` moved by the save at `sp + 0`, pairs beyond `stp`'s reach split), binary128 `long double` via `libc/common/float128.c`; 1.22× `cc -Os -fno-inline` in text size on `bench/msp430` and `libc/common` (`scripts/bench_aarch64.sh`); see [docs/Aarch64_Backend.md](docs/Aarch64_Backend.md) |
| ARM32 code gen | `backend/arm32/` | Complete: ARMv7-A in ARM state with hardware divide, VFPv3-D16, AAPCS-VFP incl. back-filled `s` registers, homogeneous float aggregates (`tac_aapcs32_class`), structs by value split between r3 and the stack, and variadics under the base standard (clang's `struct __va_list`); `long long` in register pairs with the RTABI helpers (`libc/arm32/aeabi_*.s`), `long double` = `double`; register allocation on `backend/common/regalloc.c` (registers numbered from 1 on its side, since r0 is 0), parallel moves, compare-and-branch fusion, peephole with conditional execution, `ldrd`/`strd` and the bit-field instructions `ubfx`/`sbfx`/`bfi`/`bfc`, frameless leaves and sp-addressed frames (r11 as a fallback); see [docs/Arm32_Backend.md](docs/Arm32_Backend.md) |
| x86-64 code gen | `backend/x86/` | Complete: baseline x86-64 (SSE2, `cmov`), System V psABI incl. structs by eightbyte class (`tac_sysv64_class`, all or nothing, MEMORY structs copied onto the stack), variadics (clang's `__va_list_tag[1]`, `__va_arg` in `libc/x86/va_arg.c`) and signed plain `char`; the x87 80-bit `long double` (never on the x87 stack between TAC instructions); register allocation on `backend/common/regalloc.c` (`rax` and `r10`/`r11` kept as scratch, a divide or variable shift counted as a call), two-operand selection, parallel moves, compare-and-branch fusion, peephole over register and flag liveness with `cmov`, rsp-addressed frames with the red zone (`--frame-pointer` for rbp); runs on qemu `microvm` through the PVH note, the status on the debug console; `setjmp`/`longjmp`; see [docs/X86_64_Backend.md](docs/X86_64_Backend.md) |
| AVR code gen | `backend/avr/` | Complete: the ATmega1280 (`avr51`) with the avr-gcc ABI as clang implements it — arguments from r25 down in even pairs to r8, then all on the stack, structures flattened into their members, results from r24/r22/r18, a variadic callee taking every argument on the stack; a 16-bit `int` and binary32 `double` (in `semantic/target.c`, which the front end now honours for constants, `size_t` and folding); register allocation on `backend/common/regalloc.c` with the register pair as unit (instructions that need the r18–r25 blocks or a helper count as calls through `uses_scratch`), a scratch-free selection in the destination's registers or X/Z beside the naive block form, parallel moves, compare-and-branch fusion, a peephole pass over register and SREG liveness, Y-addressed frames (`rcall .` for small ones, none without slots, Y then allocatable) and branch relaxation; a binary32 soft-float runtime (`libc/common/float32.c`) and the libgcc integer helpers with their special register contracts; runs on qemu `arduino-mega`, the status on USART1; `setjmp`/`longjmp`; see [docs/Avr_Backend.md](docs/Avr_Backend.md) |
| MSP430 code gen | `backend/msp430/` | Complete: the classic MSP430 (no MSP430X, no hardware multiplier) with the MSP430 EABI as msp430-elf-gcc implements it — arguments in r12–r15 left to right, a 32-bit value in the next two consecutive registers, the split `long` (low word in r15, high on the stack), a 64-bit one only in all four, later arguments still taking free registers; **every structure or union argument by reference** (the caller passes its own object uncopied, the callee copies it first thing, or reads through the pointer when it only reads it and makes no call, store through a pointer or global write) and every structure result through a hidden pointer in r12; a variadic callee taking its last named argument and the variable ones on the stack; results in r12 up; a 16-bit `int`, unsigned `char`, alignment 2 and a binary64 `double` (`semantic/target.c`); register allocation on `backend/common/regalloc.c` with the 16-bit register as unit (r12–r14/r11 for values not live across a call or helper, then r10–r4; r15 the selection's one scratch, never allocated; r8–r10 kept free in a function calling an r8–r11 helper), selection on operands where they lie (register or memory, either side, memory to memory), parallel moves with `xor` swaps, compare-and-branch fusion, inline constant multiply, a peephole pass (constant-generator aliases, copy/constant/memory forwarding, dead code over register and SR liveness, loads sunk into their use, read-modify-write on memory, offsets folded into addresses, `@rN+`, tail `br`, dead stores to frame slots over a byte-level slot liveness that knows where the frame's address escapes, then no frame at all when no slot is left; a call reads only its own argument registers; `volatile` left alone), SP-addressed frames and frameless leaves, branch relaxation, a section per function and variable with `--gc-sections`; a correctly rounded soft binary64 (`libc/common/float64.c`) and binary32, GCC's `__mspabi_*` helpers with their r8–r11 contract (`mspabi64.s`), libgcc's complete shift groups and shared epilogues; runs on `mspsim` (UART stdout, exit status from the stop register 0x01FE); `setjmp`/`longjmp`; interop with GCC, libgcc and newlib both ways, and with clang but for structure arguments; see [docs/Msp430_Backend.md](docs/Msp430_Backend.md) |
| MMIX code gen | `backend/mmix/` | Complete: Knuth's MMIX in user mode with the MMIXware ABI as `mmix-knuth-mmixware-gcc` implements it — `pushj $X, f` on the register stack, up to sixteen arguments in `$(X+1)`… (the callee's `$0`…), the 17th on up at `0($254)`…, the result in `$X`, `pop 1, 0`; `rJ` saved in a local by a non-leaf; structures of 8 bytes or less right-justified in one register, larger ones by reference (the callee copies, ours also passes a copy), every structure result through the address in the global `$251`; a variadic callee storing `$n`…`$15` just below its stack arguments, so `va_list` is a `char *` over 8-byte slots; LP64 with a **signed** plain `char`, **big-endian**, `long double` = `double` (`semantic/target.c`); register allocation on `backend/common/regalloc.c` in GCC's fixed model (`$0`–`$13` for values live across a call, which the register stack keeps for free, `$14` `rJ`, `$15` the hole, `$16`–`$31` the rest) and a compaction (`phys_reg`) that shifts the upper range down to just above the highest of `$0`–`$13` in use; selection with 8-bit immediates (`$248`–`$250` and `$255` as scratch), signed `/` and `%` by `div` and a six-instruction fix-up (`div` floors), `float` held as its exact binary64 value and rounded through the slot `%.fround`, `sflot` for integer to `float`, globals through linker-allocated base registers (`crt0` reserves `$247`–`$254`), fusions of compare-and-branch, `!x`-and-branch, pointer sums into indexed or offset addresses, and a returned call into a tail `jmp`; a signed `int`/`long` `+`/`-`/`*` result left unextended (its overflow is undefined, as GCC does); a peephole pass over register liveness (results in place, dead and overwritten instructions, copy forwarding, post-increment, redundant `%.fround` rounding, jump cleanups, `cs*`/`zs*`, `pb*` backward branches, `rJ` dropped with the last call); `$254`-addressed frames and frameless leaves that `pop` in place; no runtime helpers at all (multiply, divide, binary64 and `sqrt` are instructions), so `libc/mmix` is `crt0.S`, the buffered `console.s` (an `[exit N]` report on stderr), newlib's `setjmp.s`, `sqrt.s` and a bump `malloc.c`; runs on `mmix` (`$255` at `trap 0, Halt, 0` is the exit status); interop with GCC, libgcc and newlib both ways; see [docs/Mmix_Backend.md](docs/Mmix_Backend.md) |
| WebAssembly code gen | `backend/wasm/` | Complete: wasm32 with clang's C ABI for `wasm32-unknown-unknown` on Braam's features (`WASM32_FEATURES`: reference types, bulk memory, sign-ext, mutable globals, non-trapping float-to-int); ILP32 with a signed `char` and a binary128 `long double` through `libc/common/float128.c`; a scalar a wasm local, an aggregate, `long double` or address-taken name a slot in a frame on the shadow stack (`__stack_pointer`; none without slots); narrow integers kept extended in their `i32`; calls as clang makes them: a structure of one scalar (`tac_wasm32_scalar`, which counts each bit-field as a field, hence `bitfield_unit_per_field`) by value, an empty one not at all, any other by reference to a caller's copy, a result that is not a scalar and every `long double` result through an sret first parameter, a `long double` argument as two `i64`, variadics in a buffer the caller fills, its address one more `i32` (`va_list` a `char *`, `__va_start` expanded in place, and the `co_alloca` builtins `__builtin_stack_save`/`__builtin_alloca`/`__builtin_stack_restore`), function pointers as table indices and `call_indirect`; `main(void)` as `__original_main` with clang's `main`/`__main_void`, `main(argc, argv)` as `__main_argc_argv`; structured control flow by Ramsey's translation ("Beyond Relooper": reverse postorder, dominators, loop headers, merge nodes, `br_if` where one way is a bare branch), an irreducible graph made reducible first as LLVM's FixIrreducibleControlFlow does (Tarjan's regions, inside each single-entry loop too; a region with several entries gets a dispatch node, a `br_table` on a state local over its entries, which the jumps into it from outside and those back go through, the forward ones inside staying; `--no-regional` to turn it off), the whole-function dispatch skeleton (`loop`/`br_table` over a state local) as the last fallback and under `--no-structure`; rewrites of the finished code (`--no-peephole`, `--no-stackify`, `--no-coalesce`): stackify (a value set once and read once later in a straight run stays on the operand stack, waiting below stack-neutral code or, when it has no effect, moved to its use; nothing with a load crosses a store, call or volatile access), tees, dead values, tests folded into comparisons, constant addends into memarg offsets, power-of-two multiplies as shifts, adjacent constant stores merged up to an `i64`, dead code, needless branches and unnamed blocks removed; local coalescing over liveness solved on the structured code, copy-related locals merged into groups, then coloured; every `.functype` at the top of the unit (the assembler wants it before any use), symbols `inf`/`nan`/`infinity` renamed `*.vcc`; the runtime `crt0.S`, `console.s` (imports `env.putch`/`env.exit`), `memory.s` (`memcpy`/`memmove`/`memset` as `memory.copy`/`memory.fill`), `sqrt.s`, a bump `malloc.c` (and the shared coroutine runtime `libc/common/co.c`), and the node host `run.mjs` (`[exit N]` on stderr); no `setjmp`/`longjmp` (no wasm exception handling); 1.16× clang `-O2`'s code size on `libc/common`; see [docs/Wasm_Backend.md](docs/Wasm_Backend.md) |

### Key data structures

- **`Program`** (`ast/ast.h`): Root node; linked list of `ExternalDecl` (function definition or declaration).
- **`ExternalDecl`**, **`Declaration`**, **`Stmt`**, **`Expr`**, **`Type`**: Core AST nodes. Defined in `ast/ast.h`, spec in `ast/ast.asdl`.
- **`Tac_Instruction`**, **`Tac_Value`**, **`Tac_Type`**: TAC IR nodes. Defined in `tac/tac.h`, spec in `tac/tacky.asdl`.
- **`Besm_Module`**, **`Besm_Func`**, **`Besm_Block`**, **`Besm_Instr`**: BESM-6 backend IR nodes. Defined in `backend/besm6/besm.h`, spec in `backend/besm6/besm6.asdl`.
- **`symtab`** (`semantic/symtab.c/h`): Scoped identifier → `Symbol` map; `symtab_purge(level)` removes block-scope entries on block exit.
- **`structtab`** (`semantic/structtab.c/h`): Struct/union/enum tag → `StructDef` map; scoped, purged on block exit.
- **`typetab`** (`semantic/typetab.c/h`): Typedef name → `TypeDef` map; `typetab_resolve(name)` returns the underlying `Type*`.

### Important design decisions

- **No identifier shadowing**: Inner blocks may not redeclare a name that already exists in any enclosing scope. `symtab` / `structtab` / `typetab` reject duplicates with `fatal_error`. This is a permanent design decision — do not add shadowing support.
- **Anonymous struct/union tags are minted by the parser.** `fuse_type_specifiers` (`parser/decl.c`) gives every tagless `struct`/`union` **definition** a synthetic `__anon_N` tag at the single point where its `Type` node is built — *before* `parse_init_declarator` / `parse_struct_declaration` clone the base type per declarator, and `clone_type` carries the tag into every clone. That is what makes `struct { int x; } a, b;` (and the member form `struct W { struct { int x; } p, q; };`, and `typedef struct { int x; } T1, T2;`) **one** type rather than one per declarator: `compatible_type` compares struct types by tag name, so per-node minting made the declarators mutually incompatible. `register_struct_type` (`semantic/declarations.c`) therefore *requires* the tag and aborts if a definition reaches it untagged — do not reintroduce minting there. The counter is reset per translation unit by `parse()`, so `__anon_N` numbering is deterministic in the in-process test fixtures (but `ParserTest::TestType()` bypasses `parse()`, so tests on that path match the `__anon_` prefix, not the number). Two *separate* tagless definitions still get distinct tags and stay incompatible (C11 §6.7.2.3p5). Since shadowing is forbidden, a *named* tag is already TU-unique, so no scoped alias map is needed on top of this.
- **TAC name convention — frame-resident names start with `%`**: A TAC `var` name encodes its storage class by its first character. `%`+digit is a compiler temporary (`new_temp`); `%`+letter/`_` is a parameter or automatic local; a leading letter/`_`/`$` is a module-level global, static, string constant, or function. Loop and branch-target labels are also `%`-prefixed (`%L`+digit from `label_loops`, or a `%`+digit temporary). `percent_locals_in_function` in `translator/translate.c` (run just before the optimizer) prefixes parameter and automatic-local names — in the body and in the `params`/`locals` lists — with `%`. This lets a backend classify a name by its spelling alone: `backend/besm6/frame.c` gives a stack slot to any `%`-prefixed name and treats every other referenced name as an external global. The no-shadowing rule makes the renaming unambiguous, except across sibling blocks: there `semantic/declarations.c` gives a repeat of another type the backend name `name$N` (a repeat of the same type shares the slot), as for static locals. Keep the body and the `params`/`locals` lists consistent (the optimizer's alias analysis matches names against both).
- **Typed TAC.** Every frame-resident name has a type: `params` and `locals` (automatic locals *and* temporaries — `new_typed_temp`/`new_var_val` take the value's `Tac_Type`; `new_temp` is for labels only) are `{name, type}` lists, serialized. A function toplevel and each `FUN_CALL` carry a `FUN_TYPE`; `translate_unit_begin`/`translate_unit_end` bracket a unit and yield an `EXTERN` toplevel (name + type) for each name it references but does not define. A block-scope `extern` or function declaration becomes an `EXTERN` toplevel ahead of its function (its symbol is purged before lowering). The BESM-6 backend ignores all of it; the RISC-V backend needs it for widths, register files and the psABI. `tac_verify_function`/`tac_verify_program` (`tac/tac_verify.c`) check that every name is typed and operand types agree with the operator; `translate()` runs it on every function in a build without `NDEBUG`, under `lower --verify`, and in the translator, optimizer and backend test fixtures (`translate_verify`).
- **`_Bool` is int-shaped, and every conversion to it is a zero test.** `_Bool` has its own
  size/alignment pair in the target descriptor (`bool_size`/`bool_align`, `semantic/target.h`):
  one word on BESM-6, one byte on the byte-addressed targets. A byte-sized `_Bool` would be
  wrong here — on this machine the 1-byte size *means* byte-packed storage and a fat byte
  pointer, so `_Bool a[4]` would pack six to a word and `_Bool *p` would store through the
  `b/stb` read-modify-write helper, all to carry one bit. TAC has no `_Bool` kind, so
  `ast_type_to_tac_type` carries a word-sized `_Bool` in `TAC_TYPE_INT` (a byte-sized one in
  `TAC_TYPE_UCHAR`), the way `TYPE_ENUM` borrows int's. C11 §6.3.1.2 (a scalar converted to
  `_Bool` is 0 or 1) is implemented by `emit_bool_normalize` in `translator/translate.c`,
  called from `emit_cast` ahead of all width logic — that one node covers assignment,
  initialization, argument passing, return and the explicit cast — plus `gen_step` for
  `++`/`--` and `semantic/const_convert.c` for static initializers. `_Bool` also promotes to
  `int` like any narrow integer type (`is_promotable_narrow`); without that, `b + i` would
  find the two types the same size and pick the *unsigned* one, `_Bool`, as the common type.
- **`long double` constants are binary128 bits on every host.** The scanner parses a long double literal exactly (`f128_from_string`, `libutil/float128.c`), and the AST, the constant evaluator, TAC, the optimizer and the backends carry a `Float128`, never the host's `long double` (only a double on macOS arm64). Arithmetic folds in software binary128, or as a double where the target's long double is one (BESM-6). The RISC-V runtime (`libc/common/float128.c`) includes the same `float128.c`, so folded and computed values agree.
- **`.asdl` files are canonical specs, not code generators.** `ast/ast.asdl` and `tac/tacky.asdl` document the IR; `ast/ast.h` and `tac/tac.h` are maintained manually and must stay in sync.
- **Word I/O (`libutil/wio`)**: AST and TAC binary streams use `size_t`-wide words for portability. Use `wio` for all IR serialization.
- **TAC binary tags (`tac/tags.h`)**: Each TAC node header uses one `size_t`-wide word encoding `TAG_BASE + kind`. Tag constants are readable 4-letter ASCII (e.g. `cnst`, `insr`, `tval`). Stream magic is `TAC5` (`tac_export_begin_stream`). See `tac/tags.h`, `tac_export.c`, `tac_import.c`.
- **`xalloc` (`libutil/xalloc`)**: All allocations go through `xalloc`/`xfree`. In debug builds, `xalloc_report()` prints leak totals.
- **Single-pass semantics**: `typecheck_global_decl()` binds names and type-checks in a single pass.
- **Canonical initializers.** `normalize_init` (`semantic/init_normalize.c`) is the only code that interprets designators and brace elision; `build_static_init` and `typecheck_init` run it first and then consume the result by position. Canonical form: an array has exactly N items, a struct one per member, a union one item (carrying a `DESIGNATOR_FIELD` if not the first member); a NULL item `init` means zero. In automatic mode it is the only place leaf expressions are typechecked (once — a typechecked leaf has a non-NULL `Initializer.type`).
- **Bit-fields are lowered in the translator.** `register_struct_type` lays them out by the target's `bitfield_layout` (SysV, AAPCS or GCC's packed rule, measured from the reference compilers into `translator/test/bitfield_layouts.h`) and gives each named one a storage unit (`BitField` in `ast/ast.h`, on `FieldDef`, the access nodes and `InitItem`): the smallest aligned unsigned integer inside the struct that covers it, else its bytes one by one. Reads, stores, compound assignment and `++`/`--` (`translator/expr.c`, `bf_*`) load the unit into an unsigned type at least as wide as `int` and shift and mask, so TAC, the optimizer and the backends see no new instruction. Unnamed bit-fields are not members. A static initializer merges a run of bit-fields into bytes (`put_bitfield`, `semantic/initializers.c`); a struct's TAC type lists each storage unit as an unnamed unsigned member.
- **Compound literals.** A compound literal is an lvalue. An automatic one gets its own `ALLOCATE_LOCAL` slot, scalars included; at file scope one inside an address constant becomes an anonymous non-global static `_clN`, emitted by `emit_referenced_string_constants` (`translator/translate.c`).
- **Bulk zero fill.** An automatic aggregate or string-initialized char array with at least 8 zero stores is zeroed by a TAC loop first, then only its non-zero leaves are stored (`gen_aggregate_init`/`gen_string_array_init`, `translator/stmt.c`). Static `ZERO` runs are merged; the Unix emitter packs zero words 8 per `.word`.
- **Static initializers bypass typecheck; `const_convert.c` never resolves enums.** A variable with static storage duration (file-scope, or block-scope `static`) does *not* go through `typecheck_init` — `semantic/declarations.c` hands its raw parser `Initializer` to `build_static_init` (`semantic/initializers.c`), which normalizes it in `INIT_STATIC` mode (leaves stay raw), and then frees it. So the automatic path's in-place literal rewrites never happen, and an enum constant arrives still spelled as a `LITERAL_ENUM` holding the enumerator's *identifier*, not a value. `const_convert.c`'s four `literal_to_*` converters have no symbol table and treat `LITERAL_ENUM`/`LITERAL_STRING` as broken invariants (`fatal_error`); that contract is pinned by `ConstConvertTest.EnumLiteralDies`. Resolution therefore belongs to `build_static_init`'s constant-expression branches, which call `typecheck_and_decay` + `try_eval_const_int`/`try_eval_const_real` — `eval_const` folds enumerators natively. **The branch order matters**: the raw-literal fast path is tested first and must keep excluding `LITERAL_ENUM`, or enum constants get swallowed before the folding branch can see them. Every aggregate element recurses into that same scalar leaf, so one misordered guard breaks arrays, structs and unions at once. Relatedly, `is_zero_int` returns `false` for enums by design (no symtab access), and `TYPE_ENUM` shares `TYPE_INT`'s case in `new_static_init_from_literal` — it is int-sized/int-aligned/signed in `get_size`/`get_alignment`/`is_signed` and maps to `TAC_TYPE_INT`.
- **Scope tracking**: `scope_level` is incremented on block entry; `scope_decrement()` decrements it and calls `symtab_purge`, `structtab_purge`, and `typetab_purge` — all backed by `map_remove_level_free`.
- **`typedef` handling**: `STORAGE_CLASS_TYPEDEF` declarations are registered in `typetab`. `validate_type` resolves `TYPE_TYPEDEF_NAME` recursively; all type-utility helpers (`get_size`, `get_alignment`, `is_integer`, etc.) resolve typedef names transparently.
- **`switch` semantic validation**: Integer controlling expression with `int` promotion via `convert_to_kind`; constant integer case values evaluated by `try_eval_const_int`; duplicates detected via a `SwitchCtx` stack; multiple defaults and stray case/default labels rejected.
- **TAC lowering coverage**: `translate.c` calls `fatal_error()` on unimplemented constructs. All C11 constructs are now lowered.

### AST quirks

- **Function prototypes vs. definitions**: Only function *definitions* (with a body) parse as `EXTERNAL_DECL_FUNCTION`. A bare prototype such as `int f(int);` parses as `EXTERNAL_DECL_DECLARATION` / `DECL_VAR` with a `TYPE_FUNCTION` declarator type. The typecheck pass (`typecheck_file_scope_var_decl`) detects `TYPE_FUNCTION` and registers it via `symtab_add_fun()`.
- **`f(void)` sentinel**: The parser represents a `(void)` parameter list as a single `Param` node with `TYPE_VOID` and a NULL name (not as an empty list). `typecheck_fn_decl()` strips this sentinel before param processing. Params with a NULL name are skipped when adding to symtab.
- **`_Static_assert` in struct/union bodies**: The parser accepts `_Static_assert` as a struct/union member. The AST uses a `FieldKind` discriminator to distinguish it from regular member declarations.

## Tests

Tests are GoogleTest (C++17). Source lives alongside the module it tests:

- `cc/test/cc_test.cpp` (the driver end to end, with the in-tree passes through the `VCC_*` overrides; `StagedPrefix*` cases run a miniature installation with none) → `cc-tests`
- `cpp/test/test_*.cpp` (C11 conformance suite driving the built `cpp` via `test_support.h`'s `PreprocessorTest`; target options in `test_predefined_macros.cpp`) → `cpp-tests`
- `ast/test/clone_tests.cpp` → `ast-tests`
- `scanner/test/tests.cpp` → `scanner-tests`
- `parser/test/simple_tests.cpp`, `statement_tests.cpp`, … (9 files, including `negative_tests.cpp`) → `parser-tests`
- `tac/test/yaml_tests.cpp`, `graphviz_tests.cpp`, `binary_tests.cpp`, `verify_tests.cpp` → `tac-tests`
- `semantic/test/symtab_tests.cpp`, `structtab_tests.cpp`, `typetab_tests.cpp`, `typecheck_tests.cpp`, `real_tests.cpp`, `pipeline_tests.cpp`, `label_loops_tests.cpp`, `defer_tests.cpp`, `coro_tests.cpp`, `const_convert_tests.cpp`, `coercion_tests.cpp`, `init_normalize_tests.cpp` → `semantic-tests`
- `backend/besm6/test/codegen_tests.cpp`, `arith_tests.cpp`, `copy_tests.cpp`, `flow_tests.cpp`, `run_tests.cpp`, `unary_tests.cpp`, `convert_tests.cpp`, `frame_tests.cpp`, `init_tests.cpp`, `label_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `char_tests.cpp`, `peephole_tests.cpp`, `printf_tests.cpp`, `funcptr_tests.cpp`, `stdarg_tests.cpp`, `mem_tests.cpp`, `book_besm6_tests.cpp` (the BESM-6 versions of the width-dependent book programs), the Bemsh dialect tests `bemsh_tests.cpp` (golden `.bemsh`) and `bemsh_run_tests.cpp` (`CompileAndRunBemsh`: `genbesm --bemsh`+`besmc`+`dubna`), and the Unix (`b6as`) dialect tests `unix_tests.cpp` (golden `.s`), `unix_link_tests.cpp` (`CompileAndAssembleUnix`: `b6as`+`b6ld`), `unix_run_tests.cpp` (`CompileAndRunUnix`: `b6as`+`b6ld`+`b6sim`), and `alloca_tests.cpp` (goldens in the three dialects, runs on all three paths) → `besm-tests`
- `backend/riscv/test/emit_tests.cpp`, `codegen_tests.cpp` (golden assembly; `riscv_test.h` also runs programs on bare-metal `qemu-system-riscv64`, skipped without the tools), `interop_tests.cpp` (linked with clang-compiled code), `regalloc_tests.cpp` (register allocation), `float128_tests.cpp` (binary128 `long double`: the runtime against exact results from `gen_float128_cases.py`, and interop), `peephole_tests.cpp` (peephole pass on hand-built IR), the libc run tests `printf_tests.cpp`/`str_tests.cpp`/`mem_tests.cpp`/`math_tests.cpp` (ported from BESM-6, host libc output as expectation) and the book suite (each program also compiled by clang, and the outputs compared) → `riscv-tests`
- `backend/riscv/test/rv32_tests.cpp`, `llong_tests.cpp` (`long long` in register pairs, against the host's results), `interop32_tests.cpp` (ILP32D with clang: pairs, split a7/stack, `long double` by reference, hidden result pointer) and the shared `interop_tests.cpp` (RV32/ILP32D: `genriscv --rv32` run on `qemu-system-riscv32`; the same `riscv_test.h` built with `RISCV_TEST_XLEN=32`, the `libc/riscv32` headers and runtime; ctest names prefixed `rv32.`) → `riscv32-tests`
- `backend/aarch64/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `flow_tests.cpp`, `fp_tests.cpp`, `ptr_tests.cpp`, `data_tests.cpp`, `call_tests.cpp`, `struct_tests.cpp`, `hfa_tests.cpp`, `stdarg_tests.cpp` (golden assembly, the selection goldens under `NaiveSelection()`; `aarch64_test.h` also runs programs on bare-metal `qemu-system-aarch64`, skipped without the tools), `interop_tests.cpp` (a signature table and variadics linked with clang both ways), `regalloc_tests.cpp`, `peephole_tests.cpp`, `float128_tests.cpp`, the libc run tests ported from RISC-V, and the book suite (compared with clang) → `aarch64-tests`
- `backend/arm32/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `flow_tests.cpp`, `llong_tests.cpp`, `call_tests.cpp`, `data_tests.cpp`, `fp_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `hfa_tests.cpp`, `stdarg_tests.cpp`, `run_tests.cpp` (golden assembly, the selection goldens under `NaiveSelection()`; `arm32_test.h` also runs programs on bare-metal `qemu-system-arm`, skipped without the tools), `interop_tests.cpp` (a signature table, variadics and the RTABI helpers linked with clang both ways; the headers checked against clang's), `regalloc_tests.cpp`, `peephole_tests.cpp`, the libc run tests ported from AArch64, and the book suite (compared with clang) → `arm32-tests`
- `backend/aarch64/test/darwin_tests.cpp` (Mach-O and Apple ABI goldens, native runs), with `interop_tests.cpp`, the libc run tests and the book suite (plus `signed_char_tests.cpp`) built against `test/darwin_test.h` (`AARCH64_DARWIN`: compiled with `--darwin`, linked by the system `cc`, run natively, the system clang as the other side; `test/darwin_status.c` prints a book program's result and supplies `putch`) → `aarch64-darwin-tests` (ctest names prefixed `darwin.`; the runs skip off a Mac with Apple silicon)
- `backend/x86/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `flow_tests.cpp`, `fp_tests.cpp`, `x87_tests.cpp`, `ptr_tests.cpp`, `data_tests.cpp`, `call_tests.cpp`, `struct_tests.cpp`, `stdarg_tests.cpp`, `run_tests.cpp` (golden assembly, the selection goldens under `NaiveSelection()`, every output also assembled by GNU `as` when that is the assembler; `x86_test.h` also runs programs on bare-metal `qemu-system-x86_64 -M microvm`, skipped without the tools), `interop_tests.cpp` (scalars, structs of every class and variadics linked with clang both ways; the headers checked against clang's), `regalloc_tests.cpp`, `peephole_tests.cpp`, the libc run tests ported from AArch64, and the book suite (compared with clang; `backend/common/test/book/signed_char_tests.cpp` has signed-char versions of three programs, shared with MMIX) → `x86-tests`
- `backend/avr/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `relax_tests.cpp`, `call_tests.cpp`, `data_tests.cpp`, `ptr_tests.cpp`, `fp_tests.cpp`, `struct_tests.cpp`, `stdarg_tests.cpp`, `run_tests.cpp` (golden assembly, the selection goldens under `NaiveSelection()`; `avr_test.h` also runs programs on bare-metal `qemu-system-avr -M arduino-mega`, skipped without the tools), `interop_tests.cpp` (a signature table linked with clang both ways, the call-saved registers, clang's code on our runtime, the headers checked against clang's), `regalloc_tests.cpp`, `peephole_tests.cpp`, the libc run tests ported from x86-64, and the book suite (compared with avr-gcc, else clang) → `avr-tests`
- `backend/msp430/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `relax_tests.cpp`, `call_tests.cpp`, `data_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `stdarg_tests.cpp`, `fp_tests.cpp`, `run_tests.cpp` (golden assembly, the selection goldens under `NaiveSelection()`; `msp430_test.h` assembles and links with the GNU MSP430 toolchain and runs programs on `mspsim`, skipped without the tools), `float32_tests.cpp`/`float64_tests.cpp` (the soft-float runtime against the host bit for bit), `interop_tests.cpp` (a signature table linked with GCC both ways and with clang but for structures, the call-saved registers, GCC's and clang's code on our runtime, our helpers against libgcc's, our code under newlib, the headers checked against GCC's and clang's), `regalloc_tests.cpp`, the libc run tests ported from AVR (also run against newlib), and the book suite (compared with GCC's build and clang's) → `msp430-tests`
- `backend/mmix/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `flow_tests.cpp`, `call_tests.cpp`, `data_tests.cpp`, `fp_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `stdarg_tests.cpp`, `run_tests.cpp` (golden assembly, the selection goldens under `NaiveSelection()`; `mmix_test.h` assembles and links with the GNU MMIX toolchain and runs programs on Knuth's `mmix`, skipped without the tools), `interop_tests.cpp` (a signature table linked with GCC both ways, the `regcheck` harness for the registers a call keeps, GCC's code on our runtime, our code under newlib, the headers checked against GCC's), `regalloc_tests.cpp`, `peephole_tests.cpp`, the libc run tests ported from x86-64 (also run against newlib), `book_mmix_tests.cpp` (big-endian versions of the byte-order book programs) with `backend/common/test/book/signed_char_tests.cpp`, and the book suite (compared with GCC's build) → `mmix-tests`
- `backend/wasm/test/emit_tests.cpp`, `codegen_tests.cpp`, `frame_tests.cpp`, `int_tests.cpp`, `flow_tests.cpp` (the structured translation, a dispatch per irreducible region for a `goto` into a loop, Duff's device and a generator, the skeleton, random irreducible graphs run against it), `call_tests.cpp`, `data_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `stdarg_tests.cpp`, `run_tests.cpp` (golden assembly under `NaiveSelection()`, which turns the rewrites off; `wasm_test.h` assembles with clang, links with `wasm-ld` and runs modules under node, skipped without the tools), `peephole_tests.cpp` (the goldens of the default pipeline, and a run), `defer_tests.cpp` (`defer` run under node), `braam_tests.cpp` (`wasm32-braam`: programs built by `build/cc/cc` against the staged runtime and run on the fake kernel: hello, a status, `cat`, `wc` on stdin and a file, a sleep, `exit()`, the `braam` section and `--initial-pages`, the allocator, a plain `main` refused, the streams on files and stdin, the file and path calls, the worked example `docs/examples/notes.c`, signals (a byte 0x03 on stdin is a `^C`), tasks, `braam_yield`, `poll`; `braam_system.mjs` runs programs on Braam itself, `^C` at its terminal included, the ctest `braam-system`), `coro_tests.cpp` (a wide coroutine dispatch as `br_table`, the regional dispatch and the skeleton), `interop_tests.cpp` (a signature table, structures of each class, `long double`, variadics and bit-fields linked with clang both ways, clang's `main(argc, argv)` on our crt0, the headers checked against clang's), `float128_tests.cpp`, the libc run tests ported from ARM32, `backend/common/test/book/signed_char_tests.cpp`, and the book suite (compared with clang) → `wasm32-tests`
- `backend/common/test/coro/coro_run_tests.cpp` — the coroutine run programs, shared like the book suite by every backend but BESM-6 (compiled into `riscv-tests`, `riscv32-tests`, `aarch64-tests`, `aarch64-darwin-tests`, `arm32-tests`, `x86-tests`, `avr-tests`, `msp430-tests`, `mmix-tests` and `wasm32-tests`, each through its fixture `CoroTest` in `test/coro_test.h`: `Compile`, `CompileAndRunCoro`, and `QemuTest::AddUnit` for a second unit): generators, frames by `co_init` and `co_alloca`, the releases, two units, each trap; `await` in both forms against a scheduler in the program, recursion, cancel and destroy through a chain, `co_alloca` in a coroutine cascading, an arena overflow, `coro_ptr`; nothing printed depends on the target's sizes
- `backend/common/test/flow_tests.cpp` (CFG and liveness over TAC, `backend/common/flow.c`) → `backend-tests`
- `translator/test/decl_tests.cpp`, `expr_tests.cpp`, `stmt_tests.cpp`, `defer_tests.cpp`, `coro_tests.cpp`, `cast_tests.cpp`, `incdec_tests.cpp`, `switch_tests.cpp`, `ptr_tests.cpp`, `struct_tests.cpp`, `type_tests.cpp` (typed TAC, struct layout, target struct ABI) → `translate-tests`
- `optimize/test/const_fold_tests.cpp`, `jump_unreachable_tests.cpp`, `copy_prop_tests.cpp`, `cse_tests.cpp`, `loop_tests.cpp` (rotation, strength reduction, the BESM-6 opt-out), `dead_store_tests.cpp`, `type_conv_tests.cpp`, `pipeline_tests.cpp` → `optimizer-tests`
- `libutil/test/string_map_tests.cpp`, `wio_tests.cpp`, `xalloc_tests.cpp`, `float128_tests.cpp` → `libutil-tests`

The `chapter*_tests.cpp` files in `parser/test/`, `scanner/test/`, `semantic/test/`,
`optimize/test/`, and `backend/common/test/book/` are the "Writing a C Compiler" book tests; they are compiled into the same
per-module test executables as the unit tests above (e.g. `parser-tests`, `besm-tests`) and
run by `make run` (see **Build & Test** above).

## Documentation

- [README.md](README.md) — goals, getting started, component overview
- [cc/README.md](cc/README.md) — the compiler driver `vcc`: pipeline per target, options, tool lookup, linking
- [cpp/README.md](cpp/README.md) — the C preprocessor: options, targets, directives, macros, limits
- [docs/Technical_Reference.md](docs/Technical_Reference.md) — detailed reference: repo layout, components, build system, TAC YAML format, development notes
- [docs/Memory_Allocation.md](docs/Memory_Allocation.md) — memory allocator (`xalloc`) design and usage
- [docs/String_Map.md](docs/String_Map.md) — `libutil/string_map` key-value store
- [docs/Word_Oriented_IO.md](docs/Word_Oriented_IO.md) — word-oriented I/O (`wio`) for binary IR streams
- [backend/besm6/TODO.md](backend/besm6/TODO.md) — BESM-6 backend work plan with effort estimates
- [backend/besm6/Besm6_Data_Representation.md](backend/besm6/Besm6_Data_Representation.md) — BESM-6 data representation: bit layouts, ranges, and sizeof for every C scalar type
- [backend/besm6/Besm6_Calling_Conventions.md](backend/besm6/Besm6_Calling_Conventions.md) — BESM-6 C calling convention (registers, b/save, b/ret)
- [backend/besm6/Besm6_Instruction_Set.md](backend/besm6/Besm6_Instruction_Set.md) — BESM-6 instruction set reference
- [backend/besm6/Besm6_Runtime_Library.md](backend/besm6/Besm6_Runtime_Library.md) — BESM-6 runtime helper library specifications (`b/save`, `b/mul`, `b/div`, comparisons, etc.)
- [backend/besm6/Besm6_Intrinsics.md](backend/besm6/Besm6_Intrinsics.md) — BESM-6 compiler intrinsics (`libc/besm6/include/besm6.h`): the user manual for the twelve `__besm6_*` intrinsics — the two supervisor instructions `ext`/`mod` that reach every peripheral, the mode-word trio `getpsw`/`setpsw`/`maskpsw` (PSW is machine register 021; the mask write is a `vtm` with a zero modifier register), the bit-manipulation instructions with no C equivalent (`apx`/`aux`/`acx`/`anx`/`arx`), the halt `stop`, and the extracode trap. All twelve are lowered inline (never a call); the manual covers usage, the per-dialect assembly, the diagnostics, and how the lowering works
- [backend/besm6/Frexp_Ldexp.md](backend/besm6/Frexp_Ldexp.md) — the `frexp`/`ldexp` C11 math pair: meaning, usage, and a proposed frameless Madlen implementation via the BESM-6 exponent-field instructions (`E+X`, `ASN`, `STI`)
- [docs/Standard_Include_Files.md](docs/Standard_Include_Files.md) — C11 standard headers (`libc/besm6/include/`): role of each header, declared functions, inter-header relationships, and BESM-6 specifics (freestanding vs hosted, no complex/atomics/threads)
- [backend/besm6/KOI7_Encoding.md](backend/besm6/KOI7_Encoding.md) — KOI-7 character encoding: the BESM-6 code page (code→glyph), the ASCII→KOI7 conversion the codegen performs (`utf8_to_koi7.c`), and how the glyph data was collected on Dubna
- [backend/besm6/Madlen.md](backend/besm6/Madlen.md) — Madlen assembler syntax for the Dubna monitor (the assembler this backend emits; one of three BESM-6 assemblers documented here — see also Bemsh and `b6as`)
- [backend/besm6/Bemsh.md](backend/besm6/Bemsh.md) — Bemsh, the BESM-6 autocode (Shtarkman, 1967): the Cyrillic-mnemonic assembly language, its statement/column form, and how it differs from Madlen
- [backend/besm6/Besm6_Unix_Assembler.md](backend/besm6/Besm6_Unix_Assembler.md) — the BESM-6 Unix assembler (`b6as` in `cmd/as/`): AT&T-style syntax with Madlen mnemonics — tokenization, directives, operand forms, and expression evaluation
- [docs/Type_Coercion.md](docs/Type_Coercion.md) — C11 type coercion and arithmetic conversion rules
- [docs/Type_Sizes_Alignment.md](docs/Type_Sizes_Alignment.md) — type sizes and alignment per target architecture
- [docs/TAC_Optimization.md](docs/TAC_Optimization.md) — machine-independent TAC optimization: constant folding, unreachable code elimination, copy propagation, dead store elimination
- [docs/Riscv_Backend.md](docs/Riscv_Backend.md) — the RISC-V backend: decisions, passes, frame layout, calls, runtime, running a program by hand under qemu
- [docs/Aarch64_Backend.md](docs/Aarch64_Backend.md) — the AArch64 backend: target, passes, frames, AAPCS64 calls and variadics, runtime, running a program by hand under qemu
- [docs/Arm32_Backend.md](docs/Arm32_Backend.md) — the ARM32 backend: target, passes, frames, AAPCS-VFP calls and variadics, runtime, running a program by hand under qemu
- [docs/X86_64_Backend.md](docs/X86_64_Backend.md) — the x86-64 backend: target, passes, the x87 `long double`, frames, psABI calls and variadics, runtime, running a program by hand under qemu
- [docs/Avr_Backend.md](docs/Avr_Backend.md) — the AVR backend: target, the 16-bit data model and binary32 `double`, passes, frames and `Y+63`, branch relaxation, avr-gcc calls and variadics, the runtime's helper contracts, running a program by hand under qemu
- [docs/Msp430_Backend.md](docs/Msp430_Backend.md) — the MSP430 backend: target, passes, selection on operands in memory and the constant generators, frames, branch relaxation, GCC's calls with structures by reference and variadics, where clang differs, sections and `--gc-sections`, the soft binary64 and the helper contracts, costs against GCC, running a program by hand under mspsim
- [docs/Mmix_Backend.md](docs/Mmix_Backend.md) — the MMIX backend: target, passes, the register stack, GCC's fixed register model and the compaction, selection with 8-bit immediates, signed division, `float` held as binary64, linker-allocated base registers, the peephole pass, frames, GCC's calls with structures and variadics, big-endian notes, the runtime, costs against GCC, running a program by hand under `mmix`
- [docs/Coroutines_in_C.md](docs/Coroutines_in_C.md) — `defer` and coroutines, a tutorial: frames and storage, the operations, `await`, cancel and destroy, the rules, errors, traps and warnings, coroutines on Braam, the frame ABI
- [docs/Coroutines_Internals.md](docs/Coroutines_Internals.md) — how `defer` and coroutines are implemented: the design rules, the front end, the lowering of `defer` and the shared cleanup, the two-stage lowering of coroutines and the split pass, what the wasm backend sees, `coro_ptr` and the lint, the alternatives rejected, the open questions
- [docs/Braam.md](docs/Braam.md) — C programs for Braam (`vcc -t wasm32-braam`): the target, how the runtime runs a process (tasks, signals), the library, porting a program, running one under node or on Braam
- [docs/Braam_Example.md](docs/Braam_Example.md) — a program for Braam (`vcc -t wasm32-braam`) worked through: the source, building, running under node's fake kernel and on Braam, what the library offers
- [docs/Wasm_Backend.md](docs/Wasm_Backend.md) — the WebAssembly backend: target and features, locals and the shadow-stack frame, stack-code selection, structured control flow by Ramsey's translation and the dispatch fallback, stackify and the peephole rules, local coalescing, clang's calls with structures, `long double` and variadics, the assembler's peculiarities, the runtime and the node host, costs against clang, running a program by hand
- [backend/besm6/Peephole_Rewrites.md](backend/besm6/Peephole_Rewrites.md) — peephole optimization in the BESM-6 backend: concept, the `besm_peephole` pass, and the catalogue of store/reload, NTR, compare/branch, and strength-reduction rewrites (Phase M)
- [docs/C_Grammar.md](docs/C_Grammar.md) — C grammar article: scanner (`c11.l`), parser (`c11.y`), ASDL (`c11.asdl`), and how they relate to the hand-written implementation
- [docs/Tests_From_The_Book.md](docs/Tests_From_The_Book.md) — textbook-style intro for newcomers: test-driven development, how the "Writing a C Compiler" tests are organized, and how each test maps to a compiler phase
- [grammar/README.md](grammar/README.md) — C11 grammar coverage notes
- [grammar/c11.y](grammar/c11.y), [grammar/c11.l](grammar/c11.l), [grammar/c11.asdl](grammar/c11.asdl) — reference grammar and abstract syntax (not used for code generation)
