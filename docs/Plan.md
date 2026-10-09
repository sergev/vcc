# Plan: `alloca()` on the machine stack

## Goal

Support `alloca(n)` from `<alloca.h>`. It returns `n` bytes, aligned for any object, that live
until the calling function returns. In the end every target allocates them on its own machine
stack, as GCC and clang do. That is what ported programs expect: no size limit beyond the
stack, release on `longjmp`, and no runtime call.

## Where we start

- **The front end is done.** `<alloca.h>` defines `alloca(n)` as `__builtin_alloca(n)`;
  `semantic/` rejects it as a value, in a coroutine and on BESM-6. `gen_alloca`
  (`translator/coro.c`) lowers a call: on a `stack_alloca` target to `__builtin_alloca(n)`
  with the raw size, elsewhere to `__coro_alloca` on the arena, the mark saved at entry and
  restored before every `RETURN` by `gen_alloca_release`. The shared run suite
  `backend/common/test/alloca/alloca_run_tests.cpp` runs in every backend's test binary.
  Its `AllocaLongjmp` case runs where alloca is on the stack and the runtime has
  `setjmp`: not wasm32, nor the bare-metal AArch64, ARM32 and RISC-V runtimes (the list
  in the test; adding a `setjmp.s` to one of those turns it on).
- **x86-64 is done** ([X86_64_Backend.md](X86_64_Backend.md#alloca)), the hosted
  `x86_64-linux` with it. Its pieces are the model for the other backends:
  - `x86_stack_builtin` names the three builtins; `gen_call` expands them through rax.
  - A function calling one sets `Gen.moves_sp`, which forces the rbp frame
    (`Gen.frame_pointer`, also read by the register allocator and `layout_frame`).
  - `reserve_outgoing` takes the outgoing area from every call before selection, so
    `alloca` returns `rsp` plus that area rounded to 16 as a constant; `gen_call` stops
    with an error should a call need more. The frame reserves the rounded area apart from
    the slots.
  - The shared register allocator's optional `inline_call` hook
    (`backend/common/regalloc.h`) keeps the builtins from counting as calls.
- **AArch64 is done** ([Aarch64_Backend.md](Aarch64_Backend.md#alloca)), with
  `aarch64-linux` and `aarch64-darwin`, on the same pieces (`a64_stack_builtin`,
  `Gen.moves_sp` forcing the frame record, `reserve_outgoing`, the `inline_call` hook).
- **wasm32 already has the machinery.** `Target.stack_alloca` makes `co_alloca` call
  `__builtin_stack_save`, `__builtin_alloca` and `__builtin_stack_restore`
  (`stack_builtin()`, `translator/coro.c:527`). `backend/wasm/call.c:28-75` expands them
  inline, and a function using them gets a frame (`backend/wasm/frame.c:251`).
- **Every other target but BESM-6** runs `alloca` and `co_alloca` on the LIFO arena in
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

   On an arena target, until its backend learns the builtins, the translator saves the mark
   at function entry and restores it before every `RETURN` and at a reachable end.
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

Every backend task has the tests x86-64 and AArch64 have: goldens of the frame and the
selection (`AllocaLeaf`, `AllocaAboveOutgoing` in `frame_tests.cpp`); the shared run suite
and the coroutine run suite, now on the stack; an interop test with the reference
compiler both ways (`RunAllocaWithClang` in `interop_tests.cpp`); `AllocaOnStack` in
`translator/test/coro_tests.cpp` where the translator tests have a fixture for the
target; and the backend's doc gets an "alloca" section, the lists of targets on the stack
(`docs/Coroutines_*.md`, `costack.c`, `semantic/target.h`, `CLAUDE.md`) the target's name.

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
- `gen_stack_builtin` (`backend/wasm/call.c:53`) already rounds sp down to 16, so the raw
  size needs nothing more.
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
- Lift the semantic error for BESM-6, set `stack_alloca`, and add to `besm-tests` the run suite on
  the Unix (`b6sim`) path and goldens in all three dialects.

### A11. Retire the arena path

Once every target sets `stack_alloca`:
- Remove `gen_alloca_release` and the arena branch of `gen_alloca` from the translator,
  with the tests of the arena: the run suite's `AllocaArenaOverflow` and the translator's
  `AllocaOnArena`, on MMIX (should A8 come before the others, move it to a target still
  on the arena).
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

- A4–A6 are independent and of similar size.
- A7 and A8 are the largest, because of the new frame-pointer modes. Their "a" halves are
  worth doing separately, validated by the whole suite.
- A10 is optional.
- The risks:
  - peephole passes that model sp as constant;
  - the outgoing-argument offset patched after layout;
  - restoring sp before popping the saved registers in the epilogue of every backend.

