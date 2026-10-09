# Plan: `alloca()` on the machine stack

## Goal

Support `alloca(n)` from `<alloca.h>`. It returns `n` bytes, aligned for any object, that live
until the calling function returns. In the end every target allocates them on its own machine
stack, as GCC and clang do. That is what ported programs expect: no size limit beyond the
stack, release on `longjmp`, and no runtime call.

## Where we start

- There is no `alloca` today: no header, no builtin.
- **wasm32 already has the machinery.** `Target.stack_alloca` makes `co_alloca` call
  `__builtin_stack_save`, `__builtin_alloca` and `__builtin_stack_restore`
  (`stack_builtin()`, `translator/coro.c:527`). `backend/wasm/call.c:28-75` expands them
  inline, and a function using them gets a frame (`backend/wasm/frame.c:251`).
- **Every other target but BESM-6** runs `co_alloca` on the LIFO arena in
  `libc/common/costack.c`: 64 KiB, 1 KiB with a 16-bit `size_t`.
- **Every backend assumes the stack pointer is fixed after the prologue:**
  - The outgoing stack arguments are stored at `sp + off` (riscv, aarch64, arm32, x86,
    msp430, mmix). AVR and BESM-6 push them instead.
  - The epilogues add the frame size back to sp.
  - Several backends address slots from sp: the sp frame modes of riscv, aarch64, arm32 and
    x86, and always on msp430 and mmix.

## Design

1. **Spelling.** `<alloca.h>` declares `void *__builtin_alloca(size_t);` and defines
   `#define alloca(n) __builtin_alloca(n)`. The hosted headers (`libc/linux`, `libc/darwin`)
   add the same `<alloca.h>`. `__builtin_alloca` may only be called directly; taking its
   address is an error. It is an error inside a coroutine, whose frame outlives the stack.
2. **One TAC form.** `alloca(n)` lowers to `FUN_CALL __builtin_alloca(n)` with the raw size.
   The backend rounds `n` up to the target's stack alignment and returns an address aligned
   for `max_align_t`. `co_alloca` keeps rounding to 16 before the call, so on the native
   targets it uses the same builtin.
3. **Release by the epilogue.** On a `stack_alloca` target the epilogue restores sp from the
   frame pointer, so a plain `alloca` needs no save or restore in TAC.

   On an arena target, during the transition, the translator saves the mark at function entry
   and restores it before every `RETURN` and at a reachable end (`gen_alloca_release` in
   `translator/coro.c`, run on the finished body, after the defers of each exit).
4. **Backend contract.** A backend that sets `stack_alloca` supports all three builtins inline.
   They are not calls: no clobbers, and they do not end a leaf. A function containing any of
   them:
   - gets a frame-pointer frame and gives up frameless, red-zone, sp-addressed and tail-call
     forms;
   - computes `sp = (sp - round(n)) & -align`, then `dst = sp + outgoing`, where `outgoing` is
     the size of the reserved outgoing-argument area, patched at frame layout;
   - restores sp from the frame pointer before popping the saved registers.

   `__builtin_stack_save` gives `sp`, and `__builtin_stack_restore(p)` sets `sp = p`. The
   builtins never get an `.extern`.
5. **Interaction with `co_alloca`.** A `co_alloca` block exit restores sp. Any `alloca` made
   inside that block is therefore released there too, as GCC does with VLAs. Document it.

## Tasks

Each task leaves `ctest -j8` green on every target, and BESM-6 output unchanged except in A10.
Commit after each.

### A1. Front end and arena fallback (every target but BESM-6) — done

As built, it differs from the sketch below in two points:
- **No exit action.** The release is a pass over the lowered body, a restore before every
  `RETURN`, not a function-level `EXIT_STACK_RESTORE`. An action in the outermost block would
  upset the counts the `goto` checks keep per block (`semantic/defer.c`).
- **No flag on the symbol.** The translator notes the first call itself (`TacCtx.alloca_sp`).
  On wasm32 the raw size goes to `__builtin_alloca`, whose expansion already rounds sp down to
  16; A9 has nothing left to change there.

The `setjmp`/`longjmp` run case waits for A2, the first target where it can pass.

- **`libc/common/include/alloca.h`**, plus the hosted copies. `HostedHeadersAgreeWithSystem`
  covers the hosted ones.
- **`semantic/`:**
  - the checks of design §1;
  - a flag on the function symbol when its body calls `__builtin_alloca`;
  - an error on `Target.no_coroutines` targets (BESM-6) until A10.
- **`translator/`:**
  - Move `stack_builtin()` out of `coro.c` into a shared helper.
  - On arena targets, save at entry and add `EXIT_STACK_RESTORE`; `run_action` emits
    `__coro_stack_restore`. Round to 16 there, since `__coro_alloca` requires it.
  - On wasm32, emit the bare builtin.
- **`libc/common/costack.c`:** the overflow trap names `alloca`.
- **Tests:**
  - `semantic-tests` for the errors.
  - `translate-tests` for the TAC shape on both kinds of target: save at entry, restore on
    fall-off and after a `return` value, several returns.
  - The new shared run suite `backend/common/test/alloca/alloca_run_tests.cpp`, compiled into
    every backend test binary through its `CoroTest` fixture like `coro_run_tests.cpp`.
    Cases:
    - alloca in a loop, with the memory reused after return;
    - recursion;
    - the alignment of the result;
    - large and zero sizes;
    - alloca with `defer` and with an early `return`;
    - alloca beside outgoing stack arguments: a call with more than eight or nine arguments
      after the alloca, checking that neither overwrites the other;
    - alloca beside `co_alloca`;
    - `setjmp`/`longjmp` out of a function that alloca'd, run repeatedly, so a leak would
      overflow. This case is skipped on arena targets until they go native.
  - `cc-tests`: `#include <alloca.h>` through the driver.

### A2. x86-64, with the hosted `x86_64-linux`

- Intercept the builtins in `gen_call` (`backend/x86/call.c:423`, beside `__va_start`).
- Set a `has_alloca` flag. It forces `FRAME_RBP` (`frame.c:699-705`; no `FRAME_NONE`,
  `RED_ZONE` or `RSP`) and reserves rbp in `regalloc.c:87`.
- Selection: `sub rsp, n; and rsp, -16; lea dst, [rsp + outgoing]`. The `outgoing` constant is
  resolved in `resolve_frame` (`frame.c:606`).
- Epilogue: `lea rsp, [rbp - 8*nsaved]` before the pops (`frame.c:635`).
- Peephole runs on `X86_FRAME` before the frame is laid out. The rsp liveness already in
  `peephole.c:202` must see the adjustment as a def of rsp.
- Set `stack_alloca` for `x86_64` and `x86_64-linux`; the A1 arena path then turns off by
  itself.
- **Tests:**
  - goldens in `call_tests.cpp`/`frame_tests.cpp`;
  - the shared run suite on qemu and hosted;
  - interop: a clang caller of a function of ours that allocas, checking the callee-saved
    registers, and our caller of clang's `alloca` function;
  - the coroutine run suite again, now that `co_alloca` is native.

### A3. AArch64, with `aarch64-linux` and `aarch64-darwin`

- Intercept the builtins in `gen_call` (`backend/aarch64/call.c:430`).
- `has_alloca` forces `FRAME_FP` (`frame.c:982`) and disables `is_leaf` frameless
  (`frame.c:826`).
- Selection: sp cannot be an operand of `and`, so compute in x16 (or x17): `sub x16, sp, n;
  and x16, x16, -16; mov sp, x16; add dst, sp, #outgoing`.
- The epilogue's `mov sp, x29` already exists in FP mode (`frame.c:769`).
- Peephole: make sure the "value never read" deletion (`peephole.c:1996`) and `delete_reload`
  (`peephole.c:832`) treat a write to sp as a def.
- Darwin: x29 is already kept, and sp stays 16-aligned.
- **Tests:** as in A2, with `darwin_tests.cpp` goldens and the native runs on macOS.

### A4. RISC-V, RV64 and RV32

- Intercept the builtins in `gen_call` (`backend/riscv/call.c:658`).
- `has_alloca` forces `FRAME_FP` (`frame.c:879`; no `rebase_to_sp`) and disables `is_leaf`
  (`frame.c:823`).
- Selection: `sub t0, sp, n; andi t0, t0, -16; mv sp, t0; addi dst, sp, outgoing`.
- Epilogue: the FP path's `addi sp, s0, -header` (`frame.c:757`) is already right.
- **Tests:** `riscv-tests` and `riscv32-tests`, with interop with clang both ways.

### A5. ARM32

- Intercept the builtins in `gen_call` (`backend/arm32/call.c:480`).
- `has_alloca` skips the sp-frame first attempt (`codegen.c:103`) and goes straight to r11.
  r11 is then not allocatable (`regalloc.c:105`).
- Alignment 8. Selection: `sub ip, sp, n; bic ip, ip, #7; mov sp, ip; add dst, sp,
  #outgoing`.
- Epilogue: the r11 path (`frame.c:956`) already derives sp from r11.
- Peephole: check the special handling of `A32_SP` (`peephole.c:130-161`, `:1448`, `:1500`)
  and the `ldrd` pairing.
- **Tests:** `arm32-tests`, with interop.

### A6. AVR

- Intercept the builtins in `gen_call` (`backend/avr/call.c:226`).
- `has_alloca` makes `needs_frame` true (`codegen.c:47`): Y is reserved and the frame is not
  frameless.
- Selection: read SPL/SPH, subtract `n`, write SP with interrupts held off (`write_sp`,
  `frame.c:815`). `dst = SP + 1`, since there is no outgoing area: arguments are pushed.
- Epilogue (`frame.c:875-884`):
  - Do not pop the frame with `pop r0`; use `adiw Y` + `write_sp`.
  - The saved registers lie below Y, so first set SP from Y minus their count, then pop.
- Classify the builtins at `instr.c:1443`.
- **Tests:** `avr-tests`, with interop with avr-gcc or clang.

### A7. MSP430: a frame-pointer mode, then alloca

- **A7a. Frame-pointer mode, without alloca.** Add `--frame-pointer` to `genmsp430`, with r4 as
  fp:
  - Drop r4 from both pools (`regalloc.c:14-15`).
  - Address slots and incoming arguments from r4: `slot_at`, `frame.c:369` and `:474`, and
    `complete_incoming` at `frame.c:754`.
  - Epilogue: `mov r4, r1` before the pops.

  The `sp_bias` around pushes then no longer applies to slots. Run the whole `msp430-tests`
  suite with the flag forced on (an environment switch in the fixture, as `VCC_OPT_MAX_ITER`
  is read) to validate the mode before anything depends on it.
- **A7b. alloca.**
  - Intercept in `gen_call` (`call.c:207`), the frame pre-scans (`call.c:455`,
    `instr.c:1179`) and `uses_scratch` / `regalloc.c:65`.
  - `has_alloca` forces the fp mode and a frame. Never drop the frame
    (`msp_frame_referenced`, `codegen.c:63`), and never a bare `ret` (`frame.c:756`).
  - Selection: `sub n, r1; bic #1, r1; mov r1, dst; add #out_size, dst`.
  - Peephole: the forward-facts `step` and `writes_memory` (`peephole.c:659-668`, `:888`) must
    forget slot facts on any write to r1. The dead-store pass already bails on `sp_moves`.
- **Tests:** `msp430-tests` with interop with `msp430-elf-gcc` both ways. Check GCC's own
  `alloca` code for the convention it uses.

### A8. MMIX: a frame-pointer mode, then alloca

- **A8a. Frame-pointer mode.** First read GCC's `mmix.c` to see how it uses `$253` for alloca,
  and follow it. `$253` is outside the allocator's pool (`regalloc.c:19`).
  - Address slots, the vararg area and incoming stack arguments from `$253`
    (`frame.c:407-465`).
  - Save the caller's `$253` (GCC's ABI makes it callee-saved).
  - Epilogue: `$254 = $253 - …` (`frame.c:911-963`).
  - Validate with the whole suite forced into the mode, as in A7a.
- **A8b. alloca.**
  - Intercept in `gen_call` (`call.c:294`, beside `is_va_start`); exclude the builtin from
    `makes_call` (`call.c:20`).
  - `has_alloca` forces the fp mode and a frame, blocks tail calls (`call.c:377`) and the
    in-place `pop` (`call.c:232`).
  - Alignment 8. Selection: `subu $254, $254, n; andn $254, $254, 7; addu dst, $254,
    outgoing`.
- **Tests:** `mmix-tests` with interop with `mmix-knuth-mmixware-gcc`.

### A9. wasm32: switch to the final contract

wasm already allocates on the shadow stack.
- Change `gen_stack_builtin` (`backend/wasm/call.c:53`) to round a raw `n` itself.
- Remove the A1 arena path for wasm32, if any remains.
- Confirm that `wasm32-braam` rejects alloca in coroutines, including `main`, which is one
  there.
- Note: `alloca` cannot be used in a Braam `main`, because `main` is a coroutine.

### A10. BESM-6 (optional, last)

- r15 grows upward, and `b/ret` restores it from r7. A plain function's epilogue therefore
  already releases the stack.
- Intercept `__builtin_alloca` in `codegen_intrinsic` (`backend/besm6/intrinsics.c:153`; widen
  its prefix gate).
- Selection:
  - `dst` = the word address in r15, made a fat `void *` (`offset_enc 5`, as
    `GET_ADDRESS_DECAY` does);
  - then `15 ,utm,` by `ceil(n/6)` words, through a register when `n` is not constant.
- Force the `b/save`/`b/ret` frame: never the `_Noreturn` or empty-function shortcuts
  (`codegen.c:203`, `:369`).
- Check that the peephole rules on `15 ,wtc,` / `xts` (`peephole.c:853-910`) do not move an
  `xts` across it.
- Lift the A1 error for BESM-6, set `stack_alloca`, and add to `besm-tests` the run suite on
  the Unix (`b6sim`) path and goldens in all three dialects.

### A11. Retire the arena path

Once every target sets `stack_alloca`:
- Remove `gen_alloca_release` and the arena branch of `gen_alloca` from the translator.
- Mark `co_alloca`'s arena use outside coroutines as gone in
  `docs/Coroutines_Internals.md`.
- Decide whether `costack.c`'s `__coro_stack_*` stay. They are still needed for coroutines'
  task arena only if `__coro_push`/`__coro_pop` use them.

### A12. Documentation

- A section "alloca" in each `docs/*_Backend.md`: the frame it forces, the selection and the
  epilogue.
- The semantics and the `co_alloca` interaction in `docs/Coroutines_in_C.md` (§ on
  `co_alloca`).
- `cc/README.md`.
- The phases table in `CLAUDE.md`.

## Order and risk

- A1 first: it puts the tests in place on every target.
- A2–A6 are independent and of similar size. A2 and A3 come first because they serve the
  hosted targets.
- A7 and A8 are the largest, because of the new frame-pointer modes. Their "a" halves are
  worth doing separately, validated by the whole suite.
- A10 is optional.
- The risks:
  - peephole passes that model sp as constant;
  - the outgoing-argument offset patched after layout;
  - restoring sp before popping the saved registers in the epilogue of every backend.

