# MMIX backend — development plan

An eighth backend, for Donald Knuth's MMIX: a 64-bit, big-endian RISC with 256 general
registers and a register stack. It uses the **MMIXware ABI as GCC implements it**, emits
ELF objects through GNU binutils, links them into Knuth's `.mmo` format, and runs its
programs under Knuth's own simulator **`mmix`** (`/opt/homebrew/bin/mmix`, MMIXware
20160804).
- **The oracle.** The code is link-compatible with `mmix-knuth-mmixware-gcc` (default
  ABI, not `-mabi=gnu`). GCC 16.2, GNU binutils 2.47 and newlib 4.6.0 are installed in
  `~/.local/bin`, built in `~/Project/Mmixware/`. GCC is therefore the test oracle: interop
  runs in both directions, and every book program is compiled by both compilers and the
  outputs compared.
- **The toolchain.** GNU `mmix-knuth-mmixware-as`, `-ld` and `-ar` assemble, link and
  archive. Knuth's `mmixal` cannot link separate objects, so it is not used.
- **No clang.** LLVM has no MMIX target, so unlike every LLVM-era backend there is no
  second oracle and no second assembler.

It follows the shape of the RISC-V backend, the closest relative in ISA terms (a
three-operand load/store RISC with many registers and sp-addressed frames), and of x86-64
in data model (LP64 with a signed plain `char`). Like AVR and MSP430 it has a directory of
its own, a small IR, naive selection first, then register allocation on
`backend/common/regalloc.c` and a peephole pass. What is new here:

- **The first big-endian byte-addressed target.** BESM-6 is big-endian but
  word-addressed. Every hidden little-endian assumption in shared code, in the libc and in
  the tests surfaces here: type punning through unions and `char *`, small structures in
  registers, variadic slots.
- **The register stack.** `pushj $X,f` renames the register file: the callee's `$0` is the
  caller's `$(X+1)`, and the caller's `$0`…`$(X−1)` are hidden and come back intact after
  `pop`. There are no callee-saved registers to save and restore: a value is preserved
  across a call by living *below* the call's `X`. The allocator must model that.
- **One register file** for integers, pointers and both FP formats. There is no FP class.
- **Hardware binary64 only.** `float` has no arithmetic of its own: `ldsf`/`stsf` convert
  on load and store, and arithmetic is binary64. `long double` = `double`.
- **No runtime helpers.** 64-bit multiply and divide and binary64 arithmetic, comparison,
  conversion and square root are all instructions. So the runtime is only startup, I/O,
  `setjmp`/`longjmp` and the C library. No target so far has needed so little.
- **`div` floors.** Signed `div` rounds the quotient toward −∞. C truncates toward zero,
  so every signed `/` and `%` needs a fix-up.
- **Every immediate is 8 bits unsigned.** This covers arithmetic, compare and memory
  offsets alike. A wider constant or frame offset goes through a register; a 64-bit
  constant takes up to four wyde instructions.
- **Linker-allocated global registers.** A global is addressed as `ldo $x,g`. The
  assembler turns this into `ldo $x,$b,k`, where the linker allocates the base register
  `$b` (`R_MMIX_BASE_PLUS_OFFSET`).

As before, an assumption found in shared code is fixed in the shared code, not worked
around in `backend/mmix/`. BESM-6 output must not change, and the output of the other
backends changes only where a step says so.

**Lowercase mnemonics.** All MMIX assembly in this project — `genmmix`'s output, the
runtime's `.s` files, test goldens and documentation — spells instruction mnemonics in
lowercase (`addu`, `pushj`, `ldo`, `fcmp`). GNU `as` accepts them (checked).

`mmix.asdl` and `mmix.md` stay as the reference spec of the instruction set. Check any
detail taken from them against MMIXware (TAOCP Vol. 1 Fascicle 1) before relying on it:
some of their comments are wrong. For example, the backward-predicted branches are
separate opcodes but not separate mnemonics.

Step IDs are stable: a finished step is marked done, never renumbered. The prefix is `K`,
for Knuth: `A`, `R`, `B`, `V`, `X`, `M` and `T` are taken.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| CPU | MMIX as specified in MMIXware: user mode, no privileged instructions | `mmix` simulates exactly this; GCC's `mmix-knuth-mmixware` target generates it |
| Data model | `char` 1 (**signed**), `short` 2, `int` 4, `long` 8, `long long` 8, pointer 8, `size_t` = `unsigned long`, `ptrdiff_t` = `long`, `wchar_t` = `int`, `wint_t` = `unsigned int`, `_Bool` 1; natural alignment, `__BIGGEST_ALIGNMENT__` 8; **big-endian** | GCC's `-dM -E` (checked). `semantic/target.c` already has an `mmix` entry with these sizes, to be completed |
| Floating point | `float` binary32 (load/store only), `double` = `long double` binary64, in hardware; `FLT_EVAL_METHOD` 0 | `__LDBL_MANT_DIG__` 53, `__SIZEOF_LONG_DOUBLE__` 8 (checked) |
| ABI | The MMIXware ABI as GCC 16.2 implements it (details below); not `-mabi=gnu` | Interop with GCC-compiled code, libgcc and newlib |
| Output | GNU `as` syntax with lowercase mnemonics; `name:` labels, `L:n` local labels, `$n` registers, `#` hex immediates; `.text`/`.data`/`.section .rodata`/`.bss`, `.byte`/`.short`/`.long`/`.quad`, `.p2align` | Accepted by `mmix-knuth-mmixware-as` (checked). `.L1:` is *not* accepted: a leading `.` reads as a pseudo-op |
| Toolchain | `mmix-knuth-mmixware-as -x -no-predefined-syms` (the flags GCC passes), `mmix-knuth-mmixware-ld --defsym __.MMIX.start..text=0x100` (as GCC links; from 0 the image does not start), whose default emulation writes `.mmo`; `mmix-knuth-mmixware-ar` | The GNU tools that go with the oracle. `-x` lets the assembler and linker expand out-of-range branches, `geta`, `pushj` and `jmp`, and allocate base registers |
| Startup | Our own `crt0` with `Main`; GCC's `crti.o`/`crtn.o` only when linking against newlib in tests | The `.mmo` loader enters at `Main`, with argc in `$0` and argv in `$1` |
| Run environment | `mmix -q <image.mmo>` | Knuth's reference simulator: about 14 M instructions per second here. Program stdout is the host's stdout. `$255` at `trap 0,0,0` becomes the exit status |
| I/O and exit | `trap 0,Fwrite,StdOut` (6, handle 1) from a buffered `putbyte`; `trap 0,Fread,StdIn` (3, handle 0) for `getch`; `exit` sets `$255` and does `trap 0,Halt,0` (0) | MMIXware's simulator calls, as newlib's `libc/sys/mmixware` uses them. `-no-predefined-syms` means the numbers are written out |
| Memory | Text from 0x100, data from `Data_Segment` 0x2000000000000000, heap above `_end`, the memory stack from `__Stack_start` 0x6000000000000000 down, the register stack's backing store (rS) from there up | The default `ld` script and the simulator's sparse memory. No link script of our own and no size limits, unlike AVR and MSP430 |
| Backend IR | A small hand-written `Mmix_Instr` list, as in `avr_ir.h`. Every instruction is 4 bytes | `mmix.asdl` stays the reference spec. The IR covers only what we emit. Branch relaxation is the assembler's and linker's job (`-x`), so there is no relax pass |
| Executable | `genmmix` (`backend/mmix/`), library `mmix`; installed as `vgenmmix` | Mirrors `genmsp430`/`vgenmsp430` |
| Signed division | `div` and a fix-up: when the remainder is nonzero and the operands' signs differ, the quotient one more and the remainder one divisor less; six instructions either way | GCC's absolute-value sequence takes nine; `LONG_MIN / -1` gives `LONG_MIN` and 0 in both |
| Integer to `float` | `sflot`/`sflotu`, then `stsf` | They round once to binary32 (checked: 2^62 + 2^38 + 1 gives 2^62 + 2^39), where `flot` and `stsf` would round twice |
| Constants in an operation | In the type of the operation that uses them (`load_val_as`); a same-width variable of the other signedness too, and an argument in its parameter's type | A cast between `int` and `unsigned` emits no TAC, so copy propagation leaves the other kind in place; GCC's callee trusts the caller's extension |
| A constant stored to a member | In the type of the scalar member at its offset (`scalar_at`) | A partial initializer zero-fills a pointer with an `int` 0 |
| `va_start` | `__va_start(&ap)` expanded in place, not a call, so a variadic leaf stays a leaf; `va_arg` in `<stdarg.h>` alone, no `__builtin_va_class` | A variable argument is one 8-byte slot in a contiguous run, whatever its type |
| `setjmp`/`longjmp` | newlib's `libc/sys/mmixware/setjmp.S`, ported to `libc/mmix/setjmp.s` (lowercase, the MMIXware-ABI branch only, its notice kept); `jmp_buf` five `unsigned long`s | The layout of GCC's built-in. `longjmp` pops register-stack frames until `rO` is back to the saved one, so it unwinds through GCC's frames too |
| `malloc` | A bump allocator from `_end` (`libc/mmix/malloc.c`), blocks 16-aligned like newlib's | `max_align_t` needs only 8; the shared `malloc` test expects 16 |
| `printf` | The shared `doprnt`; `%f` of a huge value gets its first 17 digits right, the rest print as 0 | The libc run tests run against newlib too, but for its missing C99 formats (`j`, `z`, `t`, `hh`, `%F`) and `strerror`'s messages |
| Code quality (K21–K23) | Allocation in GCC's fixed model; in selection, a comparison or `!x` only a branch reads is a branch (on `cmp`'s sign, or the value against zero), a pointer sum only a load or store reads is its address (`ld $x,$p,$i`, `ld $x,$p,k`), a commutative operation's constant goes second, a constant of −1..−255 added is subtracted; a call whose result is returned is a tail call; a peephole pass over the IR with register liveness | See **Costs against GCC** |
| Tail calls | `jmp f` after `put rJ` and the arguments in `$0`…, when the function has no frame, every argument fits a register and the result types match | Sound under the register stack: `f` runs in our register frame, and its `pop` returns to our caller. 23% fewer instructions and 6% fewer υ on a `gcd` and countdown benchmark. With no `pushj` left, `rJ` is neither saved nor restored |
| Test machine load | `mmix` runs get 25 s each, a ctest 60 s | A book program runs 88 M instructions (7 s, GCC's build as long), which `ctest -j8` stretches past 10 s |

Verified 2026-10-05 on this machine, with scratch programs (not in the tree):

- **GCC** (`mmix-knuth-mmixware-gcc` 16.2.0, binutils 2.47, newlib 4.6.0):
  - **Data model:** the `-dM -E` macros above. It defines `__mmix__`, `__MMIX__`,
    `__MMIX_ABI_MMIXWARE__`, `__LP64__` and `_LP64`, but not `__ELF__` or
    `__CHAR_UNSIGNED__`. The fast types are `int` for 8, 16 and 32 bits and `long` for 64.
  - **A newlib program runs:** `malloc`, `strcpy`, `printf` with `%f` and `%lld`, and
    `sqrt`, under both ABIs, with the exit status reaching the host.
  - **Assembler:** GCC passes `-no-predefined-syms -x` to `as`. A hand-written file in
    our intended syntax assembles, including:
    - lowercase mnemonics, `f:` labels and `L:1` local labels;
    - `x$1` and `cnt.1` symbols, `.long` and `.quad`;
    - `lda $4,g`, `ldt $5,g` and `sto $0,cnt.1`, which become base-plus-offset forms with
      `R_MMIX_BASE_PLUS_OFFSET` relocations.
  - **Linking:** the default emulation is `mmo`, so a link writes a runnable `.mmo`.
    `--oformat elf64-mmix` gives an ELF for `objdump`. newlib's `printf` program
    allocates 19 global registers in `.MMIX.reg_contents`.
  - **Startup:** GCC's `crti.S` does the following:
    - `put rG,32`, so globals are `$32`–`$255` and **locals are `$0`–`$31` at most**;
    - loads `$254` from `__Stack_start`;
    - calls `_init`, then `main` with argc and argv;
    - `jmp exit`.

    newlib's `_exit` is `set $255,status; trap 0,0,0`. Its comment warns that a jump into
    zeroed memory executes `trap 0,0,0` too, and so looks like `exit(0)` if `$255` is 0.
- **mmix:**
  - **Speed:** a 10 M-iteration `volatile long` loop runs 60 M instructions in 4.3 s.
  - **Statistics:** `-s` prints the instruction, `mems` and `oops` counts at the end. Knuth's
    cost units are υ ("oops") for time and μ ("mems") for memory accesses.
  - **No instruction limit:** the wall-clock timeout is the only timeout.
  - **Options:** `-f` feeds stdin from a file; `-t`, `-r` and `-i` trace, show the
    register stack, and run interactively.

### Costs against GCC

`scripts/bench_mmix.sh` (`bench/mmix/*.c`): instructions / υ / μ under `mmix -s`, less
those of an empty `main`; GCC's kernels on our runtime, its `printf` newlib's.

| Bench | Naive | Allocated (K21) | Now (K23) | GCC `-O2` |
|---|---|---|---|---|
| `fib` | 1404179 / 1576117 / 716413 | 544480 / 716418 / 0 | 458507 / 630447 / 0 | 363764 / 405294 / 83325 |
| `sum` | 2280157 / 2500153 / 1730103 | 680083 / 900079 / 110000 | 550050 / 550074 / 110000 | 580082 / 580106 / 110000 |
| `sort` | 1646247 / 1801939 / 1186197 | 780145 / 935837 / 136500 | 505651 / 571053 / 136500 | 594305 / 656409 / 226201 |
| `dot` | 1605181 / 2045177 / 1205126 | 520096 / 960092 / 110000 | 455063 / 785087 / 110000 | 415084 / 745108 / 110000 |
| `copy` | 3180061 / 3989976 / 2520026 | 1250014 / 2059929 / 209999 | 760020 / 1349985 / 209999 | 700044 / 1290011 / 209991 |
| `printf("%d")` | 1733 / 2755 / 818 | 707 / 1729 / 117 | 588 / 1588 / 117 | newlib's: 2103 / 2913 / 372 |
| `printf("%g")` | 2777 / 3244 / 1260 | 1213 / 1680 / 143 | 1005 / 1452 / 143 | newlib's: 3123 / 3812 / 577 |

- Ours now runs fewer instructions than GCC's on `sum` and `sort`, and within 10% on
  `copy` and `dot`. On `fib` GCC unrolls the recursion.
- Left over:
  - `copy` spends most of its difference on `i % 26`, which GCC multiplies by a magic
    constant;
  - `dot` sets two constants in its loop, which needs loop-invariant code motion;
  - `fib` saves `rJ` before its base case, which needs shrink-wrapping.
- **Code size** (`.text`): our C library (the shared sources and `malloc`) and the
  benchmarks are 10304 bytes, GCC `-O2`'s 18444: `doprnt` 6708 against 13964, as GCC
  inlines and unrolls more; `atoi` (456 against 236) and the `str*` functions are
  larger in ours.
- Not done: tracking which registers are extended already (the selection rarely emits a
  redundant extension now), and forwarding stores to reloads (with registers allocated,
  little stays in memory).

### The MMIXware ABI, as GCC implements it

All of it was observed in GCC's `-O2` output and is pinned against GCC, both ways, by
`interop_tests.cpp` (a table of signatures), `stdarg_tests.cpp` and the book suite.

- **Fixed registers** (`gcc/config/mmix/mmix.h`):
  - `$254`: stack pointer;
  - `$253`: frame pointer, only when needed (`alloca`);
  - `$252`: static chain, unused in C;
  - `$251`: structure-result address;
  - `$255`: scratch, which GCC uses freely (as does `trap`).
- **Locals:** `$0`–`$31` under `rG` = 32. GCC's own model puts values live across a call
  in `$0`–`$14`, the hole in `$15`, and arguments in `$16`–`$31`. At output it renumbers
  the hole and the argument registers down to just above the highest local in use.
- **Calls:** `pushj $X,f`, or `pushgo $X,$f,0` through a pointer.
  - **Arguments** go in `$(X+1)`, `$(X+2)`, … and arrive as the callee's `$0`, `$1`, ….
  - **The result** comes back in the caller's `$X`.
  - **Preserved:** everything in `$0`…`$(X−1)`. Everything above `$X` is lost.
  - **`rJ`:** a non-leaf function saves it in a local, as in `get $2,rJ` … `put rJ,$2`,
    before `pop`. Example: `caller(x, y)` does `get $2,rJ; set $5,$1; set $4,$0;
    pushj $3,ext; put rJ,$2`.
- **Return:** `pop 1,0` returns the callee's `$0`; `pop 0,0` returns nothing.
- **Arguments in registers:** up to **16**, every scalar in one register.
  - Arguments 17 and up go on the stack at `0($254)`, `8($254)`, … of the caller, 8 bytes
    each, so the callee finds them at its frame size and up.
  - The caller extends a narrow integer argument to 64 bits (`negu $3,0,2` for a `short`
    −2), and the callee trusts it.
- **Narrow results:** the callee leaves them unextended. **The caller extends them** (`slu`
  56 then `sr` 56 after a `signed char` call). We extend on both sides, as on the other
  targets.
- **`float`** travels in a register as its binary32 bits in the low 32 bits. The callee
  converts with `sttu`+`ldsf` through a stack slot. A `float` result is `stsf`+`ldt`, so
  the bits come back sign-extended. `double` travels as itself.
- **Structures and unions:**
  - **8 bytes or less:** in one register, **right-justified**, as the big-endian integer of
    its bytes. `struct {char a,b,c;}` = {1,2,3} is `0x010203`, and `struct {int a,b;}` =
    {1,2} is `0x00000001_00000002`.
  - **More than 8 bytes:** **by reference, and the callee copies,** as on MSP430. GCC's
    caller passes the address of its own object as an ordinary argument, with no copy,
    and GCC's callee copies the object into its frame before writing it. (Corrected in
    K17: the interop table caught our callee writing through to GCC's original.) Our
    callee copies every such parameter at the start of its body. Our caller still passes
    a copy, which keeps an argument apart from the `$251` destination in `x = f(x)`.
  - **Results of every size**, even 1 byte, go through the address the caller puts in the
    global **`$251`** before `pushj`. The callee may return that address in `$0` but pops
    0 values (`pop 0,0`), so a caller must not rely on it.
- **Variadic functions:**
  - **The caller** passes variable arguments exactly as named ones: registers first, then
    the stack.
  - **The callee** stores the registers after its named ones (`$n`…`$15`) into the top of
    its frame, directly below the incoming stack arguments. Every variable argument is
    then one contiguous run of 8-byte slots. `va(int n, …)` stores `$1`–`$15` at
    `0($254)`…`112($254)` in a 120-byte frame.
  - **`va_list`** is a plain pointer.
  - **A slot holds a register's value,** so a narrow value or a small structure sits in
    the low-order (rightmost) bytes: `int` at offset 4, a 3-byte structure at 5, and a
    large structure as its address.
- **Signed division:** GCC never uses `div`. It divides absolute values with `divu`, fixes
  the signs with `negu`/`csn`, and takes the remainder from `rR`.
  - **`divu` divides the 128-bit `rD:$Y`,** so the ABI assumes `rD` = 0 everywhere. No code
    may change it.
  - **Signed `+`, `-`, `*`, `<<`** are `addu`, `subu`, `mulu` and `slu`. The signed forms
    set overflow bits in `rA`.
- **FP:**
  - **Conversions:** `fix $x,1,$y` (round mode 1, toward zero) for `(long)d`; `flot` and
    `flotu` for the reverse.
  - **Comparisons:**
    - `a < b` is `fcmp` and `bn`.
    - `a == b` is `feql`.
    - `a <= b` is `fcmp` and `bn`, then `feql`: `fcmp` gives 0 for an unordered pair, so
      `bnp` alone would make `NaN <= x` true.
- **Addresses:**
  - **Data:** `lda $1,arr` and `ldt $1,g`, with linker-allocated base registers.
  - **String literals** in `.rodata` (the text segment): `geta $2,LC:0`. `geta` reaches
    only multiples of 4, so GCC puts `.p2align 2` before each, and so must we: an
    unaligned target fails the link ("relocation truncated to fit: R_MMIX_GETA").
  - **Constants:** `setl`/`incml`/`incmh`/`inch` (`0x123456789abcdef` is four
    instructions), `setl $2,#1` for small ones, and `seth $4,#4004` for the double 2.5.

### Registers, as we use them

| Use | Registers |
|---|---|
| Values live across a call | `$0`…`$(P−1)`, at most `$0`–`$13` (incoming parameters arrive in `$0`… and stay there when they can) |
| `rJ` save (non-leaf only) | `$P` |
| The hole: `pushj` operand and call result | `$X` = `$(P+1)` (or `$P` in a leaf, which makes no call) |
| Arguments and values not live across a call | `$(X+1)` … `$31` |
| Selection scratch | `$248`–`$250` for operands in memory and constants, and `$255` for addresses and offsets: globals GCC treats as call-clobbered and `crt0` reserves, so never allocated and never live across a call |
| Fixed globals | `$254` SP, `$253` FP (unused), `$252` (unused), `$251` struct result; `$247`–`$254` reserved by `crt0.S` in `.MMIX.reg_contents`, as GCC's `crtn.o` does, so the linker allocates its base registers from `$246` down (it would take `$254` first) |

The allocator works in GCC's fixed model:
- `$0`–`$13` preserved;
- `$14` the `rJ` save;
- `$15` the hole;
- `$16`–`$31` arguments and scratch.

After allocation, a **compaction** renumbers `$14`, `$15` and `$16`… down to just above
the highest preserved register in use. That mapping is one uniform shift of the upper
range, so it is valid across the whole function (`phys_reg`; a leaf has no `rJ`, so its
shift starts at `$15`). The allocator runs before selection, so the selection emits
the final registers directly.

The allocation is `regalloc.c` on `backend/common/regalloc.c` (K21):
- every scalar is `REGALLOC_INT`; the pool is `$16`–`$31`, then the hole `$15` (17 that
  a call clobbers), then `$0`–`$13` (14 that a call keeps, for values live across one);
- the shared allocator assumes no cost for a callee-saved register, so it needed no hook:
  preserving one costs nothing here, there is no prologue push;
- hints: a parameter its incoming `$i`, call argument `i` `$(16+i)`, a call's result the
  hole, a returned value `$0`;
- parameters move to their registers by a parallel move in the prologue, arguments to
  theirs before a call; a move may also re-extend (to a parameter's type) or convert a
  `float` between binary64 and binary32 bits, through the slot `%.fround`.

`--no-regalloc` keeps every variable in memory, and `--no-peephole` skips the fusions
and the peephole pass; the tests' `NaiveSelection()` sets both, for the selection
goldens.

**Every scalar is one register:** `char` up to `long`, pointers, `float` and `double`. There
are no pairs and no FP class.

**Width invariant:** a register holding a narrower integer is always extended to 64 bits:
sign-extended if signed, zero-extended if unsigned. A `float` value in a register is held
as its exact binary64 value (`ldsf` loads it, `stsf` rounds it), never as binary32 bits, except at the ABI
boundary. The one exception, with the peephole optimizations on: a signed `int` `+`, `-`
or `*` that overflows is undefined, and like GCC's, its result is left unextended (K23,
decided: 2 instructions saved per `int` loop step, and the book programs agree with
GCC's). A `char` or `short` result is still extended, being a conversion back from
`int`.

`make run` stays green after every K-step.

## Phase 6 — finishing

- **K24. Driver.** `vcc -t mmix` runs:
  - `vcpp -t mmix`, `vparse`, `vlower -t mmix`, `vgenmmix`;
  - `mmix-knuth-mmixware-as -x -no-predefined-syms`;
  - `mmix-knuth-mmixware-ld crt0.o … -lc`, then `libgcc.a` when it is found, so that
    objects compiled by GCC link too.

  The output is a `.mmo` that `mmix` runs directly. Add `cc-tests` cases, including a
  staged prefix. Document `--oformat elf64-mmix` for `objdump`.
- **K25. Install.** `genmmix` as `vgenmmix`; `crt0.o`, `libc.a` and the headers (MMIX,
  `lp64` and shared) under `share/vcc/mmix/`. The runtime is installed only when the GNU
  MMIX binutils were found.
- **K26. Documentation.**
  - **`docs/Mmix_Backend.md`,** in the style of `docs/Msp430_Backend.md`:
    - the target and how code is generated;
    - the register stack, the fixed model and the compaction;
    - frames and the 8-bit offset field;
    - calls, structures (right-justified in a register, or by reference with a caller
      copy, results through `$251`) and variadics;
    - big-endian notes;
    - `float` held as binary64;
    - signed division;
    - linker-allocated base registers;
    - the costs against GCC;
    - running a program by hand under `mmix`, with `-t`, `-r`, `-i` and `-s`.
  - **Other docs:**
    - README and CLAUDE.md for eight targets;
    - `libc/mmix/include/README.md`, `libc/lp64/include/README.md`;
    - `docs/Type_Sizes_Alignment.md` (which already lists `mmix`);
    - `docs/Technical_Reference.md`, which calls `mmix/` "not implemented".
  - Remove `mmix/` from the "design notes only" sentence.

  This plan is then removed.

## Out of scope

- **`-mabi=gnu`** (arguments in the global registers `$231`… instead of the register
  stack), though GCC's multilib has it.
- **Privileged MMIX:** traps and interrupts (`rT`, `rTT`, `resume`), virtual memory
  (`rV`), and an operating system. Programs run in the simulator's user mode.
- **An `<mmix.h>` of intrinsics,** in the manner of `<besm6.h>`: `trap`, `sadd`, `mor`,
  `mxor`, `bdif`…`odif`, and `get`/`put` of special registers. A natural follow-up.
- **Bit-fields,** which no target lowers yet.
- **`mmixal` and `mmixld`.** Knuth's assembler takes only whole programs, so all
  assembly goes through GNU `as`.
- **`mmmix`,** the pipelined meta-simulator, and tuning for its cache and pipeline models.
- **newlib as our C library.** We ship our own `libc.a`; newlib is a test reference.

## Risks

- **The register-stack protocol.** A wrong `X`, a hole that clobbers a live value, a
  compaction that maps one register onto another, or an `rJ` saved above `X` silently
  corrupts the caller's locals, far from the cause.
  - Mitigation: the survival tests of `call_tests.cpp`; `interop_tests.cpp` against GCC
    both ways, with the `regcheck` harness and recursion that spills the register ring;
    `mmix -r` to watch the ring. The compaction is one function (`phys_reg`), pinned by
    the `Regalloc*` goldens.
- **The first big-endian byte-addressed target.** Hidden little-endian assumptions in
  shared code, the libc, the test fixtures and the book expectations.
  - Mitigation: the frontend audit (done); every book program compared with GCC;
    big-endian versions of the byte-order programs in `book_mmix_tests.cpp`, as BESM-6
    has, and signed-`char` versions shared with x86-64.
- **A halt in zeroed memory looks like `exit(0)`.** `trap 0,0,0` is the all-zero word.
  - Mitigation: the `[exit N]` trailer that the fixture requires of our runtime, and
    tests that check output, not only status.
- **Silent misaligned access.** An octa, tetra or wyde access drops the low address bits,
  so a layout or pointer bug reads the wrong data without a fault.
  - Mitigation: the layout tests against GCC (`TranslateTestMmix`); alignment kept
    through `ALLOCATE_LOCAL` and the outgoing area; the structure tests at every size.
- **Signed division floors.** Using `div` as if it truncated gives wrong quotients and
  remainders for negative operands only.
  - Mitigation: `int_tests.cpp`'s table of every sign combination, `LONG_MIN / -1` and
    32-bit cases, checked against the host.
- **The width invariant.** A narrow value left unextended in a 64-bit register compares,
  divides or converts wrongly, usually only for negative or wrapped values.
  - Mitigation: one rule in selection (`def_done`), a test per operation at each width
    with overflowing operands, run with registers allocated (`RunRegallocWidths`).
- **`float` rounding.** A `float` result not rounded to binary32, or an integer rounded
  twice on the way to `float`, gives a last-bit difference.
  - Mitigation: `fp_tests.cpp`'s halfway cases against the host, and the folder agreeing
    with the target.
- **Linker-allocated base registers run out.** Each distinct 256-byte window of data that
  code addresses takes one global register: 215 with `crt0`'s eight reserved (measured).
  - Past it the link fails, "too many global registers: 224, max 223", never silently;
    GCC has the same limit.
  - Mitigation, if a real program reaches it: `lda` of a nearby base, or an address
    built by a wyde sequence with relocations, if `as` supports one.
- **8-bit offsets and immediates.** A large frame or structure takes a register operand
  everywhere, and an off-by-one at 255/256 is a wrong address.
  - Mitigation: golden and run tests with slots at 248, 256 and 4096, and constants at
    255, 256, −255 and −256.
- **The oracle is little used.** GCC's MMIX port gets far less use than its others.
  - Mitigation: where GCC and we differ, check the host (for byte-order-independent
    programs) or Knuth's specification, and trace under `mmix -t`, which is the
    reference implementation of the architecture.
- **Symbol names.** Our symbols share a namespace with special-register names such as `rJ`
  or `rA` (`-no-predefined-syms` fixes their spelling), so a C global of that name would
  be misread.
  - Mitigation: a test of such names; mangle them if `as` misreads them, and document it.
