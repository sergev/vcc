# The MSP430 backend

`genmsp430` turns the compiler's intermediate code (TAC) into assembly for the classic
16-bit TI MSP430. The code follows the MSP430 EABI as **GCC** implements it, so it calls,
and is called by, code compiled with `msp430-elf-gcc -mcpu=msp430`, and it links with
GCC's `libgcc.a` and newlib. Programs run bare-metal under
[mspsim](https://github.com/sergev/mspsim), a simulator of the classic MSP430 with a
console UART and an exit device.

## The target

- **CPU:** the classic MSP430: sixteen 16-bit registers, a 64 KB address space, no
  MSP430X extensions. It has no multiplier: the MPY peripheral of some devices is not
  used, and mspsim has none. So a multiply calls the runtime, as a divide does.
- **Data model:** `char` 1 byte and **unsigned**, `short` and `int` 2, `long` 4,
  `long long` 8, pointers 2. `size_t` is `unsigned int`, `ptrdiff_t` is `int`, and
  `wchar_t` is `long`. **Every type wider than `char` has alignment 2**, so
  `struct { char c; int i; long l; double d; }` is 16 bytes.
- **Floating point:** `float` is IEEE binary32, `double` and `long double` binary64,
  all in software. It is the first soft binary64 in this project.
- **A von Neumann machine:** one address space, so a function pointer is a plain byte
  address (`mov #f, r12`, `.short f`), unlike AVR's word addresses.
- **Output:** a `.s` file in GNU msp430-as syntax (`#imm`, `&abs`, `x(rN)`, `@rN+`, the
  `.b` suffix). It is assembled by `msp430-elf-as -mcpu=msp430` and linked by
  `msp430-elf-ld`. clang's assembler (`clang --target=msp430`) and `ld.lld -n` take it
  as well.
- **Machine:** mspsim.
  - **Memory map:** RAM for `.data`, `.bss`, the heap and the stack at
    `0x0200`–`0x3fff` (15.5 KB); ROM for code, `.rodata` and the load image of `.data`
    at `0x4000`–`0xffdf` (48 KB); the vectors at `0xffe0`.
  - **Output:** stdout goes to the USCI_A0 UART (`UCA0TXBUF`, at the MSP430G2xx
    addresses).
  - **Exit:** `exit` writes `main`'s result to the stop register `0x01fe`, and mspsim
    exits with it as its status.

Not supported: MSP430X (20-bit registers and addresses, `calla`, `pushm`/`popm`), the
hardware multiplier, device headers and `-mmcu`, interrupt handlers and low-power
modes, structure arguments to or from clang's code, linker relaxation, `_Complex` and
atomics.

## How code is generated

For each function, in this order (`codegen.c`):

1. **Register allocation** (`regalloc.c`, on the shared `backend/common/regalloc.c`).
   - **The unit is one 16-bit register.** An `int`, a pointer or a `char` takes one
     register; a `long` or a `float` takes two, not necessarily adjacent. A
     `long long`, a `double`, an aggregate, a variable whose address is taken and a
     `volatile` stay in memory.
   - **Pools.** A value not live across a call or a helper may take `r12`, `r13`, `r14`
     or `r11`, the call-clobbered registers. Any other value takes one of `r10`…`r4`,
     which the prologue then pushes.
   - **`r15` is never allocated.** It is the selection's one scratch register.
   - **Helpers in `r8`–`r11`.** A function that calls a helper taking its first
     operand in `r8`–`r11` keeps `r8`–`r10` free of variables.
2. **Structure parameters read in place** (`find_byref_params`, `call.c`; see below).
3. **The frame layout** (`frame.c`).
4. **Instruction selection** (`instr.c`, `fp.c`, `call.c`).
5. **Peephole** (`peephole.c`), before the prologue and epilogue exist.
6. **Prologue and epilogue** (`frame.c`), then a second, short peephole pass.
7. **Branch relaxation** (`relax.c`), last.

To see the code without an optimization, add `--no-regalloc` or `--no-peephole` to
`genmsp430`. With both, every variable lives in its frame slot.

### Selection on operands where they lie

The MSP430 is a two-address machine whose source can be a register, an immediate,
`x(rN)`, `@rN`, `@rN+` or `&abs`, and whose destination a register, `x(rN)` or `&abs`.
So `add 2(r1), 4(r1)` is one instruction. Selection uses this:
- **Operands in place.** Each operand stays where it lies, a register or memory, on
  either side. `d = a + b` is `mov a, d; add b, d`, and a comparison is a `cmp` of the
  operands in place.
- **Wider values** go word by word, linked by the carry (`add`/`addc`, `sub`/`subc`).
- **`r15`** holds what has nowhere else to go: a pointer loaded from memory, a shift
  count, a compared constant, a copy loop's pointer.

```
bump:                         ; counter += k, with long counter
    mov     r12, r13
    rla     r12               ; the sign of k into C
    subc    r12, r12          ; 0 or -1 ...
    inv     r12               ; ... and so the high word of (long)k
    add     &counter, r13
    addc    &counter+2, r12
    mov     r13, &counter
    mov     r12, &counter+2
    ret
```

**Constant generators.** `r2` and `r3` produce 0, 1, 2, 4, 8 and −1 as a source operand
at no cost; any other immediate costs an extension word. The peephole pass uses the
aliases built on them (`clr`, `inc`, `incd`, `dec`, `decd`, `tst`, `adc`, `inv`).

**Instruction sizes.** Every instruction knows its size, 2, 4 or 6 bytes (`msp_ir.c`).
The size model follows GNU `as`:
- a source `0(rN)` is printed, and counted, as `@rN`;
- `push #4` and `push #8` take an extension word.

`SizesAgreeWithAssembler` checks the model against both assemblers.

**Other selection rules:**
- **Parallel moves.** Helper operands, call arguments and incoming parameters each go
  into place as one parallel move. A cycle is broken by three `xor`s, so no temporary
  is needed.
- **Compare and branch.** A comparison (or `!x`) whose only use is the next conditional
  jump becomes a `cmp` and that jump, with no 0/1 in between. A constant first operand
  is turned around: `k < b` is `cmp #k+1, b; jge`.
- **Inline multiply.** Every 16-bit multiply by a constant, and an index scaled by
  one, is done inline in `r15` by Horner's rule: a shift per bit below the top one
  and an add (or subtract) per digit. The digits are plain binary or canonical signed
  digits, whichever is shorter, or those of `-k` with the product negated at the end
  (`inv`, `inc`). So `x * 25173` is 21 instructions, about 21 cycles, against some 165
  for a call of `__mspabi_mpyi`.
- **Shifts.** The CPU shifts one bit at a time (`rla`, `rra`, `rrc`). An `int` or a
  `long long` shifts inline; a `long` shift by a variable count calls
  `__mspabi_slll`/`srll`/`sral`.

**Flags are not uniform.** `mov` (and so `pop` and `br`) sets none; `bit` and `and` set
C = !Z; `xor` sets V when both operands are negative; `rrc` and `rra` set C from the bit
shifted out. Selection and the peephole pass read them from one per-opcode table.

### The peephole pass

It runs over the body to a fixed point.
- **Constants:** a neutral operation goes (`bis #0`, `and #-1`), and so does an
  `add #0` ahead of an `addc`.
- **Jumps:** a branch over a jump becomes the inverse branch, and a jump to the next
  instruction goes.
- **Forward pass:** what each register is known to hold. That is a copy of another
  register, a constant, a memory word just loaded or stored, or a byte already
  extended. These go:
  - a move of what is already there;
  - a reload of what a register holds;
  - a store of what the memory holds;
  - a `mov.b r, r` of an extended byte.

  A read of a copy reads the oldest register holding it.
- **Backward pass,** over the liveness of `r4`–`r15` and SR:
  - dead instructions go; a call reads only the registers its arguments are in;
  - a load moves forward into its one use;
  - a load, an operation and a store back become one operation on memory;
  - an add of a constant to a base becomes an offset (`mov 6(r12), r12`), so a
    structure copy goes memory to memory;
  - consecutive loads through a dying pointer use `@rN+`;
  - a load through a pointer stepped by its size right after, with the flags dead, uses
    `@rN+` too;
  - a `tst` goes when the instruction before already set its flags, under the C and V
    rules.
- **Dead stores to the frame:** a backward pass over the liveness of each byte of the
  first 64 bytes of slots. A plain store (`mov`, `clr`) to slot bytes that nothing reads
  before they are overwritten, or the function returns, goes. What may read a slot:
  - an `x(r1)` operand covering it;
  - a call, which reads its stack arguments, the outgoing area below the slots;
  - once r1 has been read as a value on some path to here (`mov r1, r12`, the frame's
    address escaping), a call or any access through another register, which may read
    every slot.

  A body that pushes or pops is left alone, since its offsets do not name one slot
  throughout. When no slot is referenced any more, the function has no frame at all.
  So a `double` parameter passed straight on to a helper, or a structure read only
  through its incoming pointer, costs no slot.
- **After the frame:**
  - a jump to a lone `ret` is `ret`;
  - `call #f; ret` is the tail jump `br #f`.

A `volatile` access carries a flag that every rule leaves alone.

```
sum:                          ; for (i = 0; i < n; i++) s += p[i];
    push    r10
    clr     r10
    clr     r11
.L4:
    cmp     r13, r11
    jge     .LL2
    mov     r11, r14
    rla     r14
    add     r12, r14
    add     @r14, r10
    inc     r11
    jmp     .L4
.LL2:
    mov     r10, r12
    pop     r10
    ret
```

## Stack frame

The frame is addressed from SP (`x(r1)`), with no frame pointer unless asked for one; a
16-bit offset has no range problem.

```
SP + frame + 2*saved + 2 ...  arguments passed on the stack
SP + frame + 2*saved          return address
SP + frame ...                saved r4-r10 in use, r10 lowest
SP + out ...                  slots, each aligned to its type
SP + 0 ...                    outgoing stack arguments of the calls
```

The prologue pushes the call-saved registers the body uses, then `sub #frame, r1`; the
epilogue undoes both and returns. A function with **no slots and no saved registers**
touches neither SP nor the stack, and its early returns are `ret` in place:

```
add:
    add     r13, r12
    ret
```

The operands that address incoming stack arguments get their final offsets only once the
prologue is known.

With `--frame-pointer` the slots and the incoming arguments are addressed from r4
instead, at the same offsets: the prologue pushes r4 with the others, reserves the frame
and then sets `mov r1, r4`, and the epilogue begins with `mov r4, r1`. r4 is then never
allocated. Outgoing arguments stay at `x(r1)`, and the pushes in the body no longer shift
any slot's offset. The peephole pass keeps what it knows of `x(r4)` words across a push,
but leaves the dead stores to them alone.

```
f:
    push    r4
    sub     #8, r1
    mov     r1, r4
    ...
    mov     12(r4), r12     ; the fifth argument
    mov     r4, r1
    add     #8, r1
    pop     r4
    ret
```

The test fixture's `VCC_MSP430_FRAME_POINTER` runs every MSP430 test in this mode; the
goldens then differ, every program runs the same.

### alloca

`alloca(n)` (`<alloca.h>`) and `co_alloca` in an ordinary function take their memory
from the machine stack. The translator passes them on as calls of `__builtin_alloca`,
`__builtin_stack_save` and `__builtin_stack_restore`, which `gen_call` expands in place
through r15 alone. A function that calls any of them has the frame from r4 as under
`--frame-pointer`, whatever the option:

```
mov     r12, r15
inc     r15
bic     #1, r15         ; the size, rounded to 2
sub     r15, r1
mov     r1, r15
add     #OUT, r15       ; the memory, above the outgoing arguments
```

`OUT` is the outgoing-argument area, which `layout_frame` knows before selection. The
epilogue's `mov r4, r1` gives the memory back. The peephole pass forgets what it knew of
`x(r1)` words at the `sub`, never takes it for a dead instruction (r1 is not among the
registers its liveness tracks), and leaves the dead frame stores of such a function
alone; the register allocator does not count the builtins as calls (its `inline_call`
hook).

## Branch relaxation

A conditional or unconditional jump reaches −512…+511 words. Neither assembler relaxes
one out of range: GNU `as` and clang's both reject it ("fixup value out of range").
`relax.c` knows every instruction's size, so it finds each jump's distance exactly. A far
jump is rewritten, and the pass repeats until every jump reaches:
- `jmp L` becomes `br #L`;
- `jcc L` becomes the inverse jump over a `br #L`;
- `jn L` has no inverse, so it becomes a `jn` to a `br #L` placed behind a `jmp` over it.

## Function calls

The MSP430 EABI as GCC implements it, checked against GCC's output:

- **Registers.** `r0` is PC, `r1` SP, `r2` SR (and constant generator 1), `r3`
  constant generator 2. `r4`–`r10` are call-saved, `r11`–`r15` call-clobbered. SP is
  always even; `push.b` moves it by 2.
- **Arguments** go in `r12`, `r13`, `r14` and `r15`, left to right, each taking the next
  free registers:
  - a 16-bit value takes one register;
  - a 32-bit value takes the next **two consecutive** registers, which need not start
    on an even one (`kl(int, long, long)` puts the first `long` in `r13:r14`);
  - **the split `long`:** when only `r15` is left, a `long` goes with its low word in
    `r15` and its high word on the stack (`k(int, int, int, long)`);
  - a 64-bit value goes in `r12`–`r15` only when all four are free, else wholly on the
    stack;
  - **later arguments still take free registers:** in `g(int, long long, int)` the
    `long long` goes on the stack and the last `int` in `r13`;
  - a `char` is extended by the caller, and GCC's callee extends it again; `genmsp430`
    extends on both sides.
- **Stack arguments** sit above the return address in parameter-list order, 2-aligned.
  The caller stores them into its outgoing area and removes nothing after the call.
- **Results:** 16 bits in `r12`, 32 in `r12:r13`, 64 in `r12`–`r15`. **Every**
  structure or union, even a 1-byte one, comes back through a hidden pointer, passed in
  `r12` as the first argument.
- **Calls:** `call #f` for a direct call; an indirect call goes through `r11` or memory
  (`call r11`, `call x(r1)`, `call &fp`).

### Structures by reference

A structure or union argument travels **by reference**, whatever its size:
- **The caller** passes the address of its own object, **uncopied**, as an ordinary
  pointer argument: in a register if one is free, else on the stack. An rvalue (a
  call's result, a compound literal) goes as the address of its temporary.
- **The callee** copies the object into its own slot as the first thing it does. Until
  then, the slot's first word holds the incoming address.
- **The no-copy shortcut.** The callee reads the caller's object through the pointer
  instead, and copies nothing, when three things hold:
  - it only reads the parameter, by member or whole;
  - it makes no call and no store through a pointer;
  - it writes no global.

  The caller's object then cannot change meanwhile. A structure passed on the stack
  makes `return s.b` into `mov 2(r1), r15; mov 2(r15), r12; ret`.

### Variadic functions

A variadic callee takes its **last named argument** and all the variable ones on the
stack. The named ones before it, the hidden result pointer included, follow the rules
above: `vi(1, 2L, 3, 4)` passes 1 in `r12`, 2 in `r13:r14`, and 3 and 4 on the stack.
`va_list` is a `char *`, as GCC's is (`libc/msp430/include/stdarg.h`):
- `va_start` points it past the last named parameter;
- `va_arg` steps over each value in whole 2-byte words;
- a `char` or `short` comes promoted to `int`, a `float` to `double`;
- a structure comes as its address. `__builtin_va_class(T)` tells `va_arg` so (from
  `tac_msp430_class`), and it reads the address and then the object through it.

### Where clang differs

clang's MSP430 target agrees with GCC on every point above but these, so a program
mixing our code with clang's must avoid them:
- **Structure arguments.** clang does not pass them by reference, and no option makes it.
  Structure arguments between our code and clang's are not supported.
- **The variadic rule.** clang puts every argument of a variadic call on the stack, so it
  differs whenever a variadic function has two named parameters or more.
- **Types.** clang's `wchar_t` and `wint_t` are `int`, its `sig_atomic_t` is `long`, and
  its fast 8-bit types are `char`. We follow GCC: `long`, `unsigned int`, `int`, `int`.
- **`__mspabi_cmpd`/`cmpf`.** clang compares floating point through these, which return
  a negative, zero or positive `int` and 1 when unordered. It tests that result against
  zero for every comparison, so its `>` and `>=` come out true for a NaN. GCC's code and
  ours call the libgcc predicates (`__ltdf2` and so on), which answer every comparison
  right.

## The runtime library

In `libc/msp430/`:

- `crt0.S` — start-up code:
  - it stops the watchdog and sets SP to the top of RAM;
  - it copies `.data` from ROM and clears `.bss`;
  - it sets a stack canary between `.bss` and the heap;
  - it calls `main` and passes its result to `exit`;
  - every vector but reset goes to `__bad_interrupt`, which reports and stops with
    status 0xfe.

  It also defines `__crt0_call_exit`. GCC's `main` and ours refer to it with `.refsym`, so
  that newlib's modular startup passes `main`'s result to `exit`.
- `console.s` — `putbyte` polls `IFG2` and writes `UCA0TXBUF`. `exit` writes the status
  to the stop register. A stack that ran into the canary is reported first, as status
  0xfd.
- `link.ld` — the memory map above. The heap follows `.bss` and the stack grows down
  from the top of RAM; the link fails when less than 2 KB is left for the stack.
- `mul.s`, `divmod.s`, `shift.s` — integer multiply, divide and shifts under their EABI
  names, as ordinary calls clobbering at most `r11`–`r15`:

  | Helper | In | Out | Clobbers |
  | --- | --- | --- | --- |
  | `__mspabi_mpyi` | `r12` × `r13` | `r12` | `r13`, `r14` |
  | `__mspabi_mpyl` | `r12:r13` × `r14:r15` | `r12:r13` | `r11`, `r14`, `r15` |
  | `__mspabi_divi`, `divu` | `r12` / `r13` | `r12`, remainder `r14` | `r11`, `r13`, `r15` |
  | `__mspabi_remi`, `remu` | `r12` % `r13` | `r12` | `r11`, `r13`–`r15` |
  | `__mspabi_divli`, `divul` | `r12:r13` / `r14:r15` | `r12:r13`, remainder `r14:r15` | `r11` |
  | `__mspabi_remli`, `remul` | `r12:r13` % `r14:r15` | `r12:r13` | `r11`, `r14`, `r15` |
  | `__mspabi_slll`, `srll`, `sral` | `r12:r13` by `r14` | `r12:r13` | `r14` |
  | `__mspabi_slli`, `srli`, `srai` | `r12` by `r13` | `r12` | `r13` |

  (`divu` and `remu` leave `r11` alone.) `shift.s` has the complete groups of libgcc's `slli.o`,
  `srai.o` and `srli.o`, including the fixed-count entries `__mspabi_slli_N` and the
  `long long` shifts. It also has the shared epilogues `__mspabi_func_epilog_N` that
  GCC's functions may end with. A libgcc shift object would otherwise define our names a
  second time.
- `mspabi64.s` — **the special contract.** The helpers with two 64-bit operands take the
  first in `r8`–`r11` (`r11` high) and the second in `r12`–`r15`, and return in
  `r12`–`r15`. The caller saves `r8`–`r10` itself, because it loads them. These are
  `__mspabi_mpyll`, `divlli`, `divull`, `remlli`, `remull`, `addd`, `subd`, `mpyd`,
  `divd` and `cmpd`. Each one moves the operands to the ordinary ABI and calls the
  libgcc-named routine (`__muldi3`, `__adddf3`, …). Our code, GCC's and clang's all
  call them this way.
- `mspabif.c`, `mspabid.c` — the other EABI names for binary32 and binary64, with the
  ordinary ABI: arithmetic, `cvtfd`/`cvtdf`, and the conversions to and from integers.
  They are apart, so that a program takes in only the runtime it uses.
- `setjmp.s` — `setjmp`/`longjmp`. The buffer is nine words: `r4`–`r10`, SP after the
  return, and the return address.
- `malloc.c` — a bump allocator from the end of `.bss` up to below SP. A block that
  shrinks stays in place; `free` does nothing.
- C library: `printf` and the string and math functions from `libc/common/`; the soft
  binary32 `libc/common/float32.c` and binary64 `libc/common/float64.c`; and the
  `long long` division and conversions of `libc/ilp32/int64.c` and `int64conv.c`, with
  `frexp`, `ldexp` and `modf`. All of it is compiled by `genmsp430` itself.
  - **The soft binary64** is correctly rounded in every operation, `sqrt` included,
    and is checked bit for bit against the host's `double`. `sqrtf` is checked against
    the host over all 2^32 inputs, by a test run by hand
    (`Float32Host.DISABLED_SqrtfEveryInput`).
  - **The FP comparisons** our code makes call the libgcc predicates (`__eqdf2`,
    `__ltdf2`, …), which are right for NaN.
- Headers: `libc/msp430/include` holds MSP430's own headers (`float.h`, `limits.h`,
  `math.h`, `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h`). `libc/ip16/include` holds
  `inttypes.h` of the 16-bit data model, and `libc/common/include` the target-neutral
  ones. See [libc/msp430/include/README.md](../libc/msp430/include/README.md).

**GCC's `libgcc.a`** follows our `libc.a` in a link. Our code needs nothing from it, but
GCC's code may call helpers ours lacks (`__clzhi2` for `__builtin_clz`). Every helper of
ours is also checked against libgcc's, bit for bit.

### Sections and `--gc-sections`

`genmsp430` gives every function and variable a section of its own (`.text.f`,
`.data.x`, `.bss.x`, `.rodata.x`), as GCC's `-ffunction-sections -fdata-sections`
does, and every link uses `--gc-sections`. Whole-object linking pulled in one member
per source file, and with it the binary64 core, so `printf("%d")` was 53 KB.

### Costs against GCC

A whole program, `main` with one `printf`, in mspsim cycles and bytes of code. GCC's
is `-O1` with newlib-nano.

| | ours | GCC / newlib-nano |
| --- | --- | --- |
| `printf("%d\n")` | 18 453 cycles, 23 053 bytes | 6 000 cycles, 9.3 KB |
| `printf("%g\n")` | 78 079 cycles, 23 105 bytes | — (no float formats) |

Every `printf` links the binary64 core, since `doprnt` has one engine. Integer `printf`
is slower because `doprnt` formats every integer as a `long long`. A newlib-style split,
with float formatting in an object of its own, would shrink integer-only programs.

Against GCC and clang at `-O2`, in bytes of code:

| | ours | GCC `-O2` | clang `-O2` |
| --- | --- | --- | --- |
| the C library, its 39 C sources | 35 012 | 38 016 | 40 142 |
| 677 book programs | 195 794 | 99 942 | 85 366 |

On the benchmarks in `bench/msp430/`, in mspsim cycles and bytes of `.text`, both
linked with our runtime (`scripts/bench_msp430.sh` builds and runs them):

| | ours | GCC `-O2` |
| --- | --- | --- |
| bubble sort, 64 `int`s (`sort.c`) | 32 686 / 312 | 32 845 / 284 |
| sieve to 2000 (`sieve.c`) | 111 848 / 246 | 92 700 / 320 |
| CRC-16, 1 KB (`crc16.c`) | 149 259 / 272 | 199 259 / 572 |
| string copy and compare (`strings.c`) | 125 817 / 600 | 84 739 / 1 090 |

Sort was 66 073 cycles before these changes: the inline constant multiply (the LCG
that fills the array called `__mspabi_mpyi` 64 times), loops tested at their bottom,
and induction-variable strength reduction, which steps a pointer through the array
in place of the index (see [TAC_Optimization.md](TAC_Optimization.md)). The pointer is
stepped in place between the two loads, which the peephole pass makes
`mov @r8+, r11` (a load through a register followed by the add of its size, the flags
dead), and the swap stores behind it at `-2(r8)`: 10 cycles a pass, as GCC. The inner
loop's end pointer steps down by `decd` once per outer pass (a pointer less 2, 4 or
8 is a `sub`, whose constant the generator has), and both counters are gone. What GCC
still saves is the call, by inlining `sort`, and the inner loop's guard, which it
proves true.

The book programs are twice GCC's, because `-O2` folds and inlines most of them whole.
The book figures were measured before the dead frame stores went, which took 3.8% off
the library.

## Running a program by hand

You need the GNU MSP430 toolchain (`msp430-elf-as`, `-ld`, `-ar`, and
`msp430-elf-gcc` for its `libgcc.a`) and mspsim, all on `PATH`.

After `make install`, which installs into `~/.local`, the driver does it all:

```sh
vcc -t msp430 -o hello.elf hello.c
mspsim hello.elf
```

mspsim prints the UART output and exits with `main`'s result, confirmed by an
`[Exit code N after M cycles]` line. Its options:
- `-q` drops the banner and that line;
- `-n N` stops after N cycles, with status 124;
- `-t` traces every instruction, register change, load and store (`-o FILE` writes the
  trace to a file);
- `-g` starts paused in the interactive debugger, and Ctrl-] returns to it.

An illegal instruction gives status 132. `msp430-elf-objcopy -O ihex hello.elf
hello.hex` makes the Intel HEX image a flasher takes, and `mspsim hello.hex` runs that
too.

`vcc -v` prints each step's command. By hand, the same steps are:

```sh
P=~/.local
vcpp -t msp430 -nostdinc -I$P/share/vcc/msp430/include hello.c hello.i  # preprocess
vparse hello.i hello.ast                                             # parse
vlower -t msp430 hello.ast hello.tac                                 # check and lower
vgenmsp430 hello.tac hello.s                                         # generate assembly
msp430-elf-as -mcpu=msp430 -o hello.o hello.s
L=$P/share/vcc/msp430/lib
msp430-elf-ld --gc-sections -T $L/link.ld -o hello.elf $L/crt0.o hello.o $L/libc.a \
    $(msp430-elf-gcc -mcpu=msp430 -print-libgcc-file-name)
```

With clang's assembler and `ld.lld` instead:
`VCC_AS="clang --target=msp430 -c" VCC_LD="ld.lld -n" vcc -t msp430 hello.c`. Without
`-n`, `ld.lld` loads the ELF headers at address 0, over the peripherals.

Without installing, use `build/parse`, `build/lower`, `build/backend/genmsp430`, the
headers in `libc/msp430/include`, `libc/ip16/include` and `libc/common/include`, and the
library in `build/libc/msp430/`.

mspsim runs about 50 M cycles a second.

## Things found on the way

- **mspsim had a jump bug:** every forward jump of 256–511 words went backwards. It was
  fixed in mspsim, with a test.
- **mspsim now serves newlib's I/O,** in both forms `libsim.a` uses: TI's CIO
  breakpoint, and the GDB simulator's syscalls at 0x0180 + N. So a program built by
  `msp430-elf-gcc -msim` runs unchanged, which made GCC a full oracle.
- **The two assemblers differ on `0(rN)`.** GNU `as` shortens a source `0(rN)` to
  `@rN`, and clang's does not. So the size model follows GNU `as`, and prints `@rN`.
- **clang's assembler rejects some ISA forms:** `@rN+` with a memory destination, `push`
  of memory, `pop` to memory, and `br @rN`. Selection does not emit them.
- **newlib's modular startup** calls `exit` only when `main` refers to
  `__crt0_call_exit`, hence the `.refsym`.
- **`printf` was 53 KB** until every function got a section of its own.
- **`%.17g` is not exact.** The shared `doprnt` generates digits by `modf` in binary64,
  so digits past `DBL_DIG` need not be right: `%.17g` of 0.1 prints `0.1`. This holds on
  every target; an exact conversion needs multiword arithmetic.
- **`int64.c`'s conversions moved to `int64conv.c`,** so that dividing a `long long` no
  longer links the float runtime. AVR, ARM32 and RV32 gain too.

## Tests

`build/backend/msp430/msp430-tests` checks the generated assembly and runs programs on
mspsim:
- **Runs:** integer and floating-point arithmetic, comparisons and conversions, branch
  relaxation at the limits, calls, structures by reference, variadic functions, the
  libc, `setjmp`, register allocation and each peephole rule.
- **The runtime:** the soft binary32 and binary64, against the host bit for bit.
- **Against GCC:**
  - our code is linked with GCC's both ways, over a table of signatures;
  - `r4`–`r10` must survive our calls;
  - GCC's code runs on our runtime;
  - every helper of ours is checked against libgcc's;
  - the `str`, `mem` and integer `printf` cases run again, built by GCC with newlib;
  - every book program's output and exit status is compared with GCC's build.
- **Against clang:** linked both ways, structures aside, when clang is present.
- **Headers:** compared with GCC's and clang's own.

Programs that cannot run on the MSP430 are skipped with the reason in
`test/book_test.h`: undefined behavior with a 16-bit `int`, too big for the RAM, or too
slow. The instruction-selection goldens run with `--no-regalloc --no-peephole`. Tests
that need mspsim, GCC or clang are skipped when the tools are missing. The
`msp430-headers` CTests check that every header preprocesses and parses.
