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
patterns survive in Phase 2 below. Its stack-only frame map, its `FrameKind` type
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

Fixed here so the allocator (X23) and the naive selection (Phase 2) agree from the start:

| Use | Integer | SSE |
|---|---|---|
| Scratch for instruction selection | `rax`, `r10`, `r11` (plus `rdx`/`rcx` at a divide or variable shift, X9) | `xmm14`, `xmm15` |
| Values not live across a call | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` | `xmm0`–`xmm13` |
| Values live across a call | `rbx`, `r12`–`r15` (`rbp` too in a function without a frame pointer) | none: the psABI has no callee-saved `xmm` |
| Frame | `rbp` (until X24), `rsp` | — |

`rax` is outside the allocator's pool although it is the result register. Being scratch
lets every two-operand pattern compute in `rax` without checking the operands for
aliasing, and the `cltq`/`cqto`/`div`/`setcc` idioms all want it. The allocator's
result hint then points to `rax` only as a *move* target. A `long double` never gets a
register: it lives in its 16-byte slot, and the x87 stack holds values only inside one
instruction's pattern (X14).

`make run` stays green after every X-step.

Phase 0 is done:
- `cpp -t x86_64` predefines clang's architecture macros for the triple (no
  `__CHAR_UNSIGNED__`).
- The `x86_64` descriptor leaves every struct result to the backend
  (`struct_return_max = SIZE_MAX`) and has no `va_class` yet (X17 supplies one).
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
  from debugcon. The book suite runs chapter 1, compared with clang. Until X21 the
  test programs use the riscv64 and LP64 headers.

## Phase 2 — instruction selection, book order

Naive and correct first: every TAC variable is in a frame slot, operands are loaded into
the scratch registers of the table above, and the result is stored back. One memory
operand per instruction is allowed from the start (`addl -8(%rbp), %eax`), since x86
makes it free. Each step is done when its book chapters pass and a few golden tests pin
the selected instructions.

- **X8. Frame.** Slots come from typed TAC and `ALLOCATE_LOCAL`, with natural alignment
  and 16 for a `long double`.
  - The prologue is `push %rbp; mov %rsp, %rbp; sub $N, %rsp`, keeping `rsp` 16-aligned at
    every call. Callee-saved registers are pushed between, with the pad accounting for
    their count. The epilogue is `leave; ret`, with pops before the `leave`.
  - Displacements are any 32-bit value, so there is no range splitting. Immediates are
    32 bits sign-extended. A wider constant is `movabs $imm, %r11` and then used from
    the register.
- **X9. Integer ops** (ch. 2–4, 11, 12).
  - Arithmetic and logic in the type's width. A 32-bit `l` operation zeroes the upper
    half, so `unsigned int` → `unsigned long` is a plain `movl`.
  - `neg` and `not`.
  - `imul` in its two- and three-operand forms.
  - Signed divide: `cltd`/`cqto` + `idiv`. Unsigned: `xor %edx, %edx` + `div`. Quotient in
    `rax`, remainder in `rdx`. `INT_MIN / -1` traps (`#DE`), which C leaves undefined and
    clang's code does too.
  - Shifts by an immediate, or by `%cl`, with `sar`/`shr` chosen by signedness.
  - Comparisons: `cmp` + `setcc` + `movzbl`, with `l`/`le`/`g`/`ge` signed and
    `b`/`be`/`a`/`ae` unsigned.
  - Width conversions: `movsbl`/`movswl`/`movslq` and `movzbl`/`movzwl`.

  **Fixed registers.** `rdx` and `rcx` are argument registers, and a divide or variable
  shift clobbers them. In the naive phase everything is in memory, so this costs
  nothing. For X23, the selection's contract is that these instructions are reported to
  the allocator as clobbering.
- **X10. Control flow** (ch. 5–8): `.L` labels unique per TU, `jmp`, and `cmp`/`test` +
  `jcc`. A test of zero is `test %reg, %reg`.
- **X11. Calls, scalar ABI** (ch. 9).
  - Arguments in `rdi`–`r9` and `xmm0`–`xmm7`. The rest go in 8-byte stack slots stored
    at `0(%rsp)`, `8(%rsp)`… of an outgoing area reserved in the frame: no pushes, so
    `rsp` stays fixed and 16-aligned.
  - `call sym` for direct calls, `call *%r11` for indirect ones.
  - `%al` set to the number of `xmm` arguments before every call to a variadic or
    unprototyped callee, direct or through a pointer.
  - Narrow arguments and results extended by the sender and re-extended by the receiver.
  - `FUN_CALL_NORETURN`.
  - Parallel moves into the argument registers, ordered so no source is clobbered before
    it is read.
- **X12. Globals and static data** (ch. 10).
  - `.data`, `.bss` and `.rodata`, with every `Tac_StaticInit` kind. A `long double` is
    `.quad` significand + `.short` sign/exponent + `.zero 6`, from `f128_to_x87`.
  - Addresses are `lea sym(%rip)`, and accesses use `sym(%rip)` directly.
  - Static locals' `name$N` are spelled legally for both assemblers (`$` begins an
    immediate in AT&T syntax; check what AArch64 does).
- **X13. Floating point, SSE** (ch. 13).
  - `addsd`/`subsd`/`mulsd`/`divsd` and their `ss` forms. Negation is `xorpd` with a
    sign-mask constant.
  - `cvtss2sd`/`cvtsd2ss`. `cvtsi2sd`/`cvttsd2si` in `l` and `q` widths for the signed
    conversions.
  - `unsigned int` goes through a zero-extended 64-bit convert.
  - `unsigned long` → FP: when the top bit is set, halve with the low bit kept
    (`shr`/`or`), convert and double. FP → `unsigned long`: subtract 2^63 when ≥ 2^63,
    convert and flip the top bit.
  - **Comparisons** with `ucomisd`, all NaN-correct:
    - `<` and `<=` swap the operands and use `a`/`ae`, which are false on unordered
      without a parity test.
    - `==` is `sete` + `setnp` + `and`; `!=` is `setne` + `setp` + `or`.
    - Branches use `jp`.
  - Constants: zero by `xorps`, others from `.rodata` through `sym(%rip)`. A truth test
    of a double compares with zero, NaN being true.
- **X14. `long double`, x87** (ch. 13 again, and the book's `long double` programs).
  - Every `long double` value lives in its 16-byte slot. A pattern loads with `fldt`,
    computes (`faddp`/`fsubp`/`fmulp`/`fdivp`/`fchs`) and stores with `fstpt`. The x87
    stack is empty between TAC instructions and never deeper than two inside one.
  - Constants come from `fldz`/`fld1` or a 10-byte `.rodata` literal.
  - Integer → `long double`: `fild`. For unsigned 64-bit, add a 2^64 constant when the
    source is negative.
  - `long double` → integer: switch the control word to truncation around the store
    (`fnstcw`, `or $0xc00`, `fldcw`, `fistp`, restore), because baseline x86-64 has no
    `fisttp`. Unsigned 64-bit takes the 2^63 bias.
  - `float`/`double` ↔ `long double` through memory (`flds`/`fldl`, `fstps`/`fstpl`).
  - Comparisons: `fucomip` + `fstp %st(0)`, with the same flag and parity rules as X13.
- **X15. Pointers, arrays, chars, strings** (ch. 14–16).
  - Loads by width and signedness (`movsbl`/`movzbl`/`movswl`/`movzwl`), and stores from
    the register at the store's width.
  - `ADD_PTR` as `lea (base, index, scale)` for scale 1/2/4/8, else `imul` and `add`.
  - The byte-pointer TAC kinds as plain operations, as on RISC-V.
- **X16. Structs** (ch. 17–18). Member access via `COPY_*_OFFSET`.
  - Whole-aggregate copies by 8/4/2/1-byte moves through `r11`, and a loop past a few
    eightbytes. `rep movsb` waits for X25, since it needs `rdi`/`rsi`/`rcx`.
  - For now, struct arguments are passed whole on the stack, and every struct result goes
    through the hidden pointer in `rdi`, returned in `rax`.

## Phase 3 — ABI conformance

- **X17. Full aggregate classification.** One function, `tac_sysv64_class`, goes in
  `tac/tac_abi.c` beside `tac_aapcs64_class` and `tac_aapcs32_class`.
  - It classifies each eightbyte by the psABI merge rules into INTEGER, SSE, X87/X87UP or
    MEMORY, and returns a code the backend and `__builtin_va_class` (now the descriptor's
    `va_class`) share.
  - It drives:
    - two-eightbyte structs in up to two registers of either class, mixed included;
    - the all-or-nothing rule, under which a struct that does not fit goes wholly on the
      stack and later arguments still take registers;
    - MEMORY structs copied into the outgoing argument area;
    - `long double` arguments on the stack, 16-aligned.
  - Results: `rax`/`rdx`, `xmm0`/`xmm1`, or mixed in eightbyte order. MEMORY results go
    through `rdi` and come back in `rax`. A `long double` is returned in `st(0)`, which
    the callee loads with `fldt` and the caller stores with `fstpt`.
- **X18. Variadic functions and `<stdarg.h>`.**
  - **Calls.** Set `%al` and pass every argument by the ordinary rules: unlike ARM32,
    SysV variadics change nothing but `%al`.
  - **The variadic function's prologue** stores `rdi`–`r9` into the register save area,
    and `xmm0`–`xmm7` behind `test %al, %al; je`.
  - **`va_list`.** `typedef struct __va_list_tag {…} va_list[1];`, clang's type, so a
    `va_list` handed to clang's `vprintf` (or from it) is the pointer both sides expect.
    The macros therefore take `ap`, not `&ap` as on AArch64. `va_copy(d, s)` is
    `*(d) = *(s)`.
  - **`va_start(ap, last)`** is `__va_start(ap)`, intercepted by the backend as
    AArch64's `gen_va_start`. It sets `gp_offset` = 8 × named GP registers, `fp_offset` =
    48 + 16 × named SSE registers, `overflow_arg_area` = the first unnamed stack
    argument, and `reg_save_area`.
  - **`va_arg`** goes through the runtime `__va_arg(ap, size, align, cls, tmp)`, with
    `cls` from X17. A struct mixing INTEGER and SSE eightbytes is assembled in `tmp` from
    both halves of the save area. MEMORY and `long double` come from the overflow area,
    `long double` 16-aligned.
  - Audit the frontend for the array-typed `va_list`: a parameter of that type adjusts
    to a pointer, and `va_list` inside a struct stays an array. No earlier target had an
    array-typed `va_list`.
- **X19. Interop tests** with clang in both directions, over a table of signatures, built
  before the code they test:
  - more than six integer and more than eight FP arguments, interleaved;
  - the all-or-nothing rule (a two-eightbyte struct after five integer arguments);
  - `{long, double}`, `{double, long}`, `{float, float, int}`, `{char[3]}`, 16- and
    17-byte structs, a struct holding a `long double`;
  - `long double` arguments and results;
  - narrow ints in both directions;
  - results in `rax`:`rdx`, `xmm0`:`xmm1` and mixed;
  - variadics both ways, with `double`, `long double` and structs through `va_arg`;
  - a `va_list` handed across.
- **X20. Differential book tests.** Every book program compiled by clang too, run under
  qemu, and the outputs compared — the comparison the other LLVM-toolchain suites make.

## Phase 4 — library and headers

- **X21. Headers.** `libc/x86/include/`:
  - `float.h`, with `LDBL_*` of the x87 format: `LDBL_MANT_DIG` 64, `LDBL_MAX_EXP` 16384,
    `LDBL_EPSILON` 2^-63, and so on;
  - `limits.h` with a signed `CHAR_MIN`/`CHAR_MAX`. `libc/lp64/include/limits.h`
    hard-codes an unsigned `char`, and `__CHAR_UNSIGNED__` cannot select between the two
    under the system preprocessor the build uses;
  - `stddef.h` and `stdint.h` (`wchar_t` is `int`; compare with riscv64's, which may be
    shareable);
  - `setjmp.h` (`rbx`, `rbp`, `r12`–`r15`, `rsp`, the return address, plus `MXCSR` and
    the x87 control word);
  - X18's `stdarg.h`.

  `inttypes.h` and `math.h` come from `libc/lp64/include/`, the rest from
  `libc/common/include/`. Add an `x86_64-headers` CTest and its `-cpp` twin. Check our
  headers' type sizes and limits against clang's own for the triple, as ARM32 did.
- **X22. Libc run tests.** Port the AArch64 `printf_tests`/`str_tests`/`mem_tests`/
  `math_tests`, with host libc output as the expectation. `printf("%Lf")` exercises the
  x87 `long double` through `va_arg`, and the string tests exercise signed `char`.

## Phase 5 — code quality

- **X23. Register allocation** on `backend/common/regalloc.c`, with the pools of the
  register table.
  - **Integer pool:** the six argument registers, then `rbx` and `r12`–`r15`.
  - **SSE pool:** `xmm0`–`xmm13`, all of them "argument" registers with nothing
    callee-saved, so an FP value live across a call stays in its slot. Check that the
    allocator handles an empty callee-saved part; add a regalloc test.
  - `long double` is `REGALLOC_NONE`.
  - **Fixed registers.** A divide, remainder or variable shift is reported through the
    `runtime_call` hook, so no value lives in `rdx`/`rcx` across it. If the ch. 20
    programs show that costing much, add a narrower clobber-set hook to `regalloc.h` and
    record it.
  - **Two-operand form.** Selection computes `d = a op b` in place when `d` is `a`'s
    register (coalescing makes that common). It goes through `rax` when `d` is `b`'s
    register.
  - Callee-saved registers are pushed and popped around the frame.

  The ch. 20 tests pass.
- **X24. Leaf functions and rsp-addressed frames.**
  - Without `--frame-pointer`, slots are addressed from `rsp` and `rbp` joins the pool,
    as on AArch64.
  - A leaf function keeps its slots in the **red zone**, the 128 bytes below `rsp`, with
    no `sub`. This is safe because the runtime never enables interrupts. Check that clang
    also assumes the red zone for this triple.
  - A leaf that needs no stack has no prologue at all.
- **X25. Peephole.**
  - **Folding:**
    - memory operands into ALU instructions (`addl 8(%rsp), %edi`), and immediates;
    - `lea` for `a + b`, `a + b*k` and `a + k` into a third register;
    - copies followed into their uses, and no reload of a value just stored.
  - **Short forms:**
    - `test` for zero and mask tests;
    - `xor %eax, %eax` for zero where the flags are dead;
    - `rep movsb`/`movsq` for large copies.
  - **Branches:**
    - compare-and-branch fusion (no `setcc`/`movzbl`/`test`);
    - branch over jump, and no jump to the next line.
  - **`cmov`** for short diamonds (`if (c) x = a; else x = b;`, `?:`, min/max) — the x86
    counterpart of ARM32's conditional execution. It is register or memory source only,
    and from a memory source only where that load is always valid (a frame slot), since
    `cmov` loads unconditionally.

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

- **Fixed registers against the allocator.** `div`/`idiv`, variable shifts and `%al`
  tie instructions to registers that are also argument registers. Mitigation: three
  scratch registers outside the pool, the `runtime_call` report from X9, and golden
  tests of every fixed-register pattern with operands already in `rdx`/`rcx`.
- **Two-operand forms** invite the "destination is the second source" bug (`d = a - d`).
  Mitigation: one helper in selection owns the rule, and a test per operator pins it
  with the operands in every aliasing arrangement.
- **x87 `long double`.** Folded constants against computed values (the double rounding of a constant), the
  control-word dance for truncation, and the x87 stack discipline at calls (empty, except
  `st(0)` for a result). An x87 stack left unbalanced fails far from its cause, as a NaN
  many calls later. Mitigation: values never stay on the x87 stack across TAC
  instructions, clang as the oracle in X19/X20, and a crt0 test that checks the stack is
  empty after `main`.
- **SysV classification edge cases:** all-or-nothing, mixed eightbytes, MEMORY by value
  on the stack, `long double` in a struct, the returned address in `rax`. Mitigation:
  X19's table, written before the code it tests.
- **No callee-saved `xmm`.** FP-heavy code with calls spills around every call. That is
  the ABI, and clang pays it too; accept it, and keep the regalloc test from X23.
- **Signed plain `char`** is new among our byte-addressed targets, and libc sources or
  tests may quietly assume `char` ≥ 0. Mitigation: the Phase 0 audit found none in libc; the X22 string tests.
- **crt0.** A 32→64-bit transition that goes wrong hangs or triple-faults silently (qemu
  resets). Mitigation: the sequence verified above, the IDT reporting handler installed
  before anything can fault in 64-bit mode, the run timeout, and the runtime tests in `run_tests.cpp`.
- **No native x86 on this host** (arm64, no Rosetta). Everything runs under qemu TCG.
  The 0.04 s `microvm` boot keeps that cheap; nothing here depends on native execution.
