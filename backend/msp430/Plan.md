# MSP430 backend — development plan

A seventh backend, for the classic 16-bit TI MSP430. It uses the MSP430 ABI as clang
implements it, emits ELF objects, and runs its programs bare-metal under **`mspsim`**, our
own simulator (`~/.local/bin/mspsim`, sources in `../mspsim/`). The code is
link-compatible with clang's `--target=msp430` output. clang is therefore the test
oracle: interop runs in both directions, and every book program is compiled by both
compilers and the outputs compared.

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
- **Every struct and union travels in memory.** Arguments are copied onto the stack, and
  results come back through a hidden pointer in R12, even for a 1-byte struct.
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
| CPU | Classic MSP430 (16-bit registers, 64 KB address space), no MSP430X | mspsim models exactly this. clang's default `--target=msp430` (no `-mmcu`) generates it |
| Data model | `char` 1 (**unsigned**), `short` 2, `int` 2, `long` 4, `long long` 8, pointer 2, `size_t` = `unsigned int`, `ptrdiff_t` = `int`, `wchar_t` = `int`, `_Bool` 1; alignment 1 for `char`, 2 for everything else | clang `-dM -E` for the triple, and `sizeof`/`_Alignof` compiled by clang (checked) |
| Floating point | `float` binary32, `double` = `long double` binary64, all in software | clang's `__DBL_MANT_DIG__` = `__LDBL_MANT_DIG__` = 53 |
| Multiply | Software (`__mspabi_mpyi` and friends) | No CPU multiplier. clang without `-mhwmult` calls the helpers (checked). mspsim has no MPY peripheral |
| ABI | The MSP430 EABI as clang implements it (details below) | Interop with clang-compiled code and with clang's helper calls |
| Output | GNU msp430-as syntax as clang emits it: `#imm`, `&abs`, `x(rN)`, `@rN+`, the `.b` suffix, `;` comments | Accepted by clang's integrated assembler. GNU `msp430-elf-as` is not installed, so there is no second assembler |
| Toolchain | Homebrew `clang --target=msp430` (assemble), `ld.lld -n` (link), `llvm-ar` | The LLVM used by every other backend. No msp430-elf-gcc, binutils or TI headers needed |
| Run environment | `mspsim -n <cycles> <elf>` | Our own simulator: ELF loader, console UART, and an exit device |
| I/O and exit | stdout through the USCI_A0 UART (`UCA0TXBUF` 0x0067, poll `IFG2` 0x0003 bit 1). `exit` writes the status to the stop register 0x01FE, which becomes mspsim's exit status | No timeout-and-kill protocol is needed, unlike AVR. The cycle limit `-n` gives deterministic timeouts |
| Memory map | Data, `.bss` and stack in 0x0200–0x3FFF (15.5 KB); code and `.rodata` in 0x4000–0xFFDF (48 KB); vectors in 0xFFE0–0xFFFF, reset → `_start` | mspsim is all RAM, so the split is our choice. It mirrors a flash device, so the runtime stays honest about `.data` copying. Revisit at T5 if code size demands |
| Backend IR | A small hand-written `Msp_Instr` list, as in `avr_ir.h`. Every instruction knows its size, for branch relaxation | `msp430.asdl` stays the reference spec. The IR covers only what we emit |
| Executable | `genmsp430` (`backend/msp430/`), library `msp430`; installed as `vgenmsp430` | Mirrors `genavr`/`vgenavr` |

Verified 2026-10-04 on this machine, with scratch programs (not in the tree):

- **Toolchain.** Homebrew clang 23.1.2 lists `msp430 - MSP430 [experimental]`. `ld.lld`
  23.1.2 links an MSP430 ELF.
  - Without `-n`, lld emits a read-only segment holding the ELF headers at address 0,
    over the peripheral area. **`ld.lld -n`** drops it, leaving only the four real
    segments.
  - clang-compiled objects reference no `__do_copy_data`/`__do_clear_bss`, unlike AVR.
  - The address of a function is `mov #f, r12` and `.short f` in data.
- **Run.** A hand-written crt0 does the following:
  - sets SP;
  - calls `main`;
  - writes the result to `&0x01fe`.

  A `putbyte` polls `IFG2` and writes `UCA0TXBUF`. The program printed `.text`,
  `.rodata` and `.data` strings, and mspsim exited with `main`'s 200. The Intel HEX from
  `llvm-objcopy -O ihex` runs as well.
- **Speed.** A million iterations of a `volatile long` add loop take 43 M cycles, 0.82 s
  under mspsim: about 52 M cycles per second. A cycle limit of 250 M is a ~5-second
  budget.
- **Branches.** A `jeq` whose target is 600 `nop`s away fails in clang's assembler with
  "fixup value out of range". The failure is loud, not a silent miscompile, but nothing
  relaxes it for us.
- **No runtime.** Linking a program that multiplies `long`s fails on `__mspabi_mpyl`.
  There is no compiler-rt for MSP430 here, so we supply every helper, as for AVR.

### The MSP430 ABI, as clang implements it

Checked against clang's output where marked. All of it is exercised against clang, both
ways, by the interop tests (T17).

- **Fixed registers:** R0 = PC, R1 = SP, R2 = SR (and constant generator 1), R3 =
  constant generator 2.
- **Call-saved:** R4–R10. **Call-clobbered:** R11–R15. There is no frame pointer by
  default: clang addresses the frame as `x(r1)`, and the 16-bit offset has no range
  problem (*checked*). SP is always even; `push.b` moves it by 2.
- **Arguments** go in R12, R13, R14 and R15, left to right, each taking the next free
  registers (*checked*):
  - A 16-bit value takes one register.
  - A 32-bit value takes the next **two consecutive** registers, not necessarily
    starting on an even one: `kl(int, long, long)` puts the first `long` in R13:R14.
  - When only R15 is left, the `long` is **split**: its low word goes in R15 and its
    high word on the stack (`k(int, int, int, long)`, both sides *checked*). This may
    differ from TI's EABI. clang is the oracle; record it in the docs.
  - A 64-bit value goes in R12–R15 only when all four are free, else wholly on the stack.
  - **Later arguments still take free registers:** in `g(int, long long, int)` the
    `long long` goes on the stack and the last `int` in R13 (*checked*).
  - A `char` is extended by the caller, and clang's callee trusts it (`r4(signed char,
    unsigned char)` adds without extending, *checked*). We extend as the sender and
    re-extend as the receiver, as on AVR and x86-64.
  - **Every struct or union argument goes on the stack**, whatever its size, and the
    later scalars still take registers (`f10(int, struct S4, int)` uses R12 and R13,
    *checked*).
- **Stack arguments** sit above the return address, in parameter-list order, 2-aligned.
  The caller removes them.
- **Variadic callees** take *every* argument on the stack, named ones included
  (*checked*: `va(1, 2L, 3.0)` stores all three at `0(r1)`…`12(r1)`). `va_list` is a
  plain pointer.
- **Results:**
  - 16 bits in R12, 32 in R12:R13, 64 in R12–R15 (*checked*);
  - **every** struct or union through a hidden pointer passed in R12 as the first
    argument, even a 1-byte one (*checked*).

  Whether the caller may rely on R12 still holding that address on return is not
  established. We return it, and never rely on it.
- **Calls:**
  - a direct call is `call #f`;
  - an indirect one is `call rN` (or `call x(r1)`, `call &fp`) on a plain byte address.
- **Runtime helpers.** clang calls TI's `__mspabi_*` names (*checked*, the complete set
  for the C operators and conversions):
  - **Integer multiply:** `mpyi`, `mpyl`, `mpyll`.
  - **Integer divide:** `divi`/`divu`/`remi`/`remu`, `divli`/`divul`/`remli`/`remul`,
    `divlli`/`divull`/`remlli`/`remull`.
  - **`long` shifts by a variable:** `slll`, `srll`, `sral`. The count goes in R14,
    zero-extended from a byte. `int` shifts and `long long` shifts are inline loops.
  - **binary32:** `addf`, `subf`, `mpyf`, `divf`, `cmpf`.
  - **binary64:** `addd`, `subd`, `mpyd`, `divd`, `cmpd`.
  - **Conversions:** `cvtfd`, `cvtdf`; `fixfli`, `fixful`, `fixflli`, `fixfull`,
    `fixdli`, `fixdul`, `fixdlli`, `fixdull`; `fltlif`, `fltulf`, `fltllif`,
    `fltullf`, `fltlid`, `fltuld`, `fltllid`, `fltulld`. An `int` is converted through
    the `long` helper after extension.
  - **Library calls:** `memcpy` for struct copies, `memset` for zeroing, `sqrt`.

  **Special contracts.** The 64-bit two-operand helpers (`addd`, `subd`, `mpyd`, `divd`,
  `cmpd`, `mpyll`, …) take the **first operand in R8–R11** and the second in R12–R15,
  and return in R12–R15 (*checked* for `addd`, `cmpd` and `mpyll`). The caller saves
  R8–R10 itself because it loads them. `cmpd`/`cmpf` return a negative, zero or
  positive `int` in R12, and clang tests it with `jl`/`jeq`.
  - The unordered (NaN) result must be the one that makes every ordered comparison false
    under the operand order LLVM chooses.
  - Take each contract (inputs, outputs, clobbers, the unordered result) from LLVM's
    MSP430 lowering, its `MSP430_BUILTIN` calling convention and the libcall table, so
    that clang-compiled callers' assumptions hold.

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

## Phase 2 — instruction selection, book order

Naive and correct first. Every TAC variable lives in a frame slot. Because the ISA is
memory-to-memory, most operations need no register at all:

```
mov  4(r1), 8(r1)     ; t = a          (low word)
mov  6(r1), 10(r1)    ;                (high word)
add  12(r1), 8(r1)    ; t += b, low word, sets C
addc 14(r1), 10(r1)   ; high word, with the carry
```

Scratch registers are needed only for pointer bases, helper operands and results.
`mov` sets no flags, so a carry chain survives the moves between its links.

Each step is done when its book chapters pass and a few golden tests pin the selected
instructions.

- **T8. Frame.** Slots come from typed TAC and `ALLOCATE_LOCAL`, each aligned to its
  type (1 or 2). The frame size is rounded up to even.
  - **Prologue:** push the call-saved registers used (R10 down to R4), then `sub #N,
    r1`. The epilogue is the reverse, then `ret`.
  - **No frame pointer:** every slot is `x(r1)` with a 16-bit offset. There is no
    displacement limit to manage, unlike AVR's `Y+63`.
  - **Outgoing arguments** have a preallocated area at the bottom of the frame
    (`0(r1)` up), sized for the largest call, as on RISC-V. Arguments are stored there
    with `mov`, not pushed, so SP is constant in the body and every slot offset is
    fixed. clang instead does `sub`/`add` around each call; both are ABI-correct.
  - **Incoming stack arguments** are above the saved registers and the 2-byte return
    address. Incoming register parameters are stored to their slots in the prologue.

  *Done.* The whole naive selection landed with this step: `frame.c`, `instr.c`,
  `call.c`, `fp.c`, `data.c`, `relax.c`. The steps after it add their tests and book
  chapters, and fix what those find.
  - An operand into the incoming arguments is marked (`Msp_Operand.incoming`) and
    completed by `gen_frame`, once the pushed registers are known.
  - **Helpers.** Our code calls only ordinary-ABI helpers: `__mspabi_*` for 16 and 32
    bits, the libgcc names for 64-bit integers and FP (`__muldi3`, `__adddf3`,
    `__ltdf2`, …), with a second 64-bit operand at `0(r1)` in the outgoing area. The
    R8–R11 `__mspabi_*` entry points become shims for clang's code (T13). This also gets
    NaN right, which clang's single `__mspabi_cmpd` cannot do for both `<` and `>=`.
  - `frame_tests.cpp` checks the parameters, the stack and split parameters, alignment,
    and a run of a 1.2 KB frame.
- **T9. Integer ops** (ch. 2–4, 11, 12). After the usual conversions, arithmetic is on
  `int`, `long` or `long long`: 1, 2 or 4 words.
  - **Add and subtract:**
    - `add`/`addc` and `sub`/`subc` chains, directly on memory;
    - a constant goes as `#k` per word, and a zero high word still needs `addc #0` for
      the carry.
  - **Logic and negation:**
    - `and`, `bis` (or) and `xor` per word;
    - `inv` for complement;
    - negation is `inv` on every word, then `add #1` / `addc #0` up the chain.
  - **Multiply:** `mpyi`, `mpyl` and `mpyll` helpers. Inline shift-and-add for
    constants is T23's.
  - **Divide and remainder:** the `__mspabi_div*`/`rem*` helpers. The `long long` ones
    come from the shared C model `libc/ilp32/int64.c` under the MSPABI names, with a
    small asm shim for each R8–R11 contract. Built with `genmsp430`, they land with
    T14's library build.
  - **Shifts:**
    - By a constant:
      - whole bytes and words move first (`swpb` plus a mask for 8; a word move for
        16);
      - then `rla` (or `add x, x`) per bit, or `rra`, or `clrc; rrc` for logical,
        rippling through the words with `rlc`/`rrc`;
      - a counted loop past a few bits.
    - By a variable: an inline loop for `int` and `long long`, as clang does, and the
      `slll`/`srll`/`sral` helpers for `long`.
  - **Comparisons:**
    - `cmp src, dst` computes `dst − src`, then `jeq`/`jne`, `jl`/`jge` (signed) or
      `jlo`/`jhs` (`jnc`/`jc`, unsigned).
    - There is no `jgt`/`jle`, so `>` and `<=` swap the operands. Against a constant
      they may compare with `k+1` instead, when that does not overflow.
    - Multi-word compares go from the high word down: signed or unsigned on the high
      word, unsigned on the rest.
    - A 0/1 result is `mov #1`, then a branch over `clr`.
  - **Width conversions:**
    - truncation is free (little-endian, low word first);
    - `mov.b` into a register zero-extends;
    - `sxt` sign-extends a byte;
    - widening to `long` copies `#0`, or the sign word (`mov; swpb; sxt; swpb; sxt` or a
      `tst`/`jn` pair) into the high words.

  *Done.*
  - The sign word is `mov; rla; subc; inv` (no label).
  - Shifts by a variable are an inline loop for every width, `long` included, rather
    than the `__mspabi_sll*` helpers.
  - `int_tests.cpp` has goldens, and runs our arithmetic against the host: `int` and
    `long` `+ - * / % & | ^` and every comparison over 12×12 operands; shifts by every
    count, constant and variable; `long long` without the runtime.
  - Book chapters 2–4, 11 and 12 are enabled.
    - Skipped for good, as on AVR: `Chapter11_SwitchLong` (case values collide in a
      32-bit `long`) and `Chapter12_UnsignedTypeSpecifiers` (loops forever with a 16-bit
      `unsigned`).
    - Skipped until T14 builds the 64-bit runtime: four `Chapter11` programs.
- **T10. Control flow** (ch. 5–8) and **branch relaxation.**
  - `.L` labels are unique per TU. A zero test is `tst` on a word, or `bis` across the
    words of a wider value into a scratch register.
  - Selection emits short forms only. `jXX` and `jmp` reach −1024..+1022 bytes (10-bit
    word offset).
  - A **relaxation pass runs last**, after peephole. It computes offsets from the T6
    sizes and rewrites what is out of range:
    - `jXX L` becomes `j!XX .+6; br #L`. The pairs are `jeq`/`jne`, `jl`/`jge` and
      `jlo`/`jhs`.
    - `jn` has no inverse, so it becomes `jn 1f; jmp 2f; 1: br #L; 2:`.
    - `jmp L` becomes `br #L`.

    It repeats until nothing changes; sizes only grow, so it terminates. A size the model
    gets wrong in the safe direction (too large) only relaxes early. A wrong size in the
    other direction is a loud assembler error, never a miscompile.
  - A golden test has a branch over a body just under, at, and just over the limit. A
    run test has a loop body larger than 1 KB.

  *Done.* `relax_tests.cpp` checks every jump at 511 and 512 words, forward and back,
  every inverse, `jn`'s jump-over and `jmp` → `br`, each result also assembled by clang;
  and a run of a loop body over 1 KB. Book chapters 5–8 pass with no new skip.
- **T11. Calls, scalar ABI** (ch. 9).
  - **Arguments are placed per the ABI section:**
    - R12–R15 by consecutive free registers, the split `long` included;
    - a `long long` in registers only when all four are free;
    - later arguments still take free registers;
    - stack arguments are `mov`ed into the outgoing area in order.
  - **Calls:** `call #f` for a direct call, and `call rN` with the pointer in a scratch
    register for an indirect one. `FUN_CALL_NORETURN` is a plain `call`.
  - **Narrow values** are extended by the sender and re-extended by the receiver.
  - **Results** come back in R12, R12:R13 or R12–R15.
  - **Parallel moves** go into the argument registers, ordered so that no source is
    clobbered before it is read.

  *Done.*
  - No parallel moves are needed in the naive selection: every argument comes from
    memory or is an immediate, so the stack parts go first, then the registers.
  - An indirect call loads the pointer into r11. `call x(r1)` would read its operand
    after pushing the return address.
  - `call_tests.cpp` has a golden for each rule, and a run where our caller meets our
    callee for every rule: the split `long`, backfill after a `long long`, a `double`
    on the stack, structures, chars, seven arguments, recursion and a function pointer.
  - Book chapter 9 passes.
- **T12. Globals and static data** (ch. 10).
  - `.data`, `.bss` and `.rodata`, with every `Tac_StaticInit` kind emitted as `.byte`,
    `.short`, `.long` or `.quad`. A `double` is a `.quad` of binary64 bits, a `float` a
    `.long`.
  - **Addresses:** `#sym+k` in code, and `.short sym+k` in data, for data and functions
    alike.
  - **Accesses** are absolute: `mov &g, 2(r1)`; `add #1, &g`. An indexed global is
    `g(rN)`, as clang emits `mov.b data(r12), r13`.
  - Static locals' `name$N` must assemble as they are. Check it, as on AVR.

  *Done.* `data_tests.cpp` covers sections, alignment (none for `char`), every
  initializer kind (`double` as `.quad` bits, a function address a plain `.short`),
  absolute access, `n$1`, and a run of globals of every kind. Book chapter 10 passes.
- **T13. Floating point** (ch. 13), in software.
  - **A binary64 soft-float runtime,** new in `libc/common/float64.c`, beside AVR's
    `float32.c` and the shared `float128.c`, sharing their structure and `libutil`
    helpers where it can.
    - Arithmetic, comparison, and conversion to and from 32- and 64-bit integers and
      binary32.
    - It must be correctly rounded (nearest-even), with subnormals, infinities and NaNs,
      so it agrees bit for bit with the host's `double` and with the constant folder.
  - **The `__mspabi_*` entry points** sit over `float32.c` and `float64.c`. The
    ordinary-ABI ones (all of binary32, and the conversions) are C wrappers. The R8–R11
    ones are asm shims that move the first operand to the stack, call the C routine, and
    return in R12–R15.
  - `cmpf`/`cmpd` return the unordered result that LLVM's lowering assumes (T5's table).
    A clang-compiled NaN test against our runtime pins it at T17.
  - Compiled by `genmsp430`, so it lands after T9–T12. Until then, the FP book chapter
    waits.
  - **Selection:**
    - every operation is a helper call;
    - negation is `xor #0x8000` on the top word, and `fabs` is `bic #0x8000`, both
      inline;
    - constants are `mov #` per word, with no constant pool;
    - a truth test is "any bit but the sign set", inline, and true for NaN;
    - `int`/`unsigned` conversions widen to 32 bits first, as clang does;
    - `FLOAT_TO_DOUBLE`/`DOUBLE_TO_FLOAT` are `cvtfd`/`cvtdf`;
    - the long double conversions emit nothing, since `long double` is `double`.
  - **`sqrt`** stays a call (`hw_sqrt = 0`), to a C `sqrt` in the library.
  - **Tests:** the runtime against the host's own binary64 and binary32 arithmetic over
    a table of cases, including the halfway, subnormal and overflow edges. The host has
    both types natively, so no case generator is needed.

  *Done.*
  - **`libc/common/float64.c`:** add, subtract, multiply, divide, `sqrt` (correctly
    rounded, digit by digit), the six libgcc predicates, `__unorddf2`, the 32-bit
    integer conversions, and `float` ↔ `double`.
  - **Tested on the host,** compiled under renamed symbols (`float64_host.c`) and checked
    bit for bit against the host's `double`:
    - ~1.7 M operand pairs;
    - subnormal and overflow products and quotients;
    - 100 k square roots;
    - constructed ties to even (a mutation that drops ties-to-even fails it);
    - every conversion.
  - **`libc/msp430/mspabi.c`** holds the ordinary-ABI `__mspabi_*` names over the libgcc
    ones. **`mspabi64.s`** holds the ten R8–R11 shims (`mpyll`, the 64-bit divides,
    `addd`/`subd`/`mpyd`/`divd`, `cmpd` over `__ltdf2`).
  - **clang's NaN comparisons are wrong.** clang tests `__mspabi_cmpd`'s one result
    against zero for every comparison, so for a NaN its `>` and `>=` come out true. Our
    own code calls the libgcc predicates, and is right.
  - `fp_tests.cpp` has goldens. Runs of our code check `double` and `float` arithmetic
    bit for bit, every comparison with NaN, and every conversion.
  - Book chapters 13–16 pass, and so do the four `Chapter11` programs. Skipped:
    `DoubleAndIntParamsRecursive` and its `Library` twin exceed the cycle limit, clang's
    build too.
  - **Fixed** a load or store through a pointer whose pointee is wider than the value
    (a row of a 2-D array): the memory-to-memory copy overran the destination slot.
    Both are now clamped to the value's width (found by `Chapter15_PointerAdd`).
  - Our frontend refuses `1.0 / 0.0` as a static initializer, on every target.
- **T14. Pointers, arrays, chars, strings** (ch. 14–16).
  - Loads and stores go through a base register: `mov 2(r1), r15` then `@r15`, `x(r15)`
    or `@r15+`. Each is at the access's width, with `.b` for `char`.
  - `ADD_PTR` scales by `rla` for powers of two, else by an inline shift-and-add
    multiply, or `mpyi`.
  - Pointer comparisons are unsigned.
  - The byte-pointer TAC kinds are plain operations, as on RISC-V.
  - **A word access ignores address bit 0**, on the hardware and in mspsim. A misaligned
    `int` access silently reads the aligned word. Layout keeps everything aligned, and a
    test casts `char *` buffers only at even offsets.
  - From here, the C library (`libc/common`, plus `libc/msp430` C sources) is built with
    `genmsp430` into `libc.a`, as AVR did at M15.

  *Done.*
  - `libc.a` now has the shared C library, built by `genmsp430`, but not the variadic
    `printf` family (T16). It also has, from `libc/ilp32`, `int64.c` and the binary64
    `frexp`/`ldexp`/`modf`.
  - `muldi3.c` moved from `libc/avr` to `libc/common`, shared. AVR's tests pass
    unchanged.
  - `ptr_tests.cpp` has goldens: memory to memory through r15, index scaling by `rla`
    or `__mspabi_mpyi`, byte access. A run covers arrays, a 2-D array of `long`,
    pointer arithmetic and unsigned compares, plain `char` unsigned, and `strcpy`,
    `strcmp`, `strlen`, `strchr`, `memcpy`, `memset` and `memcmp` from the library.
  - **Book chapters 14–16 wait for T13:** 24 of their programs use `double`. Four
    others are skipped for good, as on AVR:
    - `SwitchDereferencedPointer` (case values collide);
    - `BigArray` (a 16-bit `size_t`);
    - `AccessThroughCharPointer` (reads past a 16-bit `int`);
    - `CompoundBitwiseOpsChars` (shifts an `int` by 31).
  - The four `Chapter11` programs that need `long long` division also wait for T13:
    `int64.o` refers to the binary64 runtime.
- **T15. Structs** (ch. 17–18). Member access is through `COPY_*_OFFSET`.
  - **Copies** use words for a 2-aligned struct (`mov @r14+, x(r15)`) and bytes for a
    `char`-only one. Unrolled up to a threshold, then a counted loop, or `memcpy` as
    clang does.
  - **Arguments** are copied into the outgoing stack area, size rounded up to even, at
    their place in the argument order.
  - **Results** always use the frontend's hidden pointer (T1), which arrives in R12. The
    callee returns it in R12 as well.

  *Done.*
  - The frontend copies a structure chunk by chunk, through loads and stores, on every
    byte-addressed target. The backend's own copies (`copy_named`, `copy_bytes`:
    unrolled up to 16 moves, else a loop through r12) serve whole-aggregate moves, such
    as a structure argument.
  - `malloc`/`calloc`/`realloc`/`free` arrive here, not at T20, since chapter 18 needs
    them: `libc/msp430/malloc.c`, a bump allocator in C on AVR's design.
  - `struct_tests.cpp` has goldens and runs: structures of 1, 3, 4, 10 and 60 bytes
    passed and returned, a union, nested members, arrays of structures, the allocator.
  - Book chapters 17–20 pass. Skipped, as on AVR: `Chapter17_SizeofExtern` (too large
    for 15.5 KB of RAM) and `Chapter19_..._FoldCompoundBitwiseAssignAllTypes` (shifts an
    `int` by 31).

## Phase 3 — ABI conformance

- **T16. Variadic functions and `<stdarg.h>`.**
  - **Calls:** for a variadic callee, *every* argument goes on the stack, named ones
    included. An unprototyped callee is called as non-variadic.
  - **The variadic function** finds all its parameters on the stack, so its prologue
    stores nothing.
  - **`va_list`** is `char *`, as clang's MSP430 `__builtin_va_list` is, so a `va_list`
    handed to or from clang-compiled code is the same thing.
  - **`va_start(ap, last)`** is `__va_start(ap)`, intercepted by the backend as on the
    other targets.
  - **`va_arg(ap, T)`** is the AVR pointer walk, with the size rounded up to 2. Alignment
    is at most 2, `char`/`short` promote to `int`, and `float` to the 8-byte `double`.
  - **Gate:** `printf` in `libc.a` works.
- **T17. Interop tests** with clang in both directions, over a table of signatures, built
  before the code they test:
  - **Register assignment:**
    - the split `long` in R15 and the stack;
    - a `long` in R13:R14;
    - a `long long` after one `int` going to the stack while later `int`s backfill
      registers;
    - five `int`s.
  - **Narrow types:** `char`, `signed char` and `unsigned char` arguments and results.
  - **Aggregates:** structs of 1, 2, 3, 4, 6 and 10 bytes and a union, as arguments
    mixed with scalars and as results.
  - **Wide scalars:** `long long`, `float` and `double` arguments and results.
  - **Function pointers both ways.**
  - **Preserved state:** R4–R10 survive our calls.
  - **Variadics both ways,** and a `va_list` handed across.
  - **Clang code linked with our runtime.** It multiplies, divides, shifts `long`s, and
    does `float` and `double` arithmetic and comparisons with NaN, so every helper's T5
    and T13 contract is exercised by LLVM's own assumptions.
- **T18. Differential book tests.** Every book program is also compiled by clang, run
  under mspsim, and the outputs and statuses are compared, as in the AVR suite. This is
  what makes a 16-bit `int` testable.

## Phase 4 — library and headers

- **T19. Headers.** `libc/msp430/include/`, ahead of `libc/ip16/include/` (T3) and
  `libc/common/include/`:
  - `float.h`: binary32 `FLT_*`, binary64 `DBL_*` = `LDBL_*`, and `FLT_EVAL_METHOD` 0;
  - `math.h` for binary64 `double`, with `long double` = `double`;
  - `setjmp.h`: R4–R10, SP and the return address;
  - T16's `stdarg.h`;
  - `RAND_MAX` 32767, in the target-owned place AVR moved it to.

  Add an `msp430-headers` CTest and its `-cpp` twin. Check our headers' sizes, limits and
  type identities against clang's for the triple, as ARM32, x86-64 and AVR did.
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
      That is conservative, whatever LLVM assumes;
    - the integer helpers, whose contracts are narrower than a call, get a narrower
      clobber set only if ch. 20 shows it to be worth it. Record the decision here.

  The ch. 20 tests pass.
- **T22. Frameless functions.** A function with no slots, no outgoing stack arguments and
  no call-saved registers does no `sub`/`add` on SP and pushes nothing. The common case
  of a small leaf is then the bare body and `ret`, as clang's `f1` is.
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

  Branch relaxation (T10) runs after all of these.

## Phase 6 — finishing

- **T24. Driver.** `vcc -t msp430` runs:
  - `vcpp -t msp430`, `vparse`, `vlower -t msp430`, `vgenmsp430`;
  - `clang --target=msp430 -c`, with no `-mmcu`, so that clang neither looks for TI's
    device files nor selects a hardware multiplier;
  - `ld.lld -n -T link.ld crt0.o … -lc`.

  `cc-tests` cases, including a staged prefix. The output is an ELF that mspsim runs
  directly. Intel HEX (`llvm-objcopy -O ihex`) is documented, and also runs.
- **T25. Install.** `genmsp430` as `vgenmsp430`; `crt0.o`, `libc.a`, `link.ld` and the
  headers (MSP430, `ip16` and shared) under `share/vcc/msp430/`. The runtime is installed
  only when the MSP430 clang and `llvm-ar` were found.
- **T26. Documentation.**
  - `docs/Msp430_Backend.md`, in the style of `docs/Avr_Backend.md`:
    - the target, and how code is generated;
    - the memory-to-memory selection and the constant generators;
    - frames and branch relaxation;
    - calls and variadics, with the split-`long` rule and the all-structs-in-memory rule;
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
- **Linker relaxation, running on real hardware, and msp430-elf-gcc** as a second
  oracle. gcc is not installed here, and its ABI may differ from clang's in the
  split-`long` case.

## Risks

- **clang's MSP430 target is marked experimental.** The ABI we match is clang's, and
  parts of it (the split `long`, a `long long` that does not backfill) may differ from
  TI's EABI and msp430-gcc.
  - Mitigation: T17's interop table pins every rule against clang; the docs state that
    the oracle is clang; out of scope for gcc.
- **Helper contracts.** clang-compiled code trusts what LLVM believes about
  `__mspabi_addd`'s R8–R11 operands and `cmpd`'s unordered result. A wrong guess breaks
  only the clang side, or only NaN comparisons, far from its cause.
  - Mitigation: contracts taken from LLVM's source into one table (T5), and T17 running
    clang's code (including NaN) against our runtime.
- **A soft binary64 on a 16-bit CPU.** It is large and slow, and the first in the
  project. A rounding bug shows up as a wrong last digit in `printf`, or a comparison
  disagreeing with the folder.
  - Mitigation: T13's correctly-rounded design, tested against the host's native
    `double` over edge cases; measure code size and cycles at T13 and T20, and widen the
    memory map or cycle limit if needed.
- **The size model for relaxation.** The constant-generator rule decides between 2, 4
  and 6 bytes.
  - Mitigation: T6's golden test against the assembled object for every operand form.
    An underestimate is a loud assembler error, an overestimate only early relaxation.
- **Silent misaligned word access.** A word access ignores bit 0, so a layout or pointer
  bug reads the wrong word without a fault.
  - Mitigation: T2's layout checks against clang; alignment kept through `ALLOCATE_LOCAL`
    and the outgoing area; byte copies for 1-aligned structs.
- **15.5 KB of RAM.** A stack overflow into `.bss` is silent.
  - Mitigation: the link-time stack reserve, the canary checked at `exit`, and the book
    skip list.
- **Irregular flags.** `mov` sets none, `bit`/`and` set C = !Z, and `xor` sets V oddly. A
  peephole that assumes otherwise drops a needed compare.
  - Mitigation: one per-opcode flag table from `MSP430_Instruction_Set.md`, and run tests
    for every comparison after every kind of producer.
- **mspsim is young.** Its CPU passes the openMSP430 instruction tests, but no compiler
  has used it at scale.
  - Mitigation: clang's code as a control whenever a run result looks wrong, and
    mspsim's `-t` trace. A simulator bug is fixed in `../mspsim`, with a test there.
