# The ARM32 backend

`genarm32` turns the compiler's intermediate code (TAC) into assembly for 32-bit ARM.
The code follows the standard ARM calling rules with floating point in VFP registers
(AAPCS-VFP), so it can call, and be called by, code compiled with clang for
`armv7a-none-eabihf`. Programs run under the `qemu` emulator, with no operating system.

## The target

- **CPU:** ARMv7-A in ARM state (no Thumb), with the hardware `sdiv`/`udiv` of the
  Cortex-A7/A15, and VFPv3-D16 for `float` and `double` (`s0`–`s31` = `d0`–`d15`).
  `-cpu cortex-a15` in qemu.
- **Calling rules:** AAPCS with the VFP variant, the `arm32` descriptor in
  `semantic/target.c`. `int`, `long` and pointers are 32 bits, `long long` 64; plain
  `char` is unsigned; `wchar_t` is `unsigned int`; an `enum` is 4 bytes.
- **Output:** a `.s` file in unified syntax that `arm-none-eabi-as -mcpu=cortex-a15
  -mfpu=vfpv3-d16 -mfloat-abi=hard` assembles and `arm-none-eabi-ld` links (or clang,
  `--target=armv7a-none-eabihf -mcpu=cortex-a15 -mfpu=vfpv3-d16`, and `ld.lld`). Each
  file starts with the `.eabi_attribute`s clang itself writes, so the objects agree on
  the ABI.
- **`long double`:** the same as `double`.
- **Constants and addresses:** `mov`/`mvn` of a modified immediate, else `movw`/`movt`
  (`#:lower16:sym`, `#:upper16:sym`). There are no literal pools; a `double` constant
  that is not a VFP immediate goes through two core registers into `vmov`.
- **Machine:** qemu `virt`, bare metal in SVC mode. Output goes to the PL011 UART at
  `0x09000000`; the exit status goes out through semihosting (`SYS_EXIT_EXTENDED`), so
  qemu needs `-semihosting`.

## How code is generated

For each function, in this order (`codegen.c`):

1. **Register allocation** (`regalloc.c`, on the shared `backend/common/regalloc.c`).
   Local variables and temporaries are put in registers where possible. A variable
   whose address is taken, an array or a struct stays in memory. A value not needed
   after a call may take an argument register (r0–r3, d0–d7); one that is goes in a
   register calls preserve (r4–r9, r11 when the frame is addressed from sp, d8–d13). A
   `long long` takes two core registers; a `float` takes the even `s` half of a `d`
   register, so the allocator never sees an `s` register alone.
2. **Instruction selection** (`instr.c`, `llong.c`, `fp.c`, `call.c`). Each TAC
   instruction becomes one or a few instructions, on the allocated registers or on the
   scratch registers r12, lr, r10 and d14/d15 for values in memory. A comparison whose
   only use is the conditional jump after it becomes `cmp` (`vcmp` + `vmrs` for floating
   point, `cmp` + `sbcs` for `long long`) and a conditional branch, without a 0/1 value;
   the inverted condition of a floating-point compare is the one right for a NaN.
3. **Prologue and epilogue** (`frame.c`), once the frame size is known.
4. **Peephole** (`peephole.c`), on the liveness of the registers over the whole
   function: copies followed into their uses, results computed where they are moved, no
   reload of a value just stored, addresses folded into loads and stores
   (`ldr r2, [r0, r3, lsl #2]`), shifts into operands (`add r0, r0, r1, lsl #3`),
   `mla`/`mls`, `tst`, no jump to the next line, and `ldrd`/`strd` for adjacent
   word-aligned slots. The shifts and masks of a bit-field access (or the same written
   by hand) become `ubfx`/`sbfx` for a read, `bfi` for a store and `bfc` for a store of
   zero, the `movw`/`movt` of a wide mask going with them, and a `uxtb`/`uxth` before a
   `strb`/`strh` goes:

   ```
   ldrb    r0, [r3]            @ p->b = v, b a 5-bit field at bit 3
   bfi     r0, r1, #3, #5
   strb    r0, [r3]
   ```

   A short if/else or if becomes conditional instructions, up to
   four a side:

   ```
   cmp     r0, #0
   movlt   r0, #0
   bx      lr
   ```

A register holds an integer in a fixed form: a `char` or `short` extended to 32 bits by
its type. AAPCS makes the sender extend, so a caller extends a narrow argument and a
callee its result, and the receiver trusts it, as clang does.

`long long` arithmetic is inline (`adds`/`adc`, `subs`/`sbc`, the shifts by a constant);
a multiply, a division, a shift by a variable and the conversions to and from floating
point call the run-time ABI helpers (`__aeabi_lmul`, `__aeabi_ldivmod`, `__aeabi_llsl`,
`__aeabi_l2d`, …), and the allocator counts them as calls.

A call of `sqrt` is the `vsqrt.f64` instruction: the translator lowers a call of the
library's `sqrt(double)` to the TAC operator `sqrt_double` on every target whose
descriptor has `hw_sqrt`, so it is not a call at all, and a constant operand is folded.
Its address, and a call from clang's code, reach `sqrt` in `libc.a` (`sqrt.s`).

To see the code without an optimization, add `--no-regalloc`, `--no-peephole` or
`--frame-pointer` to `genarm32`.

## Stack frame

With a frame record (r11):

```
r11 + 8 ...     arguments passed on the stack (a variadic function's r0-r3 just below)
r11 + 4         return address lr
r11 + 0         old r11
below that      saved r4-r9, then local variables, then saved d8-d15 and r10
sp + 0 ...      arguments for the functions this one calls
```

A function that makes no call, saves no register and needs no memory gets no frame at
all: its body, then `bx lr`. Any other function is addressed from sp, with r11 free for
values: one `push {…, lr}` of the registers it saves, a `vpush` of the VFP ones, then
`sub sp`; it returns with `pop {…, pc}`. sp stays 8-byte aligned by pushing one more
register, as clang does. When some offset does not fit its instruction from sp (a
halfword 300 bytes up, a frame over 4 KiB), the function is generated again with the
frame record; `--frame-pointer` asks for it always.

## Function calls

- Integers and pointers go in r0–r3, a `long long` in an even pair (r0:r1 or r2:r3);
  once the core registers run out, those arguments go on the stack (4 bytes each, 8 and
  8-aligned for a 64-bit one), and no later one takes a core register.
- A `float` goes in the lowest free of s0–s15, a `double` in the lowest free of d0–d7,
  so a `float` back-fills the `s` register a `double`'s alignment skipped. Once a
  floating-point argument goes on the stack, no later one takes a VFP register.
- Results come back in r0, r0:r1, s0 or d0.
- A **homogeneous float aggregate** — a struct, union or array of 1–4 members of one
  floating type, through nesting — travels in the lowest run of free consecutive `s`
  (or `d`) registers that holds it, else on the stack, closing the VFP registers. It
  comes back in s0–s3 or d0–d3.
- Any other struct goes **by value** in the next core registers, a word each (from an
  even register when 8-aligned), split between r3 and the stack while nothing is on the
  stack yet, else wholly on the stack. One of up to 4 bytes comes back in r0; a larger
  one is written to the address the caller passes in **r0**, ahead of the arguments.
- `tac_aapcs32_class` (`tac/tac_abi.c`) classifies the aggregates, sharing the AArch64
  code.
- Arguments and parameters already in registers are moved as if at once (a parallel
  move), so arguments that trade registers do not overwrite each other; a move into an
  `s` register waits for the `double` that occupies its `d` register.
- r9 is an ordinary callee-saved register, as for clang's bare-metal EABI. r12 may be
  clobbered by a linker veneer, so it is never live across a call.

### Variadic functions

A variadic call and a variadic function use the base standard, for *all* arguments, the
named ones too: a `float` goes as an integer, a `double` in an even core pair, never in
a VFP register; the result comes back in r0 (`float`) or r0:r1 (`double`). The function
pushes r0–r3 first, so they lie just below its stack arguments and form one area with
them. `va_list` is `struct __va_list { void *__ap; }`, as for clang, and `va_arg` in
`libc/arm32/include/stdarg.h` walks that area: 4 bytes per slot, a `double` or
`long long` at an 8-byte boundary. No helper and no compiler builtin is needed.

## The runtime library

In `libc/arm32/`:

- `crt0.S` — start-up code: sets sp, installs exception vectors that report a fault
  and exit with status 255, enables VFP, clears `.bss`, turns on the MMU and caches
  over an identity map, calls `main`, then `exit`. The MMU is needed: with it off all
  memory is Strongly-ordered, where an unaligned access faults. The map (1 MiB sections)
  makes RAM Normal memory and everything below 1 GiB Device.
- `console.s` — `putbyte` writes to the UART; `exit` stops qemu through semihosting with
  `main`'s result as its exit status.
- `link.ld` — memory layout: the program loads at 0x40010000.
- `malloc.s` — a simple bump allocator.
- `sqrt.s` — `sqrt` and `sqrtf`, one instruction each.
- `aeabi_divmod.s`, `aeabi_long.s`, `aeabi_conv.s`, `aeabi_mem.s` — the run-time ABI
  helpers, which both our code and clang's call. They take the base standard even in a
  hard-float program, so the conversions move values between core and VFP registers
  around the C routines of `libc/ilp32/int64.c` and `int64conv.c`.
- C library: `printf`, the string functions and the math helpers, all compiled by our
  compiler: most of it from `libc/common/`, the 64-bit and bit-level math from
  `libc/ilp32/`.
- Headers: `libc/arm32/include` holds `float.h`, `stdarg.h`, `stddef.h`, `stdint.h` and
  `setjmp.h`. `libc/ilp32/include` has the headers shared with riscv32, and
  `libc/common/include` the target-neutral ones.

## Running a program by hand

You need the ARM binutils (`arm-none-eabi-as` and `-ld`; or clang and `ld.lld`) and
`qemu-system-arm`. On Debian and Ubuntu: `apt install binutils-arm-none-eabi
qemu-system-arm`; on macOS: `brew install arm-none-eabi-binutils qemu`.

After `make install`, which installs into `~/.local`, the driver does it all:

```sh
vcc -t arm32 -o hello.elf hello.c
qemu-system-arm -M virt -cpu cortex-a15 -display none -serial stdio -monitor none \
    -semihosting -kernel hello.elf
```

`vcc -v` prints each step's command. By hand, the same steps are:

```sh
P=~/.local
vcpp -t arm32 -nostdinc -I$P/share/vcc/arm32/include hello.c hello.i   # preprocess
vparse hello.i hello.ast                                     # parse
vlower -t arm32 hello.ast hello.tac                          # check and lower
vgenarm32 hello.tac hello.s                                  # generate assembly
arm-none-eabi-as -mcpu=cortex-a15 -mfpu=vfpv3-d16 -mfloat-abi=hard -o hello.o hello.s
arm-none-eabi-ld -T $P/share/vcc/arm32/lib/link.ld -o hello.elf \
    $P/share/vcc/arm32/lib/crt0.o hello.o $P/share/vcc/arm32/lib/libc.a
qemu-system-arm -M virt -cpu cortex-a15 -display none -serial stdio -monitor none \
    -semihosting -kernel hello.elf
```

Without installing, use `build/parse`, `build/lower`, `build/backend/genarm32`, the
headers in `libc/arm32/include`, `libc/ilp32/include` and `libc/common/include`, and
the library in `build/libc/arm32/`.

A program that goes wrong may never stop, so run qemu under a timeout, e.g.
`timeout 10 qemu-system-arm …`. A fault prints its kind, the faulting address and the
fault status registers, and exits with status 255.

## Things found on the way

- **`ld.lld` does not check `Tag_ABI_VFP_args`:** it links a soft-float object with a
  hard-float one silently, and the floating-point arguments then go astray. Assemble
  everything with the `eabihf` triple.
- **A plain `.s` gets no ABI attributes from the command line**, so `genarm32` writes
  clang's own at the top of every file. `.arch armv7ve` crashes clang's assembler; the
  header says `.arch armv7-a` plus `.arch_extension idiv`.
- **Unaligned access needs the MMU.** With it off, an unaligned `ldr` faults. `ldrd`
  and `strd` want word alignment even with it on, so the peephole pass pairs only
  word-aligned slots: a slot of a byte struct passed in registers may be at any offset.
- **Floating-point compares and NaN.** After `vcmp`, the ARM inverse of a condition is
  true when the operands are unordered (`gt` inverts to `le`, which holds for a NaN),
  and that is what C wants: `a > b` is false then. So a fused branch or a conditional
  instruction may use the inverse freely; the run tests check every compare with NaNs.

## Tests

`build/backend/arm32/arm32-tests` checks the generated assembly and runs programs on
qemu: integer, `long long` and floating-point arithmetic, calls, structs, homogeneous
float aggregates, variadic functions, the libc, register allocation, frames and the
peephole pass. It also links our code with clang's code in both directions over a table
of signatures, checks the headers against clang's own, and compares every book
program's output and exit status with clang's. The instruction-selection goldens run
with `--no-regalloc --frame-pointer --no-peephole`. Tests that need qemu or clang are
skipped when the tools are missing; the `arm32-headers` CTests check that every header
preprocesses and parses.
