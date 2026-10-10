# Plan: `alloca()` on the machine stack

## Goal

Support `alloca(n)` from `<alloca.h>`. It returns `n` bytes, aligned for any object, that live
until the calling function returns. In the end every target allocates them on its own machine
stack, as GCC and clang do. That is what ported programs expect: no size limit beyond the
stack, release on `longjmp`, and no runtime call.

## Where we start

- **The front end is done.** `<alloca.h>` defines `alloca(n)` as `__builtin_alloca(n)`;
  `semantic/` rejects it as a value and in a coroutine. `gen_alloca`
  (`translator/coro.c`) lowers a call to `__builtin_alloca(n)` with the raw size. The
  shared run suite
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
- **wasm32 is done** ([Wasm_Backend.md](Wasm_Backend.md#alloca)): it had the builtins
  for `co_alloca` already, on the shadow stack, rounding to 16 itself. On `wasm32-braam`
  `main` is a coroutine, where `alloca` is an error; a function it calls may use it
  (`BraamTest.AllocaInMain`, `AllocaInFunction`; [Braam.md](Braam.md) §4).
- **BESM-6 is done** ([Besm6_Calling_Conventions.md](../backend/besm6/Besm6_Calling_Conventions.md#alloca)):
  `__builtin_alloca` alone (no coroutines), expanded by `intrinsics.c`: r15 as a fat
  `void *`, then r15 raised by ceil(n/6) words for a constant size, n/6 + 1 through
  `b/udiv` for a computed one. `b/ret` gives it back; the prologue and epilogue are
  unchanged. Run on all three paths (b6sim, and dubna for Madlen and Bemsh); no
  `longjmp` case, the BESM-6 runtime having no `setjmp`. `<alloca.h>` is installed
  with the compiler-owned headers.
- **The arena is gone** (A11). Until every backend had the builtins, the others took
  the memory from `libc/common/costack.c`, a static LIFO arena, the translator saving
  its mark at a function's entry and restoring it at each return. The file, the
  translator's branch, its tests and `Target.stack_alloca`, which said which way a
  target went, are removed; every backend now expands the builtins.

## Design

1. **Spelling.** `<alloca.h>` declares `void *__builtin_alloca(size_t);` and defines
   `#define alloca(n) __builtin_alloca(n)`. The hosted headers (`libc/linux`, `libc/darwin`)
   add the same `<alloca.h>`. `__builtin_alloca` may only be called directly; taking its
   address is an error. It is an error inside a coroutine, whose frame outlives the stack.
2. **One TAC form.** `alloca(n)` lowers to `FUN_CALL __builtin_alloca(n)` with the raw size.
   The backend rounds `n` up to the target's stack alignment and returns an address aligned
   for `max_align_t`. `co_alloca` keeps rounding to 16 before the call, so on the native
   targets it uses the same builtin.
3. **Release by the epilogue.** The epilogue restores sp from the frame pointer, so a
   plain `alloca` needs no save or restore in TAC.
4. **Backend contract.** Every backend supports all three builtins inline.
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

Each task leaves `ctest -j8` green on every target, and BESM-6 output unchanged but for `alloca`, which A10 added.
Commit after each.

Every backend task has the tests x86-64 and AArch64 have: goldens of the frame and the
selection (`AllocaLeaf`, `AllocaAboveOutgoing` in `frame_tests.cpp`); the shared run suite
and the coroutine run suite, now on the stack; an interop test with the reference
compiler both ways (`RunAllocaWithClang` in `interop_tests.cpp`); `AllocaOnStack` in
`translator/test/coro_tests.cpp` where the translator tests have a fixture for the
target; and the backend's doc gets an "alloca" section, the lists of targets on the stack
(`docs/Coroutines_*.md`, `CLAUDE.md`) the target's name.

### A12. Documentation

- A section "alloca" in each `docs/*_Backend.md`: the frame it forces, the selection and the
  epilogue.
- The semantics and the `co_alloca` interaction in `docs/Coroutines_in_C.md` (§ on
  `co_alloca`).
- `cc/README.md`.
- The phases table in `CLAUDE.md`.

## Order and risk

- The risks:
  - peephole passes that model sp as constant;
  - the outgoing-argument offset patched after layout;
  - restoring sp before popping the saved registers in the epilogue of every backend.

