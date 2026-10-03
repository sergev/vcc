# AArch64 backend — development plan

A third backend, for 64-bit ARM: the standard AAPCS64 procedure call standard, ELF
objects, programs running bare-metal under `qemu-system-aarch64`, exactly as the RISC-V
backend runs under `qemu-system-riscv64`. Code is link-compatible with clang's
`aarch64-none-elf` output, so clang is the test oracle: interop in both directions, and
every book program compiled by both and compared.

As with RISC-V, a BESM-6 or RISC-V assumption found in shared code is fixed in the
shared code, not worked around in `backend/aarch64/`. BESM-6 output must not change, and
RISC-V output changes only where a step says so.

Step IDs are stable: a finished step is marked done, never renumbered.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| ISA | ARMv8.0-A base A64 with scalar FP/SIMD | Enough for C11 without atomics; runs on any qemu `-cpu` |
| ABI | AAPCS64, the `aarch64` descriptor already in `semantic/target.c` | LP64, unsigned plain `char`, binary128 `long double`; what clang emits for `aarch64-none-elf` |
| Output | GNU/LLVM assembler syntax for ELF (`.s`) | Assembled by clang; we write no assembler or object format |
| Toolchain | `clang --target=aarch64-none-elf` (assemble), `ld.lld` (link), `llvm-ar` | Already used for RISC-V; Homebrew LLVM (Apple's clang also assembles AArch64 ELF) |
| Run environment | `qemu-system-aarch64 -M virt -cpu cortex-a57 -semihosting`, bare metal at EL1 | Output through the PL011 UART at `0x09000000`; exit status through semihosting `SYS_EXIT_EXTENDED` |
| Backend IR | Small hand-written `A64_Instr` list over virtual registers, like `rv.h` | `aarch64.asdl` stays the reference spec; the IR covers only what we emit |
| Executable | `genaarch64` (`backend/aarch64/`), library `aarch64`; installed as `vgenaarch64` | Mirrors `genriscv`/`vgenriscv64` |

Verified 2026-10-02 on this machine: a hand-written `_start` assembled with Homebrew
clang, linked by `ld.lld` at `0x40080000`, ran under qemu `virt` at EL1, enabled FP
through `CPACR_EL1`, printed through the PL011, and exited with status 5 through
semihosting. **With the MMU off, an unaligned load takes an alignment fault** (memory is
then Device type), so `libc/aarch64/crt0.S` turns on the MMU with an identity map. A
stray exception would loop forever: crt0 installs a vector table that reports and exits,
and the harness runs qemu under a short timeout regardless.

### AAPCS64, as it affects us

- **Registers.** Arguments `x0`–`x7` / `v0`–`v7`; results `x0`(–`x1`) / `v0`–`v3`;
  indirect result address in **`x8`** — not a hidden first argument, so the frontend's
  sret lowering must not be used (A13); scratch `x9`–`x15`, `x16`/`x17` (call veneers
  may clobber them), `v16`–`v31`; callee-saved `x19`–`x28` and the low 64 bits of
  `v8`–`v15`. `x18` is never used (a platform register elsewhere; free here, but not
  worth the incompatibility). `x29` frame pointer, `x30` link register.
- **`sp`** is 16-byte aligned whenever it is used as a base.
- **Narrow values.** Neither side extends: the upper bits of a `char`/`short`/`_Bool`
  argument or result are unspecified, so the receiver extends (clang: the caller does
  `sxtb` on a `signed char` result). Upper 32 bits of an `int` in an X register are
  unspecified too.
- **Stack arguments** take 8-byte slots (16 for a 16-byte-aligned type), in order.
- **Aggregates.** A homogeneous float aggregate (1–4 members of one FP type, `long
  double` included) travels in consecutive `v` registers; another aggregate of ≤ 16
  bytes in one or two X registers (an even pair if 16-aligned); a larger one is copied
  by the caller and passed by address. A result larger than 16 bytes (and not an HFA)
  is written through `x8`. Once the registers of a class run out, the rest of that
  class goes on the stack, and an aggregate is never split between registers and stack.
- **`long double`** is binary128, passed in a `q` register (`v0`–`v7`, all 128 bits);
  its arithmetic, comparisons and conversions call `__addtf3`, `__lttf2`, … .
- **Variadic arguments** use the same registers as named ones. The callee saves the
  remaining `x` and `q` argument registers in two save areas, and `va_list` is the
  AAPCS64 structure `{ __stack, __gr_top, __vr_top, __gr_offs, __vr_offs }` — the
  largest difference from RISC-V (A18).

## Phase 2 — instruction selection, book order

Naive and correct first: every TAC variable in a frame slot, operands loaded into
scratch registers (`x9`–`x15`, `v16`–`v31`), result stored back. Each step is done when
its book chapters pass (added to `AARCH64_BOOK_SOURCES`) and a few golden tests pin the
selected instructions. The book programs from chapter 9 on call `putchar`, `puts`,
`strcmp`, `malloc`, …: the C sources of libc join `libc.a` step by step, as soon as the
code generator compiles them (`putchar` with calls, the string and memory functions
with pointers), not all at once in A23.

- **A9. Frame. Done.** `frame.c`: slots below x29 from the typed `locals` (an
  `ALLOCATE_LOCAL` may enlarge one), `stp x29, x30, [sp, #-16]!` / `mov x29, sp` /
  `sub sp, sp, #N`, and an epilogue marker expanded to `mov sp, x29` /
  `ldp x29, x30, [sp], #16` once the frame is known. An offset that fits neither
  `ldr`/`str` (scaled 12 bits) nor `ldur`/`stur` (signed 9 bits) goes through x16, built
  by `add`/`sub` with a 12-bit immediate and `lsl #12` (x17 beyond 16 MiB); `COPY`
  through scratch registers, `RETURN` of a variable.
- **A10. Integer ops. Done** (ch. 2–4): 32-bit operations on W registers, 64-bit on
  X; `neg`, `mvn`, `mul`, `sdiv`/`udiv`, remainder as `sdiv` + `msub`; shifts;
  comparisons as `cmp` + `cset`, the operator (or a pointer) saying the signedness;
  conversions: a store truncates, a load extends by the source's type, and
  `sxtb`/`sxth`/`sxtw`/`uxtb`/`uxth` where those differ. A constant operand is loaded
  as the operation's type. Constants go through a register: immediate operands are the
  peephole's (A26).
- **A11. Control flow. Done** (ch. 5–8): a TAC label `%N` is a block labelled `.LN`
  (unique per TU), `b`, and `cbz`/`cbnz` on the condition at its type's width; a
  compare-and-branch (`cmp` + `b.cond`) is the peephole's (A26).
- **A12. Calls, scalar ABI. Done** (ch. 9): `x0`–`x7`/`v0`–`v7`, 8-byte stack slots
  in the outgoing area at sp, read by the callee at x29 + 16; each parameter stored
  into a slot; narrow arguments and results extended by the receiver (a store
  truncates, a load extends); `bl` and `blr`, `FUN_CALL_NORETURN` alike. Run against
  clang in both directions. `libc.a` gets its first C source, `putchar`.
- **A13. Globals and static data. Done** (ch. 10–12): `data.c` (`.data`, `.bss`,
  `.section .rodata`; `.byte`/`.hword`/`.word`/`.xword`, `.zero`, `.ascii`; every
  `Tac_StaticInit` kind; block-scope statics after their function, under their
  `name$N`), globals through `adrp` + `add :lo12:` (small code model, no GOT), and
  `GET_ADDRESS` of a slot, global or function. The `aarch64` descriptor's
  `struct_return_max` is `SIZE_MAX`: the front end never lowers a struct result to a
  hidden first argument, the backend owns every one (its address goes in `x8`).
- **A14. Floating point. Done** (ch. 13): `fadd`/`fsub`/`fmul`/`fdiv`/`fneg` on S/D,
  `fcvt` between them, `scvtf`/`ucvtf`/`fcvtzs`/`fcvtzu` (native 64-bit unsigned
  conversions, no helpers), `fcmp` + `cset` with NaN-correct conditions (`mi`/`ls` for
  `<`/`<=`), `fcmp #0.0` + `b.eq`/`b.ne` for a condition. A constant is built in x17
  and moved with `fmov`; `fmov`'s own 8-bit immediate is the peephole's (A26). FP
  arguments and results run against clang both ways. `libc.a` gains `fabs`, `fma`,
  `fmax`, `fmin`. `long double` operations stay a clear `fatal_error` until A21.
- **A15. Pointers, arrays, chars, strings. Done** (ch. 14–16): loads and stores
  through a pointer by width and signedness (`ldrsb`/`ldrb`, …), `ADD_PTR` as a shifted
  add for a power-of-two scale (a `mul` otherwise; an index narrower than 64 bits
  extended by its type), `PTR_DIFF`, the byte-pointer TAC kinds as plain operations,
  and the member copies `COPY_TO_OFFSET`/`COPY_FROM_OFFSET` (array initializers use
  them). An aggregate or a `long double` value is copied as bytes. `libc.a` now holds
  all of `libc/common` but the `printf` family (A18) and `libc/lp64`, all compiled by
  us; malloc's run test is back.
- **A16. Structs. Done** (ch. 17–20): member access and copies (A15), and structs by
  value under AAPCS64's general composite rules: up to 16 bytes in one or two X
  registers (an even pair when 16-byte aligned; whole on the stack, rounded to 8 and
  aligned, once the registers run out, and then no later X register), a larger one as
  a pointer to the caller's copy, a result of up to 16 bytes in x0/x1, a larger one
  through the address the caller passes in x8 (saved by the callee on entry). A last
  doubleword shorter than 8 bytes is moved byte by byte. Run against clang both ways.
  With it every book chapter passes, 19 and 20 included. Homogeneous float aggregates
  are A17's.

## Phase 3 — ABI conformance

- **A17. Full aggregate classification. Done.** `tac_aapcs64_class`/`tac_aapcs64_hfa`
  (`tac/tac_abi.c`, in the `tac` library so the semantic pass reaches it too, A18):
  an HFA — 1–4 members of one FP type, through arrays, nested structs and unions, no
  padding — goes in consecutive `v` registers, a member in each (`s`, `d` or `q`), or
  whole on the stack once they do not all fit, and then no later `v` register; the
  result in `v0`–`v3`. `long double` values are passed and returned in `q` registers
  (moved only; their arithmetic is still A21's), and a constant one is built through
  a slot. `libc.a` gains `float128.c`, compiled by us. Run against clang both ways.
- **A18. Variadic functions and `<stdarg.h>`. Done.**
  - Callers: nothing special — variadic arguments follow the normal rules.
  - Callees: the prologue of a function with `...` saves `x<n>`–`x7` and `q<m>`–`q7`
    past the named arguments at the ends of a 64-byte general and a 128-byte vector
    save area.
  - `va_start(ap)` is `__va_start(&ap)`, expanded in place by the code generator: it
    fills the five fields of the AAPCS64 `va_list` from the frame. (Not
    `__builtin_va_start`: clang reads the same header in the book comparison, and that
    name is its own builtin.) `va_copy` is a struct copy; `va_end` nothing.
  - `__builtin_va_class(T)`: a keyword and an AST node (`EXPR_VA_CLASS`), typechecked
    and folded like `_Alignof`, an `int` constant. Its value is the target's
    `Target.va_class` applied to the TAC form of `T` — for `aarch64`
    `tac_aapcs64_class`, the function the backend classifies arguments with (A17), so
    the two cannot disagree. Translating an AST type to a TAC type moved from the
    translator to `semantic/tac_type.c` for it. A target without one rejects the
    builtin. Parser, semantic and translator tests.
  - `va_arg(ap, T)` is `*(T *)__va_arg(&ap, sizeof(T), _Alignof(T),
    __builtin_va_class(T), &(T){ 0 })`: the runtime helper (`libc/aarch64/va_arg.c`,
    AAPCS64 B.4) takes the next general or vector save-area slots, or falls back to
    `__stack`, fetches a by-reference aggregate through its pointer, and gathers an
    HFA's members, one per `q` slot, into the compound literal.
  - `<stdarg.h>` is the first header of `libc/aarch64/include`, searched ahead of the
    riscv64 ones (A22 moves the rest); the test fixtures take it through an optional
    `TEST_TARGET_INCLUDE_DIR`.
  - The `printf` family still waits: `__doprnt` converts a `long double` argument to
    `double`, which is A21's.
  - The scanner's keyword table had `__func__` out of `strcmp` order (found by luck by
    the binary search); `__builtin_va_class` exposed it.
- **A19. Interop tests. Done** (`interop_tests.cpp`, with `call_tests`, `fp_tests`,
  `struct_tests` and `hfa_tests` before it): one source compiled by both sides, each
  calling the other's copy — 18 interleaved int/FP arguments, narrow values both ways,
  results of every class (3, 12 and 40-byte structs, HFAs of floats and doubles, a
  `long double` alone and in a struct), and variadic functions both ways over every
  class (HFAs read by `va_arg` included, past the registers onto the stack), with a
  `va_list` handed across in both directions. `long double` arithmetic is clang's
  until A21.
- **A20. Differential book tests.** Every book program compiled by clang too, run under
  qemu, outputs compared — the RISC-V suite's comparison.

## Phase 4 — `long double`, library and headers

- **A21. binary128 `long double`.** Values in `q` registers and 16-byte slots; moves
  between `q` and an X pair (`fmov x, d` / `mov x, v.d[1]`) only where a helper needs
  it. Arithmetic, comparisons and conversions call the routines of `libc/common/float128.c`,
  compiled for AArch64, so folded and computed values agree as on RISC-V. A
  `float128_tests` run like RISC-V's, against the same exact cases.
- **A22. Headers.** Move the six LP64 data-model headers (`float.h`, `inttypes.h`,
  `limits.h`, `math.h`, `stddef.h`, `stdint.h` of `libc/riscv64/include`) to
  `libc/lp64/include/`, searched between the target's own and `libc/common/include`
  (for `riscv64` too); `libc/aarch64/include/` then holds `setjmp.h` and the A18
  `stdarg.h`. An `aarch64-headers` CTest and its `-cpp`
  twin, like `riscv-headers`.
- **A23. Libc.** Build the C sources (`libc/common`, `LIBC_C_IEEE`, `libc/lp64`) with
  our compiler into `libc.a`; add malloc's run test; port the RISC-V `printf_tests`/
  `str_tests`/`mem_tests`/`math_tests` (host libc output as expectation).

## Phase 5 — code quality

- **A24. Register allocation** on `backend/common/regalloc.c`: `x0`–`x7`/`v0`–`v7` for values not
  live across a call, `x19`–`x28`/`v8`–`v15` otherwise, saved with `stp`/`ldp` pairs
  only when used. The ch. 20 tests pass.
- **A25. Leaf functions** with no frame and no stack; slots addressed from `sp` when
  the offsets fit, as on RISC-V.
- **A26. Peephole**: immediate operands (`add`/`sub`/`cmp` with 12 bits, a bitmask
  immediate encoder for `and`/`orr`/`eor`), redundant moves and reloads, `ldr`/`str` pairs to `ldp`/`stp`,
  `add` folded into the addressing mode, `mul`+`add` to `madd`, `cmp #0` + `b.eq` to
  `cbz`, branch over jump, jump to next label.

## Phase 6 — finishing

- **A27. Driver.** `vcc -t aarch64`: `vcpp -t aarch64`, `vparse`,
  `vlower -t aarch64`, `vgenaarch64`, `clang --target=aarch64-none-elf -c`,
  `ld.lld -T link.ld crt0.o … -lc`; `cc-tests` cases including a staged prefix.
- **A28. Install.** `genaarch64` as `vgenaarch64`; `crt0.o`, `libc.a`, `link.ld` and
  the headers under `share/vcc/aarch64/`, only when the AArch64 clang/llvm-ar were
  found.
- **A29. Documentation.** `docs/Aarch64_Backend.md` in the style of
  `docs/Riscv_Backend.md` (target, how code is generated, frame, calls, runtime, running
  a program by hand under qemu), README and CLAUDE.md for three targets. This plan is
  then removed.

## Risks

- **`__builtin_va_class` touches every frontend stage** (parser, semantic, the
  constant folder). Mitigation: it folds to a plain integer constant before lowering,
  so TAC, the optimizer and the other backends never see it; BESM-6 and RISC-V output
  stay unchanged.
- **ABI corners** (receiver-side extension, HFA rules, the no-split rule, `q`
  registers for `long double`) are easy to get almost right. Mitigation: A19's interop
  table, written before the code it tests, with clang as the oracle.

