# The AArch64 backend

`genaarch64` turns the compiler's intermediate code (TAC) into assembly for 64-bit ARM.
The code follows the standard ARM calling rules (AAPCS64), so it can call, and be called
by, code compiled with clang for `aarch64-none-elf`. Programs run under the `qemu`
emulator, with no operating system, or under Linux, linked against glibc (the
`aarch64-linux` target, see [Hosted Linux](#hosted-linux)), or under macOS on Apple
silicon, linked against libSystem (the `aarch64-darwin` target, see
[Hosted macOS](#hosted-macos)).

## The target

- **CPU:** ARMv8.0-A, the base A64 instruction set with scalar `float` and `double` in
  hardware. No atomics or vector instructions are needed, so any qemu `-cpu` runs it.
- **Calling rules:** AAPCS64, the `aarch64` descriptor in `semantic/target.c`. `int` is
  32 bits; `long` and pointers are 64; plain `char` is unsigned; `wchar_t` is
  `unsigned int`.
- **Output:** a `.s` file in GNU syntax that `aarch64-none-elf-as` assembles and
  `aarch64-none-elf-ld` links (or clang, `--target=aarch64-none-elf`, and `ld.lld`).
- **`long double`:** 128 bits (IEEE binary128), passed in a `q` register, computed in
  software by the same routines as on RISC-V.
- **Machine:** qemu `virt`, bare metal at EL1. Output goes to the PL011 UART at
  `0x09000000`; the exit status goes out through semihosting (`SYS_EXIT_EXTENDED`), so
  qemu needs `-semihosting`.

## How code is generated

For each function, in this order (`codegen.c`):

1. **Register allocation** (`regalloc.c`, on the shared `backend/common/regalloc.c`).
   Local variables and temporaries are put in registers where possible. A variable
   whose address is taken, an array, a struct or a `long double` stays in memory. A
   value not needed after a call may take an argument register (x0–x7, v0–v7); one that
   is goes in a register calls preserve (x19–x28, or v8–v15, whose low 64 bits are
   kept: all a `double` needs).
2. **Instruction selection** (`instr.c`, `call.c`). Each TAC instruction becomes one or
   a few instructions, on the allocated registers or on scratch registers for values in
   memory: x9–x15 and v16–v18 hold operands, x16/x17 large offsets and constants. A
   comparison whose only use is the conditional jump after it becomes `cmp` (or `fcmp`)
   and a `b.cond`, without a 0/1 value.
3. **Prologue and epilogue** (`frame.c`), once the frame size is known.
4. **Peephole** (`peephole.c`). Immediate operands (`add w0, w0, #100`, bitmask
   immediates for `and`/`orr`/`eor`, `cmn` for a negative compare), `wzr` for a stored
   zero, copies followed into their uses, no reload of a value just stored, addresses
   folded into loads and stores (`ldr x2, [x0, w3, sxtw #3]`), `madd`/`msub`,
   `ldp`/`stp` for adjacent slots, `cbz`, and no jump to the next line.

A register holds an integer in a fixed form: a type of 32 bits or less in the W view
with the upper half zero, `char` and `short` also extended to 32 bits by their type.
AAPCS64 leaves the upper bits of a narrow argument or result unspecified, so a
parameter is extended when it arrives and a call result when it comes back; the caller
and the callee never extend for each other.

A call of `sqrt` is the `fsqrt` instruction: the translator lowers a call of the
library's `sqrt(double)` to the TAC operator `sqrt_double` on every target whose
descriptor has `hw_sqrt`, so it is not a call at all, and a constant operand is folded.
Its address, and a call from clang's code, reach `sqrt` in `libc.a` (`sqrt.s`).

To see the code without an optimization, add `--no-regalloc`, `--no-peephole` or
`--frame-pointer` to `genaarch64`.

## Stack frame

With a frame record (x29):

```
x29 + 16 ...    arguments passed on the stack
x29 + 8         return address x30
x29 + 0         old x29
below that      saved x19-x28 / d8-d15 in pairs, then local variables
sp + 0 ...      arguments for the functions this one calls
```

A function that makes no call, saves no register and needs no memory gets no frame at
all: its body, then `ret`. Any other function is addressed from `sp` without x29 when
every offset fits its instruction; then x30 is saved only if the function makes calls.
A larger frame, or any frame under `--frame-pointer`, keeps the frame record.

## Function calls

- Arguments go in x0–x7 (integers, pointers) and v0–v7 (`float`, `double`,
  `long double`); once a class runs out, its arguments go on the stack in 8-byte slots
  (16-byte aligned for a 16-byte type). The result comes back in x0 or v0.
- A **homogeneous float aggregate** — a struct, union or array of 1–4 members of one
  floating type, through nesting, with no padding — travels in that many consecutive v
  registers, one member each; if they do not all fit, it goes whole on the stack and no
  later argument takes a v register. It comes back in v0–v3.
- Another struct of up to 16 bytes travels in one or two X registers (an even pair when
  it is 16-byte aligned), or whole on the stack; it is never split. A larger one is
  passed as a pointer to the caller's copy, and returned through the address the caller
  passes in **x8** (not as a hidden first argument).
- `tac_aapcs64_class` (`tac/tac_abi.c`) does this classification for both the code
  generator and the front end, so `va_arg` and the calls cannot disagree.
- Arguments and parameters already in registers are moved as if at once (a parallel
  move), so arguments that trade registers do not overwrite each other.
- **`long double`** arithmetic, comparisons and conversions call library functions such
  as `__addtf3` (add) and `__lttf2` (compare), from `libc/common/float128.c`, with the
  operands in q0/q1.
- x18 is never used.

### Variadic functions

A variadic argument goes where a named one would. A function with `...` saves the
argument registers its named parameters left: x*n*–x7 into a 64-byte area and
q*m*–q7 into a 128-byte one. `va_list` is the AAPCS64 structure
`{ __stack, __gr_top, __vr_top, __gr_offs, __vr_offs }`, so one can be passed to or from
clang code. `va_start(ap, last)` is `__va_start(&ap)`, which the code generator fills
in place. `va_arg(ap, T)` calls `__va_arg` (`libc/aarch64/va_arg.c`) with the size,
alignment and argument class of `T`; the class comes from `__builtin_va_class(T)`, a
compiler builtin that folds to an integer constant (0 for X registers, 1 for by
reference, `esize * 8 + count` for v registers).

## The runtime library

In `libc/aarch64/`:

- `crt0.S` — start-up code: sets the stack, enables the FP unit, installs exception
  vectors that report a fault and exit, turns on the MMU, clears `.bss`, calls `main`,
  then `exit`. The MMU is needed: with it off all memory is Device memory, where an
  unaligned access faults. An identity map makes RAM Normal memory.
- `console.s` — `putbyte` writes to the UART; `exit` stops qemu through semihosting with
  `main`'s result as its exit status.
- `link.ld` — memory layout: the program loads at 0x40080000.
- `malloc.s` — a simple bump allocator; `va_arg.c` — the `va_arg` helper.
- `sqrt.s` — `sqrt` and `sqrtf`, one instruction each.
- C library: `printf`, the string functions, the math helpers and the `long double`
  routines, all compiled by our compiler: most of it from `libc/common/`, the bit-level
  math from `libc/lp64/`.
- Headers: `libc/aarch64/include` holds `stdarg.h`, `stddef.h`, `stdint.h` and
  `setjmp.h`. `libc/lp64/include` has the headers shared with riscv64, and
  `libc/common/include` the target-neutral ones.

## Hosted Linux

The `aarch64-linux` target, `vcc`'s default on an AArch64 Linux machine, builds an
ordinary Linux executable with the bare-metal target's code: the `aarch64` descriptor
(which `lower -t aarch64-linux` aliases) is glibc's data model, binary128 `long double`
and unsigned `char` included, and the AAPCS64 `va_list` is glibc's. `genaarch64 --linux`
adds a `.note.GNU-stack` section in each unit.

It links with the system's C compiler (`aarch64-linux-gnu-gcc` when cross compiling),
`cc -no-pie … -lvcc`: glibc brings the startup files and the C library, libgcc the
binary128 arithmetic (`__addtf3`, …), and `libvcc.a` only `__va_arg`. The headers are
`libc/linux/aarch64/include` and `libc/linux/include` ahead of the bare-metal ones, as for
[x86-64](X86_64_Backend.md#hosted-linux), with glibc's 312-byte `jmp_buf` and AArch64's
`fenv.h`. It is built and tested where such a compiler exists; without one, only its
headers are installed and checked.

## Hosted macOS

The `aarch64-darwin` target, `vcc`'s default on a Mac with Apple silicon, builds an
ordinary macOS executable. It differs from Linux in the object format and in the calling
convention, so it has a descriptor of its own (`semantic/target.c`): Apple's arm64 ABI has
a signed plain `char` and a `long double` that is `double`. `genaarch64 --darwin` sets
`aarch64_darwin` (`codegen.c`) and with it `a64_macho` (`emit.c`), which change the
following.

**Mach-O.** Every C name is spelled with a leading `_` (`a64_emit_name`), and a local
label starts with `L` instead of `.L` (`a64_local_prefix`). A branch target is a
`label` operand, so it gets no `_`. There is no `.type` or `.size`. Read-only data goes in
`.const`, but read-only data holding an address goes in `__DATA,__const`, because ld64
allows no relocation in `__TEXT`. Zeroed data goes in `__DATA,__bss`. Relocations are
spelled `sym@PAGE`/`sym@PAGEOFF` in place of `sym`/`:lo12:sym` (the `A64_Reloc` of a
symbol operand).

**The GOT.** A macOS arm64 executable is always position independent, and ld64 makes no
copy relocations. So `name_addr` (`frame.c`), where every address of a global is formed,
reaches a name the unit does not define (`Gen.defined`) through its GOT entry:
`adrp x, sym@GOTPAGE` and `ldr x, [x, sym@GOTPAGEOFF]` (the `A64_MEM_GOT` memory operand,
which no peephole rewrite touches). This covers both extern data and the address of a
library function. A call stays a direct `bl`, because ld64 makes the stub.

**Calls** (`call.c`). There are three differences from AAPCS64:

- A named argument on the stack, a scalar or an HFA, takes its own size and alignment,
  not an 8-byte slot (`on_stack_named`). Another aggregate is still rounded up to
  8 bytes.
- An argument that matches the `...` goes on the stack, whatever registers are free.
  `variadic_arg` tells it from a named one by the length of the call's declared
  parameter list. It takes 8-byte slots, and its alignment when that is 16.
  `tac_apple64_class` (`tac/tac_abi.c`) decides how it travels: an aggregate over 16 bytes
  that is no HFA goes by reference, and anything else goes inline, a 32-byte HFA
  included. That same function is `__builtin_va_class` on this target, so the backend and
  `va_arg` cannot disagree.
- `va_list` is a `char *`. A variadic function saves no registers. `va_start` stores
  `x29 + 16 +` the named stack bytes rounded up to 8. `va_arg` (`libc/darwin/include/stdarg.h`)
  is a pointer walk in the header, with no `__va_arg`.

The caller extends a narrow argument to 32 bits, which a value in its canonical form
already is.

**`long double` is `double`.** `a64_size` is 8, `a64_is_fp` and `a64_is_double` count it,
and `a64_is_ld` does not. A conversion to or from it becomes the `double` one, or a copy
(`gen_ld_as_double`). Constants are rounded to binary64. HFAs use `tac_apple64_hfa`, under
which a `long double` member is a `double` one.

It links with the system's C compiler: `cc -o a.out objects…`. There is no `-no-pie` and no
`libvcc.a`, since this target needs no runtime of ours. The headers are
`libc/darwin/include` ahead of the bare-metal ones. They agree with libSystem's on what a
program hands to it:
- `errno` is `(*__error())`;
- `stdin` and its siblings are `__stdinp` and so on;
- `jmp_buf` is `int[48]`;
- the 128-byte `mbstate_t`;
- the `LC_*`, `E*` and `FP_*` numbers.

`aarch64-darwin-tests` (on a Mac; elsewhere only its goldens run) has:
- the goldens of `test/darwin_tests.cpp`;
- the run, interop and libc tests of the bare-metal suite, built against
  `test/darwin_test.h`, the same fixture interface run natively, with the system clang
  as the other side and the oracle;
- the book suite, each program's result printed by `test/darwin_status.c`.

The build sets `AARCH64_DARWIN_CC` only on a Mac with Apple silicon.

## Running a program by hand

You need the AArch64 binutils (`aarch64-none-elf-as` and `-ld`; or clang and `ld.lld`)
and `qemu-system-aarch64`. On Debian and Ubuntu: `apt install binutils-aarch64-none-elf
qemu-system-arm`; on macOS: `brew install aarch64-elf-binutils qemu`.

After `make install`, which installs into `~/.local`, the driver does it all:

```sh
vcc -t aarch64 -o hello.elf hello.c
qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -monitor none \
    -semihosting -kernel hello.elf
```

`vcc -v` prints each step's command. By hand, the same steps are:

```sh
P=~/.local
vcpp -t aarch64 -nostdinc -I$P/share/vcc/aarch64/include hello.c hello.i   # preprocess
vparse hello.i hello.ast                                         # parse
vlower -t aarch64 hello.ast hello.tac                            # check and lower
vgenaarch64 hello.tac hello.s                                    # generate assembly
aarch64-none-elf-as -o hello.o hello.s
aarch64-none-elf-ld -T $P/share/vcc/aarch64/lib/link.ld -o hello.elf \
    $P/share/vcc/aarch64/lib/crt0.o hello.o $P/share/vcc/aarch64/lib/libc.a
qemu-system-aarch64 -M virt -cpu cortex-a57 -display none -serial stdio -monitor none \
    -semihosting -kernel hello.elf
```

Without installing, use `build/parse`, `build/lower`, `build/backend/genaarch64`, the
headers in `libc/aarch64/include`, `libc/lp64/include` and `libc/common/include`, and
the library in `build/libc/aarch64/`.

A program that goes wrong may never stop (a stray exception in a loop), so run qemu
under a timeout, e.g. `timeout 10 qemu-system-aarch64 …`.

## Tests

`build/backend/aarch64/aarch64-tests` checks the generated assembly and runs programs on
qemu: calls, structs, homogeneous float aggregates, variadic functions, `long double`
(against exact results), the libc, register allocation and the peephole pass. It also
links our code with clang's code in both directions over a table of signatures, and
compares every book program's output and exit status with clang's. Tests that need qemu
or clang are skipped when the tools are missing; the `aarch64-headers` CTests check that
every header preprocesses and parses.
