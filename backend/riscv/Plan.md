# RISC-V backend — plan: the riscv32 target

The riscv64 backend is done ([docs/Riscv_Backend.md](../../docs/Riscv_Backend.md)).
This plan adds 32-bit RISC-V to the same backend, switched by a flag, not a copy.

Step IDs are stable: a finished step is marked done, never renumbered.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| ISA | RV32IMFD (`-march=rv32imfd`) | the 32-bit twin of rv64: same F and D, so `float` and `double` stay in hardware |
| ABI | ILP32D psABI, the existing `riscv32` descriptor | link-compatible with clang `--target=riscv32` |
| Data model | `int`, `long`, pointers 32-bit; `long long` 64; `long double` binary128 | as the descriptor and clang |
| Backend | `genriscv --rv32`: one backend, register width a parameter | about 80% is shared: allocation, peephole, frames, calls |
| Run | `qemu-system-riscv32 -M virt -bios none` | same machine, RAM and devices as rv64 |
| Install | `bin/vgenriscv32`, `share/vcc/riscv32/{include,lib}` | beside `vgenriscv64` and `share/vcc/riscv64/` |
| Runtime | `libc/riscv32/` beside `libc/riscv64/` (today's `libc/riscv/`, renamed) | one directory per installed `share/vcc/<target>/` |
| Tools | `vcpp -t riscv32`, `vlower -t riscv32`, `vcc -t riscv32` | the preprocessor and driver (ported from v7besm) already select a target with `-t`; `lower` defaults to riscv64 |
| `size_t`, `ptrdiff_t` | `unsigned long`, `long`, as the front end types `sizeof` and pointer differences | clang says `unsigned int`/`int`: the same in the ILP32 ABI |

Verified 2026-10-01: Homebrew clang lists `riscv32`; `qemu-system-riscv32` is installed.

## What differs from rv64

- **64-bit integers need register pairs.** `long long` (and `double` passed in integer
  registers) is two registers: add/sub with carry, compare by halves, shifts across the
  halves, `mul`+`mulhu` for multiply. Divide and remainder call `__divdi3`, `__udivdi3`,
  `__moddi3`, `__umoddi3`; conversions with `double`/`float` call `__floatdidf`,
  `__fixdfdi` and friends.
- **No `*w` instructions, no `ld`/`sd`, no `fmv.x.d`/`fmv.d.x`.** Pointers and `long`
  are `lw`/`sw`; a 32-bit value needs no sign-extension rule. A `double` constant is
  loaded from a literal in `.rodata`, and a `double` moves between register files
  through memory.
- **ILP32D calls.** XLEN = 4: a struct in registers up to 8 bytes, larger by reference;
  `long long` in a register pair (an even pair when variadic); `double` in FP
  registers, but in an integer pair when variadic; `long double` (16 bytes) by
  reference. Save slots are 4 bytes; the stack stays 16-byte aligned.
- **Runtime.** `crt0.S`, `console.s`, `malloc.s` in 32-bit forms; the C sources
  recompiled; data-model headers of their own (`limits.h`, `stdint.h`, `stddef.h`,
  `stdarg.h`, `inttypes.h`).

## Phase 9 — riscv32 (done)

- **R27. Register width as a parameter.** *Done.* `genriscv --rv32` (a global like
  `riscv_regalloc`); every `ld`/`sd`, `*w` op, slot size and canonical form chosen by
  width. `long long`, `unsigned long long` and `long double` are rejected with a clear
  `fatal_error` (until R28 and R29). `libc/riscv/` renamed `libc/riscv64/`; runtime for
  rv32 in `libc/riscv32/`: crt0, console, malloc, link script, headers, and the C
  sources that need no 64-bit integer. `riscv32` in the CMake tool check, and the test
  harness parameterized by width: a `riscv32-tests` binary built from the same run
  sources. `int`/pointer run tests pass on qemu.
- **R28. 64-bit integers.** *Done:* `llong.c`, `libc/riscv32/int64.c`; a `long long`
  is a pair value, as `long double` is on rv64, so calls pass it in a register pair
  (an even one when variadic) already. `long long` lives in an 8-byte slot (like `long double`),
  operated on in register pairs with inline sequences; division, remainder and
  int64↔FP conversions through the libgcc-named routines, written in C in the runtime.
  Tests against exact results, as for binary128.
- **R29. ILP32D calls.** *Done:* `interop32_tests.cpp`, and `interop_tests.cpp` runs on rv32 too; `riscv32` gets `struct_return_max` 16, since a 16-byte `{double, double}` comes back in fa0/fa1, and the backend passes the hidden pointer for a `long double` or a struct of 9–16 bytes. What R28 left: a `double` in an integer pair (variadic, or
  past fa7), and `long double` by reference. The classification above, with clang interop tests in both
  directions (pairs, split a7/stack, variadic alignment, structs, `long double` by
  reference). `<stdarg.h>` for rv32.
- **R30. Library, book and install.** *Done:* 88 book programs skipped on rv32, each of which gives clang's result; three of them are not valid C with a 32-bit `long`. Integer literals are re-typed for a narrow `long` in the type checker (the parser assumes 64 bits). The libc and `long double` tests on rv32; the
  book suite on rv32, compared with clang, with a skip list for the programs whose
  results depend on 64-bit `long`; `make install` for rv32 (`vgenriscv32`,
  `share/vcc/riscv32/`); `-t riscv32` in `vcpp` (`__riscv_xlen=32`, `__ILP32__`) and in
  `vcc` (assembler flags, `link.ld`, the `cc-tests` cases); docs/Riscv_Backend.md,
  cc/README.md, cpp/README.md.
- **R31. `long long` in registers.** *Done:* the high word is an allocator node of its own; a copy of a `long long` is coalesced as a pair (low with low, high with high), both words are hinted to their argument and result registers, and the peephole follows a copy to its uses within a block; together these take nearly all the moves around a pair (the rv32 libc is 15% smaller).

## Risks

- **Frontend assumptions that `long` is as wide as `long long`, or a pointer as wide as
  a 64-bit integer.** No 32-bit byte-addressed target has run yet. Mitigation: the TAC
  verifier, the interop tests, and fixes in the shared code, never worked around in
  the backend.
- **Golden assembly tests are rv64-specific.** They stay so; rv32 gets a few of its
  own, and is checked mostly by run and interop tests.
- **The book suite's generic programs assume LP64** where widths matter. A skip list
  for rv32, as BESM-6 has, rather than weakened expectations.

## Open questions

1. Answered: installed as `vgenriscv32`, with `share/vcc/riscv32/include` and `lib`.
2. Answered: runtime layout: `libc/riscv32/` beside `libc/riscv64/`. C sources shared
   by the two (`doprnt.c`, `float128.c`, …) stay in `libc/riscv64/` and are compiled
   from there for rv32 too, unless they turn out to need a 32-bit variant.
