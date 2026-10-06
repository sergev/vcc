# The x86-64 backend

`genx86` turns the compiler's intermediate code (TAC) into assembly for x86-64. The code
follows the System V AMD64 calling rules (the psABI), so it can call, and be called by,
code compiled with clang for `x86_64-none-elf`. Programs run under the `qemu` emulator,
with no operating system.

## The target

- **CPU:** baseline x86-64 (x86-64-v1): SSE2 for `float` and `double`, the x87 for
  `long double`, `cmov`; no SSE3 or later, no AVX. qemu's default CPU runs it.
- **Calling rules:** the System V psABI, the `x86_64` descriptor in `semantic/target.c`.
  `int` is 32 bits, `long` and pointers 64; plain `char` is **signed** (the first of
  our byte-addressed targets where it is); `wchar_t` is `int`.
- **`long double`:** the x87 80-bit extended format, in a 16-byte slot. The front end
  rounds every `long double` constant it folds to a 64-bit significand, so folded and
  computed values agree.
- **Code model:** small, static, not position-independent. Every symbol is addressed
  as `sym(%rip)`, so the program must lie below 2 GiB; there is no GOT or PLT.
- **Output:** a `.s` file in AT&T syntax, assembled by clang
  (`clang --target=x86_64-none-elf -c`) and linked by `ld.lld`. GNU `as`
  (`x86_64-elf-as`) accepts the same output, and the tests check that when it is
  installed.
- **Machine:** qemu `microvm`, booted through the PVH ELF note. Output goes to the
  16550 UART COM1 at I/O port `0x3f8`. The exit status goes out as a byte on the debug
  console (port `0xe9`), then a write to the `isa-debug-exit` device at port `0xf4`
  stops qemu.

Not supported: 32-bit x86 (i386, x32), macOS (Mach-O) and Windows x64, PIC/PIE and TLS,
SSE3 and later, `__int128`, `_Complex`, atomics, and Intel syntax.

## How code is generated

For each function, in this order (`codegen.c`):

1. **Register allocation** (`regalloc.c`, on the shared `backend/common/regalloc.c`).
   Local variables and temporaries go in registers where possible. A variable whose
   address is taken, an array, a struct, a `long double` or a `volatile` stays in
   memory. A value not live across a call may take a register calls overwrite: an
   argument register (`rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`) or any of
   `xmm0`–`xmm13`. One that is live across a call goes
   in a register calls preserve (`rbx`, `r12`–`r15`, and `rbp` when there is no frame
   pointer). No `xmm` register survives a call, so a `float` or `double` live across
   one stays in memory. A divide (it writes `rdx`) and a shift by a variable (its count
   must be in `cl`) count as calls for the allocator.
2. **Instruction selection** (`instr.c`, `fp.c`, `x87.c`, `call.c`). Each TAC
   instruction becomes one or a few instructions. x86 instructions take two operands
   and overwrite the second, so `d = a op b` is computed in `d`'s register; when `d` is
   `b`'s register, a commutative operator swaps its operands. Values in memory are
   loaded into the scratch registers `rax`, `r10` and `r11` (`xmm14` and `xmm15` for
   floating point), or used straight from memory where x86 allows. A comparison whose
   only use is the conditional jump after it becomes `cmp` (or `ucomisd`) and a
   conditional jump, without a 0/1 value.
3. **Peephole** (`peephole.c`), over the liveness of the registers and the flags in
   the whole function, and of the upper halves of the general registers:
   - copies are followed into their uses, and results computed where they are moved;
   - a load folds into the instruction that uses it, and an address computation into
     the memory operand (`addq (%rdi,%rdx,8), %r8`);
   - a value just stored is not loaded again;
   - `cmp $0` and a mask that is only tested become `test`, and zero becomes `xor`;
   - jumps to the next line go, and blocks are merged;
   - a short if or if/else that only picks a value becomes `cmov`:

     ```
     cmpl    %esi, %edi
     cmovle  %esi, %edi
     movl    %edi, %eax
     ret
     ```
4. **Prologue and epilogue** (`frame.c`), once the frame size is known, saving only the
   callee-saved registers the body still uses.

A register holds an integer in a fixed form: a type of 32 bits or fewer is in the
32-bit view, extended to 32 bits by its own type, and its upper half is zero (every
32-bit write clears it). The written psABI leaves the upper bits of a `char` or `short`
argument undefined, but clang extends them to 32 bits as the sender and relies on that
as the receiver. So `genx86` extends as the sender and extends again as the receiver,
which is safe with both clang and GCC.

A call of `sqrt` is the `sqrtsd` instruction: the translator lowers a call of the
library's `sqrt(double)` to the TAC operator `sqrt_double` on every target whose
descriptor has `hw_sqrt`, so it is not a call at all, and a constant operand is folded.
Its address, and a call from clang's code, reach `sqrt` in `libc.a` (`sqrt.s`).

To see the code without an optimization, add `--no-regalloc`, `--no-peephole` or
`--frame-pointer` to `genx86`.

### The x87 `long double`

A `long double` never stays on the x87 stack between TAC instructions. Each operation
loads its operands from their 16-byte slots with `fldt` (at most two deep), computes,
and stores the result with `fstpt`. So the x87 stack is empty at every call, except for
a result in `st(0)`, as the psABI wants. Conversion to an integer truncates with
`fistpq` under a control word switched to round toward zero, since `fisttp` is SSE3.
Unsigned 64-bit conversions add or subtract 2^64 or 2^63.

## Stack frame

The frame is addressed from a base 8 bytes below the return address:

```
base + 16 ...   arguments passed on the stack
base + 8        return address
base + 0        saved rbp, or (no frame pointer) the first callee-saved register
below that      the other callee-saved registers, then local variables
rsp + 0 ...     arguments for the functions this one calls
```

By default the frame is addressed from `rsp`: the prologue pushes the callee-saved
registers it uses and subtracts the rest with `sub $N, %rsp`, keeping `rsp` 16-byte
aligned at every call. A function that makes no call and saves no register keeps up to
120 bytes of local variables in the **red zone**, the 128 bytes below `rsp` that the
psABI lets a leaf use without moving `rsp`. A function that needs no memory gets no
frame at all. With `--frame-pointer` every function with a frame starts
`push %rbp; mov %rsp, %rbp`, addresses its frame from `rbp`, and `rbp` is never
allocated.

## Function calls

- Integers and pointers go in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`; `float` and
  `double` in `xmm0`–`xmm7`. Once a register file runs out, its arguments go on the
  stack, 8 bytes each, the first at the lowest address. The stack arguments are stored
  into an area at the bottom of the frame, so `rsp` does not move around a call.
- Results come back in `rax` (`rax`:`rdx`), `xmm0` (`xmm0`:`xmm1`), or `st(0)` for a
  `long double`.
- A `long double` argument always goes on the stack, in a 16-byte aligned slot.
- A **struct or union** of up to 16 bytes is split into eightbytes, and each one is
  classed INTEGER or SSE by the psABI's merge rules (`tac_sysv64_class` in
  `tac/tac_abi.c`). Each INTEGER eightbyte goes in the next general register, each SSE
  one in the next `xmm` register, so `struct { double d; long l; }` goes in `xmm0` and
  `rdi`. It is **all or nothing**: when the eightbytes do not all fit, the whole struct
  goes on the stack, and the registers stay free for later arguments.
- A larger struct, or one with an unaligned field or a `long double`, is **copied onto
  the stack** by value. When it is a result, the caller passes the address for it in
  `rdi`, ahead of the arguments, and the callee returns that address in `rax`.
- Arguments and parameters already in registers are moved as if at once (a parallel
  move), so arguments that trade registers do not overwrite each other.
- Before a call to a variadic or unprototyped function, `%al` holds the number of
  `xmm` registers used.

### Variadic functions

A variadic function saves `rdi`–`r9`, and `xmm0`–`xmm7` when `%al` is not zero, into a
176-byte register save area. `va_list` is clang's
`struct __va_list_tag { unsigned gp_offset, fp_offset; void *overflow_arg_area,
*reg_save_area; }[1]`. It is an array type, so a `va_list` passed to a function passes
a pointer, and can be handed to clang-compiled code. `va_start` is expanded in place.
`va_arg` calls `__va_arg` (`libc/x86/va_arg.c`), which takes the argument's class from
the `__builtin_va_class(T)` keyword (the same `tac_sysv64_class`) and reads the save
area, then the stack arguments. A two-eightbyte struct whose halves came in different
register files is put together in a temporary.

## The runtime library

In `libc/x86/`:

- `crt0.S` — start-up code. qemu enters `_start` through the PVH ELF note, in 32-bit
  protected mode with paging off. `crt0.S` switches to long mode over an identity map
  of four 1 GiB pages, enables SSE and the x87, installs exception handlers that report
  the vector, error code, `rip` and `cr2` and exit with status 255, clears `.bss`,
  calls `main`, then `exit`. A value left on the x87 stack after `main` exits with
  status 254. Interrupts stay disabled, which is what makes the red zone safe.
- `console.s` — `putbyte` writes to COM1. `exit` writes the status byte to the debug
  console, then stops qemu through `isa-debug-exit`.
- `link.ld` — memory layout: the program loads at 1 MiB.
- `malloc.s` — a simple bump allocator.
- `sqrt.s` — `sqrt` and `sqrtf`, one instruction each.
- `setjmp.s` — `setjmp`/`longjmp`, saving the callee-saved registers, `rsp`, the return
  address, MXCSR and the x87 control word.
- C library: `printf`, the string functions and the math helpers, all compiled by our
  compiler, from `libc/common/` and `libc/lp64/`, plus `va_arg.c`. The binary128
  `long double` runtime of the other 64-bit targets is not needed.
- Headers: `libc/x86/include` holds `float.h` (the x87 `LDBL_*`), `limits.h` (signed
  `char`), `setjmp.h`, `stdarg.h`, `stddef.h` and `stdint.h`. `libc/lp64/include` has
  the headers shared with riscv64 and aarch64, and `libc/common/include` the
  target-neutral ones.

## Running a program by hand

You need clang with x86 support, `ld.lld` and `qemu-system-x86_64`. On macOS:
`brew install llvm lld qemu`. Nothing depends on the host being x86: qemu emulates it.

After `make install`, which installs into `~/.local`, the driver does it all:

```sh
vcc -t x86_64 -o hello.elf hello.c
qemu-system-x86_64 -M microvm -display none -serial stdio -monitor none \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel hello.elf
```

The exit device makes qemu exit with `(status << 1) | 1`, which loses the top bit of
the status byte. To see `main`'s result whole, add `-debugcon file:status` and read the
first byte of the file `status`.

`vcc -v` prints each step's command. By hand, the same steps are:

```sh
P=~/.local
vcpp -t x86_64 -nostdinc -I$P/share/vcc/x86_64/include hello.c hello.i  # preprocess
vparse hello.i hello.ast                                     # parse
vlower -t x86_64 hello.ast hello.tac                         # check and lower
vgenx86 hello.tac hello.s                                    # generate assembly
clang --target=x86_64-none-elf -c hello.s -o hello.o
ld.lld -T $P/share/vcc/x86_64/lib/link.ld -o hello.elf \
    $P/share/vcc/x86_64/lib/crt0.o hello.o $P/share/vcc/x86_64/lib/libc.a
qemu-system-x86_64 -M microvm -display none -serial stdio -monitor none \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 -kernel hello.elf
```

Without installing, use `build/parse`, `build/lower`, `build/backend/genx86`, the
headers in `libc/x86/include`, `libc/lp64/include` and `libc/common/include`, and the
library in `build/libc/x86/`.

A program that goes wrong may never stop, so run qemu under a timeout, e.g.
`timeout 10 qemu-system-x86_64 …`. A fault prints its vector, error code and address,
and exits with status 255.

## Things found on the way

- **The PVH note must be a note.** Without the `@note` section type, `.note.Xen` is
  PROGBITS, the ELF has no `PT_NOTE` segment, and qemu refuses the image ("without PVH
  ELF Note").
- **`isa-debug-exit` loses a bit.** qemu exits with `(v << 1) | 1`, so status 200 comes
  back as 145. The debug console carries the whole byte.
- **Clang relies on narrow arguments being extended**, although the psABI does not
  promise it. Code that passes a `char` without extending it breaks clang's callee.
- **A `volatile` was read twice.** The `setjmp` test found that `round++` on a
  `volatile int` added to the value last stored instead of the value read: the
  translator read the variable a second time without the volatile mark. Fixed for all
  targets; see "Volatile" in [Technical_Reference.md](Technical_Reference.md).
- **A shift count is always `cl`.** The peephole pass once renamed `%cl` in a shift,
  which no x86 instruction can encode. Fixed registers like this one are modelled
  explicitly.
- **Floating-point compares and NaN.** `ucomisd` sets ZF, PF and CF all to 1 for
  unordered operands. So `a > b` and `a >= b` use `ja`/`jae`, which are false for a NaN,
  and `<`/`<=` swap the operands to do the same; `==` and `!=` need a parity jump too.
- **Unaligned access needs no set-up**, unlike AArch64 and ARM32; crt0 enables paging
  only because long mode requires it.

## Tests

`build/backend/x86/x86-tests` checks the generated assembly and runs programs on qemu:
integer and floating-point arithmetic, the x87 `long double`, calls, structs of every
class, variadic functions, the libc, `setjmp`, register allocation, frames and the
peephole pass. It also links our code with clang's code in both directions over tables
of signatures, structs and variadics, checks the headers against clang's own, and
compares every book program's output and exit status with clang's (three that assume an
unsigned plain `char` run as signed-char versions in
`backend/common/test/book/signed_char_tests.cpp`, shared with MMIX). The
instruction-selection goldens run with `--no-regalloc --frame-pointer --no-peephole`.
When GNU `as` is installed, every output is assembled by it too. Tests that need qemu
or clang are skipped when the tools are missing; the `x86_64-headers` CTests check
that every header preprocesses and parses.
