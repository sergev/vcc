# MSP430 backend — development plan

A seventh backend, for the classic 16-bit TI MSP430. It uses the MSP430 ABI as **GCC**
implements it, emits ELF objects, and runs its programs bare-metal under **`mspsim`**, our
own simulator (`~/.local/bin/mspsim`, sources in `../mspsim/`).
- **The oracle.** The code is link-compatible with `msp430-elf-gcc -mcpu=msp430`. GCC
  16.2, GNU binutils and newlib are installed in `~/.local/bin`, built per
  `~/Howto_build_MSP430_GCC.md`. GCC is therefore the test oracle: interop runs in both
  directions, and every book program is compiled by both compilers and the outputs
  compared.
- **The toolchain.** GNU `msp430-elf-as`, `-ld` and `-ar` assemble, link and archive.
- **clang.** Homebrew `clang --target=msp430` stays a second oracle and a second
  assembler wherever its ABI agrees with GCC's, which is everywhere but structure
  arguments (below).

Phases 0–2 were built against clang, before GCC was installed. T27 moves them over.

It follows the shape of the AVR backend, the closest relative:
- a directory of its own;
- a small IR;
- naive selection first;
- then register allocation on `backend/common/regalloc.c`, a peephole pass and branch
  relaxation.

Much of the hard shared work is already done, because AVR brought the first 16-bit
`int`, `size_t` and pointer into the frontend (M2), the target-relative literal typing,
and the 16-bit static initializers. What is new here:

- **A 16-bit machine with a binary64 `double`.** `float` is binary32 and `double` =
  `long double` is binary64, **all in software**: no target so far has needed a soft
  binary64. AVR's `double` is binary32, and the other targets have FP hardware.
- **Alignment 2.** `int`, `long`, `long long`, `float`, `double` and pointers are
  2-aligned (`__BIGGEST_ALIGNMENT__` 2). So `struct { char c; int i; long l; double d; }`
  is 16 bytes (checked). A `long` of size 4 with alignment 2 is a combination no target
  has had.
- **Plain `char` is unsigned** with a 16-bit `int`.
- **Every struct and union travels by reference.** An argument is a pointer to the
  caller's object, which the callee copies, and a result comes back through a hidden
  pointer in R12, even for a 1-byte struct.
- **No hardware multiplier.** The CPU has none. The MPY peripheral exists on some
  devices only, and mspsim models no peripherals. So every multiply calls the runtime, as
  every divide does.
- **Single-bit shifts only** (`rla`, `rra`, `rrc`).
- **A two-address, memory-to-memory ISA.** `add 2(r1), 4(r1)` is one instruction. Naive
  selection can therefore operate on frame slots directly, without loading them.
- **Variable-length instructions** (2, 4 or 6 bytes). An immediate of 0, 1, 2, 4, 8 or
  −1 comes free from the constant generators, and anything else costs an extension word.
  Branch relaxation must model this exactly, or conservatively.
- **A von Neumann machine:** one 64 KB address space. A function pointer is an ordinary
  byte address, unlike AVR's word addresses.

As before, an assumption found in shared code is fixed in the shared code, not worked
around in `backend/msp430/`. BESM-6 output must not change, and the output of the
RISC-V, AArch64, ARM32, x86-64 and AVR backends changes only where a step says so.

`msp430.asdl` and `msp430.md` stay as the reference spec of the instruction set. They
include MSP430X, which is out of scope. `../mspsim/MSP430_Instruction_Set.md` is the
reference for flags and cycle counts. Step IDs are stable: a finished step is marked done,
never renumbered. The prefix is `T`, for TI: `A`, `R`, `B`, `V`, `X` and `M` are taken.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| CPU | Classic MSP430 (16-bit registers, 64 KB address space), no MSP430X | mspsim models exactly this. GCC's `-mcpu=msp430` (the `430` multilib) and clang's default `--target=msp430` generate it |
| Data model | `char` 1 (**unsigned**), `short` 2, `int` 2, `long` 4, `long long` 8, pointer 2, `size_t` = `unsigned int`, `ptrdiff_t` = `int`, **`wchar_t` = `long`**, `wint_t` = `unsigned int`, `_Bool` 1; alignment 1 for `char`, 2 for everything else | GCC's `-dM -E` and `sizeof`/`_Alignof` (checked). clang agrees except `wchar_t`, which it makes `int` |
| Floating point | `float` binary32, `double` = `long double` binary64, all in software | `__DBL_MANT_DIG__` = `__LDBL_MANT_DIG__` = 53 in both compilers |
| Multiply | Software (`__mspabi_mpyi` and friends) | No CPU multiplier. Without `-mhwmult` both compilers call the helpers (checked). mspsim has no MPY peripheral |
| ABI | The MSP430 EABI as GCC implements it (details below) | Interop with GCC-compiled code, newlib and `libgcc` |
| Output | GNU msp430-as syntax: `#imm`, `&abs`, `x(rN)`, `@rN+`, the `.b` suffix, `;` comments | Accepted by GNU `msp430-elf-as -mcpu=msp430` and by clang's integrated assembler alike (every Phase 2 output, checked) |
| Toolchain | `msp430-elf-as -mcpu=msp430` (assemble), `msp430-elf-ld` (link), `msp430-elf-ar` | The GNU tools that go with the oracle. GNU `ld` needs no `-n`. TI headers are never needed |
| Second oracle | Homebrew `clang --target=msp430`, `ld.lld -n`; test-only, and its tests skip when it is absent | An independent implementation of the same ABI but for structure arguments, and a second assembler |
| GCC's libraries | `libgcc.a` of the `430` multilib is linked after our `libc.a`. newlib is used only in tests, linked by `msp430-elf-gcc -mcpu=msp430 -msim` | Our runtime defines every helper our code calls. `libgcc.a` supplies what GCC's code calls beyond that (the `int` shift helpers and the shared epilogues), and is the reference our helpers are checked against. `-msim` brings newlib's startup, `msp430-sim.ld` and `libsim.a`, whose I/O mspsim serves |
| Run environment | `mspsim -n <cycles> <elf>` | Our own simulator: ELF loader, console UART, an exit device, and newlib's host I/O |
| I/O and exit | stdout through the USCI_A0 UART (`UCA0TXBUF` 0x0067, poll `IFG2` 0x0003 bit 1). `exit` writes the status to the stop register 0x01FE, which becomes mspsim's exit status | No timeout-and-kill protocol is needed, unlike AVR. The cycle limit `-n` gives deterministic timeouts |
| Memory map | Data, `.bss` and stack in 0x0200–0x1FFF (7.5 KB); code and `.rodata` in 0x2000–0xFFDF (56 KB); vectors in 0xFFE0–0xFFFF, reset → `_start` | mspsim is all RAM, so the split is our choice. It mirrors a flash device, so the runtime stays honest about `.data` copying. The ROM grew from 48 KB at T17: `printf` with the soft binary64 under the naive selection takes over 45 KB |
| Backend IR | A small hand-written `Msp_Instr` list, as in `avr_ir.h`. Every instruction knows its size, for branch relaxation | `msp430.asdl` stays the reference spec. The IR covers only what we emit |
| Executable | `genmsp430` (`backend/msp430/`), library `msp430`; installed as `vgenmsp430` | Mirrors `genavr`/`vgenavr` |

Verified 2026-10-04 on this machine, with scratch programs (not in the tree):

- **GCC** (`msp430-elf-gcc` 16.2.0, binutils 2.47, newlib 4.6.0, `-mcpu=msp430`):
  - **Data model:** `-dM -E` and the 16-byte struct above. `wchar_t` is `long`.
  - **Assembler:** GNU `as` accepts all 1435 `.s` files `genmsp430` wrote in Phase 2.
    It encodes them as clang does, except that it shortens a `0(rN)` source operand to
    `@rN`. The size model is clang's, so under GNU `as` it can only overestimate, which
    is safe for relaxation. T27 makes the two agree.
  - **Linking:** 40 book programs from chapters 13–18, assembled by GNU `as` and linked by
    GNU `ld` with our `link.ld`, `crt0.o` and `libc.a`, run exactly as when linked by
    `ld.lld -n`.
  - **GCC's code on our runtime:** a GCC-compiled program doing `long` multiply and
    divide, an `int` shift and `double` arithmetic and comparison links with our `crt0.o`
    and `libc.a`, then `libgcc.a`, and runs correctly. Only `__mspabi_slli`/`srai`/`srli`
    and the `__mspabi_func_epilog_N` entries come from `libgcc.a`. `libgcc.a` has the
    complete `__mspabi_*` set.
  - **newlib:** a program built by `msp430-elf-gcc -mcpu=msp430 -msim` runs under
    mspsim unchanged (mspsim `b181d9f`). `libsim.a` does its I/O in two ways, and
    mspsim now serves both:
    - TI's CIO breakpoint: `write` fills `__CIOBUF__` and calls `C$$IO$$`;
    - the GDB simulator's syscalls: `exit`, `read`, `close`, `fstat` and the rest are
      calls to 0x0180 + N.

    Writes to fds 1 and 2 reach stdout and stderr, and `read` of fd 0 takes stdin.
    `exit` sets mspsim's exit status, and `abort()` gives 134. File operations fail
    with −1. `printf`, `fgets` on stdin, `snprintf` and `setjmp`/`longjmp` are checked.
    A newlib program, ours included, therefore needs no UART code and no shim.
- **clang** 23.1.2 (Homebrew, `msp430 - MSP430 [experimental]`):
  - `ld.lld` links an MSP430 ELF. Without `-n` it emits a read-only segment holding the
    ELF headers at address 0, over the peripheral area. **`ld.lld -n`** drops it.
  - Its objects reference no `__do_copy_data`/`__do_clear_bss`, unlike AVR.
  - A `jeq` 600 `nop`s away fails in its assembler with "fixup value out of range". The
    failure is loud, but nothing relaxes it for us.
  - There is no compiler-rt for MSP430: linking a `long` multiply fails on
    `__mspabi_mpyl`. So we supply every helper our code calls, as for AVR.
- **Run.** A hand-written crt0 sets SP, calls `main` and writes the result to `&0x01fe`.
  A `putbyte` polls `IFG2` and writes `UCA0TXBUF`. `.text`, `.rodata` and `.data`
  strings print, and mspsim exits with `main`'s status. Intel HEX runs as well.
- **Speed.** A million iterations of a `volatile long` add loop take 43 M cycles, 0.82 s
  under mspsim: about 52 M cycles per second. A cycle limit of 250 M is a ~5-second
  budget.
- **Common ground:**
  - the address of a function is `mov #f, r12`, and `.short f` in data;
  - the frame is addressed as `x(r1)` with no frame pointer.

### The MSP430 ABI, as GCC implements it

Checked against GCC's output where marked. All of it is exercised against GCC, both
ways, by the interop tests (T17). clang agrees on every point except structure
arguments (*checked*, the same signatures).

- **Fixed registers:** R0 = PC, R1 = SP, R2 = SR (and constant generator 1), R3 =
  constant generator 2.
- **Call-saved:** R4–R10. **Call-clobbered:** R11–R15. There is no frame pointer by
  default; the frame is `x(r1)`, and the 16-bit offset has no range problem. SP is always
  even; `push.b` moves it by 2.
- **Arguments** go in R12, R13, R14 and R15, left to right, each taking the next free
  registers (*checked*):
  - A 16-bit value takes one register.
  - A 32-bit value takes the next **two consecutive** registers, not necessarily
    starting on an even one: `kl(int, long, long)` puts the first `long` in R13:R14.
  - When only R15 is left, the `long` is **split**: its low word goes in R15 and its
    high word on the stack (`k(int, int, int, long)`).
  - A 64-bit value goes in R12–R15 only when all four are free, else wholly on the stack.
  - **Later arguments still take free registers:** in `g(int, long long, int)` the
    `long long` goes on the stack and the last `int` in R13.
  - A `char` is extended by the caller, and GCC's callee extends it again (`nc(signed
    char, unsigned char)` has `sxt` and `and #0xff`). We extend on both sides, as on AVR
    and x86-64.
  - **A struct or union argument is passed by reference**, whatever its size:
    - the caller passes the address of the object, **without copying it**, as an
      ordinary pointer argument: in a register if one is free, else on the stack
      (`f10(int, struct S4, int)` takes it in R13, and `five(int, int, int, int, struct
      S4)` on the stack);
    - **the callee copies** the object into its own frame before writing to it (`m`
      copies `*R12` into a local before `s.a = 5`). Copying always, on entry, is the
      simple form; a callee that never writes to the parameter or takes its address may
      read through the pointer instead.
    - An rvalue argument (a call's result, a compound literal) is passed as the address
      of its temporary.
    - The same applies to a variadic argument, whose pointer goes on the stack: `va_arg(ap,
      struct S)` reads a pointer and then the object through it (*checked*).
- **Stack arguments** sit above the return address, in parameter-list order, 2-aligned.
  The caller removes them.
- **Variadic callees** take their **last named argument** and all the variable ones on
  the stack; the named ones before it, the hidden result pointer included, go by the
  rules above (*checked*: `vi(1, 2L, 3, 4)` passes 1 in R12, 2 in R13:R14, and 3 and 4 on
  the stack; `va(1, 2L, 3.0)` stores all three at `0(r1)`…`12(r1)`). clang puts every
  argument of a variadic call on the stack, so it differs whenever a variadic function
  has two named parameters or more. `va_list` is a plain pointer.
- **Results:**
  - 16 bits in R12, 32 in R12:R13, 64 in R12–R15 (*checked*);
  - **every** struct or union through a hidden pointer passed in R12 as the first
    argument, even a 1-byte one (*checked*).

  GCC's callee leaves R12 as it was, so it still holds that address on return. We
  return it too, and never rely on it.
- **Calls:**
  - a direct call is `call #f`;
  - an indirect one is `call rN` (or `call x(r1)`, `call &fp`) on a plain byte address.
- **Runtime helpers.** GCC calls TI's `__mspabi_*` names and some libgcc ones
  (*checked*):
  - **Integer multiply:** `mpyi`, `mpyl`, `mpyll`.
  - **Integer divide:** `divi`/`divu`/`remi`/`remu`, `divli`/`divul`/`remli`/`remul`,
    `divlli`/`divull`/`remlli`/`remull`.
  - **Shifts by a variable:** `slll`, `srll` and `sral` for `long`, with the count in
    R14; `slli`, `srli` and `srai` for `int`. Shifts by a constant count enter fixed-count
    entries such as `__mspabi_slli_3`.
  - **binary32:** `addf`, `subf`, `mpyf`, `divf`.
  - **binary64:** `addd`, `subd`, `mpyd`, `divd`.
  - **FP comparisons:** the libgcc predicates `__eqdf2`, `__ltdf2`, `__ltsf2` and so on,
    as our code uses. clang calls `__mspabi_cmpd`/`cmpf` instead.
  - **Conversions:** `cvtfd`, `cvtdf`; `fixfli`, `fixful`, `fixflli`, `fixfull`,
    `fixdli`, `fixdul`, `fixdlli`, `fixdull`; `fltlif`, `fltulf`, `fltllif`,
    `fltullf`, `fltlid`, `fltuld`, `fltllid`, `fltulld`.
  - **Shared epilogues:** a function that saves R8–R10 may end with `br
    #__mspabi_func_epilog_N`.
  - **Other names:** `divlu` and `divllu` for `divul` and `divull`; `fltid`, `fltud`,
    `fltif` and `fltuf` from a 16-bit `int`; `__fixunssfsi` and `__fixunssfdi` from a
    `float` to unsigned (libgcc has no `fixful`/`fixfull`).
  - **Library calls:** `memcpy` for struct copies, `memset` for zeroing, `sqrt`.

  **Special contracts.** The 64-bit two-operand helpers (`addd`, `subd`, `mpyd`, `divd`,
  `cmpd`, `mpyll`, `divlli`, …) take the **first operand in R8–R11** and the second in
  R12–R15, and return in R12–R15 (*checked* in GCC's `addd` and `divlli` calls, and in
  clang's `addd`, `cmpd` and `mpyll` calls). The caller saves R8–R10 itself because it
  loads them. `cmpd`/`cmpf` return a negative, zero or positive `int` in R12. Only
  clang calls them, and for a NaN its `>`/`>=` come out true.

### Registers, as we use them

| Use | Registers |
|---|---|
| Fixed | R0 (PC), R1 (SP), R2 (SR/CG1), R3 (CG2) |
| Arguments and results | R12–R15 |
| Values not live across a call | R11–R14 (R15 is the selection's scratch) |
| Values live across a call | R4–R10, pushed and popped in the prologue and epilogue |
| Selection scratch | R15, never allocated (since T21) |

**The allocation unit is one 16-bit register.**
- `char`, `short`, `int` and pointers are one register.
- `long` and `float` are a `REGALLOC_PAIR`, two consecutive registers, as the ABI passes
  them.
- `long long` and `double` stay in their frame slots, as `long long` does on AVR. Every
  `double` operation is a helper call with fixed registers, so little is lost.

Any register takes an immediate and every addressing mode, so there is no AVR-style
upper/lower split.

**Flags are not uniform** (`MSP430_Instruction_Set.md`):
- `mov` (and so `pop`, `br`) sets none;
- `bit` and `and` set C = !Z;
- `xor` sets V when both operands are negative;
- `rrc`/`rra` set C from the shifted-out bit.

One per-opcode table, transcribed from that reference, serves selection and peephole.

`make run` stays green after every T-step.

Phase 0 is done:
- `cpp -t msp430` predefines `__MSP430__`, `__CHAR_UNSIGNED__` and `__ELF__`. The
  `msp430` descriptor has an unsigned plain `char`, alignment 2 for every type wider than
  `char`, a binary64 `double` and `long double`, and no `va_class`.
- `struct_return_max = 0` now means "every struct and union". MSP430 uses it, so every
  struct result comes through the frontend's hidden pointer, the first argument (R12).
  riscv64 spells out its 16.
- The frontend audit found no defect. `translator/test/msp430_tests.cpp` and the
  `TranslateTestMsp430` cases of `type_tests.cpp` pin:
  - unsigned `char` with a 16-bit `int`;
  - binary64 folding beside a 16-bit `int`;
  - struct layouts, against clang's;
  - copies by 2-byte words.

  The book programs, the test-fixture snippets and the C library lower with
  `-t msp430 --verify` exactly as with `-t avr`.
- **Headers.**
  - `libc/ip16/include/` holds the 16-bit `inttypes.h`, `stddef.h` and `stdint.h`, shared
    with AVR. It is searched second, and is `TEST_MODEL_INCLUDE_DIR` for both.
  - `libc/msp430/include/` has `float.h`, `limits.h`, `math.h` and `stdarg.h`; `setjmp.h`
    comes at T19.
  - The `msp430-headers` CTests exist.
- `libc/msp430/CMakeLists.txt` finds the tools (`MSP430_TOOLS_FOUND`, `MSP430_CLANG`,
  `MSP430_AR`, `MSP430_LD`, `MSPSIM`, `MSP430_LIB_DIR`, `MSP430_LINK_SCRIPT`,
  `MSP430_TARGET_FLAGS`).
- **`QemuConfig` runs mspsim:**
  - `link_flags` (for `-n`);
  - an empty `image_option`, which passes the ELF as a plain argument;
  - `exit_report`, which fails a run unless mspsim's stderr has
    `[Exit code N after M cycles]`.

  The cycle limit `-n` belongs in the backend's runner command (T7).

Phase 1 is done:
- **Runtime** (`libc/msp430/`):
  - `crt0.S` stops the watchdog, sets SP to 0x4000, copies `.data` from ROM, clears
    `.bss`, sets the canary, and calls `main`. It has a `PRINT_STATUS` variant and a
    vector table with every interrupt on `__bad_interrupt` (status 0xfe).
  - `console.s` has `putbyte`/`putch`/`flush`. `exit` writes the status to 0x01fe, or
    0xfd and "stack overflow" when the canary is broken.
  - `link.ld`: RAM 0x0200–0x3fff, ROM 0x4000–0xffdf, vectors at 0xffe0, a 2 KB stack
    reserve. Always link with `ld.lld -n`.
  - `mul.s`, `divmod.s` and `shift.s` hold `__mspabi_mpyi`/`mpyl`, the 16- and 32-bit
    `div`/`rem` family and `slll`/`srll`/`sral`.
    - Each file's header has the contract table. All of them clobber at most R11–R15.
    - The divides also leave the remainder in R14 or R14:R15.
  - `getch` and `malloc` wait for T20.
- **mspsim had a jump bug,** fixed in mspsim `090dbcc` with a test. Every forward jump of
  256–511 words went backwards.
- **Backend skeleton** (`backend/msp430/`, `genmsp430`):
  - `Msp_Instr` carries a form per opcode, and `msp_instr_size` derives the size from it.
    `SizesAgreeWithAssembler` checks the model on 58 cases against clang's assembler.
  - Immediates print sign-normalized to the operation's width. clang uses a constant
    generator only for a literal spelled 0, 1, 2, 4, 8 or −1, never for a symbol
    expression.
  - **clang's assembler rejects these ISA forms,** so selection goes through a register
    for them:
    - `@rN+` with a non-register destination;
    - `push` of anything but a register or an immediate;
    - `pop` to memory;
    - `br @rN`/`br @rN+`.

    `0(rN)` costs an extension word that `@rN` does not.
- **Run harness** (`msp430_test.h`, `book_test.h`, `msp430-tests`):
  - `mspsim -n 200000000` runs the image, and `main` returning 132 is told apart from an
    illegal instruction.
  - The runtime is tested against the host.
  - Book chapter 1 is compared with clang.

Phase 2 is done:
- **The naive selection** (`frame.c`, `instr.c`, `call.c`, `fp.c`, `data.c`, `relax.c`):
  - every variable is in memory;
  - copies, loads and stores go memory to memory;
  - an operation loads its first operand into r12 up and takes the second from memory
    or as an immediate.
- **The frame** is off SP, with no frame pointer:
  - the outgoing area at `0(r1)`, then the slots, each aligned to its type;
  - the pushed registers above;
  - operands into the incoming arguments completed by `gen_frame`.
- **Calls** follow clang's rules exactly: r12–r15 in order, the split `long`, backfill
  after a stack argument, every structure on the stack, every argument of a variadic
  callee on the stack, results in r12 up, an indirect call through r11.
- **Helpers.** Our code calls only ordinary-ABI helpers:
  - `__mspabi_*` for 16 and 32 bits;
  - the libgcc names for 64-bit integers and FP, with a second 64-bit operand at
    `0(r1)`.

  The R8–R11 `__mspabi_*` names are shims for clang's code (`mspabi64.s`), and the
  ordinary ones C wrappers (`mspabi.c`). Our comparisons are right for NaN; clang's
  `>` and `>=` through `__mspabi_cmpd` are not.
- **Runtime, built by `genmsp430`:**
  - the shared C library, but not the `printf` family (T16);
  - `float32.c`, and the new correctly rounded binary64 `float64.c` with `sqrt`,
    checked bit for bit against the host's `double`;
  - `int64.c`, `muldi3.c` (moved to `libc/common`), `frexp`/`ldexp`/`modf`;
  - a bump `malloc` in C.
- **Branch relaxation:** `jcc` → `j!cc; br`, `jn` → jump over a `jmp`, `jmp` → `br`,
  checked at ±512 words and assembled by clang.
- **Book chapters 1–20 pass,** compared with clang. Skipped, all for good and all as on
  AVR or for the same reasons:
  - `Chapter11_SwitchLong` and `Chapter14_SwitchDereferencedPointer`: case values
    collide in a 32-bit `long`;
  - `Chapter12_UnsignedTypeSpecifiers`: loops forever with a 16-bit `unsigned`;
  - `Chapter13_DoubleAndIntParamsRecursive` and its `Library` twin: past the cycle
    limit, clang's build too;
  - `Chapter15_BigArray`: a 16-bit `size_t`;
  - `Chapter16_AccessThroughCharPointer`: reads past a 16-bit `int`;
  - `Chapter16_CompoundBitwiseOpsChars` and `Chapter19_..._FoldCompoundBitwiseAssignAllTypes`:
    shift an `int` by 31;
  - `Chapter17_SizeofExtern`: too large for 15.5 KB of RAM.
- **786 MSP430 tests:** goldens, and runs of our code against the host for integer
  arithmetic and shifts, FP arithmetic, comparisons and conversions, calls, data,
  pointers and strings, structures, the allocator.
- **Findings along the way:**
  - a load through a pointer whose pointee is wider than the value overran the slot
    (fixed);
  - the frontend refuses `1.0 / 0.0` as a static initializer, on every target;
  - the frontend expands structure copies chunk by chunk, so the backend's copy loop
    serves only whole-aggregate moves.

Phase 3 is done:
- **GCC is the oracle and the toolchain (T27).**
  - GNU `as`, `ld` and `ar` build the runtime and link every test.
  - `libgcc.a` follows our `libc.a`.
  - clang and `ld.lld` are a second toolchain the tests use when present. The whole
    suite passed once on them as well.
  - The size model follows GNU `as`:
    - a source `0(rN)` is printed and counted as `@rN`;
    - `rla`/`rlc` of one is spelt out as `add @rN, 0(rN)`;
    - `push #4`/`#8` take an extension word.

    `SizesAgreeWithAssembler` checks the model against both assemblers.
- **Structures go by reference, as GCC passes them.**
  - The caller passes the address of its own object, uncopied, as an ordinary pointer
    argument.
  - The callee copies the object into its own slot in the prologue, through the address
    kept meanwhile in the slot's first word.
  - Struct parameter slots are therefore at least 2 bytes.
- **GCC's types.** MSP430 has its own `stddef.h` and `stdint.h` (AVR keeps the `ip16`
  ones):
  - `wchar_t` is `long`;
  - `wint_t` is `unsigned int`;
  - `sig_atomic_t` and the fast 8-bit types are `int`;
  - the 16-bit types stay `int`, where GCC has `short`, so that the shared `inttypes.h`
    fits them.
- **Variadics (T16), as GCC passes them.**
  - The last named argument and every later one go on the stack; earlier named ones take
    registers.
  - A structure goes as its address. `va_arg` follows the address through
    `__builtin_va_class`, now given by `tac_msp430_class`.
  - `printf`, `sprintf` and `snprintf` are in `libc.a`.
  - `int64.c`'s conversions moved to `int64conv.c`, so dividing a `long long` no longer
    links the float runtime. This applies to AVR, ARM32 and RV32 too.
- **Our code calls GCC's helpers by GCC's conventions (T17).** Our objects therefore link
  with libgcc and newlib alone:
  - `__mspabi_addd`/`mpyll` and the rest take a first operand in R8–R11, which the
    prologue saves;
  - `__mspabi_addf`, `fixdli`, `fltlid`, `cvtfd` and the like use the ordinary ABI;
  - the libgcc predicates do the FP comparisons;
  - `main` refers to `__crt0_call_exit` with `.refsym`, as GCC's does, so that newlib's
    startup passes its result to `exit`. Our crt0 defines the name.
- **The runtime serves GCC's code alone.**
  - `shift.s` has the complete shift groups of libgcc's `slli.o`, `srai.o` and `srli.o`
    (the `int` and `long long` shifts and the fixed-count entries), and the shared
    epilogues. A libgcc shift object would otherwise define our names twice.
  - There are aliases `divlu`/`divllu` and the 16-bit `fltid`/`fltud`/`fltif`/`fltuf`.
  - `mspabi.c` is split into `mspabif.c` (binary32) and `mspabid.c` (binary64), so that a
    program takes in only the runtime it uses.
- **Interop (T17).**
  - **Signature tables:** compiled by GCC and by us, each side calling the other, and
    with clang without the structure arguments. They cover the split `long`, R13:R14,
    `long long` backfill, five `int`s, narrow types, structures of 1–10 bytes, a union,
    nested ones and function pointers.
  - **Preserved registers:** R4–R10 survive our calls.
  - **GCC's code on our runtime:** checked against the host bit for bit, with every
    helper and the NaN comparisons. It is also linked with our runtime alone.
  - **clang's code on our runtime:** the same, but for NaN `>`/`>=`.
  - **Our helpers against libgcc's:** every `__mspabi_*` helper, linked side by side
    with libgcc and `libmul_none.a` prefixed `gcc`, agrees bit for bit over the operand
    table. Four programs keep it within the ROM.
  - **Our code under newlib:** newlib's startup, `printf`, `strtod` and `qsort` with our
    callback, and a GCC function taking our structure.
  - **clang's assembler** also takes every output when present.
- **Book programs (T18):** compared with GCC's own build, made with newlib through
  `--wrap=main`, and with clang's.
  - **Unskipped:** three programs, because GCC agrees with ours: `Chapter16_AccessThroughCharPointer`, `Chapter16_CompoundBitwiseOpsChars` and
    `Chapter19_..._FoldCompoundBitwiseAssignAllTypes`.
  - **Undefined at 16 bits:** in those three GCC and clang differ, so they are not
    compared with clang.
  - **The rest of the skip list** fails in GCC's build too, or GCC rejects it. The one
    exception, `Chapter3_BitwiseShiftrNegative`, is undefined and GCC differs.
- **Memory map:** 56 KB of ROM and 7.5 KB of RAM. The ROM grew because `printf` no longer
  fit.
- **Findings along the way:**
  - GCC and clang differ in three ways:
    - structure arguments;
    - the variadic rule for named arguments;
    - `wchar_t`.
  - newlib's modular startup needs the `.refsym`.
  - the shared `doprnt` prints at most about 16 significant digits (`%.17g` of 0.1 is
    `0.1`) on every target; T20 looks at it.
- **Tests:** 805 MSP430 tests, and the full suite (9837), pass.

## Phase 4 — library and headers

Phase 4 is done:
- **Headers (T19).**
  - **New:** `setjmp.h`. `jmp_buf` is nine words: R4–R10, SP after the return and the
    return address, the registers newlib's `jmp_buf` saves.
  - **Updated:** `math.h` lists what `libc.a` has. `sqrt` is the correctly rounded one
    of `float64.c`; `sqrtf` is `float32.c`'s, the same digit-by-digit algorithm in
    32-bit integers, checked against the host over all 2^32 inputs by a test run by
    hand (`Float32Host.DISABLED_SqrtfEveryInput`, `float32_tests.cpp`).
  - **Checked against GCC:** `HeadersAgreeWithGcc` compares our headers' sizes, limits
    and `float.h` values with GCC's `-mcpu=msp430` headers, and they agree.
  - **Checked against clang:** `HeadersAgreeWithClang` compares the same values. clang
    differs from GCC in more than `wchar_t`: its `wchar_t` and `wint_t` are `int`, its
    `sig_atomic_t` `long` and its fast 8-bit types `char`. We follow GCC.
  - **Shared headers:** `SharedHeadersFitInt16` covers `RAND_MAX`, `char32_t`,
    `inttypes.h`, `sqrt` and `sqrtf`.
- **`setjmp`/`longjmp` (T20)** are in `setjmp.s`. They work from our code and from GCC's
  and clang's (`RunSetjmpLongjmp`, `RunSetjmpLongjmpGccClang`).
- **Run tests (T20).**
  - **Ported:** the `printf`/`str`/`mem`/`math` tests come from AVR, with the host's
    output as the expectation. Two changes: plain `char` is unsigned, and a block is
    2-aligned. MSP430's `double` is the host's, so no digit carve-out is needed.
  - **Added:** x86-64's bit-exact `sqrt` runs, and a `sqrt`/`sqrtf` library test with
    GCC.
- **Against newlib (T20).** `RunAgainstNewlib` builds a case a second time with GCC and
  newlib, runs it under mspsim, and requires the same output and result.
  - **Every `str` and `mem` case** runs this way, except the `strerror` texts and
    `realloc`'s shrink in place.
  - **Every integer `printf` case within newlib-nano's formats** runs this way, plus a
    new `h`/`l` case.
  - **newlib-nano's limits:** it has no `ll`, `j`, `z`, `t` or `hh`, it dereferences a
    null `%s`, and it has no floating point (`-u _printf_float` links no converter).
- **`malloc` (T20)** stays MSP430's own C bump allocator, AVR's design. AVR keeps its
  assembly version, which is smaller there. `realloc` now keeps a block that shrinks in
  place, as AVR's does.
- **Sections and `--gc-sections`.**
  - **Why:** one object per source file, in whole-object linking, made `printf("%d")`
    53 KB. Every member came in whole, and `mspabid.o` → `int64conv.o` → `mspabif.o`
    pulled in the binary32 runtime as well.
  - **Change:** `genmsp430` now gives every function and variable a section of its own
    (`.text.f`, `.data.x`, `.bss.x`, `.rodata.x`), as GCC's `-ffunction-sections
    -fdata-sections` does. The tests link with `--gc-sections`; T24's driver must too.
  - **Result:** `printf("%d")` is now 33.8 KB.
- **`doprnt` (T20).**
  - **`FBUFSIZE`:** the 352-byte buffer fits. `%f` of `DBL_MAX`, the longest conversion,
    runs in the 7.5 KB of RAM (`PrintfDblMaxFits`).
  - **`%.17g` is not fixed.** The engine generates digits by `modf` in binary64: it
    multiplies the fraction by 10 and divides the integer part by 10, so digits past
    `DBL_DIG` need not be exact. `%.17g` of 0.1 prints `0.1`, and the 16th digit of
    `DBL_MAX` comes out as 7 where the host prints 5.
  - **Why not:** an exact conversion needs multiword arithmetic, roughly 1100 bits for a
    binary64 fraction. That is more code and cycles on every target, and the 16-bit ones
    can afford it least. It would be a shared task on its own, not an MSP430 one.
- **Costs against GCC (T20)**, in mspsim cycles; GCC `-O1` code, ours still naive (Phase 5):

  | | ours | GCC / newlib-nano |
  |---|---|---|
  | `printf("%d\n")` | 20 900 cycles, 33.8 KB | 6 000 cycles, 9.3 KB |
  | `printf("%g\n")` | 184 000 cycles | — (no float formats) |
  | `printf("%f", DBL_MAX)` | 26.6 M cycles | — |
  | binary64 `+` `-` `*` `/` | 3 900, 4 200, 27 600, 16 500 | 4 800, 4 800, 23 600, 11 300 |
  | binary32 `+` `-` `*` `/` | 2 400, 2 600, 3 300, 6 100 | 2 200, 2 200, 6 600, 2 700 |
  | the four binary64 operations | 15.1 KB | 5.5 KB |
  | the four binary32 operations | 6.4 KB | 2.8 KB |

  Speed is comparable, and our code is 2.5–3 times larger.
  - **Integer `printf` is 3.5 times slower,** because `doprnt` formats every integer as
    a `long long`.
  - **Size:** `__doprnt` alone is 9.8 KB, and every `printf` links the binary64 core
    (`cvt`, `+ - * /`, about 13 KB more).

  A newlib-style split, with float formatting in its own object linked on demand,
  would remove that core from integer-only programs. It is left for after Phase 5,
  which shrinks the code first.
- **Tests:** 897 MSP430 tests pass.

## Phase 5 — code quality

Phase 5 is done:
- **The selection works on operands where they lie (T21).**
  - Any operand may be a register or memory, on either side. So `d = a + b` is
    `mov a, d; add b, d`, and a compare is a `cmp` of the operands in place.
  - r15 is the one scratch register: a pointer from memory, a shift count, a constant
    compared, or a copy loop's pointer, with r13 and r14 pushed around the loop.
  - Helper operands and call arguments go into place as one parallel move. A cycle is
    broken by three `xor`s, so no temporary is needed.
  - The same selection serves `--no-regalloc`, with every operand in memory.
- **Register allocation (T21)** on `backend/common/regalloc.c`.
  - **Classes:** an `int`, a pointer or a `char` takes one register; a `long` or a
    `float` takes two, not necessarily adjacent; a `long long` or a `double` stays in
    memory.
  - **Pools:** r12, r13, r14 and r11 for values not live across a call or a helper,
    then r10–r4, which the prologue pushes. r15 is never allocated, so a value that
    arrives there is moved.
  - **r8 helpers:** a function with a helper that takes its first operand in r8–r11
    keeps r8–r10 free of variables.
  - **Numbering:** registers keep their own numbers on the allocator's side, since 0 is
    never in the pool.
  - **No narrower clobber sets:** every one of our helpers may clobber r11–r15, as a
    call does, so a narrower set gains nothing.
  - The ch. 20 tests pass.
- **Frameless functions (T22).** A leaf with its variables in registers has no slots and
  saves nothing, so it touches neither SP nor the stack: `add r13, r12; ret`, as GCC's.
  Its early returns are `ret` in place.
- **In selection (T23):**
  - a comparison (or `!x`) whose result only the next conditional jump reads is a `cmp`
    and that jump, with no 0/1 in between;
  - a constant first operand is turned around (`k < b` is `cmp #k+1, b; jge`);
  - a 16-bit multiply by a small constant, and an index scaled by one, is done inline in
    r15 by Horner's rule;
  - a structure parameter is read through the incoming pointer, uncopied, when three
    things hold:
    - the callee only reads it, by member or whole;
    - it makes no call and no store through a pointer;
    - it writes no global.

    The caller's object then cannot change meanwhile, so `return s.b` is
    `mov 2(r1), r15; mov 2(r15), r12; ret`.
- **The peephole pass (T23, `peephole.c`)**, over the body to a fixed point:
  - **Constants:** the constant-generator aliases (`clr`, `inc`, `incd`, `dec`, `decd`,
    `tst`, `adc`, `inv`). A neutral constant goes (`bis #0`, `and #-1`), and so does
    `add #0` ahead of an `addc`.
  - **Jumps:** a branch over a jump becomes the inverse branch, and a jump to the next
    line goes.
  - **Forward pass:** it tracks copies, constants, memory words held in registers and
    bytes already extended. Moves, reloads, stores of what is already there, and a
    `mov.b r, r` of an extended byte go. A read of a copy reads the oldest register
    holding it.
  - **Backward pass**, on the liveness of r4–r15 and SR:
    - dead instructions go;
    - a load moves forward into its one use;
    - a load, an operation and a store back become one operation on memory;
    - an add of a constant to a base becomes the offset (`mov a(r15), r14`,
      `mov 6(r12), r12`), so a struct copy is memory to memory;
    - consecutive loads into registers use `@rN+`;
    - a `tst` whose flags the instruction before already set goes, under the C and V
      rules.
  - **After the frame:** a jump to a lone `ret` is `ret`, and `call; ret` is a tail
    jump `br`.
  - Volatile accesses carry a flag that the pass leaves alone. Branch relaxation runs
    after it.
- **Measured** against `msp430-elf-gcc -O2 -mcpu=msp430` and clang `-O2`.
  - **Code size, in bytes:** the C library is its 39 C sources; the book programs are the
    677 that all four compile, text only.

    | | phase 4 | T21 | T22 | T23 | GCC `-O2` | clang `-O2` |
    |---|---|---|---|---|---|---|
    | C library | 60 964 | 41 800 | 41 766 | 36 404 | 38 016 | 40 142 |
    | book programs | 365 688 | | | 195 794 | 99 942 | 85 366 |

  - **Cycles / code bytes:** benchmarks linked with our runtime, as built at the time of
    each run. T22 changed none of them. The binary64 loop spends nearly all its time in
    the runtime, which every column of a run shares, so only the last run's cycles are
    given for it.

    | | phase 4 | T21 | T23 | GCC `-O2` | clang `-O2` |
    |---|---|---|---|---|---|
    | bubble sort, 64 ints | 381 326 / 648 | 125 436 / 288 | 83 807 / 190 | 32 309 / 156 | 45 803 / 142 |
    | sieve to 2000 | 408 705 / 244 | 150 037 / 100 | 91 796 / 56 | 74 650 / 138 | 79 567 / 60 |
    | CRC-16, 1 KB | 749 546 / 524 | 212 205 / 202 | 147 129 / 156 | 119 922 / 98 | 21 065 / 156 |
    | binary64 loop | 5 838 325 / 814 | — / 664 | 5 823 478 / 568 | 5 799 336 / 310 | 5 798 287 / 676 |
    | string copy and compare | 387 055 / 690 | 116 048 / 258 | 108 082 / 214 | 72 701 / 228 | 74 723 / 180 |

  - **Reading the numbers:**
    - The library is now smaller than both compilers'.
    - The book programs are twice GCC's, since `-O2` folds and inlines most of them
      whole.
    - On sort, GCC still wins: the same index is computed again and again, a common
      subexpression that is a TAC-level matter for every backend.
    - clang's CRC is folded at compile time.
- **Book programs set aside:** three programs are undefined at 16 bits, and their
  results depend on garbage. Two declare `strlen` as returning `unsigned long`, so they
  read r13; one reads past an `int` and past an array.
- **Tests:** 914 MSP430 tests, and the full suite (9947), pass.

## Phase 6 — finishing

- **T24. Driver.** `vcc -t msp430` runs:
  - `vcpp -t msp430`, `vparse`, `vlower -t msp430`, `vgenmsp430`;
  - `msp430-elf-as -mcpu=msp430`;
  - `msp430-elf-ld --gc-sections -T link.ld crt0.o … -lc`, then `libgcc.a` when it is
    found, so that objects compiled by GCC link too.

  `cc-tests` cases, including a staged prefix. The output is an ELF that mspsim runs
  directly. Intel HEX (`msp430-elf-objcopy -O ihex`) is documented, and also runs.
  `VCC_AS`/`VCC_LD` with clang and `ld.lld -n` are documented, with a `cc-tests` case that
  skips without them.
- **T25. Install.** `genmsp430` as `vgenmsp430`; `crt0.o`, `libc.a`, `link.ld` and the
  headers (MSP430, `ip16` and shared) under `share/vcc/msp430/`. The runtime is installed
  only when the GNU MSP430 binutils were found.
- **T26. Documentation.**
  - `docs/Msp430_Backend.md`, in the style of `docs/Avr_Backend.md`:
    - the target, and how code is generated;
    - the memory-to-memory selection and the constant generators;
    - frames and branch relaxation;
    - calls and variadics, with the split-`long` rule and structures by reference;
    - where clang differs (structure arguments, the variadic rule, `wchar_t`,
      `wint_t`, `sig_atomic_t`, the fast 8-bit types, `cmpd`);
    - sections per function and `--gc-sections`, and the costs against GCC (Phase 4);
    - the soft binary64 and the MSPABI helper contracts;
    - running a program by hand under mspsim, with `-t` tracing and `-g` debugging.
  - README and CLAUDE.md for seven targets, `libc/msp430/include/README.md`,
    `libc/ip16/include/README.md`, and `docs/Type_Sizes_Alignment.md`.
  - Remove `msp430/` from the "design notes only" sentence.

  This plan is then removed.

## Out of scope

- **MSP430X:** the 20-bit registers and addresses, the large memory model, and the
  `.a`/`calla`/`pushm`/`popm`/`rrcm` instructions. mspsim does not model them.
  `msp430.asdl` keeps them as spec, and the IR should not preclude 4-byte pointers.
- **The hardware multiplier peripheral** (`-mhwmult=16bit/32bit/f5series`). mspsim has
  none. Supporting it is a runtime variant of `mpyi`/`mpyl`, a natural follow-up once
  mspsim models MPY.
- **Device support:** no `-mmcu` table, TI device headers or linker scripts, info memory
  or flash programming.
- **Interrupt handlers** (`__attribute__((interrupt))`, `reti`) and low-power modes.
  - A `<msp430.h>` with `dint`/`eint`/`nop`/low-power intrinsics, in the manner of
    `<besm6.h>`, is a natural follow-up.
- **clang's structure arguments.** There is no option for clang's by-value convention,
  so a struct argument between our code and clang's is not supported.
- **newlib as our C library.** We ship our own `libc.a`; newlib is a test reference.
- **Linker relaxation, and running on real hardware.**

## Risks

- **Structures by reference with a callee copy.** The caller hands over its own object
  uncopied. A callee that writes before copying, or reads after a store that aliases the
  object, corrupts the caller's data with no fault.
  - Mitigation: the copy is the first thing the prologue does (T27); T17's aggregate
    cases write to every parameter and check the caller's object; the no-copy shortcut
    (T23) requires that the parameter is only read, and that the callee makes no call,
    no store through a pointer and no write to a global.
- **Helper contracts.** GCC's and clang's code trust what their compilers believe about
  the R8–R11 operands of `__mspabi_addd` and the like, and clang's about `cmpd`'s
  unordered result. A wrong guess breaks only the foreign side, or only NaN comparisons,
  far from its cause.
  - Mitigation: one contract table per runtime file, T17 running both compilers' code
    (NaN included) against our runtime, and our helpers checked against `libgcc`'s.
- **A soft binary64 on a 16-bit CPU.** It is large and slow, and the first in the
  project. A rounding bug shows up as a wrong last digit in `printf`, or a comparison
  disagreeing with the folder.
  - Mitigation: T13's correctly-rounded design, tested against the host's native
    `double` over edge cases; measure code size and cycles at T13 and T20, and widen the
    memory map or cycle limit if needed.
- **The size model for relaxation.** The constant-generator rule decides between 2, 4
  and 6 bytes, and the two assemblers differ on `0(rN)`.
  - Mitigation: T6's golden test against the assembled object for every operand form,
    under both assemblers (T27). An underestimate is a loud assembler error, an
    overestimate only early relaxation.
- **Silent misaligned word access.** A word access ignores bit 0, so a layout or pointer
  bug reads the wrong word without a fault.
  - Mitigation: T2's layout checks against the compilers; alignment kept through
    `ALLOCATE_LOCAL` and the outgoing area; byte copies for 1-aligned structs.
- **7.5 KB of RAM.** A stack overflow into `.bss` is silent.
  - Mitigation: the link-time stack reserve, the canary checked at `exit`, and the book
    skip list.
- **Irregular flags.** `mov` sets none, `bit`/`and` set C = !Z, and `xor` sets V oddly. A
  peephole that assumes otherwise drops a needed compare.
  - Mitigation: one per-opcode flag table from `MSP430_Instruction_Set.md`, and run tests
    for every comparison after every kind of producer.
- **mspsim is young.** Its CPU passes the openMSP430 instruction tests, but no compiler
  has used it at scale. Its newlib host I/O is newer still.
  - Mitigation: GCC's code as a control whenever a run result looks wrong, and mspsim's
    `-t` trace, which also lists every host call (`*** CIO write(1, 36) = 36`). A
    simulator bug is fixed in `../mspsim`, with a test there.
