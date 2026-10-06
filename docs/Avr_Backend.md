# The AVR backend

`genavr` turns the compiler's intermediate code (TAC) into assembly for the 8-bit AVR
microcontrollers. The code follows the avr-gcc calling rules as clang implements them,
so it can call, and be called by, code compiled with clang for `--target=avr
-mmcu=atmega1280`. Programs run under the `qemu` emulator, with no operating system.

## The target

- **CPU:** the ATmega1280 (`avr51`): 128 KB of flash, 8 KB of SRAM at
  `0x200`–`0x21ff`, with `mul`, `movw` and `jmp`/`call`. Its program counter is 2 bytes,
  so return addresses and function pointers are plain 16-bit word addresses: no `EIND`,
  no `gs()` stubs. qemu's `arduino-mega` machine models it.
- **Data model:** `char` 1 byte and **signed**, `short` and `int` 2, `long` 4,
  `long long` 8, pointers 2. `size_t` is `unsigned int`, `ptrdiff_t` and `wchar_t` are
  `int`. **Every type has alignment 1**, so `struct { char c; long l; }` is 5 bytes.
- **Floating point:** `float`, `double` and `long double` are all IEEE binary32, in
  software. That is clang's and avr-gcc's default; `-mdouble=64` is not supported.
- **A Harvard machine.** Code addresses are *word* addresses in flash, data addresses
  *byte* addresses in SRAM, both 16 bits. A function's address is `pm(f)` in data and
  `pm_lo8(f)`/`pm_hi8(f)` in code. `.rodata` is copied to SRAM with `.data`, so an
  ordinary pointer reaches every object.
- **Output:** a `.s` file in GNU avr-as syntax (`lo8()`, `hi8()`, `Y+q`), as clang emits
  it, assembled by `avr-as -mmcu=atmega1280` and linked by `avr-ld -m avr51` into an ELF
  (or by clang, `--target=avr -mmcu=atmega1280`, and `ld.lld`).
- **Machine:** qemu `arduino-mega`, the image loaded with `-bios` (`-kernel` loads
  nothing on AVR). Output goes to USART0; `main`'s result goes out as one byte on USART1.
  Nothing on the machine can make qemu exit, so whoever runs it stops it once that byte
  has arrived.

Not supported: other devices (the ATmega2560 and other 3-byte-PC parts, `avrtiny`,
`avrxmega`, cores without `mul`/`movw`), data in flash (`PROGMEM`, `__flash`, named
address spaces), a 64-bit `double`, interrupt handlers, linker relaxation, `_Complex` and
atomics.

## The 16-bit `int` and the 32-bit `double`

AVR is the first target with an `int` narrower than 32 bits and a `double` that is not
binary64. Both reach into the shared front end, which now takes them from the target
descriptor (`semantic/target.c`) rather than from the host:

- An integer constant gets its type from its spelling and the target's widths (C11
  §6.4.4.1): `40000` is a `long` here, `0xffff` an `unsigned int`.
- `size_t` and `ptrdiff_t` follow the pointer width; `unsigned short` promotes to
  `unsigned int` where `short` is as wide as `int`; casts between pointers and `long`
  truncate or extend; case values and enumerators are narrowed to the target's `int`.
- With `double_mant_dig` 24, both constant folders round every `double` result to
  binary32, convert an integer to it in one rounding, and take a literal's `strtof`
  value, so a folded constant and a computed one agree to the last bit.

## How code is generated

For each function, in this order (`codegen.c`):

1. **Register allocation** (`regalloc.c`, on the shared `backend/common/regalloc.c`).
   The unit is an even register pair. An `int`, a pointer or a `char` takes one pair (a
   `char` wastes the high byte); a `long` or a `float` takes two, not necessarily
   adjacent. A `long long`, an aggregate, a variable whose address is taken and a
   `volatile` stay in memory. A value not live across a call may take an argument pair,
   `r24`, `r22`, `r20` or `r18`; one that is live across a call takes a call-saved pair,
   `r16`, then `Y` (`r28`) in a function without a frame, then `r14` … `r2`, which the
   prologue saves.
2. **Instruction selection** (`instr.c`, `fp.c`, `call.c`), in two forms.
   - The **naive form** loads the operands into two register blocks laid out as the first
     two arguments of a call (`r25:r24`/`r23:r22`, `r25:r22`/`r21:r18`, or
     `r25:r18`/`r17:r10` for 8 bytes), computes there with byte chains linked by the
     carry, and stores the result. The runtime helpers then need no moves. It handles
     what needs a helper or many bytes: multiply and divide, floating point, 8-byte
     operations, shifts by a variable, aggregates over 16 bytes. The allocator counts
     these instructions as calls (`uses_scratch`), so no value lives across them in
     `r18`–`r25`.
   - Every other instruction computes **in its destination's registers**, or in the
     scratch `Z` and `X` when the destination is in memory or its registers cannot take
     an immediate (only `r16`–`r31` can). Operand bytes come straight from registers,
     through `r0` from memory, or as immediates. `ldd`, `ld` and `ldi` leave the flags
     alone, so a carry chain survives the loads between its links:

     ```
     add     r16, r20        ; s += a[i]
     adc     r17, r21
     ```

   - Operands, call arguments and parameters on entry are gathered by one **parallel
     move** each, so values that trade registers do not overwrite each other. A cycle is
     broken through a free `X`/`Z` pair with `movw`, else `r0`, else the stack.
   - A comparison whose only use is the conditional jump after it becomes `cp`/`cpc`
     and a branch, without a 0/1 value.
3. **Peephole** (`peephole.c`), before the prologue and epilogue exist:
   - forward through each block, what each register is known to hold: a copy of
     another, a constant, or a byte of a slot or global just loaded or stored. A move or
     `ldi` of what a register already holds goes, a reload becomes a move, a store of
     what the slot holds goes. A store through a pointer or a call forgets memory; a
     `volatile` access always stays;
   - backward, over the liveness of the registers and SREG: dead instructions go;
     `ldi t, k; cp r, t` becomes `cpi r, k`, and `subi`/`sbci` of 1…63 on `r24`–`r30`
     becomes `adiw`/`sbiw` when the flags are dead;
   - jumps to the next instruction go, a branch over a jump is inverted, unreachable
     code goes.
4. **Prologue and epilogue** (`frame.c`), saving the call-saved registers the body still
   uses; then a call followed by the return becomes a tail `jmp`.
5. **Branch relaxation** (`relax.c`), last. A conditional branch reaches only ±64 words
   and `rjmp` ±2K words, and neither clang's assembler nor `ld.lld` relaxes one that is
   out of range: the assembler accepts it, and the link fails. Every instruction knows
   its size, so `relax.c` finds each branch's distance exactly and rewrites the far ones
   (a branch as the inverse branch over an `rjmp`, an `rjmp` as a `jmp`), repeating
   until all reach.

To see the code without an optimization, add `--no-regalloc` or `--no-peephole` to
`genavr`.

`r1` is zero at every call and return (`__zero_reg__`). `mul` overwrites it, so every
`mul` is followed by `clr r1`, and the peephole pass treats `r1` as zero only where no
`mul` came before.

## Stack frame

SP is an I/O register, and nothing addresses memory relative to it, so the frame is
addressed from `Y` (`r29:r28`), with a displacement `q` of 0…63:

```
Y + frame + 5 ...   arguments passed on the stack
Y + frame + 3       return address (2 bytes)
Y + frame + 1       saved Y
Y + 1 ...           slots: scalars of up to 4 bytes first, then long long, then aggregates
below Y             the call-saved registers in use, pushed after Y is set up
```

- A function with **no slots and no stack arguments** sets up no `Y` and leaves SP alone;
  `Y` is then one more call-saved pair for variables. When a function that was given it
  turns out to need a frame after all, it is allocated again without it.
- Up to 6 bytes of slots are reserved with `rcall .` (2 bytes each) and released with
  `pop r0`, as avr-gcc does. A larger frame is reserved by writing SP, with interrupts
  held off between its two halves:

  ```
  in      r0, __SREG__
  cli
  out     __SP_H__, r29
  out     __SREG__, r0       ; the I flag back: it takes effect after the next instruction
  out     __SP_L__, r28
  ```

- A slot past `Y+63` is reached through `Z` (or `X`) loaded with its address. The
  scratch-free selection reaches slots only as `Y+q`, so a function whose scalar slots
  would lie past `Y+63` is compiled with the naive form and every variable in memory.

## Function calls

- **Arguments** are allocated left to right from `r25` down. Each takes the registers
  just below the previous one, its size rounded up to even: an `int` in `r25:r24`, then
  a `long` in `r23`–`r20`. Registers run down to `r8`. The first argument that does not
  fit above `r8` goes on the stack, **and so does every later one**, even one that would
  fit. A `char` takes a whole pair; clang does not extend it, so `genavr` extends as the
  sender and again as the receiver.
- **Structures** are passed as clang passes them, which is not as one block: a structure
  is **flattened** into its top-level members, each an argument of its own rounded up to
  a pair (a nested structure, a union, an array, or the storage unit of bit-fields stays
  one piece). So `struct { char a; int b; int c; }` goes `a` in `r24`, `b` in `r23:r22`,
  `c` in `r21:r20`, and a structure can be split between registers and the stack.
- **Stack arguments** lie above the return address in the order of the parameter list,
  unaligned; the caller pushes them last first and removes them after the call.
- **Results** start at `r24` for up to 2 bytes, `r22` for up to 4, `r18` for up to 8, in
  ascending registers; a structure of up to 8 bytes comes back the same way. A larger
  one goes through a hidden pointer, passed as the first argument; unlike most ABIs, the
  callee does not return the address.
- **Call-saved argument registers.** `r8`–`r17` hold both arguments and call-saved
  values. A variable there that an argument (or the second operand of an 8-byte helper)
  overwrites is pushed before the call and popped after it, since the callee preserves
  the argument, not the variable.

### Variadic functions

A variadic callee takes **every** argument on the stack, named ones included, so its
parameters live where they came in. `va_list` is a `char *`, as clang's for the target;
`va_start` points it past the last named parameter, and `va_arg` steps over each value
(`libc/avr/include/stdarg.h`). A `char` or `short` comes promoted to `int`, a `float` to
`double`, which is the same binary32. A `va_list` can be handed to clang's code and back.

## The runtime library

In `libc/avr/`:

- `crt0.S` — start-up code: the interrupt vector table, `r1` cleared, SP at the top of
  SRAM, `.data` (with `.rodata`) copied from flash with `elpm`, `.bss` cleared, a stack
  canary set, the USART0 and USART1 transmitters enabled, `main` called, its result
  passed to `exit`. It defines `__do_copy_data` and `__do_clear_bss`, which clang's
  objects reference. An interrupt, which nothing enables, reports itself and exits with
  status 254.
- `console.s` — `putbyte` writes to USART0. `exit` writes the status byte to USART1 and
  stops the CPU; a stack that ran into the canary is reported first, as status 253.
- `link.ld` — flash at 0, SRAM at `0x800200` (the AVR toolchain's data address space),
  the heap after `.bss`, the stack from the top; the link fails when less than 1 KB is
  left for the stack.
- `divmod.s`, `mul.s` — integer division and 32-bit multiplication under their libgcc
  names, with avr-gcc's **special register contracts**, which clang's code relies on:

  | Helper | In | Out | Clobbers |
  | --- | --- | --- | --- |
  | `__udivmodqi4` | `r24` / `r22` | quotient `r24`, remainder `r25` | `r23` |
  | `__divmodqi4` | `r24` / `r22` | quotient `r24`, remainder `r25` | `r0`, `r22`, `r23`, T |
  | `__udivmodhi4` | `r25:r24` / `r23:r22` | quotient `r23:r22`, remainder `r25:r24` | `r21`, `r26`, `r27` |
  | `__divmodhi4` | `r25:r24` / `r23:r22` | quotient `r23:r22`, remainder `r25:r24` | `r0`, `r21`, `r26`, `r27`, T |
  | `__udivmodsi4` | `r25:r22` / `r21:r18` | quotient `r21:r18`, remainder `r25:r22` | `r26`, `r27`, `r30`, `r31` |
  | `__divmodsi4` | `r25:r22` / `r21:r18` | quotient `r21:r18`, remainder `r25:r22` | `r0`, `r26`, `r27`, `r30`, `r31`, T |
  | `__mulsi3` | `r25:r22` × `r21:r18` | `r25:r22` | `r0`, `r26`, `r27`, `r30`, `r31` |

  A 16-bit multiply is inline: three `mul` and a `clr r1`. `genavr` counts every helper
  as an ordinary call, which clobbers a superset of these.
- `malloc.s` — a bump allocator from the end of `.bss` up to 256 bytes below SP;
  `free` does nothing.
- `setjmp.s` — `setjmp`/`longjmp`, saving `r2`–`r17`, `Y`, SP, SREG and the return
  address.
- C library: `printf` and the string and math functions from `libc/common/`; the
  binary32 soft-float runtime `libc/common/float32.c` (`__addsf3`, `__mulsf3`, `__divsf3`,
  the comparisons and conversions, correctly rounded, under the libgcc names with the
  ordinary ABI); the `long long` division and conversions of `libc/ilp32/int64.c` and `int64conv.c`, and
  the multiply of `libc/common/muldi3.c`; and AVR's own `frexp.c`, `ldexp.c`, `modf.c`.
  All of it is compiled by `genavr` itself.
- Headers: `libc/avr/include` holds AVR's own headers (`float.h`, `limits.h`, `math.h`,
  `setjmp.h`, `stdarg.h`), `libc/ip16/include` those of the 16-bit data model
  (`inttypes.h`, shared with MSP430, and `stddef.h`, `stdint.h`), and `libc/common/include` the
  target-neutral ones. See
  [libc/avr/include/README.md](../libc/avr/include/README.md).

## Running a program by hand

You need the AVR binutils (`avr-as` and `avr-ld`; or clang and `ld.lld`) and
`qemu-system-avr`. On Debian and Ubuntu: `apt install binutils-avr qemu-system-misc`; on
macOS: `brew tap osx-cross/avr && brew install avr-binutils qemu`. No avr-gcc or avr-libc
is needed.

After `make install`, which installs into `~/.local`, the driver does it all:

```sh
vcc -t avr -o hello.elf hello.c
qemu-system-avr -M arduino-mega -display none -monitor none \
    -serial stdio -serial file:status -bios hello.elf
```

The output appears on the terminal; `main`'s result is the one byte in the file
`status` (`od -An -tu1 status`). qemu keeps running after `exit`: stop it with Ctrl-C,
or from a script once `status` is not empty. For a real board, `llvm-objcopy -O ihex
hello.elf hello.hex` makes the Intel HEX image a flasher takes.

`vcc -v` prints each step's command. By hand, the same steps are:

```sh
P=~/.local
vcpp -t avr -nostdinc -I$P/share/vcc/avr/include hello.c hello.i  # preprocess
vparse hello.i hello.ast                                          # parse
vlower -t avr hello.ast hello.tac                                 # check and lower
vgenavr hello.tac hello.s                                         # generate assembly
avr-as -mmcu=atmega1280 -o hello.o hello.s
avr-ld -m avr51 -T $P/share/vcc/avr/lib/link.ld -o hello.elf \
    $P/share/vcc/avr/lib/crt0.o hello.o $P/share/vcc/avr/lib/libc.a
```

Without installing, use `build/parse`, `build/lower`, `build/backend/genavr`, the headers
in `libc/avr/include`, `libc/ip16/include` and `libc/common/include`, and the library in `build/libc/avr/`.

qemu emulates AVR slowly: a million iterations of a `long` loop take about two seconds.

## Things found on the way

- **Nothing relaxes an AVR branch.** clang's assembler accepts a `breq` 200 bytes from
  its target as an `R_AVR_7_PCREL` relocation, and `ld.lld` then fails with "out of
  range". Hence `relax.c`.
- **qemu cannot be stopped from inside.** `cli; sleep`, `break` and a watchdog reset
  under `-no-reboot` all leave it running, so the status goes out on a second serial
  port and the harness stops qemu when it arrives. `-kernel` loads nothing on AVR;
  `-bios` does.
- **clang flattens structure arguments.** A structure is not one block of registers but
  one argument per member, which the interop tests found; a structure can even be split
  between registers and the stack.
- **clang does not extend a `char` argument**, and a callee compiled by clang may read
  the whole pair, so `genavr` extends as the sender too.
- **`long long` to `float` rounded twice.** `libc/ilp32/int64.c` converted through a
  `double` assumed to be binary64; with a binary32 `double` the high and low halves
  each rounded. It now folds the bits beyond 32 into a sticky bit and rounds once.
- **`sprintf` wrote nothing.** Its "unbounded" size `1 << 24` overflows a 16-bit `int`.
  It is `INT_MAX` now where `int` is that narrow.
- **Signedness from the instruction, not the operand.** Copy propagation can leave an
  `int`-to-`float` conversion or a right shift with an operand of the other signedness,
  so `genavr` takes it from the TAC instruction's kind.
- **clang `-O0` is not always a usable reference.** It miscompiles one `long long`
  comparison (`Chapter11_LargeConstants`, where `-O1` agrees with us) and runs out of
  registers on a few programs, which the book suite therefore compiles with `-O1`.

## Tests

`build/backend/avr/avr-tests` checks the generated assembly and runs programs on qemu:
integer and floating-point arithmetic, frames past `Y+63`, branch relaxation at the
limits, calls, structures, variadic functions, the libc, `setjmp`, register allocation
and each peephole rule. It links our code with clang's in both directions over a table
of signatures, checks that `r2`–`r17` and `Y` survive our calls through a hand-written
caller, runs clang's code on our runtime, checks the headers against clang's own, and
compares every book program's output and exit status with avr-gcc's (`-O0`, on our runtime
and then its `libgcc.a`), or clang's where there is no avr-gcc; programs that cannot
run on AVR (undefined behavior with a 16-bit `int`, too big for 8 KB of SRAM, or too
slow under qemu) are skipped with the reason in `test/book_test.h`. The
instruction-selection goldens run with `--no-regalloc --no-peephole`. Tests that need
qemu or clang are skipped when the tools are missing; the `avr-headers` CTests check that
every header preprocesses and parses.
