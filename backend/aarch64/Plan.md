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
then Device type), so crt0 must turn on the MMU with an identity map (see A6). A stray
exception otherwise loops forever: crt0 installs a vector table that reports and exits,
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

## Phase 0 — groundwork

- **A1. Target plumbing.** Review the `aarch64` descriptor in `semantic/target.c`
  (`struct_return_max` chosen in A13). Teach `cpp -t aarch64` its predefined macros
  (`__aarch64__`, `__ARM_ARCH=8`, `__ARM_64BIT_STATE`, `__LP64__`, `_LP64`,
  `__CHAR_UNSIGNED__`, `__ELF__`, the `__SIZEOF_*__` set) with a test in
  `test_predefined_macros.cpp`; `lower -t aarch64` already accepts the name.
- **A2. TAC audit for `aarch64`.** Run the test corpus (chapter sources, translator
  fixtures, libc sources) through `lower -t aarch64`. Being LP64 with binary128 `long
  double` and unsigned `char` like `riscv64`, few surprises are expected; fix any in
  shared code and list them.
- **A3. Share the register allocator.** `backend/riscv/regalloc.c` colours TAC
  variables, not machine instructions, so most of it is target-neutral. Move the core
  (interference over `backend/common/flow.c`, Briggs coalescing, optimistic spilling,
  hints, the cross-call constraint) to `backend/common/regalloc.c`, parameterized by a
  register-class descriptor: per class the argument pool, the callee-saved pool, the
  hint registers, and an optional "needs a pair" hook (RV32 `long long`). RISC-V output
  stays byte-identical — the golden tests are the net. If the coupling with RISC-V's
  `Gen` turns out too deep, fork instead and record why.
- **A4. Shared qemu test plumbing.** Split the target-neutral half of
  `backend/riscv/test/riscv_test.h` (assemble, link, run qemu with a timeout, decode
  the exit status, the clang comparison) into a shared fixture with a per-target
  command line, so `aarch64_test.h` is small. The book suite gets an `aarch64`
  `BookTest` with its skip list. RISC-V tests unchanged.
- **A5. Share the LP64 runtime sources.** `libc/riscv64/float128.c`, `doprnt.c`,
  `frexp.c`, `ldexp.c`, `modf.c` contain nothing RISC-V-specific; move what is
  target-neutral to `libc/common/` (or an `lp64` subdirectory if they assume LP64), so
  `libc/aarch64` reuses them. Likewise the data-model headers that would be identical.

`make run` stays green after every A-step.

## Phase 1 — skeleton

- **A6. Runtime.** `libc/aarch64/`:
  - `crt0.S`: set `sp`; enable FP/SIMD (`CPACR_EL1.FPEN`); install a vector table
    whose every entry prints the exception syndrome (`ESR_EL1`, `ELR_EL1`) and exits
    with a distinctive status; build a two-entry level-1 identity map in 1 GiB blocks
    (`0`–`1 GiB` Device for the UART, `1`–`2 GiB` Normal cacheable for RAM), set
    `MAIR_EL1`/`TCR_EL1`/`TTBR0_EL1` and enable the MMU and caches (`SCTLR_EL1`), so
    unaligned access is legal as on any real AArch64 system; clear `.bss`; call `main`;
    pass its result to `exit`.
  - `console.s`: `putbyte` to the PL011 data register, `exit` through semihosting
    `SYS_EXIT_EXTENDED` (`hlt #0xf000`, `ADP_Stopped_ApplicationExit` + status).
  - `link.ld`: load at `0x40080000` (RAM starts at `0x40000000`; qemu may put the DTB
    at its start), stack and heap at the top of a 128 MiB RAM.
  - `malloc.s`, and the shared sources of A5, built with our compiler; archived with
    `llvm-ar` as `libc.a`.
- **A7. Skeleton.** `backend/aarch64/` with `CMakeLists.txt`, `a64.h` (IR: function,
  block, instruction over virtual registers; operands: register with width W/X/S/D/Q,
  immediate, symbol with optional `:lo12:` modifier, memory `[base, #off]` with
  pre/post-index, shifted/extended register, condition code), `codegen.c`, `emit.c`,
  `main.c` on `backend/common/driver.c`, and `genaarch64`. Emits sections, `.globl`,
  `.type`/`.size`, `.p2align`, labels.
- **A8. Run harness and first program.** `CompileToAarch64` (golden assembly) and
  `CompileAndRunAarch64` (assemble, link with crt0 and `libc.a`, run qemu with
  `-display none -serial stdio -monitor none -semihosting` under a short timeout).
  CMake finds `qemu-system-aarch64`; tests guard with `SKIP_IF_NO_AARCH64_TOOLS()`.
  `int main(void) { return 2; }` runs and returns 2.

## Phase 2 — instruction selection, book order

Naive and correct first: every TAC variable in a frame slot, operands loaded into
scratch registers (`x9`–`x15`, `v16`–`v31`), result stored back. Each step is done when
its book chapters pass and a few golden tests pin the selected instructions.

- **A9. Frame.** Slot layout from typed TAC, `ALLOCATE_LOCAL`, prologue
  `stp x29, x30, [sp, #-N]!` / `mov x29, sp`, epilogue, 16-byte alignment. Immediates
  and offsets out of range: `add`/`sub` take a 12-bit immediate (optionally `lsl 12`),
  `ldr`/`str` a scaled unsigned 12-bit offset or an unscaled signed 9-bit one
  (`ldur`/`stur`); beyond that, through `x16`/`x17`. Constants via `movz`/`movn`/`movk`.
- **A10. Integer ops** (ch. 2–4, 11, 12): 32-bit operations on W registers, 64-bit on
  X; `neg`, `mvn`, `mul`, `sdiv`/`udiv`, remainder as `msub`; shifts; comparisons as
  `cmp` + `cset`; width conversions with `sxtb`/`sxth`/`sxtw`/`uxtb`/`uxth` and the
  W-write zeroing of the upper half. A logical-immediate encoder (bitmask immediates)
  decides when `and`/`orr`/`eor` take an immediate.
- **A11. Control flow** (ch. 5–8): `.L` labels unique per TU, `b`, `cbz`/`cbnz`,
  `cmp` + `b.cond`.
- **A12. Calls, scalar ABI** (ch. 9): `x0`–`x7`/`v0`–`v7`, 8-byte stack slots, narrow
  arguments and results extended by the receiver, `bl` and `blr`, `FUN_CALL_NORETURN`.
- **A13. Globals and static data** (ch. 10): `.data`, `.bss`, `.rodata`, every
  `Tac_StaticInit` kind, `adrp` + `add :lo12:` addressing (small code model; no GOT in a
  static bare-metal link), static locals' `name$N` spelled legally. Set
  `struct_return_max` for `aarch64` so the frontend never lowers a struct result to a
  hidden first argument: the backend owns every struct result, since its address goes
  in `x8` (as RV32 already owns its 9–16-byte ones).
- **A14. Floating point** (ch. 13): `fadd`/`fsub`/`fmul`/`fdiv`/`fneg` on S/D,
  `fcvt` between them, `scvtf`/`ucvtf`/`fcvtzs`/`fcvtzu` (native 64-bit unsigned
  conversions, no helpers), `fcmp` + `cset` with NaN-correct conditions (`mi`/`ls` for
  `<`/`<=`), `fmov` for 8-bit-encodable constants, otherwise a literal in `.rodata`.
  Until A21, `long double` operations are a clear `fatal_error`, not a miscompile.
- **A15. Pointers, arrays, chars, strings** (ch. 14–16): loads/stores by width and
  signedness (`ldrsb`/`ldrb`, …), `ADD_PTR` with scaled-register addressing where it
  fits, and the byte-pointer TAC kinds as plain operations, as on RISC-V.
- **A16. Structs** (ch. 17–18): member access via `COPY_*_OFFSET`, whole-aggregate
  copies (`ldp`/`stp` of 16 bytes, then a tail), struct arguments and results passed
  whole through memory and `x8` for now.

## Phase 3 — ABI conformance

- **A17. Full aggregate classification.** HFAs in `v` registers, ≤ 16 bytes in one or
  two X registers, larger by reference to a caller copy, results through `x8`, the
  no-split rule, stack arguments after the registers.
- **A18. Variadic functions and `<stdarg.h>`.**
  - Callers: nothing special — variadic arguments follow the normal rules (an HFA is
    classified as usual, a `float` is promoted to `double` by the frontend).
  - Callees: the prologue of a function with `...` saves `x<n>`–`x7` (8 bytes each) and
    `q<m>`–`q7` (16 bytes each) past the named arguments into the general and vector
    save areas.
  - `va_start` is a backend-intercepted call, `__builtin_va_start(&ap)`, declared in
    `<stdarg.h>` (the mechanism the `__besm6_*` intrinsics use): it fills the five
    fields from the frame. `va_copy` is a struct copy; `va_end` nothing.
  - `__builtin_va_class(T)`, a frontend builtin taking a type name like `sizeof`, and
    like it an integer constant expression folded in the semantic pass. It encodes
    the type's AAPCS64 argument class: general registers, by reference (an aggregate
    over 16 bytes, not an HFA), or FP registers with the element size (4, 8 or 16) and
    count (1–4) — so a scalar `float`/`double`/`long double` and an HFA struct are the
    same case. The classification is a target property, reached through the target
    descriptor (the way `immediate_args` is), and is the same function the backend
    uses for calls (A17), so the two cannot disagree. Parser, semantic and
    `translator` tests; a target without one rejects the builtin.
  - `va_arg(ap, T)` is a header macro over a runtime helper
    `__va_arg(&ap, sizeof(T), _Alignof(T), __builtin_va_class(T))` returning the
    argument's address: it takes the next general or vector save-area slots (an HFA's
    members are spread one per `q` slot, so the helper gathers them into a
    temporary), or falls back to `__stack`, and fetches a by-reference aggregate
    through its pointer.
  - `va_list` passed to a clang-compiled `vprintf`, and ours receiving clang's, are
    covered by A19.
- **A19. Interop tests** with clang in both directions over a table of signatures:
  mixed int/FP, narrow ints (both extension directions), HFAs, small and large
  structs, struct results, `long double`, many arguments, variadics both ways (HFA
  structs read by `va_arg` included), and a `va_list` handed across.
- **A20. Differential book tests.** Every book program compiled by clang too, run under
  qemu, outputs compared — the RISC-V suite's comparison.

## Phase 4 — `long double`, library and headers

- **A21. binary128 `long double`.** Values in `q` registers and 16-byte slots; moves
  between `q` and an X pair (`fmov x, d` / `mov x, v.d[1]`) only where a helper needs
  it. Arithmetic, comparisons and conversions call the routines of the A5 `float128.c`,
  compiled for AArch64, so folded and computed values agree as on RISC-V. A
  `float128_tests` run like RISC-V's, against the same exact cases.
- **A22. Headers.** `libc/aarch64/include/`: `float.h`, `limits.h`, `stdint.h`,
  `inttypes.h`, `stddef.h`, `math.h`, `setjmp.h` (sharing with `riscv64` where A5 made
  that possible) and the A18 `stdarg.h`. An `aarch64-headers` CTest and its `-cpp`
  twin, like `riscv-headers`.
- **A23. Libc run tests.** Port the RISC-V `printf_tests`/`str_tests`/`mem_tests`/
  `math_tests` (host libc output as expectation).

## Phase 5 — code quality

- **A24. Register allocation** on the A3 allocator: `x0`–`x7`/`v0`–`v7` for values not
  live across a call, `x19`–`x28`/`v8`–`v15` otherwise, saved with `stp`/`ldp` pairs
  only when used. The ch. 20 tests pass.
- **A25. Leaf functions** with no frame and no stack; slots addressed from `sp` when
  the offsets fit, as on RISC-V.
- **A26. Peephole**: redundant moves and reloads, `ldr`/`str` pairs to `ldp`/`stp`,
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

- **The allocator extraction (A3)** touches a working backend. Mitigation:
  byte-identical RISC-V golden output, and the fork fallback.
- **`__builtin_va_class` touches every frontend stage** (parser, semantic, the
  constant folder). Mitigation: it folds to a plain integer constant before lowering,
  so TAC, the optimizer and the other backends never see it; BESM-6 and RISC-V output
  stay unchanged.
- **crt0 is more than on RISC-V**: MMU, caches, exception vectors. A mistake there
  shows up as a hang or a fault far from its cause. Mitigation: the reporting vector
  table, the run timeout, and A6 tested on its own (an unaligned load, an FP op, a
  deliberate fault) before any compiled code depends on it.
- **ABI corners** (receiver-side extension, HFA rules, the no-split rule, `q`
  registers for `long double`) are easy to get almost right. Mitigation: A19's interop
  table, written before the code it tests, with clang as the oracle.

