# The AArch64 backend

`genaarch64` turns the compiler's intermediate code (TAC) into assembly for 64-bit ARM.
The code follows the standard ARM calling rules (AAPCS64), so it can call, and be called
by, code compiled with clang for `aarch64-none-elf`. Programs run under the `qemu`
emulator, with no operating system.

## The target

- **CPU:** ARMv8.0-A, the base A64 instruction set with scalar `float` and `double` in
  hardware. No atomics or vector instructions are needed, so any qemu `-cpu` runs it.
- **Calling rules:** AAPCS64, the `aarch64` descriptor in `semantic/target.c`. `int` is
  32 bits; `long` and pointers are 64; plain `char` is unsigned; `wchar_t` is
  `unsigned int`.
- **Output:** a `.s` file in GNU/LLVM syntax that clang assembles
  (`clang --target=aarch64-none-elf -c`); `ld.lld` links.
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
- C library: `printf`, the string functions, the math helpers and the `long double`
  routines, all compiled by our compiler: most of it from `libc/common/`, the bit-level
  math from `libc/lp64/`.
- Headers: `libc/aarch64/include` holds `stdarg.h`, `stddef.h`, `stdint.h` and
  `setjmp.h`. `libc/lp64/include` has the headers shared with riscv64, and
  `libc/common/include` the target-neutral ones.

## Running a program by hand

You need clang with AArch64 support, `ld.lld` and `qemu-system-aarch64`. On macOS:
`brew install llvm lld qemu`.

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
clang --target=aarch64-none-elf -c hello.s -o hello.o
ld.lld -T $P/share/vcc/aarch64/lib/link.ld -o hello.elf \
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
