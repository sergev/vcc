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
- **RISC-V is done** ([Riscv_Backend.md](Riscv_Backend.md#alloca)), RV64 and RV32, the
  same way (`rv_stack_builtin`, the frame from s0).
- **ARM32 is done** ([Arm32_Backend.md](Arm32_Backend.md#alloca)): the body generated
  with r11 from the start, and 4 bytes more below the saves when r10 is one of them.
- **AVR is done** ([Avr_Backend.md](Avr_Backend.md#alloca)): SP written through Z with
  interrupts held off, the memory at SP + 1, and the epilogue's SP put back below the
  registers pushed under Y before it pops them.
- **MSP430 is done** ([Msp430_Backend.md](Msp430_Backend.md#alloca)): `--frame-pointer`
  (r4; `VCC_MSP430_FRAME_POINTER` runs the whole MSP430 suite so) and alloca on it.
- **MMIX is done** ([Mmix_Backend.md](Mmix_Backend.md#alloca)): `--frame-pointer` (`$253`,
  saved in a slot of the frame; `VCC_MMIX_FRAME_POINTER` runs the whole MMIX suite so)
  and alloca on it. The interop `regcheck` harness now checks `$253` too.
- **wasm32 already has the machinery.** `Target.stack_alloca` makes `co_alloca` call
  `__builtin_stack_save`, `__builtin_alloca` and `__builtin_stack_restore`
  (`stack_builtin()`, `translator/coro.c:527`). `backend/wasm/call.c:28-75` expands them
  inline, and a function using them gets a frame (`backend/wasm/frame.c:251`).
- **No target is left on the arena** (`libc/common/costack.c`): every coroutine target
  sets `stack_alloca`. The translator's arena branch is dead code until A11 removes it.
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
  with the run suite's `AllocaArenaOverflow`, now skipped everywhere (the translator's
  `AllocaOnArena` went with A8).
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

- A10 is optional.
- The risks:
  - peephole passes that model sp as constant;
  - the outgoing-argument offset patched after layout;
  - restoring sp before popping the saved registers in the epilogue of every backend.

