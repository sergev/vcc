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

Checked against clang's output where marked; all of it is exercised against clang, both
ways, by the interop tests (`backend/avr/test/interop_tests.cpp` and the others).

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
  - *Found at M16:* clang **flattens a struct argument** into its top-level members.
    Each member is an argument of its own, rounded up to a pair. A nested struct, a
    union, an array, or the storage unit of bit-fields stays one piece, laid out in
    ascending registers. `struct { char a; int b; int c; }` goes `a` in `r24`, `b` in
    `r23:r22` and `c` in `r21:r20`, not as one 6-byte block. A piece that does not fit
    above `r8` goes on the stack with everything after it, so one struct can be split
    between registers and the stack. Whether avr-gcc agrees is not checked here.
- **Stack arguments** sit above the 2-byte return address in the order of the parameter
  list. They are not aligned. The caller removes them.
- **Variadic callees** take *every* argument on the stack, named ones included (checked:
  `va(1, 2L, 3.0)` stored all three through `Z+1`…`Z+10`). `va_list` is a plain pointer.
- **Results:** 1 byte in `r24`, 2 bytes in `r25:r24`, 4 bytes in `r22`–`r25`, 8 bytes in
  `r18`–`r25`. A struct of up to 8 bytes is returned in registers, in ascending order
  from `r24`, `r22` or `r18` for up to 2, 4 or 8 bytes; a 3-byte struct came back in
  `r22`–`r24` (*checked*), and a 5-byte one in `r18`–`r22` (*checked* at M16). A larger
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

`make run` stays green after every M-step.

## Phase 5 — code quality

- **M22. Register allocation** on `backend/common/regalloc.c`, with the pair as unit.
  *Done* (`backend/avr/regalloc.c`):
  - **Classes:** `char`/`short`/`int`/pointer are `REGALLOC_INT`; `long`/`float`/
    `double` are `REGALLOC_PAIR`, two pairs not necessarily adjacent; `long long` and
    aggregates stay in memory.
  - **Numbering:** a pair is its low register's own number; 0 never occurs.
  - **Pools:** argument pairs `r24`, `r22`, `r20`, `r18` first, then `r16` … `r2`
    (`r16` first, for the immediates), pushed by the prologue when used. Parameters
    and call arguments and results are hinted to their ABI registers.
  - **Two forms of selection.** The allocator's `runtime_call` hook is
    `uses_scratch`: an instruction that the naive form selects (a helper, 8 bytes,
    multiply/divide, a variable shift, an aggregate over 16 bytes, an index scaled by a
    multiply) counts as a call, so no value lives across it in `r18`–`r25`. Every other
    instruction computes in its destination's registers, or in `Z`/`X` when the
    destination is in memory or needs immediates its registers cannot take, with
    operand bytes straight from registers, through `r0` from memory, or as immediates.
  - **Moves:** operands, call arguments and parameters on entry are gathered by one
    parallel move per instruction, a cycle broken through the stack.
  - **Call-saved argument registers:** a value in `r8`–`r17` that an argument (or the
    second operand of an 8-byte helper) overwrites is pushed and popped around it.
  - **Y+63:** the scratch-free form reaches slots only as `Y+q`. A function whose
    scalar slots of up to 4 bytes would lie past `Y+63` falls back to the naive form,
    all in memory.
  - **Helper clobbers:** every helper counts as a call; no narrower clobber set for the
    special-contract helpers.

  The ch. 20 tests pass.
- **M23. Frameless functions.** *Done.*
  - A function with no slots and no stack arguments sets up no `Y` and does not touch SP.
    `Y` joins the call-saved pool, after `r16`; when a function given it turns out to
    need a frame after all, it is allocated again without it.
  - Frames of 2–6 bytes (rounded up to even) are reserved with `rcall .` and released
    with `pop r0`, as avr-gcc does, instead of the SP sequence.
  - The interrupt-safe SP write stays for larger frames, because user code may enable
    interrupts.
- **M24. Peephole.** *Done* (`backend/avr/peephole.c`, and fusion in selection):
  - **Compare-and-branch fusion** in selection: a comparison read only by the
    conditional jump after it branches on its flags, integer or FP.
  - **Forward, per block:** register copies and constants known (`r1` zero), so a
    move or `ldi` of what a register holds goes; a slot or global byte just loaded or
    stored is forwarded to a reload as a move, and a store of what it holds goes. A
    store through a pointer or a call forgets memory; a volatile access stays.
  - **Backward, over register and SREG liveness:** a dead instruction goes;
    `ldi t, k; cp r, t` is `cpi r, k`; `subi`/`sbci` of 1..63 on `r24`–`r30` is
    `adiw`/`sbiw` where the flags are dead.
  - **Jumps:** none to the next instruction, a branch over a jump inverted, code after
    an unconditional jump gone; after the frame, a jump to a lone `ret` is `ret`, and a
    call followed by the return a tail `jmp`.
  - **Cycles** in parallel moves go through a free `X`/`Z` pair or `r0` before the
    stack.
  - **Not done:** skip instructions (`sbrs`/`sbrc`/`cpse`), post-increment for
    consecutive bytes (`ldd` costs the same as `ld Z+`), and `ldi 0` → `mov r1` (the
    same cost).
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
