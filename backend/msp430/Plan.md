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
| Memory map | Data, `.bss` and stack in 0x0200–0x3FFF (15.5 KB); code and `.rodata` in 0x4000–0xFFDF (48 KB); vectors in 0xFFE0–0xFFFF, reset → `_start` | mspsim is all RAM, so the split is our choice. It mirrors a flash device, so the runtime stays honest about `.data` copying. Revisit at T5 if code size demands |
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
- **Variadic callees** take *every* argument on the stack, named ones included
  (*checked*: `va(1, 2L, 3.0)` stores all three at `0(r1)`…`12(r1)`). `va_list` is a
  plain pointer.
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
| Values not live across a call | R11–R15 |
| Values live across a call | R4–R10, pushed and popped in the prologue and epilogue |
| Naive-selection scratch | R14, R15 (pointer bases, helper operands), R11 |

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

## Phase 3 — ABI conformance

- **T27. GCC as the oracle and toolchain.** This comes first in Phase 3. Phases 0–2 were
  built against clang; this step moves them to GCC. clang becomes the second oracle.
  - **Tools** (`libc/msp430/CMakeLists.txt`):
    - find `msp430-elf-as`, `-ld`, `-ar` and `-gcc` (`MSP430_AS`, `MSP430_LD`,
      `MSP430_AR`, `MSP430_GCC`);
    - assemble the runtime (`crt0.S` through `msp430-elf-gcc -c`, the `.s` files and the
      C library's `genmsp430` output) with GNU `as`;
    - archive with `msp430-elf-ar`;
    - find GCC's `libgcc.a` through `msp430-elf-gcc -mcpu=msp430
      -print-libgcc-file-name` (`MSP430_LIBGCC`).

    `MSP430_TOOLS_FOUND` now means GNU binutils and mspsim. clang and `ld.lld` become
    optional (`MSP430_CLANG_FOUND`).
  - **Run harness** (`msp430_test.h`): the program is assembled by GNU `as` and linked by
    `msp430-elf-ld -T link.ld crt0.o prog.o libc.a` (no `-n`).
  - **Structures by reference:**
    - **The caller** (`call.c`) passes the address of each struct or union argument as a
      pointer argument in the usual register or stack place, and copies nothing. An
      rvalue's temporary is already a frame slot, so its address serves.
    - **The callee** (`frame.c`) gives each struct parameter a frame slot of its own and
      copies the object into it through the incoming pointer in the prologue. The body
      is unchanged.
    - The variadic case (a pointer on the stack) is T16's.
    - This is backend-only: the frontend still hands the backend a struct-typed
      argument.
  - **`wchar_t` = `long`:**
    - `libc/msp430/include/` gets its own `stddef.h`, ahead of `ip16`;
    - `WCHAR_MIN`/`WCHAR_MAX` become 32-bit, in a `stdint.h` of its own or through a
      macro the `ip16` one tests;
    - AVR keeps `int`, as avr-gcc has it.
  - **Size model:**
    - selection emits `@rN` for a `0(rN)` source, so that clang's and GNU `as`
      encodings agree;
    - `SizesAgreeWithAssembler` checks the model against GNU `as`, and against clang's
      when present.
  - **Book comparison:** `book_test.h` compares with GCC's build, not clang's. That
    build is wholly GCC's: `msp430-elf-gcc -mcpu=msp430 -msim -O0`, with newlib and its
    startup. So the reference shares neither our C library nor our runtime, and a bug in
    either cannot hide by appearing on both sides.
    The present skip list is rechecked: an entry that GCC's build runs correctly is a
    bug of ours, not a limit of the target.
  - **The clang-specific runtime stays.** `mspabi64.s` (the R8–R11 shims, which GCC's
    code also calls) and the `__mspabi_cmpd`/`cmpf` that only clang calls are still
    needed for clang interop.
  - **Gate:** the 786 tests pass on the GNU tools. Rerun them with clang's assembler and
    `ld.lld -n` once, to show that nothing was lost.
- **T16. Variadic functions and `<stdarg.h>`.**
  - **Calls:** for a variadic callee, *every* argument goes on the stack, named ones
    included. A struct argument goes there as its pointer. An unprototyped callee is
    called as non-variadic.
  - **The variadic function** finds all its parameters on the stack, so its prologue
    stores nothing.
  - **`va_list`** is `char *`. GCC's and clang's `__builtin_va_list` are both a plain
    2-byte pointer, so a `va_list` handed to or from their code is the same thing.
  - **`va_start(ap, last)`** is `__va_start(ap)`, intercepted by the backend as on the
    other targets.
  - **`va_arg(ap, T)`:**
    - for a scalar, it is the AVR pointer walk, with the size rounded up to 2;
      alignment is at most 2, `char`/`short` promote to `int`, and `float` to the 8-byte
      `double`;
    - for a struct or union, it reads a pointer, and the object through it.
  - **Gate:** `printf` in `libc.a` works.
- **T17. Interop tests** with GCC in both directions, over a table of signatures, built
  before the code they test. Then the same table with clang, structure arguments
  excluded:
  - **Register assignment:**
    - the split `long` in R15 and the stack;
    - a `long` in R13:R14;
    - a `long long` after one `int` going to the stack while later `int`s backfill
      registers;
    - five `int`s.
  - **Narrow types:** `char`, `signed char` and `unsigned char` arguments and results.
  - **Aggregates:** structs of 1, 2, 3, 4, 6 and 10 bytes and a union, as arguments
    mixed with scalars and as results. Each callee writes to its parameter, and the
    caller checks that its own object is unchanged.
  - **Wide scalars:** `long long`, `float` and `double` arguments and results.
  - **Function pointers both ways.**
  - **Preserved state:** R4–R10 survive our calls, including GCC's callers that return
    through `__mspabi_func_epilog_N`.
  - **Variadics both ways,** a struct among the variadic arguments, and a `va_list`
    handed across.
  - **Linking:** a mixed program is linked as ours are, then `libgcc.a` for the
    GCC-only names (the `int` shift helpers, the shared epilogues).
  - **GCC's code on our runtime:** it multiplies, divides, shifts, and does `float` and
    `double` arithmetic and comparisons with NaN, so every helper contract is exercised
    by GCC's own assumptions. The same program compiled by clang reaches
    `__mspabi_cmpd`/`cmpf` and the R8–R11 shims.
  - **Our helpers against libgcc's:**
    - every helper both runtimes have is run over the same operand table, edge cases
      included: division by zero, the most negative dividend, NaN, infinities,
      subnormals, and conversions out of range;
    - results are compared bit for bit;
    - a disagreement is settled by C11 or IEEE 754 where they define the result, and
      otherwise recorded in the runtime's contract table.
  - **Our code with newlib:** a small program of ours calls newlib's `printf`, `strtod`
    and `qsort` (with a callback of ours). Our object is linked by `msp430-elf-gcc
    -mcpu=msp430 -msim` with newlib, its startup and `libgcc.a`, and runs under mspsim
    with newlib's own I/O. This tests variadics and function pointers against a large
    body of code nobody here wrote. The reverse, newlib's `qsort` calling our comparison
    function, comes in the same test. Our own `main` is then called by newlib's `crt0`,
    and its result passes through newlib's `exit`.
  - **clang's assembler on every output**, when it is installed. `msp430_test.h` also
    assembles each golden and run test's `.s` with `clang --target=msp430 -c`, as the
    x86-64 tests do with a second assembler.
- **T18. Differential book tests.** Every book program is also compiled by GCC
  (`-mcpu=msp430 -msim -O0`, with newlib), run under mspsim, and the outputs and
  statuses are compared, as in the AVR suite. This is what makes a 16-bit `int`
  testable.
  - **clang as a third compiler.** Each program is also built by clang and run. A whole
    program has no cross-compiler calls, so the structure argument difference does not
    arise.
  - **Disagreements.** Our output must match both. Where GCC and clang disagree with
    each other, the program depends on behaviour C leaves undefined or unspecified at 16
    bits. It goes on a list with the reason, and there ours must match GCC's.

## Phase 4 — library and headers

- **T19. Headers.** `libc/msp430/include/`, ahead of `libc/ip16/include/` (T3) and
  `libc/common/include/`:
  - `float.h`: binary32 `FLT_*`, binary64 `DBL_*` = `LDBL_*`, and `FLT_EVAL_METHOD` 0;
  - `math.h` for binary64 `double`, with `long double` = `double`;
  - `setjmp.h`: R4–R10, SP and the return address;
  - T16's `stdarg.h`, and T27's `stddef.h`;
  - `RAND_MAX` 32767, in the target-owned place AVR moved it to.

  Add an `msp430-headers` CTest and its `-cpp` twin. Check our headers' sizes, limits and
  type identities against GCC's `-mcpu=msp430 -dM -E`, as ARM32, x86-64 and AVR did
  against clang. Then check them against clang's, where only `wchar_t`/`WCHAR_*` should
  differ.

  Compare `jmp_buf` with newlib's (`~/.local/msp430-elf/include/machine/setjmp.h`) to
  check which registers must be saved, not its layout: it is a different library.
- **T20. Libc and run tests.**
  - **`doprnt.c`** already sizes `%z`/`%t` by `size_t`/`ptrdiff_t`, and its buffers by
    `DBL_MANT_DIG` (AVR, M21). Check them for binary64 on a 16-bit `int`. The ~350-byte
    `FBUFSIZE` fits the stack, but count it against the 15.5 KB.
  - **`frexp`/`ldexp`/`modf`** come from `libc/ilp32` (binary64).
  - **`malloc`** sits between `__heap_start` and the stack. Reuse AVR's design, ideally
    as one portable C allocator in `libc/common` if its assumptions allow, else
    MSP430's own.
  - **`setjmp`/`longjmp`** in assembly.
  - **Run tests:** port the `printf`/`str`/`mem`/`math` run tests, with host libc output
    as the expectation. MSP430's `double` is the host's, so no digit carve-out is needed.
    Measure the cycle cost of `printf("%g")` through soft binary64.
  - **Integer formats against newlib.** Build the integer-format cases with
    `msp430-elf-gcc -msim` as well, and run them through newlib's `printf` under
    mspsim: a second expectation, from a `printf` on the target. newlib was built
    without float formatting, so it cannot check `%f`/`%e`/`%g`.
  - **The `str` and `mem` run tests** run against newlib the same way.
    Each case is one program, compiled once by `genmsp430` with our `libc.a` and once by
    GCC with newlib, and the two outputs must be equal. This adds to the host
    expectation; it does not replace it.
  - **Cost against GCC.** Compare the cycles and code size of our `printf` and the
    `float64.c` operations with newlib's `printf` and `libgcc`'s FP. This is a
    measurement for the docs, not a gate.

## Phase 5 — code quality

- **T21. Register allocation** on `backend/common/regalloc.c`.
  - **Classes:**
    - `char`/`short`/`int`/pointer are `REGALLOC_INT`;
    - `long`/`float` are `REGALLOC_PAIR` of consecutive registers;
    - `long long`/`double` are `REGALLOC_NONE`.
  - **Numbering:** registers are numbered from 1 on the allocator's side, as ARM32 and
    AVR did. Only R4–R15 are allocatable.
  - **Pools:**
    - R12–R15 and R11 for values not live across a call, with result hints on R12
      (R12:R13 for a `long`);
    - R4–R10 for values live across a call, pushed and popped.
  - **Helper calls:**
    - every helper is reported through the `runtime_call` hook;
    - the R8–R11-argument helpers also clobber R8–R11 as far as our code is concerned.
      That is conservative, whatever GCC assumes;
    - the integer helpers, whose contracts are narrower than a call, get a narrower
      clobber set only if ch. 20 shows it to be worth it. Record the decision here.

  The ch. 20 tests pass.
- **T22. Frameless functions.** A function with no slots, no outgoing stack arguments and
  no call-saved registers does no `sub`/`add` on SP and pushes nothing. The common case
  of a small leaf is then the bare body and `ret`, as GCC's `f1` is.
- **T23. Peephole.**
  - **Constants:**
    - prefer the constant-generator forms: `clr`, `inc`, `incd`, `dec`, `decd`, `tst`,
      and `#1`/`#2`/`#4`/`#8`/`#-1` immediates;
    - fold a zero high word out of `bis`/`xor`, and an all-ones one out of `and`;
    - inline shift-and-add for multiplication by a small constant.
  - **Memory operands:**
    - fold a load-then-operate into the memory form (`mov x(r1), r15; add r15, r14` ⇒
      `add x(r1), r14`), and operate directly on a memory destination;
    - use `@rN+` for consecutive word loads;
    - no reload of a word just stored.
  - **Flags:**
    - a liveness pass over SR, driven by the per-opcode flag table;
    - compare-and-branch fusion, with no 0/1 materialized;
    - drop a `tst` or `cmp #0` whose flags the previous instruction already set, under
      the C = !Z and V rules.
  - **Extensions:** no `mov.b` or `sxt` of a value already extended.
  - **Branches:** branch over jump, no jump to the next line, and a tail call as `br #f`
    when the epilogue leaves nothing on the stack.
  - **Struct parameters:** a callee that never writes to a struct parameter and never
    takes its address reads through the incoming pointer, with no copy, as GCC does.

  Branch relaxation (T10) runs after all of these.

  **Measure:** the code size and cycles of the book programs and the C library against
  `msp430-elf-gcc -O2 -mcpu=msp430` and clang `-O2`, before and after each of T21–T23.
  This is a table for the docs, not a gate. GCC's output is the first place to look for
  idioms worth a peephole rule.

## Phase 6 — finishing

- **T24. Driver.** `vcc -t msp430` runs:
  - `vcpp -t msp430`, `vparse`, `vlower -t msp430`, `vgenmsp430`;
  - `msp430-elf-as -mcpu=msp430`;
  - `msp430-elf-ld -T link.ld crt0.o … -lc`, then `libgcc.a` when it is found, so that
    objects compiled by GCC link too.

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
    - where clang differs (structure arguments, `wchar_t`, `cmpd`);
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
    waits for T23 and requires that the parameter is never written and never has its
    address taken.
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
- **15.5 KB of RAM.** A stack overflow into `.bss` is silent.
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
