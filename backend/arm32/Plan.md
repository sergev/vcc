# ARM32 backend — development plan

A fourth backend, for 32-bit ARM: ARMv7-A in ARM (A32) state, the hard-float AAPCS
(AAPCS-VFP), ELF objects, programs running bare-metal under `qemu-system-arm`, exactly
as the AArch64 backend runs under `qemu-system-aarch64`. Code is link-compatible with
clang's `armv7a-none-eabihf` output, so clang is the test oracle: interop in both
directions, and every book program compiled by both and compared.

It draws on both earlier backends. Its shape (directory layout, IR over virtual
registers, qemu `virt` with PL011 and semihosting, crt0 with an MMU) comes from
AArch64. Its data model (ILP32, `long long` in register pairs, a `char *` walk for
`va_arg`) comes from RV32. As before, a BESM-6, RISC-V or AArch64 assumption found in
shared code is fixed in the shared code, not worked around in `backend/arm32/`. BESM-6
output must not change; RISC-V and AArch64 output changes only where a step says so.

Step IDs are stable: a finished step is marked done, never renumbered. The prefix is
`V` (for ARMv7), since `A` (AArch64), `R` (RISC-V) and `B` (Bemsh) are taken.
`make run` stays green after every V-step.

Phase 0 is done: `cpp -t arm32` predefines clang's macros for the triple, the `arm32`
descriptor leaves every struct result to the backend (`struct_return_max = SIZE_MAX`),
the frontend lowers the whole test corpus for `arm32` with no defect, RV32's ILP32
runtime and headers are shared in `libc/ilp32/`, and `libc/arm32/CMakeLists.txt` finds
the tools (`ARM32_TOOLS_FOUND`, `ARM32_CLANG`, `ARM32_LD`, `ARM32_QEMU`,
`ARM32_LIB_DIR`, `ARM32_LINK_SCRIPT`).

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| ISA | ARMv7-A, ARM state only (no Thumb-2), with hardware `sdiv`/`udiv` (the ARMv7VE integer divide of Cortex-A7/A15) | Fixed 32-bit encodings and full conditional execution; Thumb-2 only shrinks code size, and needs `IT` blocks and narrow-encoding rules. Hardware divide keeps every 32-bit operation inline |
| FPU | VFPv3-D16 instructions and registers (`s0`–`s31` = `d0`–`d15`) | Every ARMv7-A FPU has them; `d16`–`d31` have no `s` view, so they would help only `double` |
| ABI | AAPCS with the VFP variant (`-mfloat-abi=hard`), the `arm32` descriptor already in `semantic/target.c` | ILP32, unsigned plain `char`, `long double` = `double`; what clang emits for `armv7a-none-eabihf` |
| Output | GNU/LLVM unified assembler syntax for ELF (`.s`, `.syntax unified`, `.arm`) | Assembled by clang; we write no assembler or object format |
| Toolchain | `clang --target=armv7a-none-eabihf -mcpu=cortex-a15` (assemble), `ld.lld` (link), `llvm-ar` | The Homebrew LLVM already used for RISC-V and AArch64 |
| Run environment | `qemu-system-arm -M virt -cpu cortex-a15 -semihosting`, bare metal in SVC mode | Output through the PL011 UART at `0x09000000`; exit status through semihosting `SYS_EXIT_EXTENDED` |
| Constants and addresses | `movw`/`movt` (`#:lower16:sym` / `#:upper16:sym`), never literal pools | No pool placement or 4 KiB reach to manage; one rule for constants and symbols |
| Backend IR | Small hand-written `A32_Instr` list over virtual registers, like `a64.h`, with a condition on every instruction | `arm32.asdl` stays the reference spec; the IR covers only what we emit |
| Executable | `genarm32` (`backend/arm32/`), library `arm32`; installed as `vgenarm32` | Mirrors `genaarch64`/`vgenaarch64` |

Verified 2026-10-03 on this machine, with scratch programs (not in the tree):
- **Run environment.** A hand-written ARM-state `_start`, assembled by Homebrew clang
  (`armv7a-none-eabihf`, `-mcpu=cortex-a15`) and linked by `ld.lld` at `0x40010000`, ran
  under `qemu-system-arm -M virt -cpu cortex-a15`. It enabled VFP (`CPACR` cp10/cp11,
  then `FPEXC.EN`), executed `vadd.f64` and `sdiv`, printed through the PL011, and exited
  with status 5 through semihosting `SYS_EXIT_EXTENDED` (`svc 0x123456`, `r0 = 0x20`,
  `r1` → `{0x20026, status}`). `hlt 0xf000` is ARMv8-only; ARMv7 uses the `svc` form.
- **Alignment.** As on AArch64, **with the MMU off, an unaligned `ldr` faults**: memory is
  then Strongly-ordered. With a short-descriptor identity map in 1 MiB sections (below
  1 GiB Device, above it Normal write-back) and `SCTLR.M`/`C`/`I` set and `A` clear, the
  same load succeeds. crt0 does that (V5).
- **Immediates.** `0xc06` is not an ARM immediate (8 bits rotated by an even amount), so
  the assembler rejected it. That is why V9's operand2 encoder is needed.
- **Clang's data model for the triple:** `enum` is 4 bytes (no short enums), `wchar_t`
  is `unsigned int`, `long double` is 8 bytes, `char` is unsigned, `__ARM_PCS_VFP`.

### AAPCS-VFP, as it affects us

- **Registers.** Core arguments `r0`–`r3`, results `r0` (`r0`:`r1` for 64 bits).
  Callee-saved `r4`–`r11` (`r9` is the platform register; bare-metal EABI makes it an
  ordinary callee-saved one, as clang does). `r12` (`ip`) is scratch and may be
  clobbered by linker veneers; `r13` is `sp`, `r14` is `lr`, `r15` is `pc`. VFP
  arguments and results are in `s0`–`s15` / `d0`–`d7`; `d8`–`d15` are callee-saved.
- **`sp`** is 8-byte aligned at every public interface. We keep it 8-aligned throughout.
- **Narrow values.** Unlike AAPCS64, the sender extends: a `char`/`short`/`_Bool`
  argument is sign- or zero-extended to 32 bits by the caller, a result by the callee.
  The receiver trusts that, as clang does.
- **64-bit values.** A `long long` or `double` (in core registers) takes an even
  register pair, `r0`:`r1` or `r2`:`r3`, skipping `r1`/`r3`. Once one has gone to the
  stack, no later argument goes in a core register. On the stack it is 8-aligned.
- **FP arguments** (`float`, `double`, and homogeneous float aggregates of 1–4 members)
  are allocated to `s`/`d` registers with **back-filling**. A `float` after a `double`
  fills the `s` register the `double`'s even alignment skipped. Once an FP argument
  goes to the stack, every remaining VFP register is closed.
- **Composite arguments** are passed **by value**, never by reference: rounded up to
  whole words, copied into the next core registers (an 8-aligned one from an even
  register) and **split between `r3` and the stack** if they do not fit, as long as
  nothing is on the stack yet; otherwise wholly on the stack. A large struct is copied
  whole into the outgoing argument area.
- **Results.** Integer and pointer in `r0`, `long long` in `r0`:`r1`, `float`/`double`
  in `s0`/`d0`, an HFA in `s0`–`s3`/`d0`–`d3`, any other composite of ≤ 4 bytes in `r0`.
  Anything larger is written to memory whose address the caller passes **in `r0`, as
  an extra first argument**, shifting the real arguments one register along.
- **Variadic functions use the base standard**, caller and callee both: *all*
  arguments, the named ones included, travel in core registers and the stack, so a
  `double` goes in an even core pair and never in a `d` register. A call through a
  function pointer follows the pointer's type. `va_list` is
  `struct __va_list { void *__ap; }`, a pointer walk over a contiguous area — no
  register save areas, no runtime helper and no `__builtin_va_class` (V18).
- **Runtime helper names** come from the ARM run-time ABI (RTABI), not libgcc:
  `__aeabi_ldivmod`/`__aeabi_uldivmod` (quotient in `r0`:`r1`, remainder in `r2`:`r3`),
  `__aeabi_lmul`, `__aeabi_llsl`/`llsr`/`lasr`, `__aeabi_l2d`/`ul2d`/`l2f`/`ul2f`,
  `__aeabi_d2lz`/`d2ulz`/`f2lz`/`f2ulz`, and the `__aeabi_memcpy`/`memmove`/`memset`/
  `memclr` family (with `4` and `8` variants). Clang-compiled code calls these, so
  `libc.a` must provide them even where our own code inlines the operation.

### Registers, as we use them

Fixed here so the allocator (V23) and the naive selection (Phase 2) agree from the
start:

| Use | Core | VFP |
|---|---|---|
| Scratch for instruction selection | `r12`, `lr` (saved by any function that uses it) | `d14`, `d15` (`s28`–`s31`), saved like any callee-saved register when used |
| Values not live across a call | `r0`–`r3` | `d0`–`d7` |
| Values live across a call | `r4`–`r10` (`r11` too in a function without a frame pointer) | `d8`–`d13` |

Two integer scratch registers is few. It works because loads and stores leave the
flags alone: a `long long` add from memory to memory is `ldr`/`ldr`/`adds`/`str`, then
`ldr`/`ldr`/`adc`/`str` on `r12` and `lr`, with the carry surviving between the
halves. A pattern that needs more (a 64×64 multiply, a variable 64-bit shift) calls its
RTABI helper unless its operands are already in registers. If V8–V11 find a pattern
that cannot fit, `r10` leaves the allocator's pool to become a third scratch; record
the case.

A `float` occupies a whole `d` register in the allocator's view (its even `s` half), so
`s`/`d` aliasing never reaches the allocator. Call setup alone deals in single `s`
registers, for back-filling.

## Phase 1 — skeleton

- **V5. Runtime.** `libc/arm32/`:
  - `crt0.S`: set `sp`. Point `VBAR` at a vector table whose every entry prints the
    exception kind with `DFSR`/`DFAR`/`IFSR` and the faulting `lr`, then exits with a
    distinctive status. Enable VFP (`CPACR`, `FPEXC`). Build the 4096-entry
    short-descriptor level-1 identity map (Device below 1 GiB for the UART, Normal
    write-back above it for RAM), set `TTBR0`/`TTBCR`/`DACR`, and enable the MMU and
    caches with `SCTLR.A` clear (verified above). Clear `.bss`, call `main`, and pass
    its result to `exit`. A `PRINT_STATUS` variant, as for the other targets.
  - `console.s`: `putbyte` to the PL011 data register, and `exit` through `svc 0x123456`
    `SYS_EXIT_EXTENDED`.
  - `link.ld`: load at `0x40010000` (verified clear of qemu's DTB), stack and heap at
    the top of a 128 MiB RAM, and `.ARM.exidx` placed (clang's objects carry unwind
    index entries even for C).
  - `malloc.s` (from RV32, whose 32-bit layout matches), the `libc/common` and
    `libc/ilp32` sources built with our compiler, and `aeabi.s`. The latter holds the
    RTABI entry points as thin wrappers: `__aeabi_uldivmod`/`ldivmod` around the
    `int64.c` division, returning the remainder in `r2`:`r3`; the conversions and
    shifts under their `__aeabi_` names; and `__aeabi_memcpy*`/`memmove*`/`memset*`
    (note the `(dest, n, c)` argument order)/`memclr*` onto the C routines. Archived
    with `llvm-ar` as `libc.a`.
  - V5 is tested on its own before any compiled code depends on it: an unaligned load, a
    VFP operation, a deliberate fault that must report rather than hang.
- **V6. Skeleton.** `backend/arm32/` with `CMakeLists.txt`, `a32.h`, `a32.c`,
  `codegen.c`, `emit.c`, `main.c` (on `backend/common/driver.c`), and `genarm32`. The
  IR, `a32.h`, has a function, block, and instruction over virtual registers. Every
  instruction carries a condition (default `al`) and an `S` flag. The operands are:
  a core register, an `s` or `d` register, an immediate, a symbol with
  `:lower16:`/`:upper16:`, an operand2 shifted register, memory `[base, #±off]` or
  `[base, ±reg, lsl #n]` with pre/post-index, and a register list. The module header
  emits `.syntax unified`, `.arm`, `.fpu vfpv3-d16`, and the `.eabi_attribute`s clang
  writes for the triple (`Tag_ABI_VFP_args` above all). Check them with
  `llvm-readelf -A` against a clang object, so `ld.lld` sees matching objects. Then
  sections, `.globl`, `.type sym, %function`/`%object`, `.size`, `.p2align`, labels.
- **V7. Run harness and first program.** `arm32_test.h` on `QemuTest`:
  `CompileToArm32` (golden assembly) and `CompileAndRunArm32` (assemble, link with
  crt0 and `libc.a`, run qemu with `-display none -serial stdio -monitor none
  -semihosting` under a short timeout). Tests guard with `SKIP_IF_NO_ARM32_TOOLS()`;
  the test binary is `arm32-tests`. The book suite gets an `arm32` `BookTest` with its
  skip list, starting from RV32's ILP32 reasons. `int main(void) { return 2; }` runs
  and returns 2.

## Phase 2 — instruction selection, book order

Naive and correct first: every TAC variable in a frame slot, operands loaded into the
scratch registers of the table above, result stored back. Each step is done when its
book chapters pass and a few golden tests pin the selected instructions.

- **V8. Frame.** Slot layout from typed TAC, `ALLOCATE_LOCAL`, and 8-byte alignment.
  The prologue is `push {…, r11, lr}` with `r11` as the frame pointer (ARM-state AAPCS
  convention) and an even register count, then `vpush {d8–…}` when used. The epilogue
  is `pop {…, r11, pc}`, which returns and interworks; a function that pushed nothing
  returns with `bx lr`. Offset ranges differ by instruction: `ldr`/`str`/`ldrb`/`strb`
  take ±4095, `ldrh`/`ldrsh`/`ldrsb`/`strh`/`ldrd`/`strd` ±255, `vldr`/`vstr` ±1020 in
  multiples of 4. Beyond that the address goes through a scratch register. Constants
  come from `mov`/`mvn` with an operand2 immediate, else `movw`, else `movw`+`movt`.
- **V9. Integer ops** (ch. 2–4, 11, 12): an operand2 encoder (8 bits rotated right by
  an even amount) decides when `add`/`sub`/`and`/`orr`/`eor`/`cmp` take an immediate.
  It also tries the complement (`mvn`, `bic`) and the negation (`cmn`, `sub`↔`add`).
  Then `rsb` for negation, `mvn` for `~`, `mul`, `sdiv`/`udiv`, and remainder as
  `sdiv`+`mls`. Shifts are `lsl`/`lsr`/`asr` by immediate or register (C leaves an
  amount ≥ 32 undefined, so the register form's 0..255 behaviour is fine). Comparisons
  are `cmp`, then `mov<cond> rd, #1` and `mov<inv> rd, #0`. Width conversions use
  `sxtb`/`sxth`/`uxtb`/`uxth`.
- **V10. Control flow** (ch. 5–8): `.L` labels unique per TU, `b`, and `cmp` +
  `b<cond>`; a test of zero is `cmp rN, #0` + `beq`/`bne` (ARM state has no `cbz`).
- **V11. 64-bit integers.** `long long` in two words (low first), carried in register
  pairs like RV32 (`backend/riscv/llong.c` is the model, not shared code: ARM has a
  carry flag, so the sequences are far shorter). `adds`/`adc`, `subs`/`sbc`, `rsbs`/
  `rsc` for negation, word-wise logic. Constant shifts are inline (`lsl`+`orr … lsr`
  across the halves, and the ≥ 32 cases). Comparisons are `cmp` low, `sbcs` high and a
  signed or unsigned condition; equality is `cmp` high, then `cmpeq` low. Widening is
  `asr #31` or `mov #0` for the high word; narrowing takes the low word. Variable
  shifts go to `__aeabi_llsl`/`llsr`/`lasr`, multiply to `__aeabi_lmul` (inline
  `umull` + two `mla` once V23 has the operands in registers), and division and
  remainder to `__aeabi_ldivmod`/`uldivmod`. Conversions with FP use the RTABI helpers
  of the Decisions table.
- **V12. Calls, scalar ABI** (ch. 9): `r0`–`r3`, even pairs for 64-bit values, `s`/`d`
  registers with back-filling, 8-aligned stack slots, narrow arguments and results
  extended by the sender, `bl` for direct calls (the linker turns it into `blx` for a
  Thumb callee), `blx rN` for indirect ones, `FUN_CALL_NORETURN`. Parallel moves into
  `r0`–`r3` and `d0`–`d7` are ordered so no source is clobbered before it is read.
- **V13. Globals and static data** (ch. 10): `.data`, `.bss`, `.rodata`, every
  `Tac_StaticInit` kind (`long long` and `double` as two words, low first), and
  addresses as `movw`/`movt` of the symbol. No GOT or PIC in a static bare-metal link.
  Static locals' `name$N` are spelled legally.
- **V14. Floating point** (ch. 13): `vadd`/`vsub`/`vmul`/`vdiv`/`vneg`/`vabs` on `.f32`
  and `.f64`. `vcvt` between them, and between them and `s32`/`u32` (native unsigned
  conversions). Comparisons are `vcmp` + `vmrs APSR_nzcv, fpscr`, then a NaN-correct
  condition (`mi`/`ls` for `<`/`<=`, as on AArch64). Constants come from `vmov.f32`/
  `.f64 #imm` when the 8-bit FP immediate encodes them, else from `.rodata` through
  `movw`/`movt` + `vldr`. `long double` is `double`: its TAC kinds map onto the `.f64`
  forms and its conversions to and from `double` are copies. A `long double` constant
  or static initializer may carry binary128 bits (an unfolded literal does), so it is
  always read through `f128_to_double`, as the BESM-6 backend and the folder do, so unlike AArch64 there
  is no binary128 phase at all.
- **V15. Pointers, arrays, chars, strings** (ch. 14–16): loads and stores by width and
  signedness (`ldrsb`/`ldrb`/`ldrsh`/`ldrh`), `ADD_PTR` with a scaled-register operand
  (`add rd, rn, rm, lsl #2`) when the scale is a power of two, and the byte-pointer TAC
  kinds as plain operations, as on RISC-V.
- **V16. Structs** (ch. 17–18): member access via `COPY_*_OFFSET`, whole-aggregate
  copies (word by word through `r12`/`lr`, a loop past a few words, then a byte or
  halfword tail; `ldm`/`stm` blocks wait for V25), and struct arguments and results passed whole through
  memory and the `r0` result address for now.

## Phase 3 — ABI conformance

- **V17. Full aggregate classification.** One function, `tac_aapcs32_class` beside
  `tac_aapcs64_class` in `tac/tac_abi.c`, decides HFA or not (AAPCS counts `long
  double` as `double`, so a struct mixing the two is still homogeneous). It drives HFAs
  in VFP registers (with back-filling and the stack-closes-VFP rule), other composites
  in core registers with the even-register rule and the `r3`/stack split, the copy of a
  large struct into the outgoing argument area, and results: ≤ 4 bytes in `r0`, HFAs in
  `s0`–`s3`/`d0`–`d3`, the rest through the address in `r0`.
- **V18. Variadic functions and `<stdarg.h>`.**
  - Calls to a variadic callee, direct or through a pointer, use the base standard: FP
    arguments in core registers and stack (a `float` already promoted to `double` by the
    frontend), named arguments included; HFAs travel as plain composites.
  - A variadic function's own parameters arrive under the same rules. Its prologue
    pushes `r0`–`r3` (the unnamed ones at least) just below the incoming stack
    arguments, so registers and stack form one contiguous area. Its named parameters
    are read from there.
  - `<stdarg.h>` is pure macros, RV32-style: `typedef struct __va_list { char *__ap; }
    va_list;` (clang's type, a 4-byte composite passed in a core register like a
    pointer), `va_start` from the address after the last named parameter, `va_arg`
    rounding to 4 bytes and aligning to 8 for an 8-aligned type. No by-reference case
    exists: AAPCS passes every composite by value. `va_copy` is a struct copy, `va_end`
    nothing.
  - `va_list` handed to a clang-compiled `vprintf`, and ours receiving clang's, are
    covered by V19.
- **V19. Interop tests** with clang in both directions over a table of signatures, built
  before the code they test: mixed int/FP with back-filling (`float, double, float`),
  `long long` forcing the even pair and the `r3` skip, narrow ints in both extension
  directions, HFAs of `float` and `double` (1–4 members), small and odd-sized structs,
  a struct split between `r3` and the stack, large structs by value, struct results
  (≤ 4 bytes, HFA, through `r0`), many arguments, variadics both ways (`double` in an
  even core pair, HFA structs through `va_arg`), a `va_list` handed across, and a
  clang callee that divides `long long` (pulling `__aeabi_ldivmod` from our `libc.a`).
- **V20. Differential book tests.** Every book program compiled by clang too, run under
  qemu, outputs compared — the RISC-V and AArch64 suites' comparison.

## Phase 4 — library and headers

- **V21. Headers.** `libc/arm32/include/`: `float.h` (`LDBL_*` equal to `DBL_*`),
  `stddef.h` and `stdint.h` (unsigned `wchar_t`), `setjmp.h` (`r4`–`r11`, `sp`, `lr`,
  `d8`–`d15`), and the V18 `stdarg.h`. The rest comes from `libc/ilp32/include/` and `libc/common/include/`. Add an `arm32-headers` CTest and its `-cpp` twin, like
  `riscv32-headers`.
- **V22. Libc run tests.** Port the AArch64 `printf_tests`/`str_tests`/`mem_tests`/
  `math_tests` (host libc output as expectation). `printf("%Lf")` exercises the 8-byte
  `long double` through `va_arg`.

## Phase 5 — code quality

- **V23. Register allocation** on `backend/common/regalloc.c`, with the pools of the
  register table. The pair hook RV32 added carries `long long`. Callee-saved core
  registers are pushed with the frame record in one `push`/`pop`, VFP ones with one
  `vpush`/`vpop` of the used range. The ch. 20 tests pass.
- **V24. Leaf functions and sp-addressed frames.** A leaf that needs no stack has no
  prologue at all and returns with `bx lr`. Without `--frame-pointer`, slots are
  addressed from `sp` and `r11` joins the allocator's pool, as on AArch64.
- **V25. Peephole.** Immediate and shifted-register operand2 forms (`add r0, r1, r2,
  lsl #2`), `cmn`/`bic`/`mvn` for constants that fit only complemented or negated,
  copies followed into their uses, no reload of a value just stored, adjacent slots
  through `ldrd`/`strd` (an even/odd pair, word-aligned) or `ldm`/`stm`, `mla`/`mls`,
  `tst` for a mask test, the `pop {…, pc}` return, branch over jump, and no jump to the
  next line. **Conditional execution** replaces a short diamond (`if (c) x = a; else x =
  b;`, `?:`, `min`/`max` shapes) with predicated instructions — the one optimization
  AArch64 had no counterpart for, limited to a few instructions per arm, with no call
  and nothing that sets flags.

## Phase 6 — finishing

- **V26. Driver.** `vcc -t arm32`: `vcpp -t arm32`, `vparse`, `vlower -t arm32`,
  `vgenarm32`, `clang --target=armv7a-none-eabihf -mcpu=cortex-a15 -c`,
  `ld.lld -T link.ld crt0.o … -lc`; `cc-tests` cases including a staged prefix.
- **V27. Install.** `genarm32` as `vgenarm32`; `crt0.o`, `libc.a`, `link.ld` and the
  headers (`libc/arm32`, `libc/ilp32`, `libc/common`) under `share/vcc/arm32/`, the
  runtime only when the ARM clang/llvm-ar were found.
- **V28. Documentation.** `docs/Arm32_Backend.md` in the style of
  `docs/Aarch64_Backend.md` (target, how code is generated, frame, calls, variadics,
  runtime, running a program by hand under qemu), README and CLAUDE.md for four
  targets, and `libc/arm32/include/README.md`. This plan is then removed.

## Out of scope

- **Thumb-2.** `arm32.asdl` covers it (`IT` blocks, `cbz`, `tbb`), and code size would
  drop by roughly a third, but it doubles the encoding rules the emitter and peephole
  must respect. Our ARM-state code already interworks with Thumb callers and callees
  (`bl`/`blx`/`bx lr`/`pop {pc}`), so it can be added later as a separate mode.
- **Soft-float (`armv7a-none-eabi`)** and cores without hardware divide (Cortex-A8/A9).
  Both are helper-call variants of V11/V14 behind a flag, if ever wanted.
- **NEON / Advanced SIMD**, atomics (`ldrex`/`strex`), and big-endian.

## Risks

- **Two integer scratch registers** may not be enough for every naive pattern (large
  frame offsets on both a load and a store, 64-bit ops on memory operands).
  Mitigation: flags survive loads and stores, the RTABI helpers absorb the wide cases,
  and `r10` is the agreed third if needed.
- **AAPCS-VFP argument rules** — back-filling, the closed-VFP-after-stack rule, the
  even-pair rule, the `r3`/stack split, and the switch to the base standard for
  variadics — are each easy to get almost right. Mitigation: V19's interop table,
  written before the code it tests, with clang as the oracle in both directions.
- **`s`/`d` aliasing** would corrupt values silently if two allocator units overlapped.
  Mitigation: the allocator sees only `d` registers, a `float` living in the even half,
  and only call setup (V12, V17) names odd `s` registers. A regalloc test pins that.
- **Missing RTABI symbols** surface only when clang-compiled code is linked. Mitigation:
  V5 provides the whole family up front, and V19 has a clang callee for each.
- **Build-attribute mismatches** (`Tag_ABI_VFP_args`, FP and arch tags) can make
  `ld.lld` reject or warn on a link of our objects with clang's. Mitigation: V6
  compares `llvm-readelf -A` output with a clang object.
- **crt0** — MMU, caches, exception vectors — fails as a hang or a fault far from its
  cause. Mitigation: the reporting vector table, the run timeout, and V5 tested on its
  own first, as on AArch64.
