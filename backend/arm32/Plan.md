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

Phase 1 is done. `libc/arm32/` has `crt0.S` (vectors that report a fault and exit 255
from SVC mode — the exception modes have no stack —, VFP on, `.bss` cleared, the
short-descriptor identity map and the MMU and caches on, `PRINT_STATUS`), `console.s`
(PL011, semihosting exit), `malloc.s`, `link.ld` (at `0x40010000`, `.ARM.exidx`
placed), and `aeabi_*.s`: the RTABI helpers, which use the *base* standard even in a
hard-float program, so the conversions move values between the core and VFP registers
around the hard-float C routines of `libc/ilp32/int64.c`. All of them were checked
against the host through clang-compiled callers. `libc.a` is assembly only so far.
`genarm32` (`backend/arm32/`: `a32.h` IR, `emit.c`, `codegen.c`) returns integer
constants; every module starts with a header (`.syntax unified`, `.arch armv7-a`,
`.arch_extension idiv`, `.fpu vfpv3-d16`, the ABI `.eabi_attribute`s) whose ABI tags
equal clang's — a plain `.s` gets none from the command line, and `.arch armv7ve`
crashes clang's assembler. `ld.lld` does not check `Tag_ABI_VFP_args`: it links a
soft-float object with a hard-float one silently. `arm32-tests` (`arm32_test.h` on
`QemuTest`, `book_test.h`) runs chapter 1 of the book, compared with clang; until V21
the test programs use the riscv32 headers.

Phase 2 is done. `genarm32` selects every TAC instruction naively (`frame.c`,
`instr.c`, `llong.c`, `fp.c`, `call.c`, `data.c`): each `%` name in a slot below r11,
operands loaded into scratch registers, the result stored back. The frame is
`push {r11, lr}`, the slots, then d14/d15 and r10 when used, and the outgoing area; a
function using none of these returns with a bare `bx lr`. Book chapters 1–18 pass,
compared with clang. Findings and changes against the plan:
- **r10 is the third scratch register**, as the Risks foresaw. It holds an address while
  r12 and lr hold the two words of a `long long` (stored to a global or a far slot), and
  the third register of a copy between two addresses. It also holds the quotient of `%`
  (`sdiv` + `mls`). It is callee-saved, so a function that uses it saves it.
- **FP constants** use `vmov.f32`/`.f64 #imm` when they are VFP immediates (the
  assembler wants a point, `#1.0`). Otherwise their bits go through core registers into
  `vmov`, so there is no `.rodata` pool. A returned constant's bits go through r0/r1,
  which needs no frame.
- **The RTABI helpers are split into four objects** (`aeabi_divmod.s`, `aeabi_long.s`,
  `aeabi_conv.s`, `aeabi_mem.s`). In one object, `__aeabi_lmul` dragged in references to
  every C routine.
- **`int64.c` joined `libc.a` at V15, not V11/V14.** It stores through pointers. Until
  then the division and conversion run tests waited.
- **The C sources in `libc.a` so far:** `putchar`, the string, memory and math
  functions, `atoi`, `puts`, and `libc/ilp32` (`frexp`, `ldexp`, `modf`, `int64`). The
  printf family waits for V18.
- **Structs already follow AAPCS** for every composite that is not a homogeneous FP
  aggregate, interop-tested with clang. They go in core registers (from an even one if
  8-aligned) and split between r3 and the stack while that is still empty. A result of
  up to 4 bytes comes back in r0, a larger one through the address in r0. HFAs travel
  as plain composites until V17.
- **Variadic calls** already use the base standard (a double in a core pair). A
  variadic *definition* is V18.
- The `long long` run tests are RV32's `llong_tests`, ported (host results as
  expectation).

Phase 3 is done. The calls are AAPCS-VFP in full, interop-tested with clang both ways
over a table of signatures, and the whole book runs, compared with clang (only the
ILP32 skip list stays out). Findings and changes against the plan:
- **`tac_aapcs32_class`** shares `tac_aapcs64_class`'s code in `tac/tac_abi.c`, with
  `long double` counted as `double` and 8 bytes. An HFA takes the lowest run of free `s`
  registers (even pairs for doubles) that holds it, as an FP scalar does, so it is
  back-filled too. Under the base standard it is a plain composite, as an argument and
  as a result (≤ 4 bytes in r0, else through the address in r0).
- **A variadic function** pushes `r0`–`r3` before its frame record and returns with
  `pop {r11, lr}; add sp, sp, #16; bx lr`. It reads every parameter, the named ones
  too, in place in that one area (`r11 + 8` on), and returns a `float` in r0 and a
  `double` in r0:r1.
- **`libc/arm32/include/stdarg.h`** came at V18, ahead of the other ARM32 headers.
  `TEST_TARGET_INCLUDE_DIR` (`libutil/test/test_preprocess.h`) and the libc build put it
  before the riscv32 headers. V21 drops that once the ARM32 directory is complete.
- **`libc.a` has the printf family** (`printf`, `sprintf`, `snprintf`, `__doprnt`): all of
  `LIBC_C_COMMON` but no `float128`, since `long double` is a double.
- **Clang-compiled code calls** `__aeabi_ldivmod`, `__aeabi_uldivmod`, `__aeabi_l2d`,
  `__aeabi_ul2f`, `__aeabi_d2lz`, `__aeabi_f2ulz` and `__aeabi_memcpy4` from our
  `libc.a`; a run test checks each.
- **Chapters 19 and 20** passed as soon as they were enabled; there is no chapter filter
  any more.

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
  same load succeeds. crt0 does that.
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
| Scratch for instruction selection | `r12`, `lr` (saved by any function that uses it), `r10` (saved when used) | `d14`, `d15` (`s28`–`s31`), saved like any callee-saved register when used |
| Values not live across a call | `r0`–`r3` | `d0`–`d7` |
| Values live across a call | `r4`–`r9` (`r11` too in a function without a frame pointer) | `d8`–`d13` |

Two integer scratch registers carry most patterns because loads and stores leave the
flags alone: a `long long` add from memory to memory is `ldr`/`ldr`/`adds`/`str`, then
`ldr`/`ldr`/`adc`/`str` on `r12` and `lr`, with the carry surviving between the
halves. A pattern that needs more (a 64×64 multiply, a variable 64-bit shift) calls its
RTABI helper. `r10` is the third scratch for the cases Phase 2 found (above).

A `float` occupies a whole `d` register in the allocator's view (its even `s` half), so
`s`/`d` aliasing never reaches the allocator. Call setup alone deals in single `s`
registers, for back-filling.

## Phase 4 — library and headers

- **V21. Headers.** `libc/arm32/include/`: `float.h` (`LDBL_*` equal to `DBL_*`),
  `stddef.h` and `stdint.h` (unsigned `wchar_t`), `setjmp.h` (`r4`–`r11`, `sp`, `lr`,
  `d8`–`d15`), beside the V18 `stdarg.h`. The rest comes from `libc/ilp32/include/` and
  `libc/common/include/`. Add an `arm32-headers` CTest and its `-cpp` twin, like
  `riscv32-headers`, and switch `arm32-tests` and the libc build from the riscv32
  headers to these (dropping `TEST_TARGET_INCLUDE_DIR`).
- **V22. Libc run tests.** Port the AArch64 `printf_tests`/`str_tests`/`mem_tests`/
  `math_tests` (host libc output as expectation). `printf("%Lf")` exercises the 8-byte
  `long double` through `va_arg`.

## Phase 5 — code quality

- **V23. Register allocation** on `backend/common/regalloc.c`, with the pools of the
  register table. The pair hook RV32 added carries `long long`. Callee-saved core
  registers are pushed with the frame record in one `push`/`pop`, VFP ones with one
  `vpush`/`vpop` of the used range. The ch. 20 tests keep passing.
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

- **Scratch registers** (r12, lr, r10) must stay out of the allocator's pools at V23,
  and r10 out of the callee-saved pool. Mitigation: the register table above.
- **AAPCS-VFP argument rules** — back-filling, the closed-VFP-after-stack rule, the
  even-pair rule, the `r3`/stack split, and the switch to the base standard for
  variadics — are each easy to get almost right. Mitigation: V19's interop table, with
  clang as the oracle in both directions; V23–V25 must keep it green.
- **`s`/`d` aliasing** would corrupt values silently if two allocator units overlapped.
  Mitigation: the allocator sees only `d` registers, a `float` living in the even half,
  and only call setup (`call.c`) names odd `s` registers. A regalloc test pins that.
- **Missing RTABI symbols** surface only when clang-compiled code is linked. Mitigation:
  `aeabi_*.s` provide the whole family up front, and V19 links clang callees that use
  the common ones.
