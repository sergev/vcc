# AVR backend — development plan

A sixth backend, for the 8-bit AVR microcontrollers. It uses the avr-gcc ABI, which
clang implements, emits ELF objects, and runs its programs bare-metal under
`qemu-system-avr`, as the LLVM-toolchain backends run under their qemus. The code is
link-compatible with clang's `--target=avr -mmcu=atmega1280` output. clang is therefore
the test oracle: interop runs in both directions, and every book program is compiled by
both compilers and the outputs compared.

It follows the shape the AArch64, ARM32 and x86-64 backends settled on: a directory of
its own, a small IR, naive selection first, and then register allocation on
`backend/common/regalloc.c` and a peephole pass. Several things are new to this project:

- **An 8-bit machine.** Every `int` is two registers, a `long` four and a `long long`
  eight. Every operation is a byte chain linked by the carry flag.
- **A 16-bit `int`** and a 16-bit `size_t`. No target so far has had `int` narrower than
  32 bits. The frontend has never been run on such a target, and the audit below finds it
  is not ready.
- **A 32-bit `double` and `long double`:** IEEE single, like `float`. This is clang's and
  avr-gcc's default. No target so far has had a `double` that is not binary64.
- **A Harvard architecture.** Code addresses (function pointers) are *word* addresses in
  flash, and data addresses are *byte* addresses in SRAM. Both are 16 bits.
- **Short branches.** A conditional branch reaches ±64 words, and neither clang's
  assembler nor `ld.lld` relaxes one that is out of range (verified below), so the backend
  sizes its own branches.
- **Every type has alignment 1,** so `struct { char c; long l; }` is 5 bytes.
- **No floating-point hardware and no divide instruction.** Every `float` and `double`
  operation, and every division, calls the runtime.

As before, an assumption found in shared code is fixed in the shared code, not worked
around in `backend/avr/`. BESM-6 output must not change, and RISC-V, AArch64, ARM32 and
x86-64 output changes only where a step says so.

`avr.asdl` and `avr.md` stay as the reference spec of the instruction set. Step IDs are
stable: a finished step is marked done, never renumbered. The prefix is `M`, for
microcontroller, since `A` (AArch64), `R` (RISC-V), `B` (Bemsh), `V` (ARM32) and `X`
(x86-64) are taken.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| Device | ATmega1280 (`avr51`): 128 KB flash, 8 KB SRAM at `0x200`–`0x21ff`, `MUL`, `MOVW`, `JMP`/`CALL`, `ELPM` | qemu's `arduino-mega` machine models it. A 2-byte PC means return addresses and function pointers are plain 16-bit word addresses, with no `EIND` and no `gs()` stubs. That rules out the ATmega2560 (3-byte PC). The ATmega328P (Uno) has only 2 KB of SRAM, too little for `printf` and the book programs |
| Data model | `char` 1 (signed), `short` 2, `int` 2, `long` 4, `long long` 8, pointer 2, `size_t` = `unsigned int`, `ptrdiff_t` = `int`, `wchar_t` = `int`, `_Bool` 1, alignment 1 for every type | clang `-dM -E` for the triple, and `sizeof`/`_Alignof` compiled by clang (checked). avr-gcc agrees, plain `char` signed included |
| Floating point | `float` = `double` = `long double` = IEEE binary32, in software | clang's default (`__DBL_MANT_DIG__` = `__LDBL_MANT_DIG__` = 24). clang also accepts `-mdouble=64` (checked), but that stays out of scope |
| ABI | avr-gcc, as clang implements it (details below) | Interop with clang-compiled code and with clang's helper calls |
| Output | GNU avr-as syntax (`lo8()`, `hi8()`, `pm_lo8()`, `Y+q`, `;` comments), as clang emits it | Accepted by clang's integrated assembler. GNU `avr-as` is not installed here, so there is no second assembler to cross-check |
| Toolchain | Homebrew `clang --target=avr -mmcu=atmega1280` (assemble), `ld.lld` (link), `llvm-ar` | The LLVM used by every other backend. No avr-gcc, avr-binutils or avr-libc is needed |
| Run environment | `qemu-system-avr -M arduino-mega -bios <elf>` | `-kernel` loads nothing on AVR (checked) |
| I/O and exit | stdout on USART0 (`-serial stdio`). The status byte goes on USART1 (`-serial file:<scratch>.status`), and the harness kills qemu once that byte arrives | Nothing on the AVR machine can make qemu exit: `cli; sleep`, `break`, and a watchdog reset under `-no-reboot` all leave it running (checked) |
| Backend IR | A small hand-written `Avr_Instr` list, as in `a32.h`/`x86.h`. Every instruction knows its size (2 or 4 bytes), for branch relaxation | `avr.asdl` stays the reference spec. The IR covers only what we emit |
| Executable | `genavr` (`backend/avr/`), library `avr`; installed as `vgenavr` | Mirrors `genx86`/`vgenx86` |

Verified 2026-10-03 on this machine, with scratch programs (not in the tree):

- **Toolchain.** Homebrew clang 23.1.2 lists the `avr` target. `ld.lld` 23.1.2 links an
  `elf32-avr` image with `EF_AVR_ARCH_AVR51`. Clang emits `pm_lo8(f)`/`pm_hi8(f)` and
  `.short pm(f)` for function addresses, `lds r18, g` for data, and `.globl
  __do_copy_data`/`__do_clear_bss` references that the runtime must satisfy. `$` in a
  symbol name and `.quad` both assemble.
- **Run.** A hand-written crt0 does the following:
  - sets SP to `0x21ff`;
  - copies `.data` (with `.rodata` inside it) from flash with `lpm`;
  - enables the USART0 and USART1 transmitters;
  - calls `main`.

  The output appears on stdout. `main`'s result 200 arrives intact as the one byte in the
  USART1 file, 24 ms after qemu starts, and the poller then kills qemu.
- **Speed.** A million iterations of a `volatile long` add loop take 1.8 s under qemu
  TCG. Book programs fit comfortably in the fixture's 5-second timeout, but a test with a
  heavy loop does not.
- **Branches.** A `breq` 200 bytes from its target assembles without complaint, as an
  `R_AVR_7_PCREL` relocation. `ld.lld` then fails with "relocation R_AVR_7_PCREL out of
  range: 200 is not in [-128, 127]". Nothing relaxes it for us. The failure is loud, not
  a silent miscompile.

### The avr-gcc ABI, as clang implements it

Checked against clang's output where marked; the rest is the avr-gcc ABI document, to be
confirmed by M18's interop table.

- **Fixed registers.** `r0` is `__tmp_reg__`, scratch. `r1` is `__zero_reg__`: it is
  zero at every call and return, and `mul` overwrites it, so every `mul` is followed by
  `clr r1`.
- **Call-clobbered:** `r18`–`r27`, `r30`–`r31`, `r0` and the T flag. **Call-saved:**
  `r2`–`r17`, `r28`–`r29`. `Y` (`r29:r28`) is the frame pointer. SP is in I/O space (0x3d
  and 0x3e) and nothing can address memory relative to it, so the frame is addressed
  through `Y+q`, with `q` limited to 0..63.
- **Arguments** are allocated left to right from `r25` downward. Each argument's size is
  rounded up to an even number, and it occupies the registers ending just below the
  previous one: an `int` in `r25:r24`, then a `long` in `r23`–`r20`. Registers run down to
  `r8`. *Checked:*
  - `f(long long, long long, long, int)` puts the two `long long`s in `r18`–`r25` and
    `r10`–`r17`.
  - The `long` does not fit in `r8:r9`, so **it and every later argument** go on the
    stack, even the `int` that would have fit.
  - A `char` takes a whole pair (`r24`, then `r22`) and clang does **not** extend it. We
    extend as the sender and re-extend as the receiver, as on x86-64.
  - A struct is passed in registers like any other argument when it fits. A 10-byte
    struct went in `r14`–`r23`.
- **Stack arguments** sit above the 2-byte return address in the order of the parameter
  list. They are not aligned. The caller removes them.
- **Variadic callees** take *every* argument on the stack, named ones included (checked:
  `va(1, 2L, 3.0)` stored all three through `Z+1`…`Z+10`). `va_list` is a plain pointer.
- **Results:** 1 byte in `r24`, 2 bytes in `r25:r24`, 4 bytes in `r22`–`r25`, 8 bytes in
  `r18`–`r25`. A struct of up to 8 bytes is returned in registers, its size rounded up to
  even and ending at `r25`; a 3-byte struct came back in `r22`–`r24` (*checked*). A larger
  struct is returned through a hidden pointer passed in `r24:r25` as the first argument,
  and the callee does not return that address (*checked*: 9 bytes go through memory, 8
  come back in `r18`–`r25`).
- **Runtime helpers.** clang calls the libgcc names:
  - `__divmodhi4`/`__udivmodhi4`, `__divmodsi4`/`__udivmodsi4` and `__mulsi3`, with the
    special register contracts of avr-gcc's libgcc. For example, `__divmodhi4` takes its
    operands in `r25:r24` and `r23:r22` and returns the quotient in `r23:r22` and the
    remainder in `r25:r24` (*checked* from clang's use of them).
  - `__muldi3` and the soft-float `__addsf3`, `__divsf3`, `__fixsfsi` and the rest,
    which follow the ordinary ABI.

  Our runtime must honour exactly the contracts LLVM assumes, or clang-compiled code
  linked with our `libc.a` breaks silently.
- **Struct copies:** clang calls `memcpy` for large ones (*checked*).

### Registers, as we use them

Fixed here so the allocator (M22) and the naive selection (Phase 2) agree from the start:

| Use | Registers |
|---|---|
| Fixed | `r0` (tmp), `r1` (zero) |
| Frame pointer | `Y` = `r29:r28` (until M23 frees it in frameless functions) |
| Selection scratch, pointer-capable and immediate-capable | `Z` = `r31:r30`, `X` = `r27:r26` |
| Values not live across a call | `r25:r24`, `r23:r22`, `r21:r20`, `r19:r18` |
| Values live across a call | `r17:r16` … `r3:r2` |

**The allocation unit is an even register pair.** The shared allocator colours one
"register" per value, or two for `REGALLOC_PAIR`.
- An `int`, a pointer, a `short` or a `char` is one pair; a `char` wastes the high byte.
- A `long`, `float` or `double` is a `REGALLOC_PAIR` of pairs.
- A `long long` stays in its frame slot until the allocator gains a four-unit class, if
  ever.

Pairs fit the machine too: `movw` copies one, the ABI starts every argument on an even
register, and only `adiw`/`sbiw` care which pair it is.

Only `r16`–`r31` take an immediate operand (`ldi`, `subi`, `sbci`, `andi`, `ori`, `cpi`).
A value in `r2`–`r15` therefore goes through `Z`/`X` for any operation with a constant.
One selection helper owns that rule.

Phase 0 is done:
- `cpp -t avr` predefines clang's macros for the ATmega1280 (no `__CHAR_UNSIGNED__`).
  The `avr` descriptor has a signed plain `char`, returns structs of up to 8 bytes
  itself and larger ones through the frontend's hidden pointer
  (`struct_return_max = 8`), and has no `va_class`.
- **A 16-bit `int` in the frontend.**
  - The parser records each integer constant's spelling (`Literal.spelling`: radix and
    suffixes), and `type_int_literal` types it by the target's widths (C11 §6.4.4.1).
    That also makes `0x80000000` an `unsigned int` on the 32-bit-`int` targets, as it
    should be.
  - `size_t` and `ptrdiff_t` are `size_kind()`/`ptrdiff_kind()`: `unsigned int`/`int`
    on AVR, `unsigned long`/`long` elsewhere as before. That covers `sizeof`, pointer
    indices and pointer differences.
  - `unsigned short` promotes to `unsigned int` where `short` is as wide as `int`
    (`ushort_promotes_unsigned`); BESM-6 keeps its documented simplification.
  - Pointer ↔ `long` casts truncate or zero-extend. A 2-byte `int` is an `I16`/`U16`
    static initializer.
  - `case` values are compared after conversion to the promoted controlling type
    (`narrow_const_int`). An enumerator must fit `int` or `unsigned int`, and is stored
    as the target's `int`. A character constant must fit `int`.
- **A 32-bit `double` and `long double`.**
  - `double_mant_dig = 24` (and `ldouble_mant_dig = 24`) make both constant folders
    round every `double` result to binary32 (`target_double_round`), and convert an
    integer to it in one rounding (`target_double_from_i64`/`_u64`).
  - A `double` literal takes its `strtof` value, which the parser records in
    `Literal.single_val`, so it is rounded once. A literal just above a binary32 halfway
    point pins this.
  - An unfolded `long double` constant keeps its binary128 bits in TAC, as on x86-64;
    the backend rounds it when it emits it.
- The TAC audit (512 book programs, 1040 test-fixture snippets, the C library) found no
  defect beyond these.
- `libc/avr/CMakeLists.txt` finds the tools (`AVR_TOOLS_FOUND`, `AVR_CLANG`, `AVR_AR`,
  `AVR_LD`, `AVR_QEMU`, `AVR_LIB_DIR`, `AVR_LINK_SCRIPT`, `AVR_TARGET_FLAGS`).
- `QemuConfig.image_option` loads the image with `-bios`, and
  `QemuConfig.status_from_serial` adds `-serial file:<scratch>.status`. The run ends
  when that file is not empty, and `main`'s result is its first byte.
- `enum { A = 1 } e = A;`, an enumerator used in the same declaration that defines it,
  fails with "Symbol not found" on every target. This is an existing frontend defect,
  not an AVR one, and is still open.

`make run` stays green after every M-step.

## Phase 1 — skeleton

- **M6. Runtime, hand-written part.** `libc/avr/`:
  - **`crt0.S`:**
    - A vector table. Reset jumps to the start code; every other vector goes to
      `__bad_interrupt`, which prints the vector number and exits with a distinctive
      status.
    - `clr r1`, SREG = 0, and SP at the top of SRAM (`0x21ff`).
    - `__do_copy_data`: copy `.data` (with `.rodata` in it) from flash with `elpm` and
      `RAMPZ`, since the load address can pass 64 KB.
    - `__do_clear_bss`.
    - Enable the USART0 and USART1 transmitters, call `main`, and pass its result to
      `exit`.

    The two `__do_*` symbols are defined because clang-compiled objects reference them.
    Interrupts stay off throughout. A `PRINT_STATUS` variant, as for the other targets.
  - **`console.S`.** `putbyte` polls `UDRE0` and writes `UDR0`. `exit` writes the status
    byte to `UDR1`, waits for it to go out, and then sits in `cli; sleep`.
  - **`link.ld`:**
    - Flash at 0 (128 KB), and SRAM at `0x800200` (8 KB, in the AVR toolchain's data
      address space).
    - `.data` and `.rodata` in SRAM with their load address in flash, then `.bss`.
    - `__heap_start` after `.bss`, and the stack at the top.
    - An `ASSERT` that `.data` + `.bss` leave a minimum stack, e.g. 1 KB, so an
      oversized program fails to link rather than corrupting itself.
  - **Integer helpers in assembly,** under their libgcc names and register contracts:
    `__mulsi3`, `__divmodqi4`/`__udivmodqi4`, `__divmodhi4`/`__udivmodhi4` and
    `__divmodsi4`/`__udivmodsi4`.
    - Take each contract (inputs, outputs, clobbered registers) from LLVM's AVR backend,
      where its special-cased libcalls are lowered, so a clang-compiled caller's
      assumptions hold.
    - Record the contracts in a table in the source. M10 and M22 read the same facts.
  - **Tested on its own,** before any compiled code depends on it. An assembly program
    prints through `putbyte` and returns a status, and data copy and `.bss` clearing are
    checked. Each helper is checked against a table of cases: zero, `INT_MIN / -1`,
    signs, and `UINT_MAX`.
- **M7. Skeleton.** `backend/avr/` with `CMakeLists.txt`, `avr_ir.h`, `avr_ir.c`,
  `codegen.c`, `emit.c` and `main.c` (on `backend/common/driver.c`), producing `genavr`.
  - The IR has a function, a block and an instruction. Its operands are:
    - a byte register, physical or virtual (`r0`–`r31`);
    - a register pair, for `movw`/`adiw`;
    - a pointer register with mode: `X`/`Y`/`Z`, `X+`/`-X`, `Y+q`/`Z+q`;
    - an immediate, possibly `lo8`/`hi8`/`hlo8`/`hhi8` or `pm_lo8`/`pm_hi8` of a
      symbol;
    - a data symbol + offset, for `lds`/`sts`;
    - a label.
  - Every instruction knows its size: 4 bytes for `lds`, `sts`, `jmp` and `call`, 2
    otherwise.
  - The module header emits:
    - the `__tmp_reg__`, `__zero_reg__`, `__SREG__`, `__SP_L__` and `__SP_H__` equates,
      as clang does;
    - sections, `.globl`, `.type sym, @function`/`@object`, `.size`, and labels.
  - A test pins the rendering of every operand form.
- **M8. Run harness and first program.** `avr_test.h` on `QemuTest`:
  - `CompileToAvr` produces golden assembly.
  - `CompileAndRunAvr` assembles with clang, links with `crt0.o` and `libc.a`, and runs
    `qemu-system-avr -M arduino-mega -display none -monitor none -serial stdio -serial
    file:… -bios …`.
  - Tests guard with `SKIP_IF_NO_AVR_TOOLS()`, and the test binary is `avr-tests`.
  - The book suite gets an `avr` `BookTest` with its skip list. Its reasons are new:
    - "too big for 8 KB of SRAM";
    - "exceeds the run timeout".

    A program whose result simply differs on a 16-bit `int` is *not* skipped: the
    comparison with clang (M19) makes it a valid test.

  Done when `int main(void) { return 200; }` runs and the fixture reports 200.

## Phase 2 — instruction selection, book order

Naive and correct first. Every TAC variable lives in a frame slot, and each operation
runs **byte-serially** through scratch registers. In the naive phase that is every
call-clobbered register, since no value is allocated yet. `ldd`, `std`, `ld` and `st`
leave SREG alone, so a carry chain survives the loads and stores between its links:

```
ldd r24, Y+1    ; a, low byte
ldd r25, Y+3    ; b, low byte
add r24, r25
std Y+5, r24
ldd r24, Y+2    ; high bytes: the carry from `add` is still there
ldd r25, Y+4
adc r24, r25
std Y+6, r24
```

Each step is done when its book chapters pass and a few golden tests pin the selected
instructions.

- **M9. Frame.** Slots come from typed TAC and `ALLOCATE_LOCAL`. Alignment is 1, so
  there is no padding.
  - **Prologue:**
    - push the call-saved registers used, and `Y`;
    - `in r28, __SP_L__; in r29, __SP_H__`;
    - lower `Y` by the frame size (`sbiw` up to 63, `subi`/`sbci` beyond);
    - write SP back with the interrupt-safe sequence: `in r0, __SREG__; cli; out
      __SP_H__, r29; out __SREG__, r0; out __SP_L__, r28`.

    The epilogue is the reverse, then `ret`. Slot `n` is at `Y+1+n`. Stack arguments are
    above the saved registers and the 2-byte return address.
  - **The `Y+63` limit.** A slot whose last byte is past `Y+63` is reached by building
    its address in `Z` (`movw r30, r28; subi r30, lo8(-q); sbci r31, hi8(-q)`) and
    walking it with `Z+`. One helper owns the rule.
  - Scalars and temporaries take the low offsets and arrays and structs the high ones, so
    the common case stays within `Y+q`.
  - Incoming register parameters are stored to their slots in the prologue.
- **M10. Integer ops** (ch. 2–4, 11, 12). After the usual conversions, arithmetic is on
  `int`, `long` or `long long`: 2, 4 or 8 bytes.
  - **Add and subtract:** `add`/`adc` and `sub`/`sbc` chains.
    - AVR has no add-immediate, so `x + k` is `subi lo8(-k)` / `sbci hi8(-k)` on an
      upper register.
  - **Logic and negation:**
    - `and`/`or`/`eor` byte by byte, and `com`.
    - Negation is `com` on the high bytes, `neg` on the low byte, and `sbci 0xff` up the
      chain.
  - **Multiply:**
    - 16-bit inline from three `mul`s, with `clr r1` after each;
    - 32-bit through `__mulsi3`, 64-bit through `__muldi3`.
  - **Divide and remainder** call `__divmodhi4`/`__udivmodhi4` or
    `__divmodsi4`/`__udivmodsi4` with their M6 contracts. The `long long` forms call
    `__divdi3`, `__udivdi3`, `__moddi3` and `__umoddi3`, built from the shared C model
    `libc/ilp32/int64.c`.
  - **Shifts:**
    - By a constant: whole bytes move first, then `lsl`/`rol`, `lsr`/`ror` or
      `asr`/`ror` per bit, or a counted loop past a few bits.
    - By a variable: a loop, `dec` + `brpl` around the bit step, as clang does.
  - **Comparisons:**
    - `cp`/`cpc` chains, then `breq`/`brne`, `brlt`/`brge` (signed) or `brlo`/`brsh`
      (unsigned).
    - `>` and `<=` swap the operands.
    - A 0/1 result is `ldi`, a branch over `clr`.
  - **Width conversions:**
    - truncation is free (little-endian, low bytes first);
    - zero extension copies `r1`;
    - sign extension is `mov; lsl; sbc rX, rX` for the high byte, then copies of it.

  **Fixed registers.** `mul` writes `r1:r0`, and the helpers have fixed operands. In the
  naive phase everything is in memory, so this costs nothing. For M22, the selection's
  contract is that each of these reports its clobbers.
- **M11. Control flow** (ch. 5–8) and **branch relaxation.**
  - `.L` labels are unique per TU. A test of zero is `or` across the bytes, or `cp`/`cpc`
    against `r1`.
  - Selection emits short forms: `brXX`, and `rjmp` within a function.
  - A **relaxation pass runs last**, after peephole. It computes every instruction's
    offset from the M7 sizes and rewrites what is out of range:
    - `brXX L` beyond ±64 words becomes `br!XX .+2; rjmp L`, or `.+4; jmp L`;
    - `rjmp` beyond ±2 K words becomes `jmp`.

    It repeats until nothing changes; sizes only grow, so it terminates.
  - A golden test has a branch over a body just under, at, and just over each limit. A
    run test has a loop body larger than 128 bytes.
- **M12. Calls, scalar ABI** (ch. 9).
  - Arguments are placed per the ABI section: registers from `r25` down, and from the
    first argument that does not fit, it and the rest on the stack.
    - Stack arguments are `push`ed, last argument first and high byte first, so they lie
      in memory in order and little-endian.
    - After the call SP is restored: with `pop r0` for up to a few bytes, else the
      `in`/`adiw`/`out` sequence.
  - **Calls:** `call sym` for a direct call. An indirect call is `icall`, with `Z`
    holding the word address that a function pointer already is.
  - **Narrow values** are extended to their 2-byte register pair by the sender, and
    re-extended by the receiver.
  - **Results** come back in `r24`, `r25:r24`, `r22`–`r25` or `r18`–`r25`.
  - `FUN_CALL_NORETURN` is a plain `call`, which keeps the return address for debugging.
  - **Parallel moves** go into the argument registers, by pair with `movw` where both
    sides are even, ordered so that no source is clobbered before it is read.
- **M13. Globals and static data** (ch. 10).
  - `.data`, `.bss` and `.rodata` with every `Tac_StaticInit` kind, emitted as `.byte`,
    `.short`, `.long` and `.quad`; a `double` is a `.long` of binary32 bits (M3).
  - **Addresses:**
    - a data address is `ldi lo8(sym)` / `ldi hi8(sym)`;
    - a function address is `pm_lo8`/`pm_hi8`;
    - in data, they are `.short sym+off` and `.short pm(f)`.

    `TAC_STATIC_INIT_POINTER` must know whether its target is a function. Find out from
    the referenced name's type (the `EXTERN`/function toplevels), and add a flag to the
    initializer only if that is not enough.
  - Accesses are `lds`/`sts sym+k` directly.
  - `.rodata` lives in SRAM, as with clang and avr-gcc. Constants in flash are out of
    scope.
  - Static locals' `name$N` assemble as they are (checked).
- **M14. Floating point** (ch. 13), in software.
  - **A binary32 soft-float runtime** under the libgcc names, with the ordinary ABI:
    - arithmetic: `__addsf3`, `__subsf3`, `__mulsf3`, `__divsf3`;
    - comparisons: `__eqsf2`, `__nesf2`, `__ltsf2`, `__lesf2`, `__gtsf2`, `__gesf2`,
      `__unordsf2`;
    - conversions: `__fixsfsi`, `__fixunssfsi`, `__floatsisf`, `__floatunsisf`,
      `__fixsfdi`, `__fixunssfdi`, `__floatdisf`, `__floatundisf`.
  - Write it in C, in `libc/common/float32.c` (a sibling of `float128.c`, sharing
    `libutil`'s `narrow` where it can). It must be correctly rounded (nearest-even), with
    subnormals, infinities and NaNs, so it agrees bit for bit with compiler-rt, clang's
    runtime, and with M3's folding. It is compiled by `genavr`, so it lands after M10–M13.
    Until then, the FP book chapter waits.
  - **Selection:**
    - every operation is a helper call;
    - negation flips bit 7 of the top byte (`subi r25, 0x80`), and `fabs` clears it,
      both inline;
    - constants are four `ldi`s, with no constant pool;
    - a truth test is "any bit but the sign set", inline, and true for NaN;
    - `int`/`unsigned int` conversions widen to 32 bits first;
    - `FLOAT_TO_DOUBLE`/`DOUBLE_TO_FLOAT` and the long double conversions emit nothing.
  - **`sqrt`** stays a call (`hw_sqrt = 0`), to a C `sqrt`/`sqrtf` in `libc/avr`.
  - **Tests:** the runtime against the host's own binary32 arithmetic over a table of
    cases. The host has the type natively, so no case generator is needed.
- **M15. Pointers, arrays, chars, strings** (ch. 14–16).
  - Loads and stores go through `Z` (or `X`): `movw r30, p` then `ld`/`ldd Z+k`. Loads
    and stores are byte by byte, at the access's width, with a sign or zero extension as
    in M10.
  - `ADD_PTR` scales a 16-bit index by a shift for powers of two, else by an inline 16-bit
    multiply.
  - Pointer comparisons are unsigned.
  - The byte-pointer TAC kinds are plain operations, as on RISC-V.
  - From here, the C library (`libc/common`, plus `libc/avr` C sources) is built with
    `genavr` into `libc.a`, as x86-64 did at X15.
- **M16. Structs** (ch. 17–18). Member access is through `COPY_*_OFFSET`.
  - Copies are byte by byte, since alignment is 1: unrolled `ld X+` / `st Z+` up to a
    threshold, then a counted loop.
  - Struct arguments follow the scalar rules: registers when they fit, size rounded up to
    even, else the stack.
  - Results of up to 8 bytes come back in registers ending at `r25`. Larger ones are the
    frontend's hidden pointer (M1's `struct_return_max = 8`).

## Phase 3 — ABI conformance

- **M17. Variadic functions and `<stdarg.h>`.**
  - **Calls:** for a variadic callee, *every* argument goes on the stack, named ones
    included. An unprototyped callee is called as non-variadic, as avr-gcc does.
  - **The variadic function** finds all its parameters on the stack, so its prologue
    stores nothing.
  - **`va_list`** is `char *`. clang's AVR `__builtin_va_list` is a plain pointer, so a
    `va_list` handed to or from clang-compiled code is the same thing.
  - **`va_start(ap, last)`** is `__va_start(ap)`, intercepted by the backend as on the
    other targets. It yields the address just past the last named argument in the
    incoming stack area.
  - **`va_arg(ap, T)`** is a macro and needs no runtime or class:
    `(*(T *)((ap += sizeof(T)) - sizeof(T)))`. Alignment is 1, `char`/`short` promote to
    2-byte `int`, and `float` to a 4-byte `double`.
  - **Gate:** `printf` in `libc.a` works.
- **M18. Interop tests** with clang in both directions, over a table of signatures, built
  before the code they test:
  - **Register overflow:** the argument that does not fit, and the later ones that would
    have fitted.
  - **Odd sizes:**
    - `char`, `char` + `int`;
    - structs of 1, 3, 5, 8, 9 and 10 bytes, as arguments and as results;
    - `long long` arguments and results;
    - `float`/`double` arguments and results.
  - **Function pointers both ways:** word addresses taken in one compiler and called in
    the other.
  - **Preserved state:** `r2`–`r17` and `Y` survive our calls, and `r1` is zero on
    return.
  - **Variadics both ways,** and a `va_list` handed across.
  - **Clang code linked with our runtime:** it divides, multiplies and does float
    arithmetic, so the helpers' M6 contracts are exercised by LLVM's own assumptions.
- **M19. Differential book tests.** Every book program is also compiled by clang (default
  `-mdouble=32`), run under qemu, and the outputs and statuses are compared, as in the
  other LLVM-toolchain suites. This is what makes a 16-bit `int` testable: the book's
  expected values assume a 32-bit `int`, but clang's AVR output does not.

## Phase 4 — library and headers

- **M20. Headers.** `libc/avr/include/`:
  - `float.h`: `FLT_*` = `DBL_*` = `LDBL_*`, with `MANT_DIG` 24, `EPSILON` 2⁻²³ and
    `DECIMAL_DIG` 9;
  - `limits.h`: `INT_MAX` 32767, `LONG_MAX` 2³¹−1, and a signed `char`;
  - `stdint.h`: `int16_t` = `int`, `int32_t` = `long`, `intptr_t` = `int`, `SIZE_MAX`
    65535;
  - `stddef.h`: `size_t`, `ptrdiff_t`, `wchar_t` and `max_align_t`;
  - `inttypes.h`, with `PRId32` "ld" and `PRId16` "d";
  - `math.h`, with the `double` functions also serving as the `float` ones;
  - `setjmp.h`: `r2`–`r17`, `Y`, SP and the return address;
  - M17's `stdarg.h`.

  All of them are AVR's own; no other target shares a 16-bit data model. Shared-header
  defects found by the audit:
  - `RAND_MAX` (`common/include/stdlib.h:18`) exceeds `INT_MAX`. Move it to a
    target-owned header, at 32767 on AVR, unchanged elsewhere.
  - `char32_t` (`uchar.h:13`) is a plain `unsigned`, 16 bits here. Make it
    `uint_least32_t`, and `char16_t` `uint_least16_t`.

  Add an `avr-headers` CTest and its `-cpp` twin. Check our headers' sizes and limits
  against clang's for the triple, as ARM32 and x86-64 did.
- **M21. Libc fixes and run tests.**
  - **`libc/common/doprnt.c`:**
    - `%z`/`%t` are read as `long`; size them by `size_t`/`ptrdiff_t`.
    - `FBUFSIZE` (352 bytes on an 8 KB stack) and `MAX_DIG` 17 assume binary64; derive
      them from `DBL_MANT_DIG`/`DBL_MAX_EXP`.
  - **`libc/ilp32/int64.c`** converts between 64-bit integers and FP assuming a 53-bit
    `double` (`:68-85`). Make it round correctly for binary32 too, or give AVR its own
    conversions in `float32.c`.
  - **Run tests:** port the `printf`/`str`/`mem`/`math` run tests. Use host libc output
    as the expectation, except for float digits beyond binary32's 9 significant ones.
    There, take the expectation from the host's `float` arithmetic.

## Phase 5 — code quality

- **M22. Register allocation** on `backend/common/regalloc.c`, with the pair as unit.
  - **Classes:**
    - `char`/`short`/`int`/pointer are `REGALLOC_INT`;
    - `long`/`float`/`double` are `REGALLOC_PAIR`;
    - `long long` is `REGALLOC_NONE`.
  - **Numbering:** pairs are numbered from 1 on the allocator's side, since 0 means
    "none" there (as ARM32 did for `r0`).
  - **Pools:**
    - argument pairs `r24`, `r22`, `r20`, `r18`, with result hints on `r24` (and `r22`
      for a `long`);
    - call-saved pairs `r16` … `r2`, pushed and popped around the frame.
  - **Upper registers:**
    - `r16`–`r31` take immediates and `r2`–`r15` do not, so put `r16:r17` first among the
      call-saved pairs.
    - An immediate operation on a lower pair goes through `Z`/`X` in selection. Measure
      the cost on ch. 20.
  - **Helper calls:**
    - every helper call is reported through the `runtime_call` hook;
    - the special-contract helpers (`__divmodhi4` clobbers far fewer registers than a
      call) get a narrower clobber-set hook in `regalloc.h` if ch. 20 shows that worth
      it, and the decision is recorded here;
    - a `mul` clobbers `r0`/`r1` only, which are outside the pool.

  The ch. 20 tests pass.
- **M23. Frameless functions.**
  - A function with no slots and no stack arguments sets up no `Y` and does not touch SP.
    `Y` then joins the call-saved pool.
  - Frames of 2–6 bytes are allocated with `rcall .` (2 bytes each, the return-address
    push) and released with `pop r0`, as avr-gcc does, instead of the SP sequence.
  - The interrupt-safe SP write stays, because user code may enable interrupts.
- **M24. Peephole.**
  - **Moves and constants:**
    - `movw` for pair copies;
    - `r1` for zero (`cp r24, r1`, `mov r25, r1`) and no `ldi 0`;
    - `adiw`/`sbiw` for small constants on `r24`, `X`, `Y` or `Z`;
    - constant bytes of `0x00`/`0xff` folded out of `and`/`or`, and a zero low byte of an
      added constant starting the chain at the next byte;
    - shifts by 8, 16 or 24 as byte moves.
  - **Memory:**
    - post-increment `ld Z+`/`st X+` for consecutive bytes;
    - no reload of a byte just stored;
    - no `clr r1` that is already zero.
  - **Branches:**
    - compare-and-branch fusion, with no 0/1 materialized;
    - branch over jump, and no jump to the next line;
    - skip instructions (`sbrs`/`sbrc`, `cpse`) for an `if` whose body is one
      instruction. This is AVR's counterpart of `cmov` and conditional execution.
  - **Tail calls:** `jmp` when the epilogue leaves nothing on the stack.
  - Branch relaxation (M11) runs after all of these.

## Phase 6 — finishing

- **M25. Driver.** `vcc -t avr` runs:
  - `vcpp -t avr`, `vparse`, `vlower -t avr`, `vgenavr`;
  - `clang --target=avr -mmcu=atmega1280 -c`;
  - `ld.lld -T link.ld crt0.o … -lc`.

  `cc-tests` cases, including a staged prefix. The output is an ELF. Intel HEX for
  flashing (`llvm-objcopy -O ihex`) is documented, not run.
- **M26. Install.** `genavr` as `vgenavr`; `crt0.o`, `libc.a`, `link.ld` and the
  headers under `share/vcc/avr/`; the runtime only when the AVR clang and `llvm-ar` were
  found.
- **M27. Documentation.**
  - `docs/Avr_Backend.md`, in the style of `docs/X86_64_Backend.md`:
    - the target, and how code is generated;
    - the frame and `Y+63`, and branch relaxation;
    - calls and variadics;
    - the 16-bit data model and the 32-bit `double`;
    - the runtime and its helper contracts;
    - running a program by hand under qemu.
  - README and CLAUDE.md for six targets, `libc/avr/include/README.md`, and
    `docs/Type_Sizes_Alignment.md`.
  - Remove `backend/avr/` from the "design notes only" sentence.

  This plan is then removed.

## Out of scope

- **Other devices.** No `-mmcu` choice:
  - the ATmega2560 and other 3-byte-PC parts (`EIND`, `gs()` stubs, a 3-byte return
    address);
  - `avrtiny` (16 registers);
  - `avrxmega`;
  - reduced cores without `MUL`/`MOVW`/`JMP`.

  Leave room for an `-mmcu` table, but nothing more.
- **Data in flash:** `PROGMEM`, `__flash`, `__memx`, named address spaces and `lpm`
  access from C. `.rodata` is in SRAM.
- **A 64-bit `double`** (`-mdouble=64`, `-mlong-double=64`).
- **Interrupt handlers** (`ISR`, the `signal`/`interrupt` attributes, `reti`), EEPROM, and
  I/O intrinsics.
  - A `<avr.h>` with `cli`/`sei`/`sleep`/`wdr` intrinsics, in the manner of
    `<besm6.h>`, is a natural follow-up.
- **Linker relaxation** (`call` → `rcall`), and running on real hardware.
- **GNU `avr-as`/avr-gcc as a second oracle.** It is not installed here.

## Risks

- **A 16-bit `int` in the shared frontend.** This is the largest shared change since
  BESM-6, and the audit shows `long` hard-coded as `size_t` and `ptrdiff_t`, and the
  host's ranges typing literals. A missed site miscompiles silently on AVR only.
  - Mitigation: M2's items each with a `-t avr` test; M4's corpus audit; M19 comparing
    every book program with clang; every other target's goldens unchanged.
- **A binary32 `double`.** A constant folded at 53 bits and computed at 24 disagree in
  the last place, which shows up as a wrong comparison or a wrong `printf` digit.
  - Mitigation: M3's rounding at every fold, the literal rounded once, and the soft-float
    runtime correctly rounded so it agrees with both the folder and compiler-rt.
- **Branch ranges.** `brXX` reaches only ±64 words, and nothing in the toolchain relaxes
  it. The failure is a link error, not a miscompile.
  - Mitigation: M11's relaxation pass over exact sizes, and tests at the limits.
- **The `Y+63` displacement.** Larger frames need computed addresses, and an off-by-one
  in a multi-byte slot near the limit corrupts a neighbour.
  - Mitigation: one addressing helper, and tests with frames over 64 bytes and values
    straddling `Y+63`.
- **8 KB of SRAM.** `.rodata` takes SRAM, and a stack overflow into `.bss` is silent.
  - Mitigation:
    - the link-time stack reserve (M6);
    - a canary below the stack, checked at `exit`, that reports an overflow as a
      distinctive status;
    - the book skip list for programs that do not fit.
- **The `r1` = 0 invariant.** A path that leaves `r1` nonzero after `mul` corrupts every
  later zero test and extension.
  - Mitigation: selection emits `clr r1` right after each `mul` and peephole never moves
    it; a test calls clang code after a multiply.
- **Helper contracts.** clang-compiled code trusts that `__divmodhi4` and friends
  preserve what LLVM thinks they preserve. A helper that clobbers more breaks only the
  clang side, and far from its cause.
  - Mitigation: the contracts taken from LLVM's source into one table (M6); M18 runs
    clang code against our runtime.
- **Code size and speed of naive 8-bit code.** It is several times larger than clang's,
  and slow under TCG.
  - Mitigation: 128 KB of flash is ample for the test programs; M22–M24 recover most of
    it; the timeout reasons in the skip list.
- **qemu's AVR model is less used than the others.** It has no exit device, the USART
  timing is simplified, and there may be instruction bugs.
  - Mitigation: the status-on-USART1 protocol verified above; clang's code as a control
    whenever a run result looks wrong.
