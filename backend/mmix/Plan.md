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

### The MMIXware ABI, as GCC implements it

All of it was observed in GCC's `-O2` output and is to be pinned against GCC, both ways, by
the interop tests (K17).

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
  - **More than 8 bytes:** **by reference.** The caller copies the object into its own
    frame and passes the copy's address as an ordinary argument, so the callee may read
    and write through it freely. This is the opposite of MSP430, where the callee copies.
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
| Selection scratch | `$255`, never allocated |
| Fixed globals | `$254` SP, `$253` FP (unused), `$252` (unused), `$251` struct result; `$247`–`$254` reserved by `crt0.S` in `.MMIX.reg_contents`, as GCC's `crtn.o` does, so the linker allocates its base registers from `$246` down (it would take `$254` first) |

The allocator works in GCC's fixed model:
- `$0`–`$13` preserved;
- `$14` the `rJ` save;
- `$15` the hole;
- `$16`–`$31` arguments and scratch.

After allocation, a **compaction** renumbers `$14`, `$15` and `$16`… down to just above
the highest preserved register in use. That mapping is one uniform shift of the upper
range, so it is valid across the whole function.

**Every scalar is one register:** `char` up to `long`, pointers, `float` and `double`. There
are no pairs and no FP class.

**Width invariant:** a register holding a narrower integer is always extended to 64 bits:
sign-extended if signed, zero-extended if unsigned. A `float` value in a register is held
as its exact binary64 value (`ldsf` loads it, `stsf` rounds it), never as binary32 bits, except at the ABI
boundary.

`make run` stays green after every K-step.

## Phase 3 — ABI conformance

- **K16. Variadic functions and `<stdarg.h>`.**
  - **Calls** need nothing special: variable arguments go exactly like named ones. An
    unprototyped callee is called the same way.
  - **The variadic callee** stores `$n`…`$15` into a save area at the top of its frame,
    directly below the incoming stack arguments. `__va_start(ap)` (intercepted by the
    backend, as on the other targets) points `ap` at the first variable slot.
  - **`va_list`** is `char *`.
  - **`va_arg(ap, T)`** needs no argument classes:
    - a `T` larger than 8 bytes is read through the pointer in the slot;
    - otherwise it is read at `slot + 8 − sizeof(T)`, the right-justified bytes;
    - `ap` advances by 8.
    - So `__builtin_va_class` stays unused and `va_class` is `NULL`. `float` arrives
      promoted to `double`.
  - **Gate:** `printf` in `libc.a` works.
- **K17. Interop tests with GCC,** in both directions, over a table of signatures, built
  before the code they test:
  - **Register assignment:**
    - 16 register arguments, then 17 and 18 on the stack;
    - mixed `int`/`long`/pointer/`double`/`float`;
    - `float` as binary32 bits, both ways;
    - narrow types (`char`, `signed char`, `unsigned char`, `short`) as arguments and as
      results, both unextended from GCC and extended by us.
  - **Aggregates:**
    - structures of 1, 2, 3, 4, 5, 8, 9, 16 and 24 bytes and a union, as arguments mixed
      with scalars and as results through `$251`;
    - a callee writing to a large structure parameter, with the caller's original
      unchanged;
    - a 1-byte structure result.
  - **Function pointers both ways.**
  - **The register stack:** our values survive GCC's calls and GCC's survive ours, deep
    recursion through both, and `rJ`, `$254` and `rD` unchanged.
  - **Variadics both ways,** a `va_list` handed across, and GCC's `printf` from newlib
    called by our code.
  - **GCC's code on our runtime,** linked with our `crt0.o` and `libc.a`, then
    `libgcc.a`. **Our code under newlib,** linked by `mmix-knuth-mmixware-gcc` with
    GCC's `crti.o`/`crtn.o`: newlib's `printf`, `strtod` and `qsort` with our callback.
- **K18. Differential book tests.**
  - Every book program is also compiled by GCC with newlib, run under `mmix`, and the
    outputs and statuses compared, as for MSP430.
  - **Byte-order-dependent programs** get big-endian versions in `book_mmix_tests.cpp`, as
    BESM-6 has in `book_besm6_tests.cpp`. This covers programs that read an `int`'s bytes
    through a `char *` or pun through a union. GCC's agreement on them is part of the
    check.
  - **Skipped so far** (`book_test.h`; GCC's build gives what ours gives on each):
    - an unsigned plain `char` expected: chapter 16's `StaticInitializers`, chapter 18's
      `ClassifyParams` and `UnionInits` (x86-64 has signed-`char` versions of the
      three in `book_x86_tests.cpp`);
    - a little-endian byte order expected: chapter 16's `AccessThroughCharPointer`, and
      chapter 18's `CopyThruPointer`, `NestedUnionAccess`, `StaticUnionAccess`,
      `StaticUnionInits`, `UnionTempLifetime` and `UnionsInConditionals`.

## Phase 4 — library and headers

- **K19. Headers.**
  - **`setjmp.h`:** newlib's MMIX `jmp_buf`, five `unsigned long`s (`_JBLEN` 5): fp, `rJ`,
    sp, `rO` before the call, and the value handed from `longjmp`. GCC's built-in uses the
    same layout.
  - **`math.h`:** lists what `libc.a` has.
  - **Checked against GCC:** `HeadersAgreeWithGcc` compares our headers' sizes, limits,
    type identities and `float.h` values with GCC's.
- **K20. Libc and run tests.**
  - **`setjmp`/`longjmp`: newlib's (decided).** `libc/mmix/setjmp.s` is newlib's
    `libc/sys/mmixware/setjmp.S`, Hans-Peter Nilsson's, with its permission notice kept
    as its license requires. The port:
    - lowercase mnemonics;
    - only the MMIXware-ABI branch of its `#ifdef`, so a plain `.s` with no
      preprocessor.

    **How it works:**
    - **`setjmp`** stores `$253`, `rJ` and `$254`. It then pops back into itself (`put rJ`
      to a local label, `pop 1,0`) to read `rO` as it was *before* the call.
    - **`longjmp`** pops one register-stack frame at a time (`pop 0,0` until `rO` is
      back to the saved value). It then restores `$253`/`$254` and `go`es to the saved
      `rJ`, with the value in the caller's result register.
    - It uses `$251`, `$252` and `$255` as scratch, which is safe: none holds anything
      live across a call to either function.

    **Tests:**
    - from our code, from GCC's, and across both;
    - a `longjmp` out of deep recursion, which has spilled the register ring;
    - `longjmp(env, 0)` returning 1.

    The earlier `~/Project/Mmixware/setjmp-mmix/` project is not used.
  - **`malloc`** is done: `libc/mmix/malloc.c`, a bump allocator from `_end` up to the pool
    segment, written when chapter 18 needed it.
  - **`frexp`/`ldexp`/`modf`** come from the shared binary64 sources. `sqrt` is `fsqrt`.
  - **`doprnt.c`:** check `%z`/`%t` and the `FBUFSIZE` sizing for LP64 with an 8-byte
    `long double`. `%Lf` takes a `double`-sized argument.
  - **Run tests:** port the `printf`/`str`/`mem`/`math` run tests from x86-64, the other
    LP64 target with a signed `char`, with the host's output as the expectation.
    - **Byte order:** wherever a case's output depends on it, take GCC's output instead.
    - **Against newlib:** run the `str`/`mem` cases and the integer `printf` cases
      against newlib too.
  - **Costs against GCC:** measure with `mmix -s` (instructions, υ and μ) for
    `printf("%d")`, `printf("%g")` and a few kernels, and record them here.

## Phase 5 — code quality

- **K21. Register allocation** on `backend/common/regalloc.c`.
  - **What it replaces:** the naive selection keeps every variable in memory and `rJ` in
    `$0`, so every call is `pushj $1` with the arguments loaded from memory into `$2`…
    and nothing else live. With values in registers:
    - the width invariant matters again: a narrow result in a register is re-extended;
    - the arguments need parallel moves;
    - a `float` result needs its rounding (`stsf`/`ldsf` through a scratch slot), which
      the naive store gave for free.
  - **Class:** every scalar is `REGALLOC_INT`, and `REGALLOC_FP` is unused (empty FP
    pool). There are no pairs.
  - **Numbering:** registers are numbered from 1 on the allocator's side, since `$0` is
    register 0, as ARM32 does.
  - **Pools, in GCC's fixed model:**
    - `$16`–`$31` are the "argument" registers, for values not live across a call;
    - `$0`–`$13` are the "callee-saved" ones, for values live across a call;
    - preserving a register costs nothing on MMIX, so there is no prologue push.
  - **Hints:**
    - a parameter prefers its incoming `$i`;
    - call argument `i` prefers `$16+i`, through `call_hints`;
    - a call result prefers the hole `$15`;
    - a returned value prefers `$0` (`ret_int`).
  - **Compaction:** after allocation, `$14` (the `rJ` save), `$15` and `$16`… shift down
    to just above the highest preserved register in use. This is GCC's
    `pushj $3` / arguments `$4`, `$5` shape.
  - **Pressure:** more than 16 values not live across a call, or more than 14 live
    across one, spill to frame slots.
  - **Shared code:** check whether `regalloc.c` assumes that a callee-saved register
    costs a save. If it does, add a hook rather than a special case.
  - The ch. 20 tests pass, and K17's register-stack tests pass under allocation.
- **K22. Frameless functions.** A function with no slots, no outgoing stack arguments and
  no calls touches neither `$254` nor `rJ`:
  - `add` is `addu $0,$0,$1; pop 1,0`, as GCC's;
  - early returns are a `pop` in place.
- **K23. Selection and peephole.**
  - **Compare and branch:**
    - a comparison only a conditional branch reads is a `cmp` and a branch on its sign,
      or no `cmp` at all against zero;
    - `!x` and a truth test branch on the value itself.
  - **Conditional sets:**
    - `zs*` for 0/1 results;
    - `cs*` for a `?:` or `if` that only selects between two values, with no branch.
  - **Immediates:**
    - the Z immediate for 0–255;
    - `negu` for small negatives;
    - `subu`/`addu` turned around for a negative constant;
    - the wyde-sequence generator's shortest form.
  - **Addressing:**
    - `2addu`…`16addu` for scaled indices;
    - the register form `ldo $x,$p,$i` instead of an `addu` and a load;
    - offsets folded into the 8-bit field.
  - **The width invariant:**
    - track which registers are already extended and drop redundant `slu`/`sr` pairs;
    - decide here, with measurements, whether a signed `int` `+`/`−`/`*` skips the
      re-extension as GCC's does. That is legal (overflow is undefined) but changes what
      overflowing programs print, so the book comparisons must agree.
  - **Memory:** forward stores to reloads, and drop dead stores to frame slots.
  - **Branches:**
    - `pb*` (probable) for loop back-edges;
    - a branch over a jump inverted;
    - a jump to the next line deleted.
  - **Tail calls:** decide whether `jmp f` after moving the arguments to `$0`… and
    restoring `rJ` is sound under the register stack, and measure it.
  - **Measured** against GCC `-O2`, in `mmix -s` instructions, υ and μ, and in code size,
    on the C library, the book programs and the Phase 4 benchmarks, and recorded here.

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
  - Mitigation: the survival tests of `call_tests.cpp`; K17's tests against GCC both ways and deep
    recursion that spills the register ring; `mmix -r` to watch the ring. The compaction
    is one function with a unit test of its mapping.
- **The first big-endian byte-addressed target.** Hidden little-endian assumptions in
  shared code, the libc, the test fixtures and the book expectations.
  - Mitigation: the frontend audit (done); every book program compared with GCC (K18); big-endian
    versions of the byte-order programs, as BESM-6 has.
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
  - Mitigation: one rule in selection, a test per operation at each width with
    overflowing operands, and the peephole's extension tracking checked by the same tests.
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
