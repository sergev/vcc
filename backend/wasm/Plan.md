# Plan: a wasm32 backend (`genwasm`)

## Context

vcc has eight machine backends. WebAssembly would be the ninth, and the first for a
stack machine. Goal: a standalone wasm32 backend. C11 goes in, a linked `.wasm`
comes out, link-compatible with clang's `wasm32-unknown-unknown`, tested under node
the way the other backends are tested under qemu. Braam is out of scope, but nothing
here should get in its way later.

Decisions already made:
- **Braam's wasm features** (`cmake/wasm32-unknown-unknown.cmake` in braam-core):
  `-mreference-types -mbulk-memory -msign-ext -mmutable-globals -mnontrapping-fptoint`
  on clang's default CPU. One CMake variable, `WASM32_FEATURES`, holds that list.
  The assembler, cc and every clang reference build take it, so all objects carry
  the same `target_features` section and wasm-ld accepts the mix. clang's default
  CPU also contributes `multivalue`, `bulk-memory-opt` and `call-indirect-overlong`;
  they leave the C ABI as is.
  - What the backend uses:
    - `i32.extend8_s`/`extend16_s` and `i64.extend8/16/32_s` (sign-ext);
    - `*.trunc_sat_*` for float to int (nontrapping-fptoint);
    - `memory.copy 0, 0` and `memory.fill 0` (bulk memory);
    - `call_indirect __indirect_function_table, (sig)`, the reference-types
      spelling.
  - Nothing else: no `externref` in C, no multivalue returns.
- **Minimal custom host imports**: `env.putch(i32)` and `env.exit(i32)`, supplied by
  a ~30-line node runner `libc/wasm32/run.mjs`.
- **Output is LLVM's wasm assembly**, assembled by
  `clang --target=wasm32 $WASM32_FEATURES -c` and linked by `wasm-ld`. That gives relocatable objects, sections, `--gc-sections`
  and clang interop for free. The tools are installed: Homebrew clang 23, wasm-ld,
  node 26, wasm-validate, wasm-objdump.

## The ABI (as clang does it with those features; checked with `-S`)

- **Data model**: ILP32. Native i64 `long long`. f32/f64. `long double` is binary128
  (16/16, the `__addtf3`… helpers in `libc/common/float128.c`). **Plain `char` is
  signed.** `_Bool` is one byte. Little-endian. SysV bitfields.
- **Wasm value types**: anything 32 bits or narrower (integers, pointers) is `i32`;
  64-bit integers are `i64`; `float` is `f32`; `double` is `f64`. A narrow value is
  kept extended in its i32: `i32.extend8_s`/`extend16_s` for signed types, an `and`
  mask for unsigned ones.
- **Calls**: each scalar argument is one wasm parameter, and a scalar result is the
  wasm result.
  - Aggregates that travel as a scalar:
    - a struct holding exactly one scalar after flattening travels as that scalar
      (`struct{int}`, `struct{struct{double}}`, `struct{int a[1]}`);
    - an empty struct argument is dropped.
  - Aggregates that travel indirectly:
    - a union and every other aggregate is passed as an `i32` pointer to a
      **caller-made copy**, which the callee may modify;
    - such a result goes through a hidden sret `i32` first parameter, and the
      function then returns nothing.
  - `long double`:
    - an argument (or `struct{long double}`) is two `i64` parameters, low half
      first;
    - a result goes through sret.
- **Variadics**: the function type is the named parameters plus one trailing `i32`.
  - The caller fills a buffer on its shadow stack:
    - each slot is at least 4 bytes, aligned to its type (double 8, long double 16);
    - an aggregate takes a 4-byte slot holding a pointer to a copy.
  - The callee gets the buffer's address. `va_list` is a `char *`.
  - `va_start` uses mmix's hook: a `void __va_start(va_list *)` prototype plus a macro
    in `stdarg.h`, expanded inline by the backend (see `backend/mmix/call.c`,
    `libc/mmix/include/stdarg.h`). No `.functype` is emitted for it. `va_arg` is a
    pointer walk in a macro.
- **Shadow stack**: wasm-ld's global `__stack_pointer` (i32, grows down, frames
  16-aligned, `--stack-first`).
- **Function pointers** are table indices (`i32.const f`, and `.int32 f` in data,
  both table relocations). Indirect calls are
  `call_indirect __indirect_function_table, (params) -> (results)`, with
  `.tabletype __indirect_function_table, funcref` declared.
- **Float to int**: `i32/i64.trunc_sat_f32/f64_s/u`, as clang emits it. No trap,
  no guard.
- **`main`**: named exactly as clang names it, or it won't link or will trap on a
  signature mismatch.
  - `main(void)` is emitted as `__original_main`, with a `main(i32,i32)` wrapper and a
    `__main_void` alias.
  - `main(argc, argv)` is emitted as `__main_argc_argv`, with no `main` at all.
  - crt0 calls `__main_void`. libc has a weak `__main_void` that calls
    `__main_argc_argv(0, 0)`.

## Shared-code changes (small)

| Where | Change |
|---|---|
| `semantic/target.c` | A `wasm32` entry **appended** to `targets[]`, so `DEFAULT_TARGET_INDEX` is undisturbed. Every positional field is spelled out. Values: `char_signed=1`, `aggregate_align=1`, `struct_return_max=SIZE_MAX` (the backend classifies), `struct_args_split=0`, `va_class=NULL`, `ldouble_mant_dig=0` (binary128, as riscv32), `hw_sqrt=1` (`f64.sqrt`), `double_mant_dig=0`, `no_loop_opt=0`, little-endian, `BITFIELD_SYSV` (check unit rules against clang in the interop test). |
| `cpp/cpp.c` | Target `wasm32`: `__wasm__ __wasm32__ __wasm __wasm32 __ILP32__ _ILP32`. Test in `cpp/test/test_predefined_macros.cpp`. |
| `translator/test/wasm32_tests.cpp` | Sizes, layouts and bitfields (as `mmix_tests.cpp`), registered in `translator/CMakeLists.txt`. |
| `scripts/CrossTools.cmake` | `vcc_find_cross` gets a one-value `LD` argument (default `ld.lld`) and an `LLVM_ONLY` option. `find_program` locates `wasm-ld`. With `LLVM_ONLY` there is no GNU prefix search. |
| `cc/cc.c` | `is_llvm()` only recognises names containing "lld" (cc.c:578), and the `push_tool` fallback picks `ld.lld` (cc.c:645). Add a per-target LLVM linker name and the target's assembler flags (the `WASM32_FEATURES` list). Use `no_script`. Add the `wasm32` table entry and `WASM32_AS/LD` macros. |

## Backend: `backend/wasm/` (structured like `backend/mmix/`, driven by `backend/common/driver.c`)

| File | Role |
|---|---|
| `main.c` | `genwasm`. Flags `--no-structure` (dispatch loop only), `--no-stackify`, `--no-peephole`. `output_ext` is ".s". |
| `wasm_ir.c/.h` | `Wasm_Func` holds a flat list of `Wasm_Instr {op, valtype, imm (i64 / f64 / symbol+offset / depth), memarg {offset, align}}`. `block`/`loop`/`if`/`else`/`end` are instructions in the list. |
| `internal.h` | The `Gen` state and prototypes. |
| `frame.c` | Decides where each frame-resident TAC name lives. **wasm local**: a scalar not in `Flow.in_memory`. **Shadow-frame slot**: a name in `in_memory` (address-taken, `ALLOCATE_LOCAL`, volatile), **any aggregate-typed name by its type** (struct call results are temporaries with no `ALLOCATE_LOCAL`, see expr.c:1824), long double values, by-value argument copies, the variadic buffer. Lays out the frame: alignment clamped at 16, the way arm32/frame.c:166 clamps at 8. Prologue `global.get __stack_pointer; i32.const N; i32.sub; local.tee fp; global.set`; the epilogue goes on every return. No frame when there are no slots. |
| `structure.c` | CFG to `block`/`loop`/`if`, described below. Builds predecessors, RPO and dominators itself; `flow.c` gives only blocks, `succ[2]` and liveness. |
| `instr.c` | Selection: push operands (`local.get`, `*.const`, slot load), do the op, pop to the destination (`local.set` or a store). The operand value type always comes from the operator or destination type, never from a constant's kind. Mixed i32/i64 shift counts get a `wrap` or `extend` (see riscv/instr.c:741). Width conversions. LOAD/STORE use `load8_s`/`load16_u`/… by type. The byte and "fat pointer" kinds, which every target sees (`LOAD_BYTE`, `STORE_BYTE`, `GET_ADDRESS_BYTE`/`_DECAY`, `COPY_BYTE_*_OFFSET`, `PTR_TO_CHAR_PTR`, `PTR_DIFF`), map to their plain forms as in arm32/instr.c:637-669; `PTR_DIFF` becomes `i32.sub`. ADD_PTR, `COPY_*_OFFSET`, GET_ADDRESS (a global is `i32.const sym`, a slot is `fp + off`, a function is its table index). |
| `fp.c` | f32/f64 arithmetic, comparisons, and conversions with `trunc_sat`, `convert_*` and `promote`/`demote`. Long double operations become helper calls (dispatch on the operand type, not the operator; the helper table and names come from riscv/instr.c:654-880), passing (i64,i64) pairs and an sret slot. A comparison result is tested against 0. |
| `call.c` | Direct `call f` and `call_indirect`. Argument classification: scalar, single-scalar struct, by-copy pointer, long double pair, sret, empty (dropped). Builds the variadic buffer and expands `__va_start`. Aggregate copies are inline loads/stores below a threshold, `memory.copy 0, 0` above; zero fill uses `memory.fill 0`. A `FUN_CALL_NORETURN` is followed by `unreachable`. |
| `data.c` | Static data in `.data.N`, `.rodata.N` and `.bss.N` sections (`.skip` for bss). `.int8` through `.int64`, `.ascii`, and relocated `.int32 sym+off`. `.type` (`@object`/`@function`) and `.size` on every symbol. Superseded tentative definitions are skipped (`tac_static_superseded`). The strictest `_Alignas` is honoured (`declared_alignment`). Static locals are emitted per function as in arm32/codegen.c:109; their `x$1` names assemble fine. |
| `emit.c` | On the first codegen call (`tl == program`; the driver passes the whole chain each time, driver.c:193-209) it emits **every `.functype` up front**: defined and static functions plus all `EXTERN` toplevels, which already cover names used only by address (translate.c:1304-1322). llvm-mc rejects a symbol used before its `.functype`. Also emits `.globaltype __stack_pointer, i32`, `.section .text.f,"",@`, `.globl`/`.hidden`, `.local`, and `end_function`, with an `unreachable` first whenever the last block can fall through. Non-void functions can reach the end after an infinite loop (declarations.c:1092), and a void function may fall off the end. |
| `peephole.c` | Rewrites: `local.set N; local.get N` becomes `local.tee N`. **Stackify**: a local used once, immediately, stays on the operand stack. It must never move a load across a store, a call or a volatile access, nor reorder volatile accesses (tac.h:209). A `tee` of a dead local is dropped. `i32.const 0; i32.eq` becomes `i32.eqz`. Paired `eqz` and `eqz; br_if` are folded. A constant `add` is folded into the memarg offset. Dead code after `br`/`return`/`unreachable` is removed up to the next `end`. |
| `locals.c` | Local coalescing: interference from `flow.c` liveness, greedy colouring per value type. |
| `wasm.md` | A short reference for the instruction set and assembly syntax (as `mmix.md`). |

`backend/CMakeLists.txt`: `add_subdirectory(wasm)`, `add_executable(genwasm wasm/main.c)`,
installed as `vgenwasm`.

### Structured control flow (`structure.c`)

TAC has only labels, `JUMP` and `JUMP_IF_[NOT_]ZERO`. There are no computed jumps
and no jump tables: `switch` lowers to a compare chain (translator/stmt.c:557).
Every node has at most two successors. `flow.c` keeps unreachable blocks, so
structure.c drops whatever isn't reachable from the entry.

1. **Stage 1, a dispatch loop**, correct for any CFG. A `state` local, and
   `loop { block … block; br_table }` with one block per basic block. A jump is
   `local.set state; br loop`, except a fallthrough, which runs straight into the
   next block. It gets every test running early and remains the fallback.
2. **Stage 2, Ramsey's "Beyond Relooper"** (JFP 2022), for reducible CFGs:
   - reverse postorder, then a Cooper–Harvey–Kennedy dominator tree;
   - a back-edge target opens a `loop`;
   - a merge node gets a `block` around its dominator's earlier children;
   - a two-way branch whose arms it dominates becomes `if`/`else`;
   - branch depths come from a context stack.

   A retreating edge to a non-dominator marks the CFG irreducible (Duff's device,
   `goto` into a loop); those functions use stage 1.

## Runtime: `libc/wasm32/`

- **`crt0.s`**: `_start` (wasm-ld's default entry) calls `__wasm_call_ctors`
  unconditionally (wasm-ld synthesises it), then `__main_void`, then `exit`. With
  `-DPRINT_STATUS` (`crt0-status.o`, for the book suite) it prints the result as
  `%d\n` first. Built with `vcc_assemble_crt0`.
- **`console.s`**:
  - `putbyte`/`putch` call the imported `env.putch`, declared with
    `.import_module`/`.import_name`;
  - `flush` does nothing;
  - `exit` calls `env.exit`, then `unreachable`.
- **`memory.s`**: `__grow(pages)` wraps `memory.grow`, plus `sqrt`/`sqrtf` as
  `f64.sqrt`/`f32.sqrt`. Also `memcpy`/`memmove` as `memory.copy` and `memset` as
  `memory.fill`. These replace the `libc/common` C versions in this archive. `malloc.c` is a bump allocator starting at `__heap_base`,
  compiled by us. `main.s` holds the weak `__main_void`.
- **Compiled C**: `LIBC_C_COMMON` (which already includes mem*/str*/putchar),
  `LIBC_C_IEEE` (doprnt, float128) and `frexp`/`ldexp`/`modf`, through a
  `wasm32_compile_libc_c` macro: `SystemCpp -E`, `parse`, `lower -t wasm32`,
  `genwasm`, assembler. Archived with `llvm-ar`. Not `int64*.c`, which is
  `__divdi3` and friends, native on wasm.
- **`setjmp`/`longjmp`**: impossible without wasm exception handling or stack
  switching, and Braam's feature set has neither. `setjmp.h` gets a note, and the tests are skipped.
- **Headers** in `libc/wasm32/include/`:
  - `limits.h` for signed `char`;
  - `float.h` for binary128 (riscv32's);
  - `stddef.h`/`stdint.h` (`wchar_t` is `int`, `size_t` is `unsigned long`, as clang);
  - `stdarg.h` with the `__va_start` hook;
  - `setjmp.h`;
  - `inttypes.h`/`math.h` come from `libc/ilp32/include`.

  `wasm32-headers` and `wasm32-headers-cpp` ctests run them through
  `scripts/check_headers.sh`. Everything installs to `share/vcc/wasm32/{lib,include}`.
- **`run.mjs`**: `node run.mjs prog.wasm`.
  - `env.putch` writes to buffered stdout.
  - `env.exit` throws a sentinel. On that clean exit the runner flushes, prints
    `[exit N]` on stderr, and exits with N.
  - A trap prints its message on stderr and exits 255 without the trailer, so it
    can't pass for `return 255`.
  - Installed to `share/vcc/wasm32/lib`.

## Driver: `cc/cc.c`

- Target `wasm32`: codegen `vgenwasm`; assembler
  `clang --target=wasm32 <WASM32_FEATURES> -c`.
- Linker `wasm-ld --stack-first --gc-sections -z stack-size=65536`, then `crt0.o`,
  the objects and `-lc`. No linker script.
- `cc/CMakeLists.txt`, `cc/test` (defines, dependencies, a `-v` echo check) and
  `cc/README.md` updated as for the other targets.

## Tests: `backend/wasm/test/` → `wasm32-tests` (same shape as `backend/arm32/test`)

- **`wasm_test.h`**: `WasmTest : QemuTest`.
  - `QemuConfig`: runner `{node, run.mjs}`, `image_option=""`, an empty
    `link_script` (no `-T`), `exit_report="[exit "`, and the assembler and linker
    from CMake via `cross_tools`. `QemuTest::Run`'s order (crt0, objects, `libc.a`)
    suits wasm-ld.
  - `SKIP_IF_NO_WASM32_TOOLS()`, `SKIP_IF_NO_WASM32_CLANG()`.
  - Helpers: `CompileToWasm`, `Code()`, `NaiveSelection()` (dispatch loop, no
    stackify, peephole or coalescing), `EXPECT_CODE`, `CompileAndRunWasm`,
    `CompileAndRunBook`, `ClangRunBook` (`-O0` plus the feature list), `CompileAndRunWithClang`.
  - Every linked test image goes through `wasm-validate`.
- **Golden files**: emit, codegen, frame, int, flow (if/else, loops,
  break/continue, switch, goto, Duff's device, the irreducible fallback), fp, ptr,
  data, call, struct, stdarg, peephole, locals. Until phase 7 the goldens use
  `NaiveSelection()`. Default-pipeline goldens come after phase 7, since phases 6–7
  rewrite every skeleton.
- **Run tests**: `printf`/`str`/`mem`/`math` ported from arm32 (ILP32 expectations),
  `float128_tests` from riscv32, `run_tests`.
- **`interop_tests.cpp`**, with clang both ways (`BothSides`):
  - a signature table and a variadic table;
  - single-scalar structs, unions, sret, empty structs, long double pairs, function
    pointers across the boundary;
  - `main(argc, argv)` compiled by clang on our crt0;
  - `HeadersAgreeWithClang`;
  - bitfields (`backend/common/test/bitfield_interop.h`).
- **Book suite**: `${BOOK_TEST_SOURCES}` (including `chapter18_tests5`, bitfields)
  plus `signed_char_tests.cpp`. `book_test.h` takes arm32's "expects a 64-bit long"
  skip list and the unsigned-char skips MMIX uses. Compared with `ClangRunBook`.
- **`fatal_error`** is defined once, in `emit_tests.cpp`.

## Phases (each ends green on `ctest -R wasm`, then a commit)

1. **Skeleton** (done): target entry, cpp macros, headers, CrossTools and cc changes,
   `run.mjs`, crt0/console, and a `genwasm` that compiles `return <const>`. Assemble,
   link, run; `cc -t wasm32` end to end. Book chapter 1.
2. **Integers, locals, calls, dispatch-loop control flow** (done; scalar static data
   came in here too, for chapter 10's file-scope variables): book chapters 2–12
   (operators, locals, `if`, blocks, loops, functions, file scope, long, unsigned).
3. **Memory** (done): shadow frame, globals and static data, static locals, pointers,
   arrays, chars and strings, LOAD/STORE, ADD_PTR, byte kinds. libc subset:
   mem/str/putchar plus malloc. Chapters 14–17. f32/f64 (arithmetic, comparisons,
   `trunc_sat` conversions) and the double libc (`fabs`, `fma`, `fmax`, `fmin`,
   `frexp`, `ldexp`, `modf`) came in here too, since chapters 14–17 use `double`;
   chapter 13 passes with them.  The str and mem run tests use printf: phase 5.
4. **Aggregates and the ABI**: struct/union offsets, the single-scalar rule,
   by-copy arguments, sret, variadics with `__va_start`, function pointers,
   bitfields. Chapter 18, stdarg tests, interop (integer side).
5. **Floating point and long double**: binary128 through the helpers. Full libc (doprnt, float128, sqrt). Chapter 13, printf,
   math and float128 tests, interop with doubles. **Whole book suite** (19–20
   included).
6. **Structured control flow** (Ramsey) for reducible CFGs, with the dispatch loop
   as fallback. Flow goldens, then the book suite again.
7. **Quality**: stackify, peephole, local coalescing, frameless leaves. Default
   goldens. Code size against `clang -O2` with the same features on the book programs.
8. **Docs**: `docs/Wasm_Backend.md`; `CLAUDE.md`, `README.md`,
   `docs/Technical_Reference.md`, `docs/Type_Sizes_Alignment.md`, `cc/README.md`,
   `cpp/README.md`.

## Verification

- `ctest --test-dir build -j8 -R wasm` after each step, output tee'd to a scratch
  file. Full `ctest -j8` after any change to shared code (target table, cpp,
  CrossTools, cc), and at the end, to confirm the other backends stay green and
  BESM-6 output is unchanged. `make self` still works.
- By hand:
  `build/cc/cc -t wasm32 hello.c -o hello.wasm && wasm-validate hello.wasm && node libc/wasm32/run.mjs hello.wasm; echo $?`
- `wasm-objdump -x` on one of our objects shows the same `target_features` list as
  a clang object built with `WASM32_FEATURES`. The linked module imports only
  `env.putch` and `env.exit`.
- The book suite and interop are compared with clang built with the same features.

## Known limits (documented, not solved)

- No `setjmp`/`longjmp` (would need wasm exception handling).
- Calling through a mismatched prototype traps (in `call_indirect`, or in
  wasm-ld's signature-mismatch stub), as with clang.
- Alignment above 16 for automatic variables is clamped.
