# x86-64 backend — development plan

A fifth backend, for x86-64: the System V AMD64 psABI, ELF objects, and programs running
bare-metal under `qemu-system-x86_64`, the way the other three LLVM-toolchain backends
run under their qemus. Code is link-compatible with clang's `x86_64-none-elf` output, so
clang is the test oracle: interop in both directions, and every book program compiled by
both and compared.

It follows the shape the AArch64 and ARM32 backends settled on: a directory of its own,
a small IR over virtual registers, naive selection first, and then register allocation
on `backend/common/regalloc.c` and a peephole pass. Three things are new to this project:

- a **two-operand, destructive** instruction set, with memory operands on most
  instructions;
- instructions tied to **fixed registers** (`div`/`idiv` in `rax`:`rdx`, shift counts in
  `cl`, `%al` before a variadic call);
- an **80-bit x87 `long double`**, the first `long double` that is neither binary128 nor
  `double`.

As before, an assumption found in shared code is fixed in the shared code, not worked
around in `backend/x86/`. BESM-6 output must not change, and RISC-V, AArch64 and ARM32
output changes only where a step says so.

This replaces `TODO.md`, which predates every LLVM-toolchain backend. Its instruction
patterns went into Phase 2. Its stack-only frame map, its `FrameKind` type
inference (TAC is typed now) and its macOS/Mach-O output do not. `x86_64.asdl` and
`x86_64.md` stay as the reference spec of the instruction set.

Step IDs are stable: a finished step is marked done, never renumbered. The prefix is `X`,
since `A` (AArch64), `R` (RISC-V), `B` (Bemsh) and `V` (ARM32) are taken.

## Target and decisions

| Decision | Choice | Why |
|---|---|---|
| ISA | x86-64 baseline (x86-64-v1): SSE2, no SSE3/SSE4/AVX | What clang targets by default for the triple; qemu's default CPU runs it. `cmov` is baseline; `fisttp` (SSE3) is not |
| Floating point | SSE2 scalar for `float`/`double`; x87 only for `long double` | The psABI: `float`/`double` in `xmm`, `long double` the 80-bit extended format in a 16-byte slot |
| ABI | System V AMD64 psABI, LP64, the `x86_64` descriptor in `semantic/target.c` | Signed plain `char`, `wchar_t` = `int`, `long double` 16 bytes with a 64-bit significand (clang `-dM -E`, checked) |
| Code model | Small, static, non-PIC; every symbol through `sym(%rip)` | Bare-metal static link below 2 GiB: no GOT, PLT or `movabs` of addresses |
| Output | GNU/LLVM AT&T syntax for ELF (`.s`) | The default of clang, GNU `as` and `objdump`, and what the old TODO and `x86_64.asdl` assume |
| Toolchain | `clang --target=x86_64-none-elf` (assemble), `ld.lld` (link), `llvm-ar`; `x86_64-elf-binutils` as a second assembler and for `objdump`/`readelf` | Homebrew LLVM as for the other backends. GNU `as` accepting our output too keeps it free of LLVM-only syntax |
| Run environment | `qemu-system-x86_64 -M microvm`, booted through the PVH ELF note into a 32-bit entry, crt0 switching to long mode | No firmware, no bootloader; `microvm` boots in 0.04 s against 0.10 s for `q35` |
| I/O and exit | COM1 at port `0x3f8` (`-serial stdio`); the status byte on port `0xe9` (`-debugcon file:…`), then a write to `isa-debug-exit` at `0xf4` | `isa-debug-exit` makes qemu exit with `(v << 1) | 1`, which loses bit 7 of an 8-bit status; debugcon carries the full byte |
| Backend IR | Small hand-written `X86_Instr` list over virtual registers, like `a64.h`/`a32.h`: two operands in AT&T order, a width per instruction | `x86_64.asdl` stays the reference spec; the IR covers only what we emit |
| Executable | `genx86` (`backend/x86/`), library `x86`; installed as `vgenx86` | Mirrors `genaarch64`/`vgenaarch64` and `genarm32`/`vgenarm32`: the installed name is the in-tree name with the `v` prefix |

Verified 2026-10-03 on this machine, with a scratch program (not in the tree):

- **Boot.** An ELF64 linked by `ld.lld` at `0x100000`, with a `.note.Xen` section of type
  `SHT_NOTE` holding `XEN_ELFNOTE_PHYS32_ENTRY` (type 18), boots under `qemu-system-x86_64
  -kernel` on `microvm`, `q35` and `pc`. Without the `@note` type the section is
  PROGBITS, there is no `PT_NOTE` segment, and qemu refuses the image ("without PVH ELF
  Note").
- **Long mode.** From the 32-bit entry the code did the following: built a PML4 and a PDPT
  of 1 GiB identity pages; set `CR4.PAE`, `OSFXSR` and `OSXMMEXCPT`, then `EFER.LME`;
  cleared `CR0.EM` and set `MP` and `PG`; loaded a three-entry GDT; and far-jumped to
  `.code64`. There it ran `addsd`/`cvttsd2si`, printed through `outb` to `0x3f8`, and
  exited. The default CPU has 1 GiB pages.
- **Exit status.** 200 written to `isa-debug-exit` came back as 145 (`401 & 255`). The
  same byte written to port `0xe9` arrived intact in the debugcon file.
- **Assemblers.** Clang's integrated assembler and `x86_64-elf-as --64` both accept the
  mixed `.code32`/`.code64` source.
- **Unlike AArch64 and ARM32,** an unaligned access needs no MMU set-up on x86; crt0
  enables paging only because long mode requires it.

### The psABI, as it affects us

- **Registers.** Integer arguments in `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`. Results in
  `rax` (`rax`:`rdx` for a two-eightbyte struct). Callee-saved: `rbx`, `rbp` and
  `r12`–`r15`. Caller-saved: `rax`, `rcx`, `rdx`, `rsi`, `rdi` and `r8`–`r11`. FP
  arguments in `xmm0`–`xmm7`, results in `xmm0` (`xmm0`:`xmm1`). **Every `xmm` register
  is caller-saved.** `r10` is the static chain, which C does not use. `r11` is free
  scratch.
- **`rsp`** is 16-byte aligned at every `call`, so it is `≡ 8 mod 16` on entry.
- **Stack arguments** go in 8-byte slots, the first at the lowest address. A
  `long double`, or a struct with 16-byte alignment, takes a 16-aligned slot.
- **Narrow values.** The written ABI leaves the upper bits of a `char`/`short`/`_Bool`
  argument or result undefined. Clang extends to 32 bits as the sender *and relies on it*
  as the receiver; GCC extends but does not rely on it. We **extend as sender and
  re-extend as receiver**, which is safe against both.
- **Aggregates** are classified by **eightbyte**. Each 8-byte chunk is INTEGER, SSE, X87,
  X87UP or MEMORY, by the merge rules of psABI §3.2.3. Over 16 bytes, or containing an
  unaligned field or a `long double`, the struct is MEMORY. A two-eightbyte struct may be
  mixed, e.g. `{double, long}` goes in `xmm0` + `rdi`.
- **All or nothing.** If an argument's eightbytes do not all fit in the remaining
  registers, the *whole* argument goes on the stack, and the registers stay available for
  later arguments. AAPCS splits instead (ARM32's `r3`/stack split); this rule is the
  opposite.
- **MEMORY arguments are copied by value onto the stack**, not passed by reference (unlike
  RISC-V and AArch64).
- **MEMORY results** are written to memory whose address the caller passes in `rdi`, as a
  hidden first argument. The callee **returns that address in `rax`**.
- **`long double`** is class X87. As an argument it always goes in memory (on the stack).
  As a result it comes back in `st(0)`. The x87 stack is otherwise empty at every call
  boundary.
- **Variadic calls.** The caller sets `%al` to an upper bound on the number of `xmm`
  registers used. The callee's prologue saves `rdi`–`r9` and, when `%al` ≠ 0, `xmm0`–`xmm7`
  into a 176-byte register save area. `va_list` is
  `struct __va_list_tag { unsigned gp_offset, fp_offset; void *overflow_arg_area,
  *reg_save_area; }[1]`. It is an **array type**, so passing a `va_list` passes a pointer.

### Registers, as we use them

As the allocator and selection use them:

| Use | Integer | SSE |
|---|---|---|
| Scratch for instruction selection | `rax`, `r10`, `r11` (plus `rdx`/`rcx` at a divide or variable shift, X9) | `xmm14`, `xmm15` |
| Values not live across a call | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` | `xmm0`–`xmm13` |
| Values live across a call | `rbx`, `r12`–`r15` (`rbp` too in a function without a frame pointer) | none: the psABI has no callee-saved `xmm` |
| Frame | `rsp` (`rbp` with `--frame-pointer`) | — |

`rax` is outside the allocator's pool although it is the result register. Being scratch
lets every two-operand pattern compute in `rax` without checking the operands for
aliasing, and the `cltq`/`cqto`/`div`/`setcc` idioms all want it. So the allocator
has no integer result hint; the peephole pass computes a result in `rax` instead. A `long double` never gets a
register: it lives in its 16-byte slot, and the x87 stack holds values only inside one
instruction's pattern (X14).

`make run` stays green after every X-step.

Phase 0 is done:
- `cpp -t x86_64` predefines clang's architecture macros for the triple (no
  `__CHAR_UNSIGNED__`).
- The `x86_64` descriptor leaves every struct result to the backend
  (`struct_return_max = SIZE_MAX`); X17 gave it its `va_class`.
- **`long double` is the x87 format in the frontend.** The descriptor's
  `ldouble_mant_dig = 64` makes both constant folders round every `long double` operand
  and result to a 64-bit significand (`target_ld_round`, over `f128_round` in
  `libutil/float128.c`). `f128_to_x87`/`f128_from_x87` convert to and from the 10-byte
  format. The double rounding of a constant (decimal → binary128 → 64 bits) is pinned
  by a test.
- The whole test corpus lowers for `x86_64` with no defect: against `aarch64` the TAC
  differs only by signed plain `char` and x87 folding. An *unfolded* `long double`
  literal keeps its binary128 bits in TAC, and the backend rounds it with
  `f128_to_x87`. No libc routine depends on the signedness of plain `char`.
- `libc/x86/CMakeLists.txt` finds the tools (`X86_TOOLS_FOUND`, `X86_CLANG`,
  `X86_LD`, `X86_QEMU`, `X86_GNU_AS`, `X86_LIB_DIR`, `X86_LINK_SCRIPT`,
  `X86_TARGET_FLAGS`).
- `QemuConfig.status_from_debugcon` makes the shared fixture pass `-debugcon
  file:<scratch>.status` and take main's result from that file's first byte.

Phase 1 is done:
- **The runtime is in `libc/x86/`.**
  - `crt0.S` enters through the PVH note in 32-bit mode and switches to long mode over
    static page tables (four 1 GiB pages). It enables SSE and the x87, installs 32
    exception gates that report vector, error code, `rip` and `cr2` and exit with 255,
    clears `.bss` and calls `main`. A value left on the x87 stack after `main` exits
    with 254. `crt0-status.o` prints main's result first.
  - `console.s` (`putbyte` on COM1, `exit` through debugcon and `isa-debug-exit`),
    `malloc.s` and `link.ld` (load at 1 MiB, `.eh_frame` discarded).
  - `libc.a` holds only these assembly leaves. The C library joins it once `genx86` can
    compile it, as ARM32's did. `malloc.o` needs `memset`/`memcpy` from it, so a
    program that calls `malloc` links only after that.
- **The backend skeleton is in `backend/x86/`.**
  - `x86.h`/`x86.c`: the IR over physical (hardware numbering, then `xmm`, then `st`)
    and virtual registers. Each operand has a width of its own; a suffixed opcode spells
    the instruction's width.
  - `emit.c` writes AT&T syntax, `codegen.c` turns TAC into IR, and `main.c` builds
    `genx86` on the shared driver. It handles `return` of an integer constant so far.
- **The tests are in `x86-tests`.** `x86_test.h` on `QemuTest` provides
  `CompileToX86`, which also assembles every output with GNU `as` when installed, and
  `CompileAndRunX86`/`CompileAndRunBook`/`RunAssembly`/`ClangRunBook`, with the status
  from debugcon. The book suite runs chapter 1, compared with clang.

Phase 2 is done:
- **Naive selection.** Every TAC variable has a slot below `rbp`, any other name is
  `sym(%rip)`. An instruction loads its operands into `rax`/`r10`/`r11` (`xmm14`/`xmm15`)
  with one of them straight from memory or as an immediate, and stores the result back.
  `frame.c` (slots, value access, the `.rodata` constant pool, `gen_memcopy`),
  `instr.c`, `call.c`, `fp.c` (SSE), `x87.c` and `data.c`.
- **Frame.** `push %rbp; mov %rsp, %rbp; sub $N, %rsp` and `leave; ret`, none at all in a
  leaf that touches neither the stack nor a slot.
- **Integers.** Two-operand forms through `rax`, three-operand `imul` by an immediate,
  `cltd`/`cqto` + `idiv` and `xor` + `div`, shifts by `%cl`, `cmp` + `setcc` +
  `movzbl`, the width conversions.
- **Control flow.** `test` + `jcc`; an FP truth test against zero with `jp`. Local
  labels `.Lx<n>` are numbered per translation unit.
- **Calls.** Scalars in `rdi`–`r9` and `xmm0`–`xmm7`, the rest in an outgoing area at
  the bottom of the frame; `%al` before a variadic or unprototyped callee; `call *%r11`.
- **SSE.** NaN-correct comparisons (swapped operands for `<`/`<=`, parity for `==`/`!=`),
  unsigned 64-bit conversions both ways, negation by a 16-byte `xorps` mask from
  `.rodata`.
- **x87.** Every `long double` in its slot, `fldt`/`fstpt` around each operation; the
  truncating `fistpq` under a switched control word; unsigned 64-bit with a 2^64 or 2^63
  correction; arguments in 16-byte aligned stack slots, results in `st(0)`. Checked
  against clang both ways.
- **Structs** went whole on the stack, and every struct result through the address in
  `rdi`, until X17. Copies move pieces through `r11`, in a loop (`rax`, `r10`, `rcx`)
  past 64 bytes.
- **Static data** with the x87 `long double` as `.quad` + `.short` + `.zero 6`; `name$N`
  static locals are legal symbols for both assemblers.
- **`libc.a`** holds the C library compiled by `genx86`; the `printf` family joined it
  in X18.
- **Tests.** The whole book suite runs, compared with clang. Three programs are skipped
  because they assume an unsigned plain `char` (clang gives what we give). There are
  goldens and run tests per step, and interop with clang for integer, FP and
  `long double` arguments.

Phase 3 is done:
- **Classification.** `tac_sysv64_class` (`tac/tac_abi.c`) classes each eightbyte by the
  psABI merge rules and returns MEMORY 0, X87 3 (a `long double`, or a struct of one), or
  the INTEGER (1) / SSE (2) class of each eightbyte as `class0 | class1 << 2`. It is the
  `x86_64` descriptor's `va_class`, so `__builtin_va_class` and `call.c` share it.
- **Structs** of up to 16 bytes go in registers by eightbyte, mixed included, all or
  nothing; the rest are copied onto the stack. An odd-sized eightbyte is put together from
  pieces through `r11`, reading and writing nothing past the struct's end. Results come
  back in `rax`/`rdx` and `xmm0`/`xmm1` in eightbyte order, in `st(0)` for X87, or through
  the address in `rdi`.
- **Variadics.** A variadic function saves `rdi`–`r9`, and `xmm0`–`xmm7` behind
  `testb %al, %al; je`, into a 176-byte save area; `__va_start(ap)` is expanded in place.
  `__va_arg` (`libc/x86/va_arg.c`) takes the class from X17 and puts a two-eightbyte
  value from registers together in `tmp`. `va_list` is clang's array type; the frontend
  needed no change (a parameter adjusts to a pointer, a struct member stays an array, a
  copy of the struct copies it).
- **Headers.** `libc/x86/include/` holds `stdarg.h`, plus `stddef.h` and `stdint.h`
  from riscv64 (`wchar_t` is `int` on both); the tests and the `libc.a` build search it
  first, then the LP64 and common headers. `libc.a` now has the `printf` family,
  `doprnt` and `va_arg`, but not the binary128 runtime.
- **Tests.** Goldens and run tests per step; three interop tables with clang both ways
  (scalars, structs of every class with the all-or-nothing rule in both register
  files, variadics with a `va_list` handed across), each checked against a deliberate
  mutation. The book suite has compared every program with clang since Phase 1; the three
  programs it skips now run as signed-char versions (`book_x86_tests.cpp`), also
  compared with clang.

Phase 4 is done:
- **Headers.** `libc/x86/include/` has `float.h` (the x87 `LDBL_*`, clang's values),
  `limits.h` (signed `char`), `setjmp.h`, `stdarg.h`, `stddef.h`, `stdint.h` and a
  README; `inttypes.h` and `math.h` come from `libc/lp64/include/`. `X86_INCLUDE_DIR` is
  set beside the other targets' in the top-level `CMakeLists.txt`. The
  `x86_64-headers` CTest and its `-cpp` twin parse every header, and
  `HeadersAgreeWithClang` checks the types, limits and `LDBL_*` values against clang's
  own headers for the triple.
- **`setjmp`/`longjmp`** (`libc/x86/setjmp.s`, in `libc.a`) save `rbx`, `rbp`,
  `r12`–`r15`, `rsp`, the return address, MXCSR and the x87 control word. The first
  target to have them.
- **Volatile.** The `setjmp` test found that `round++` on a `volatile int` added to the
  value last stored instead of the value read: the translator read the variable a second
  time without the volatile mark, and copy propagation folded it. Fixed for all targets:
  a volatile variable is read once in `++`, `--` and compound assignment, its
  initializer is a volatile write, and an assignment's value is the value stored. The
  backends with register allocation keep a volatile variable in memory, and no peephole
  pass deletes a volatile reload or pairs volatile accesses (BESM-6 rule #27 included).
  See "Volatile" in `docs/Technical_Reference.md`.
- **Libc run tests.** AArch64's `printf_tests`/`str_tests`/`mem_tests`/`math_tests`
  ported, plus `%Lf`/`%Le`/`%Lg` of the x87 `long double` through `va_arg` and the
  string routines on bytes over 127 with a signed plain `char`.

Phase 5 is done:
- **Register allocation** (`regalloc.c`, on `backend/common/regalloc.c`). The pools are
  `rdi`–`r9` then `rbx`, `r12`–`r15` (and `rbp` without a frame pointer), and
  `xmm0`–`xmm13`, all caller-saved, so an FP value live across a call keeps its slot. A
  divide or a shift by a variable is reported through the `runtime_call` hook: in the
  book suite that costs 20 memory references in 6153, so no narrower hook was added. A
  struct copy past 64 bytes loops through `xmm15` and counts in `r11`, and so writes
  no allocated register. Selection computes `d = a op b` in `d` unless `d` is `b`'s
  register (a commuting operator swaps them). Parameters and arguments are moved as if
  at once; a cycle is broken through `rax` or `xmm14`. In shared code, a parameter dead
  on entry no longer keeps its incoming register (the `dead_param` hook, in all four
  allocating backends).
- **Frames.** Without `--frame-pointer` the frame is addressed from `rsp` and the first
  callee-saved register goes where `rbp` would be pushed. A leaf whose slots fit keeps
  them in the red zone (clang assumes it too for the triple), and a leaf that needs no
  stack has no prologue. Slots are addressed from a pseudo register, `X86_FRAME`,
  resolved once the frame is known.
- **Peephole** (`peephole.c`), on the body before the prologue is built, over register
  liveness that includes the flags, and a second liveness of the upper halves of the
  general registers (a call notes which argument registers are 64 bits wide):
  - moves followed into their uses, and results computed in the register they are moved to;
  - loads and immediates folded into their users, `lea` into the memory operand it feeds;
  - reloads deleted, `test` for `cmp $0` and masks, `xor` for zero;
  - jump-to-next, branch-over-jump and dead code removed, and unreferenced blocks merged;
  - a 32-bit move to itself dropped where no upper half is read;
  - `cmov` for one-move triangles and diamonds, from a register or a slot, or a
    constant through `r11`. The unconditional move must be safe on both paths, so a
    load through a pointer never becomes one.
  
  The prologue then saves only the callee-saved registers still used. Comparisons fuse
  with their branch in selection, FP ones with the parity jumps. The libc's
  instructions went from 6571 (naive) to 3325.
- `rep movs` for large copies was not done. It needs `rdi`, `rsi` and `rcx`, which may
  hold the operands of the call being set up, and the `xmm15` loop already copies 16
  bytes per iteration.
- **Tests:** `regalloc_tests.cpp` and `peephole_tests.cpp`, the frame goldens, and run
  tests of every two-operand aliasing arrangement, of `rsp` alignment at calls
  (checked by clang's code), and of the rewrites. Each was checked against a mutation.

## Phase 6 — finishing

- **X26. Driver.** `vcc -t x86_64` runs:
  - `vcpp -t x86_64`, `vparse`, `vlower -t x86_64`, `vgenx86`;
  - `clang --target=x86_64-none-elf -c`;
  - `ld.lld -T link.ld crt0.o … -lc`.
  
  `cc-tests` cases, including a staged prefix.
- **X27. Install.** `genx86` as `vgenx86`; `crt0.o`, `libc.a`, `link.ld` and the
  headers (`libc/x86`, `libc/lp64`, `libc/common`) under `share/vcc/x86_64/`; the
  runtime only when the x86-64 clang/llvm-ar were found.
- **X28. Documentation.**
  - `docs/X86_64_Backend.md`, in the style of `docs/Arm32_Backend.md`: target, how code
    is generated, frame, calls, variadics, the x87 `long double`, runtime, running a
    program by hand under qemu.
  - README and CLAUDE.md for five targets, and `libc/x86/include/README.md`.
  - Remove `backend/x86/` from the "design notes only" sentence.

  This plan is then removed.

## Out of scope

- **i386 and x32.** A 32-bit mode (`--i386`, cdecl, x87 for all FP) would be the RV32
  counterpart of this backend, later. Leave room for it, but nothing more.
- **macOS (Mach-O) and Windows x64.** Different object format, symbol prefix, and (for
  Windows) calling convention. The psABI here is SysV only.
- **PIC/PIE**, GOT, PLT and TLS: a static bare-metal link needs none.
- **SSE3 and later, AVX/VEX encodings**, `__int128`, `_Complex`, atomics (`lock`
  prefixes), and Intel syntax.
- **x87 for `float`/`double`**, i.e. `-mfpmath=387`.

## Risks

- **x87 `long double`.** Folded constants against computed values (the double rounding of a constant), the
  control-word dance for truncation, and the x87 stack discipline at calls (empty, except
  `st(0)` for a result). An x87 stack left unbalanced fails far from its cause, as a NaN
  many calls later. Mitigation: values never stay on the x87 stack across TAC
  instructions, clang as the oracle in X19/X20, and a crt0 test that checks the stack is
  empty after `main`.
- **SysV classification edge cases:** all-or-nothing, mixed eightbytes, MEMORY by value
  on the stack, `long double` in a struct, the returned address in `rax`. Mitigation:
  X19's table with clang, and X17's interop test.
- **Signed plain `char`** is new among our byte-addressed targets, and libc sources or
  tests may quietly assume `char` ≥ 0. Mitigation: the Phase 0 audit found none in libc; the X22 string tests.
- **crt0.** A 32→64-bit transition that goes wrong hangs or triple-faults silently (qemu
  resets). Mitigation: the sequence verified above, the IDT reporting handler installed
  before anything can fault in 64-bit mode, the run timeout, and the runtime tests in `run_tests.cpp`.
- **No native x86 on this host** (arm64, no Rosetta). Everything runs under qemu TCG.
  The 0.04 s `microvm` boot keeps that cheap; nothing here depends on native execution.
