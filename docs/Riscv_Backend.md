# The RISC-V backend

`genriscv` turns the compiler's intermediate code (TAC) into assembly for 64-bit
RISC-V. The code follows the standard RISC-V calling rules, so it can call, and be
called by, code compiled with clang. Programs run under the `qemu` emulator, with no
operating system.

## The target

- **CPU:** RV64IMFD — 64-bit integers, multiply/divide, `float` and `double` in hardware.
- **Calling rules:** the standard LP64D convention. `int` is 32 bits; `long` and
  pointers are 64.
- **Output:** a `.s` file that clang assembles.
- **`long double`:** 128 bits, computed in software.

## How code is generated

For each function, in this order:

1. **Register allocation** (`regalloc.c`). Local variables and temporaries are put in
   CPU registers where possible. A variable whose address is taken (`&x`), an array or a
   struct stays in memory. A value still needed after a function call goes in a register
   that calls preserve (s1–s11, fs0–fs11).
2. **Instruction selection** (`instr.c`, `call.c`). Each TAC instruction becomes one or
   a few RISC-V instructions. Registers t0–t6 and ft0–ft11 are scratch space.
3. **Prologue and epilogue** (`frame.c`). The code that sets up and tears down the
   function's stack space.
4. **Peephole** (`peephole.c`). Small cleanups: `addi` instead of `li` + `add`, no
   reload of a value just stored, no jump to the next line.

To see the code without an optimization, add `--no-regalloc`, `--no-peephole` or
`--frame-pointer` to `genriscv`.

## Stack frame

```
s0 + 0 ...      arguments passed on the stack
s0 - 64 ...     a0-a7 saved here, only in a function with "..."
s0 - H + 8      return address ra   (H = 16, or 80 with "...")
s0 - H          old s0
below that      saved registers, then local variables
sp + 0 ...      arguments for the functions this one calls
```

A small function that needs no memory gets no frame at all. When the frame is small,
it is addressed from `sp` and `s0` is not used.

## Function calls

- Arguments go in a0–a7 (integers, pointers) and fa0–fa7 (`float`, `double`); the rest
  go on the stack. The result comes back in a0 or fa0.
- A struct of up to 16 bytes travels in one or two registers; a larger one is passed as
  a pointer to a copy.
- A `long double` travels in two integer registers. Its arithmetic is done by library
  functions such as `__addtf3` (add) and `__lttf2` (compare), in
  `libc/riscv/float128.c`.
- A function with `...` saves a0–a7 next to its stack arguments, so `va_arg` simply
  walks one array.

## The runtime library

In `libc/riscv/`:

- `crt0.S` — start-up code: sets up the stack, calls `main`, then `exit`.
- `console.s` — `putbyte` prints a character; `exit` stops qemu. `exit(0)` makes qemu
  exit with status 0; any other value becomes qemu's exit status.
- `link.ld` — memory layout: the program loads at address 0x80000000.
- C library: `printf`, the string functions, a simple `malloc`, the math helpers and
  the `long double` routines.

## Running a program by hand

You need clang with RISC-V support, `ld.lld` and `qemu-system-riscv64`. On macOS:
`brew install llvm lld qemu`.

After `make install`, which installs into `~/.local`:

```sh
P=~/.local
cc -E -nostdinc -I$P/share/vcc/riscv64/include hello.c -o hello.i    # preprocess
vparse hello.i hello.ast                                         # parse
vlower -t riscv64 hello.ast hello.tac                            # check and lower
vgenriscv64 hello.tac hello.s                                    # generate assembly
clang --target=riscv64 -march=rv64imfd -mabi=lp64d -c hello.s -o hello.o
ld.lld -T $P/share/vcc/riscv64/lib/link.ld -o hello.elf \
    $P/share/vcc/riscv64/lib/crt0.o hello.o $P/share/vcc/riscv64/lib/libc.a
qemu-system-riscv64 -M virt -bios none -display none -serial stdio -monitor none \
    -kernel hello.elf
```

Without installing, use `build/parse`, `build/lower`, `build/backend/genriscv`, the
headers in `libc/riscv/include` and `libc/common/include`, and the library in
`build/libc/riscv/`.

To read the intermediate code, run `lower` with `--yaml`.

## Tests

`build/backend/riscv/riscv-tests` checks the generated assembly and runs programs on
qemu. It also links our code with clang's code in both directions, and compares every
book program's output with clang's. Tests that need qemu or clang are skipped when the
tools are missing.
