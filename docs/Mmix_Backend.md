# The MMIX backend

`genmmix` turns the compiler's intermediate code (TAC) into assembly for Donald Knuth's
MMIX, the 64-bit RISC of *The Art of Computer Programming*. The code follows the
**MMIXware ABI as GCC implements it**, so it calls, and is called by, code compiled with
`mmix-knuth-mmixware-gcc`, and it links with GCC's `libgcc.a` and newlib. GNU binutils
assemble and link it into Knuth's `.mmo` object format. Programs run under Knuth's own
simulator, `mmix`, from MMIXware.

## The target

- **CPU:** MMIX as MMIXware specifies it, in user mode: 256 general registers of 64
  bits, a register stack, 32 special registers. No privileged instructions, traps or
  virtual memory.
- **Data model:** LP64. `char` 1 byte and **signed**, `short` 2, `int` 4, `long` and
  `long long` 8, pointers 8. `size_t` is `unsigned long`, `ptrdiff_t` is `long`,
  `wchar_t` is `int`. Natural alignment, at most 8.
- **Big-endian.** It is the first big-endian target here that addresses bytes (BESM-6
  addresses words).
- **Floating point:** `float` is binary32, `double` and `long double` binary64. All of
  it is in hardware, and all arithmetic is binary64: `float` exists only in memory
  (`ldsf` converts on load, `stsf` rounds on store).
- **No runtime helpers.** 64-bit multiply and divide, and binary64 arithmetic,
  comparison, conversion and square root, are all instructions. The runtime is only
  startup, I/O, `setjmp`/`longjmp` and the C library.
- **Output:** a `.s` file in GNU `as` syntax with lowercase mnemonics (`addu`, `pushj`,
  `ldo`), `name:` labels, `L:n` local labels and `$n` registers. It is assembled by
  `mmix-knuth-mmixware-as -x -no-predefined-syms` and linked by
  `mmix-knuth-mmixware-ld`, the flags GCC passes. There is no clang: LLVM has no MMIX
  target.
- **Machine:** Knuth's `mmix`.
  - **Memory:** text from 0x100, data from `Data_Segment` 0x2000000000000000, the heap
    above `_end`, the stack from 0x6000000000000000 down. The linker's default script
    lays it out; there is no script of ours and no size limit.
  - **Output:** stdout through the simulator's `Fwrite` call; stdin through `Fread`.
  - **Exit:** `exit` puts `main`'s result in `$255` and halts with `trap 0, Halt, 0`; `mmix`
    exits with it as its status.

Not supported: GCC's `-mabi=gnu` (arguments in global registers), privileged MMIX,
intrinsics for instructions C has no word for (`mor`, `sadd`, `bdif`…), `_Complex`,
atomics and bit-fields.

## How code is generated

For each function, in this order (`codegen.c`):

1. **Register allocation** (`regalloc.c`, on the shared `backend/common/regalloc.c`).
   Every scalar takes one register, `char` to `long`, pointers, `float` and `double`
   alike: there are no register pairs and no FP file. Aggregates, variables whose
   address is taken and `volatile`s stay in memory.
2. **The frame layout** (`frame.c`).
3. **Instruction selection** (`instr.c`, `fp.c`, `call.c`), with the fusions below.
4. **The prologue and epilogue** (`frame.c`).
5. **Peephole** (`peephole.c`), over the whole function.

There is no branch relaxation: with `-x`, the assembler and linker expand a branch,
`geta`, `pushj` or `jmp` that does not reach.

To see the code without an optimization, add `--no-regalloc` or `--no-peephole` to
`genmmix`. With both, every variable lives in its frame slot and each TAC instruction is
selected on its own.

### The register stack

`pushj $X, f` renames the register file. The callee's `$0` is the caller's `$(X+1)`; the
caller's `$0`…`$(X−1)` are hidden and come back intact after `pop`; `$X` receives the
result. So there are no callee-saved registers to save: a value survives a call by
living **below** the call's `$X`, at no cost (the hardware spills the register ring to
memory only when it fills).

```
f:                          ; long f(long a, long b) { return g(a) + b; }
    get     $2, rJ          ; the return address, saved in a local
    set     $4, $0          ; a: the first argument, just above the hole
    pushj   $3, g           ; $3 is the hole; b stays in $1, below it
    addu    $0, $3, $1      ; g's result came back in $3
    put     rJ, $2
    pop     1, 0            ; return $0
```

### The fixed model and the compaction

The allocator works in GCC's fixed model:
- `$0`–`$13` hold values live across a call, parameters first where they arrive;
- `$14` holds `rJ` in a function that makes calls;
- `$15` is the hole, the `pushj` operand and so a call's result;
- `$16`–`$31` hold the arguments of a call and the values live across none.

The pool is `$16`–`$31`, then the hole, then `$0`–`$13`. Hints put a parameter in its
incoming `$i`, call argument `i` in `$(16+i)`, a call's result in the hole and a
returned value in `$0`.

After allocation a **compaction** renumbers the upper range down to just above the
highest of `$0`–`$13` in use, `P`: `$14` becomes `$P`, `$15` `$(P+1)`, and so on; in a
leaf, which keeps no `rJ`, the hole becomes `$P`. That mapping is one uniform shift, so
it holds across the whole function (`phys_reg`), and the allocator runs before
selection, which emits the final registers directly. This is what GCC does at output,
and it keeps the register frames small: `rG` is 32, so locals end at `$31`.

Arguments into place before a call, and parameters into their registers in the
prologue, are each one parallel move; a cycle is broken through `$250`. A move may also
extend a value to a parameter's type, or convert a `float` between binary64 and the
binary32 bits the ABI passes.

**Scratch registers.** Selection uses `$248`–`$250` for operands and constants and `$255`
for addresses and offsets. They are global registers that `crt0` reserves and GCC
treats as call-clobbered, so they are never allocated and never live across a call.

**The width invariant.** A register holds its value extended to 64 bits, sign-extended
if signed, zero-extended if unsigned, so a comparison, a division or a conversion reads
it as it is. The exception, with the peephole optimizations on: a signed `int` or `long`
`+`, `-` or `*` result is left as it is, since its overflow is undefined, as GCC does. A
`char` or `short` result, a conversion back from `int`, is still extended.

### Selection

- **8-bit immediates.** Every immediate operand is 8 bits unsigned: arithmetic,
  comparisons and memory offsets alike. A wider constant goes through a scratch
  register, built by `setl`, `incml`, `incmh` and `inch` (four instructions for a full
  64 bits). A commutative operation takes its constant second; a constant from −255 to
  −1 added is subtracted.
- **Compare and branch.** A comparison (or `!x`) whose only use is the next conditional
  branch becomes a `cmp` and a branch on its sign, or a branch on the value against zero,
  with no 0/1 in between. A backward branch is the predicted-taken `pb*` form.
- **Addresses.** A pointer sum only a load or store reads becomes its address:
  `ldo $x, $p, $i`, or `ldo $x, $p, k` for an offset of 0 to 255. Scaling by 2, 4, 8 or 16 is
  `2addu`…`16addu`.
- **Conditional sets.** `x = c ? a : 0` and the like become `zs*` and `cs*`.
- **Tail calls.** A call whose result is returned is `put rJ` and `jmp f`, when the
  function has no frame, every argument fits a register and the result types agree.
  `f` then runs in our register frame, and its `pop` returns to our caller. With no
  `pushj` left, `rJ` is neither saved nor restored.

```
sum:                        ; for (i = 0; i < n; i++) s += p[i];
    setl    $4, #0
    8addu   $5, $1, $0      ; the end pointer
    bnp     $1, L:L0
L:5:
    ldo     $3, $0, 0
    addu    $4, $4, $3
    addu    $0, $0, 8
    cmpu    $248, $0, $5
    pbn     $248, L:5
L:L0:
    set     $0, $4
    pop     1, 0
```

The pointer stepped through the array and the end pointer come from the TAC
optimizer's strength reduction (see [TAC_Optimization.md](TAC_Optimization.md)).

### Signed division

MMIX's signed `div` rounds the quotient toward −∞; C truncates toward zero. So a signed
`/` or `%` is `div` and a fix-up: when the remainder is nonzero and the operands' signs
differ, the quotient goes one up and the remainder one divisor down. Six instructions
either way:

```
q:                          ; long q(long a, long b) { return a / b; }
    div     $250, $0, $1
    get     $255, rR        ; the remainder
    xor     $248, $0, $1
    zsn     $248, $248, 1   ; 1 if the signs differ ...
    csz     $248, $255, 0   ; ... and the remainder is not 0
    addu    $0, $250, $248
    pop     1, 0
```

GCC divides absolute values with `divu` and fixes the signs, in nine. `LONG_MIN / -1`
gives `LONG_MIN` and 0 in both. Unsigned division is `divu`, which divides the 128-bit
`rD:$Y`: the ABI assumes `rD` is 0 everywhere, so no code may change it, and `crt0`
checks it at startup.

Signed `+`, `-`, `*` and `<<` are `addu`, `subu`, `mulu` and `slu`: the signed forms only
add overflow bits in `rA`.

### `float` held as binary64

A `float` in a register is its exact binary64 value. Each operation is done in binary64
and then rounded to binary32 by a store and load through the slot `%.fround` (`stsf`,
`ldsf`), so the result is the correctly rounded `float`. An integer goes to `float` by
`sflot`/`sflotu`, which round once to binary32; `flot` and then `stsf` would round
twice.

At the ABI boundary a `float` travels as its binary32 bits in the low 32 bits of the
register, so the callee converts it on entry (`sttu`, `ldsf`) and its result on return
(`stsf`, `ldt`):

```
avg:                        ; float avg(float a, float b) { return (a + b) / 2; }
    subu    $254, $254, 8
    sttu    $0, $254, 0
    ldsf    $0, $254, 0
    sttu    $1, $254, 0
    ldsf    $1, $254, 0
    fadd    $3, $0, $1
    stsf    $3, $254, 0     ; round a + b to float
    ldsf    $3, $254, 0
    seth    $249, #4000     ; 2.0
    fdiv    $0, $3, $249
    stsf    $0, $254, 0
    ldt     $0, $254, 0     ; the binary32 bits, sign-extended
    addu    $254, $254, 8
    pop     1, 0
```

The peephole pass drops a store and load through `%.fround` that rounds a value already
rounded. FP comparisons are `fcmp` and a branch, and `feql` for equality; `a <= b` also
checks `feql`, since `fcmp` gives 0 for an unordered pair.

### Linker-allocated base registers

A global is addressed by name: `ldo $x, g`, `lda $x, g`, `sto $x, g`. The assembler turns
this into `ldo $x, $b, k` with an `R_MMIX_BASE_PLUS_OFFSET` relocation, and the linker
allocates the global register `$b` that holds a base near `g`, placing its value in
`.MMIX.reg_contents`. `crt0` reserves `$247`–`$254` in that section, as GCC's `crtn.o`
does, so the bases come from `$246` down. Each distinct 256-byte window of data that
code addresses takes one register, 215 at most here. Past that the link fails with "too
many global registers"; GCC has the same limit.

Code is addressed with `geta`, which reaches only multiples of 4, so every string
literal in `.rodata` is aligned to 4 as GCC aligns it; otherwise the link fails with
"relocation truncated to fit: R_MMIX_GETA".

### The peephole pass

It runs over the function's MMIX IR to a fixed point.
- **Results in place.** A result computed in a scratch register and then moved is
  computed in its destination.
- **Liveness,** over `$0`–`$31` and the scratch registers, with a branch target's
  live-in added at the branch:
  - a pure instruction whose result is dead goes;
  - a read of `t` after `set t, x` reads `x`;
  - `set t, p; addu p, p, k` and a load or store through `t` becomes the access through `p`
    and then the `addu`.
- **Overwritten results.** An instruction whose result is overwritten unread goes.
- **`float`.** A redundant `stsf`/`ldsf` pair through `%.fround` goes.
- **Jumps.** Dead code after `jmp` or `pop` goes, a jump to the next instruction goes, a
  branch over a jump becomes the inverse branch, and a jump to a lone `pop` (or `set`
  and `pop`) becomes the epilogue itself.
- **Conditional sets** of two shapes become `cs*` and `zs*`.
- **`rJ`.** When no call is left, its save and restore go.

## Stack frame

The frame is addressed from `$254`, the stack pointer; there is no frame pointer. Only
values the registers do not hold live in it.

```
$254 + frame + 8*(i-16)     the 17th argument and later, 8 bytes each
$254 + frame - 8*(16-n)     a variadic function's saved argument registers $n...$15
$254 + ...                  the copies of structure arguments passed to calls
$254 + out ...              slots, each aligned to its type, %.fround among them
$254 + 0 ...                outgoing stack arguments of the calls
```

The prologue is `subu $254, $254, frame` (through `$255` when the frame is over 255
bytes), then `get rJ` in a function that makes calls; the epilogue undoes both and pops.
A slot whose offset is over 255 is reached through `$255`. A function with **no slots**
touches neither `$254` nor memory, and its early returns are `pop` in place:

```
add:
    addu    $0, $0, $1
    pop     1, 0
```

## Function calls

The MMIXware ABI as GCC implements it, checked against GCC's output and by
`interop_tests.cpp` both ways:

- **Fixed registers:** `$254` the stack pointer; `$253` GCC's frame pointer and `$252`
  its static chain (both unused here); `$251` the structure-result address; `$255`
  scratch. `crt0` sets `rG` to 32, so `$32`–`$255` are global and locals end at `$31`.
- **Calls:** `pushj $X, f`, or `pushgo $X, $249, 0` through a pointer. The result comes back
  in `$X`; `$0`…`$(X−1)` are preserved and everything above `$X` is lost.
- **Return:** `pop 1, 0` returns `$0`, `pop 0, 0` nothing.
- **Arguments:** up to **16** in `$(X+1)`…, each scalar in one register, arriving as the
  callee's `$0`, `$1`, …. Arguments 17 and up go on the stack at `0($254)`, `8($254)`, …
  of the caller, 8 bytes each, a narrow value in its low-order (last) bytes.
- **Narrow values.** The caller extends a narrow integer argument to 64 bits, and GCC's
  callee trusts it. GCC's callee leaves a narrow result unextended and its caller
  extends it. We extend on both sides.
- **`float`** travels as its binary32 bits (above); `double` as itself.

### Structures

- **8 bytes or less:** in one register, **right-justified**, as the big-endian integer
  of its bytes: `struct {char a,b,c;}` = {1,2,3} is `0x010203`, and `struct {int a,b;}`
  = {1,2} is `0x00000001_00000002`. The callee stores it into its slot.
- **More than 8 bytes:** **by reference.** GCC's caller passes the address of its own
  object, uncopied, as an ordinary argument, and GCC's callee copies the object before it
  writes it. Our callee copies every such parameter at the start of its body. Our caller
  passes the address of a copy, which keeps an argument apart from the result in `x =
  f(x)`.
- **Results of every size,** even 1 byte, go through the address the caller puts in the
  global `$251` before the `pushj`. The callee pops no value (`pop 0, 0`).

### Variadic functions

The caller passes variable arguments exactly as named ones: registers first, then the
stack. The callee stores the registers after its named ones, `$n`…`$15`, at the top of
its frame, directly below the incoming stack arguments, so every variable argument is
then one contiguous run of 8-byte slots. `va(int n, ...)` stores `$1`–`$15` at
`0($254)`…`112($254)` of a 120-byte frame.

`va_list` is a plain `char *` (`libc/mmix/include/stdarg.h`), and `va_arg` reads a value
from the low-order bytes of its slot: an `int` at offset 4, a 3-byte structure at 5, a
large structure as its address. `va_start` is `__va_start(&ap)`, which `genmmix` expands
in place rather than calling, so a variadic leaf stays a leaf.

## Big-endian notes

- **Shared code** had no hidden little-endian assumption: the front end, the TAC
  optimizer's folding and the static initializers all work in values, not bytes.
  Constant folding needed one fix, to read an unsigned constant cast to a signed type at
  the target's width.
- **A truncation from memory** loads the low-order bytes, at offset `size(src) −
  size(dst)`, not at offset 0.
- **Small structures** are right-justified in their register, and a narrow value sits at
  the end of its 8-byte argument or `va_arg` slot.
- **The book programs** that read an `int`'s or a union's bytes expect little-endian
  output. They are skipped in the shared suite and run in big-endian versions in
  `book_mmix_tests.cpp`. Those that expect an unsigned plain `char` run in signed-`char`
  versions shared with x86-64, `backend/common/test/book/signed_char_tests.cpp`.

## The runtime library

In `libc/mmix/`:

- `crt0.S` — start-up code. The `.mmo` loader enters at `Main` with argc in `$0` and argv
  in `$1`. `Main` sets `rG` to 32, checks that `rD` is 0 (else it halts with status
  0xfe), loads `$254` from `__Stack_start`, calls `main` and passes its result to `exit`.
  It reserves `$247`–`$254` in `.MMIX.reg_contents`. Built with `-DPRINT_STATUS` (as
  `crt0-status.o`), it prints `main`'s result first, for the book tests.
- `console.s` — `putbyte` buffers stdout, 1 KB at a time, and `flush` writes it with
  `trap 0, Fwrite, StdOut`; `getch` reads with `Fread` from StdIn. `exit` flushes,
  reports `[exit N]` on stderr and halts with `$255` = N. The report tells a real exit
  from a jump into zeroed memory, which executes `trap 0, 0, 0` too and so looks like
  `exit(0)`.
- `setjmp.s` — `setjmp`/`longjmp`, newlib's `libc/sys/mmixware/setjmp.S` (its notice
  kept). `jmp_buf` is five `unsigned long`s, the layout of GCC's built-in. `longjmp` pops
  register-stack frames until `rO` is back to the saved one, so it unwinds through
  GCC's frames too.
- `sqrt.s` — `sqrt` and `sqrtf` by `fsqrt`, for a call through a pointer and for GCC's
  code; our direct call is the instruction itself.
- `malloc.c` — a bump allocator from `_end`, 16-aligned blocks as newlib's are.
- C library: `printf` and the string and math functions from `libc/common/`, with
  `frexp`, `ldexp` and `modf` from `libc/lp64/`. All of it is compiled by `genmmix`
  itself. There are no arithmetic helpers.
- Headers: `libc/mmix/include` holds MMIX's own (`float.h`, `limits.h`, `setjmp.h`,
  `stdarg.h`, `stddef.h`, `stdint.h`), `libc/lp64/include` those of the LP64 data model,
  and `libc/common/include` the target-neutral ones. See
  [libc/mmix/include/README.md](../libc/mmix/include/README.md).

**GCC's `libgcc.a`** follows our `libc.a` in a link. Our code needs nothing from it, but
GCC's code may call helpers ours lacks (`__clzdi2` for `__builtin_clzl`).

## Costs against GCC

On the benchmarks in `bench/mmix/`, in instructions / υ (Knuth's "oops", time) / μ
("mems", memory accesses) under `mmix -s`, less those of an empty `main`
(`scripts/bench_mmix.sh` builds and runs them). GCC's kernels are linked with our
runtime; its `printf` is newlib's.

| | naive | registers allocated | now | GCC `-O2` |
| --- | --- | --- | --- | --- |
| `fib` | 1 404 179 / 1 576 117 / 716 413 | 544 480 / 716 418 / 0 | 458 507 / 630 447 / 0 | 363 764 / 405 294 / 83 325 |
| `sum` | 2 280 157 / 2 500 153 / 1 730 103 | 680 083 / 900 079 / 110 000 | 550 050 / 550 074 / 110 000 | 580 082 / 580 106 / 110 000 |
| `sort` | 1 646 247 / 1 801 939 / 1 186 197 | 780 145 / 935 837 / 136 500 | 505 651 / 571 053 / 136 500 | 594 305 / 656 409 / 226 201 |
| `dot` | 1 605 181 / 2 045 177 / 1 205 126 | 520 096 / 960 092 / 110 000 | 455 063 / 785 087 / 110 000 | 415 084 / 745 108 / 110 000 |
| `copy` | 3 180 061 / 3 989 976 / 2 520 026 | 1 250 014 / 2 059 929 / 209 999 | 760 020 / 1 349 985 / 209 999 | 700 044 / 1 290 011 / 209 991 |
| `printf("%d")` | 1 733 / 2 755 / 818 | 707 / 1 729 / 117 | 588 / 1 588 / 117 | newlib's: 2 103 / 2 913 / 372 |
| `printf("%g")` | 2 777 / 3 244 / 1 260 | 1 213 / 1 680 / 143 | 1 005 / 1 452 / 143 | newlib's: 3 123 / 3 812 / 577 |

- Ours runs fewer instructions than GCC's on `sum` and `sort`, and is within 10% on
  `copy` and `dot`. On `fib` GCC unrolls the recursion.
- What is left:
  - `copy` spends most of its difference on `i % 26`, which GCC multiplies by a magic
    constant;
  - `dot` sets two constants inside its loop, which needs loop-invariant code motion;
  - `fib` saves `rJ` before its base case, which needs shrink-wrapping.
- **Code size** (`.text`): our C library and the benchmarks are 10 304 bytes, GCC
  `-O2`'s 18 444. `doprnt` is 6 708 against 13 964, since GCC inlines and unrolls more;
  `atoi` (456 against 236) and the `str*` functions are larger in ours.
- Tail calls took 23% off the instructions and 6% off the υ of a `gcd` and countdown
  benchmark.

## Running a program by hand

You need the GNU MMIX toolchain (`mmix-knuth-mmixware-as`, `-ld`, `-ar`, and
`mmix-knuth-mmixware-gcc` for its `libgcc.a`) and Knuth's `mmix`, all on `PATH`.

After `make install`, which installs into `~/.local`, the driver does it all:

```sh
vcc -t mmix -o hello.mmo hello.c
mmix hello.mmo
```

`mmix` prints the program's output and exits with `main`'s result, which our `exit`
also reports as `[exit N]` on stderr. Its options:
- `-q` drops the simulator's own messages;
- `-s` prints the instruction, mem and oop counts at the end (and after each traced
  instruction);
- `-t N` traces each instruction the first N times it runs;
- `-r` shows the register stack's hidden work: what `pushj`, `pop` and the ring spill;
- `-i` runs interactively, prompting for commands (step, show registers and memory);
- `-f FILE` feeds stdin from a file.

`mmix` runs about 14 M instructions a second, and has no instruction limit.

`vcc -v` prints each step's command. By hand, the same steps are:

```sh
P=~/.local
vcpp -t mmix -nostdinc -I$P/share/vcc/mmix/include hello.c hello.i   # preprocess
vparse hello.i hello.ast                                           # parse
vlower -t mmix hello.ast hello.tac                                 # check and lower
vgenmmix hello.tac hello.s                                         # generate assembly
mmix-knuth-mmixware-as -x -no-predefined-syms -o hello.o hello.s
L=$P/share/vcc/mmix/lib
mmix-knuth-mmixware-ld --defsym=__.MMIX.start..text=0x100 -o hello.mmo $L/crt0.o hello.o \
    $L/libc.a $(mmix-knuth-mmixware-gcc -print-libgcc-file-name)
```

The text must start at 0x100, where the `.mmo` loader expects it. `objdump` cannot read
a `.mmo`; for a disassembly, link the same objects with `--oformat elf64-mmix` as well,
and run `mmix-knuth-mmixware-objdump -d` on that ELF.

Without installing, use `build/parse`, `build/lower`, `build/backend/genmmix`, the
headers in `libc/mmix/include`, `libc/lp64/include` and `libc/common/include`, and the
library in `build/libc/mmix/`.

## Things found on the way

- **GCC's callee copies a large structure,** not its caller. The interop table caught our
  callee writing through to GCC's original.
- **A link from address 0 does not start:** the `.mmo` loader wants the text at 0x100,
  as GCC links it.
- **Without its `.MMIX.reg_contents` reservation,** the linker allocated its first base
  register as `$254`, the stack pointer.
- **`geta` reaches only multiples of 4,** so an unaligned string literal fails the link.
- **`.L1:` is not a label** for the MMIX assembler, whose leading `.` reads as a
  pseudo-op; local labels are `L:1`.
- **A C global named like a special register** (`rJ`, `rA`) is an ordinary symbol, thanks
  to `-no-predefined-syms`.
- **A halt in zeroed memory looks like `exit(0)`,** since `trap 0, 0, 0` is the all-zero
  word. Hence the `[exit N]` report, which the tests require of our runtime.
- **Signed `char` and big-endian programs of the book** needed versions of their own, as
  above.
- **A copy loop cannot sit in the prologue,** whose block it would split; a large
  structure parameter is copied at the start of the body.

## Tests

`build/backend/mmix/mmix-tests` checks the generated assembly and runs programs on `mmix`:
- **Runs:** integer arithmetic (every sign combination of `/` and `%`, at each width),
  floating point (halfway cases of the binary32 rounding against the host), control
  flow, calls (values surviving calls, the register ring spilled by recursion), pointers,
  static data, structures of every size, variadic functions, the libc, `setjmp`,
  register allocation and each peephole rule.
- **Against GCC:**
  - our code is linked with GCC's both ways, over a table of signatures, with a
    `regcheck` harness that checks the registers a call must keep;
  - GCC's code runs on our runtime, and ours under newlib;
  - the libc run cases run again under newlib, but for the formats newlib lacks and
    `strerror`'s messages;
  - every book program's output and exit status is compared with GCC's build;
  - the headers agree with GCC's.

Programs whose expectation does not hold on a big-endian machine with a signed `char`
are skipped with the reason in `test/book_test.h`, and run in versions of their own.
The instruction-selection goldens run with `--no-regalloc --no-peephole`. Tests that need
`mmix` or GCC are skipped when the tools are missing. The `mmix-headers` CTests check
that every header preprocesses and parses.
