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

## Phase 4 — `long double`, library and headers

- **A21. binary128 `long double`. Done.** Values live in 16-byte slots and travel in
  `q` registers; no X pair is needed. Arithmetic, comparisons and conversions call the
  routines of `libc/common/float128.c` (operands in `q0`/`q1`, an integer in `x0`/`w0`,
  a comparison's int tested with `cmp w0, #0` + `cset`), so folded and computed values
  agree as on RISC-V. Negation flips the sign bit of a copy; a zero test ors the two
  doublewords, the sign shifted out. `libc.a` now has the `printf` family too.
  `float128_tests` runs RISC-V's exact cases, plus interop with clang both ways. The
  AST importer rejected `EXPR_VA_CLASS` (its range check ended at `EXPR_GENERIC`),
  found by the first libc source with `va_arg`, `doprnt.c`.
- **A22. Headers. Done.** `float.h`, `inttypes.h`, `limits.h` and `math.h` moved from
  `libc/riscv64/include` to `libc/lp64/include/`, searched between the target's own and
  `libc/common/include` (riscv64 too; installed into its one include directory).
  `stddef.h` and `stdint.h` stay per target, against the plan: `wchar_t` is `int` on
  RISC-V but `unsigned int` under AAPCS64. `libc/aarch64/include/` holds those two,
  `setjmp.h` (declarations only, as on RISC-V) and `stdarg.h`. The test fixtures take
  the LP64 directory through an optional `TEST_LP64_INCLUDE_DIR`. `aarch64-headers`
  CTest and its `-cpp` twin, like `riscv-headers`.
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

