# Peephole Rewrites in the BESM-6 Backend

This article explains *peephole optimization* — what it is, why a code generator needs it,
and how it applies to the BESM-6 backend of this compiler and the Madlen assembly it emits.
It is written to be read top to bottom as a tutorial; every example is a real instruction
sequence the backend produces today (see [instr.c](instr.c) and
[emit.c](emit.c)), with the improvement it should become.

---

## 1. What a peephole optimization is

A **peephole optimization** is a local, pattern-based cleanup applied near the end of code
generation. The optimizer slides a small window — the *peephole* — over a short run of
consecutive instructions (typically two to five) in the generated code or a low-level
intermediate representation. If the window matches a known suboptimal pattern, the optimizer
**rewrites** it into a semantically equivalent but cheaper sequence: fewer instructions,
fewer memory references, or a form the target executes faster.

The technique is attractive precisely because it is *small*:

- **Local.** No whole-program analysis. The window slides; each step looks at a handful of
  instructions.
- **Target-specific.** The rules encode the quirks of one machine — here, the BESM-6's
  single accumulator, its mode register, and the Madlen instruction repertoire.
- **Complementary.** It mops up the mechanical, repetitive code that instruction selection
  emits. Selection is written to be *correct and simple*, one TAC instruction at a time;
  peephole makes the *seams between* those instructions cheap.
- **Cheap to run.** Rules are a table of *pattern → replacement*; the pass repeats until no
  rule fires (a *fixpoint*).

Classic textbook examples, machine-independent in spirit:

- **Redundant moves:** `MOV R1,R2; MOV R2,R1` → drop the second.
- **Strength reduction:** `MUL R,2` → `SHL R,1` (a shift is cheaper than a multiply).
- **Algebraic identities:** `ADD R,0` → delete; `AND R,-1` → delete.
- **Dead/unreachable code** inside the window: a no-op move, or instructions after an
  unconditional jump and before the next label.

Peephole rewriting sits at the **low-level end** of the optimization spectrum: after the
machine-independent optimizations (our `optimize/` TAC passes) and after instruction
selection, just before the final assembly is written out. It is the *final polish*.

---

## 2. Why this backend needs one

Our shared frontend lowers C to **TAC**, a three-address IR: every operation reads its
operands and writes its result to a *named* destination. The BESM-6 backend then selects
instructions one TAC node at a time. Because the BESM-6 has a **single accumulator (A)** and
most operations are *accumulator ⊕ memory*, the natural translation of one three-address
node is:

```
load  src1 into A
combine A with src2
store A into the destination's frame slot
```

The destination of one TAC node is almost always the source of the next. So the *store* that
ends one node and the *load* that begins the next read and write the **same frame slot back
to back** — the value is already sitting in A. Multiply this by every temporary the frontend
creates while flattening expressions and you get a generated program that spends a large
fraction of its instructions storing a value and immediately reloading it.

Three structural inefficiencies dominate the output:

1. **Store/reload churn** — `atx t` followed by `xta t` for the same slot (Section 5.1).
2. **Mode-register churn** — floating-point work is bracketed by `ntr 0 … ntr 7`, and
   adjacent FP operations leave a `ntr 7` butted against the next `ntr 0` (Section 5.3).
3. **Compare-then-reload-then-branch** — relational results are stored to a boolean
   temporary that the following conditional jump immediately reloads (Section 5.4).

None of these is visible to the machine-independent TAC optimizer: store/reload and `ntr`
brackets do not *exist* until instruction selection invents them. They are exactly the
"messy seams" a peephole pass is built to clean.

---

## 3. Where it fits in the pipeline

The full compiler pipeline is:

```
Source (.c)
  → [parse]   Scanner → Parser → AST
  → [lower]   Typecheck → Translate → Optimize → TAC      (machine-independent optimizer)
  → [genbesm] Frame alloc → Instruction select → Madlen   (BESM-6 backend)
```

The BESM-6 backend's own internal flow today is a straight line. `codegen_function`
([codegen.c](codegen.c)) builds one `Besm_Block` — a linked list of
`Besm_Instr` nodes — by calling `codegen_instr` ([instr.c](instr.c)) for
each TAC instruction, then hands that list directly to `emit_madlen_module`
([emit_madlen.c](emit_madlen.c)):

```
TAC → [ codegen_instr per TAC node ] → Besm_Instr list → emit_madlen_module → .mad text
```

The peephole pass slots in as a new stage **on the `Besm_Instr` list**, between construction
and emission:

```
TAC → [ codegen_instr ] → Besm_Instr list → [ besm_peephole ] → emit_madlen_module → .mad
```

This placement matters. The pass works on the backend IR — real BESM-6 instructions with
known accumulator and mode-register behavior — so it can reason about machine state the TAC
optimizer cannot see. Conversely, anything that *can* be done earlier (constant folding, dead
TAC stores, copy propagation) is already done by `optimize/` and should stay there; the
peephole pass is for the machine-level residue only.

---

## 4. The peephole on a BESM-6 instruction stream

To reason about a window of BESM-6 instructions, the pass tracks the small amount of
**implicit machine state** that makes a rewrite legal:

- **A — the accumulator.** Most instructions read and/or write A. Knowing "A currently holds
  the value last stored to location *L*" is what licenses dropping a reload of *L*. A
  *location* is a frame slot, a global, or a word reached through a pointer — see
  Section 5.9, which explains why the naive "slot (register, offset)" is not enough.
- **R — the mode register** (set by `ntr`, written `NTR n` in the comments here). `b/save`
  leaves **R = 7** (logical mode, normalization and rounding suppressed) so integer and
  bitwise instructions act on raw words. Floating-point instructions need **R = 0**
  (normalize + round), so FP code flips R to 0 and back. Knowing the current R lets the pass
  delete an `ntr` that re-establishes a value R already has.
- **ω — the branch condition.** Not a latched flag: the conditional branches `uza` (branch if
  ω = 0) and `u1a` (branch if ω ≠ 0) recompute ω at branch time from A and the **ω group** in
  R (bits 5–3), which every accumulator operation rewrites. Knowing the group is what
  licenses branching without a reload (5.4) — and, when it is not the logical one, what tells
  the pass to insert the one-instruction fixup that makes the branch mean "A = 0?" (5.11).

### Basic-block boundaries

Tracked state is only valid along **straight-line code**. Two kinds of instruction end a
window:

- **A label** (`BESM_STMT_LABEL`, emitted as `name: ,bss,`). Control can jump *into* a label
  from elsewhere, so on arrival A, R, and ω are unknown. The pass must not assume a value in
  A survives across a label.
- **A branch** (`uj`, `uza`, `u1a`, `call`, …). After a branch the next instruction may be a
  branch target, and a `call` runs a helper that clobbers A.
- **A supervisor instruction** (`ext`, `mod`, an extracode — the `<besm6.h>` intrinsics, see
  5.10 and 5.12). These are not branches, but they rewrite the state the pass tracks:
  `ext`/`mod` put the AU mode register R into logical mode on a read address, and an extracode
  runs the monitor's handler, which may do anything at all.

So the peephole pass treats the instruction list as a sequence of **basic blocks** delimited
by labels and branches, resets its tracked A/R/ω state at every boundary, and never rewrites
a pattern that straddles one. (Today the whole function body is a single `Besm_Block`, but it
contains many labels; the boundaries are the labels and branches *within* the list, not the
`Besm_Block` structure.)

### Madlen statement shape

Every example below is written in Madlen's three-field form

```
<label> : <index_reg> ,<mnemonic>, <address>
```

with the **two commas mandatory** (see [Madlen.md](Madlen.md)). The index register precedes
the first comma; the mnemonic sits between the commas; the address follows. In the generated
code, a frame slot is addressed as `<reg> ,<op>, <offset>` where the register is **r7** for
automatic locals (`REG_AUTO`) or **r6** for parameters (`REG_PAR`), and the offset is the
slot number. A bare `,xta,` / `,atx,` (no register, no address) operates through the C
register, used for module-level globals after a `,utc, name`.

---

## 5. Catalogue of rewrites

Each rule below shows the *before* sequence the backend emits today and the *after* it should
become. Slot numbers (`0`, `1`, …) stand for automatic-local offsets under r7; `t` marks a
compiler temporary.

### 5.1 Redundant reload elimination

The flagship rewrite. Consider `c = a + b;` with `a`, `b`, `c` at auto slots 0, 1, 2 and the
sum routed through temporary `t` at slot 3. Instruction selection emits the result store and
the consuming load independently:

```
   7 ,xta, 0      ; A = a
   7 ,a+x, 1      ; A = a + b
   7 ,atx, 3      ; t = A
   7 ,xta, 3      ; A = t     ← redundant: A already holds t
   7 ,atx, 2      ; c = A
```

`atx` stores A without disturbing it, so the reload `7 ,xta, 3` is pure waste. **Rule: an
`atx` to some location, followed by an `xta` of that same location with A undisturbed in
between ⇒ delete the `xta`.**

```
   7 ,xta, 0
   7 ,a+x, 1
   7 ,atx, 3
   7 ,atx, 2
```

This is the highest-frequency pattern in the whole backend, because *every* value-producing
TAC instruction ends with `emit_atx` and *every* consumer begins with `emit_xta_val`.

The rule is stated in terms of a **location**, not a `(register, offset)` pair, and the
difference matters. Only a frame slot is addressed by a register and an offset; a global is
reached through `,utc, g` and a pointer through `,wtc,`, and in both of those the following
`,xta,` carries the fields `(0, 0)` — an offset from the C register, naming nothing. Reading
those fields as a slot number is how a peephole miscompiles. Section 5.9 develops the
location model; the same rule then removes reloads of all three kinds:

```
   ,utc, g       ; g.x = 7                6 ,wtc,       ; *p = x
   ,atx,                                    ,atx,
   ,utc, g       ; ← redundant           6 ,wtc,        ; ← redundant
   ,xta,           (whole group)           ,xta,          (whole group)
```

Note the *whole group* goes, setter and consumer together. Deleting the `,xta,` alone would
leave the `,utc,` to load C for whatever instruction fell in behind it.

### 5.2 Dead temporary-store elimination

After 5.1 the store `7 ,atx, 3` remains, but `t` (slot 3) is never read again. If a
`%`-temporary is stored and not read before it is overwritten or the block ends, the store is
**dead**. **Rule: delete an `atx` to a temporary whose value is never subsequently read in the
block.** This needs a small backward last-use scan, but combined with 5.1 it erases the
store *and* reload of a single-use temporary entirely:

```
   7 ,xta, 0
   7 ,a+x, 1
   7 ,atx, 2      ; c = a + b, computed straight into c
```

Three instructions instead of the original five — and slot 3 need not be allocated at all.

### 5.3 NTR mode coalescing

Floating-point arithmetic must run with **R = 0** so the additive/multiplicative unit
normalizes and rounds, then restore **R = 7** for the integer-mode code around it. Selection
brackets each FP op with `ntr 0 … ntr 7` ([instr.c](instr.c), the FP add/sub/
mul/div path). Two FP operations in a row — `e = a + b; f = e + c;` — therefore produce a
`ntr 7` immediately chased by a `ntr 0` (here `e` is still live, so 5.1 removes only the
reload of `e`, leaving its store):

```
   7 ,xta, 0      ; A = a
     ,ntr, 0      ; R = 0  (normalize + round)
   7 ,a+x, 1      ; A = a + b
     ,ntr, 7      ; R = 7  ← churn
   7 ,atx, 4      ; e = A
     ,ntr, 0      ; R = 0  ← churn
   7 ,a+x, 2      ; A = e + c
     ,ntr, 7      ; R = 7
   7 ,atx, 5      ; f = A
```

The `atx` between the `ntr 7` and `ntr 0` does not depend on R, so R can simply stay 0 across
both operations and be restored once at the end. **Rule: track R; delete any `ntr n` whose
operand equals the current known R, and collapse adjacent `ntr x; ntr y` (with only
R-independent instructions between) to `ntr y`.**

```
   7 ,xta, 0
     ,ntr, 0      ; R = 0 once
   7 ,a+x, 1
   7 ,atx, 4
   7 ,a+x, 2
     ,ntr, 7      ; R = 7 once, at the end
   7 ,atx, 5
```

The same rule deletes the leading `ntr 7` an integer routine would never need after `b/save`
already set R = 7, and tidies the `ntr` brackets around FP negate and the int→FP conversion.

### 5.4 Compare → branch fusion

Relational operators lower to a runtime helper that leaves **0 or 1 in A** (and sets ω
accordingly) — `b/lt`, `b/le`, `b/eq`, the unsigned `b/ult …`, and the FP `b/flt …`. The
boolean is stored to a temporary; the following `JUMP_IF_ZERO` reloads it and tests ω. For
`if (a < b) { … }`:

```
   7 ,xta, 0      ; A = a
   7 ,xts, 1      ; push a; A = b
     ,call, b/lt  ; A = (a < b) ? 1 : 0, ω set from A
   7 ,atx, 3      ; t = A
   7 ,xta, 3      ; A = t      ← reload
     ,uza, .Lend  ; branch if ω = 0  (condition false)
```

Rule 5.1 removes the reload; rule 5.2 then removes the dead store of `t`; what remains is the
helper result feeding the branch directly:

```
   7 ,xta, 0
   7 ,xts, 1
     ,call, b/lt
     ,uza, .Lend  ; branch on the ω the helper already set
```

This fusion needs no dedicated rule: it is the emergent product of rule 5.1 (reload
elimination) and rule 5.2 (dead-store elimination). It is only *valid*, however, if `atx`
preserves ω and the helper's last accumulator operation leaves ω consistent with its
returned A. Both hold here: every runtime relational helper exits with **ω = logical** (the
`A = 0?` group the following `uza`/`u1a` tests), per the logical-ω exit contract documented
in [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) ("ω mode and the AU mode register
R"), and `atx` stores A without disturbing ω. This was confirmed on the simulator (Section
7) — signed, unsigned, and FP comparisons feeding `if` branches compute correctly — so the
fusion is enabled and locked in by the `CompareBranchFused` tests.

Note what the contract does *not* cover: a condition the machine's own arithmetic produced,
with no helper in between. `if (x - y)` reaches the branch in the additive group, and the
same two rules that fuse a comparison turn that into a sign test. Rule 5.11 is the general
guarantee; the helper contract is what keeps it from firing on the shape above.

### 5.5 Jump and label cleanup

Several rewrites need only the control-flow shape, not tracked data state. All three are
implemented as rule #31 (see [peephole.c](peephole.c)); because they
need list look-ahead or list mutation rather than the `(cur, state)` predicate the rule
table expects, they are handled directly in the sweep alongside the other look-ahead
rules. They are locked in by the `DuplicateEpilogueJumpRemoved`, `ConditionalOverJumpInverted`,
`WhileLoopJumpIfZero`, `JumpToNextLabelRemoved`, and `UnreachableTailRemoved` tests.

- **Jump to the next instruction.** A `uj` whose target label is the very next instruction is
  a no-op:

  ```
       ,uj, .L
   .L: ,bss,        →     .L: ,bss,
  ```

- **Duplicate epilogue jump.** `RETURN` emits `,uj, b/ret`, and the function epilogue emits
  another `,uj, b/ret`. For `return x;` as the last statement:

  ```
   7 ,xta, 0
     ,uj, b/ret     ; from RETURN
     ,uj, b/ret     ; from the epilogue  ← unreachable
     ,end,
  ```

  The second is unreachable; drop it. (The backend source already flags this as dead code.)

- **Unreachable tail.** Any instruction between an unconditional `uj` and the next label is
  unreachable and can be deleted. Note that the halt (`stop`, emitted by the `__besm6_stop`
  intrinsic) does **not** open such a run: the halt is resumable — the operator presses continue
  on the console and execution goes on at the next instruction — so the code after it is live.

- **Branch over an unconditional jump.** A conditional that only skips a following `uj`
  inverts:

  ```
       ,uza, .L     ; if ω = 0 skip the jump
       ,uj, .M
   .L: ,bss,        →     ,u1a, .M     ; if ω ≠ 0 jump to M
                         .L: ,bss,
  ```

  `uza` (branch if ω = 0) over `uj .M` becomes `u1a .M` (branch if ω ≠ 0), removing one
  instruction and one fall-through.

### 5.6 Constant strength reduction

Strictly this is an *instruction-selection* improvement rather than a peephole on the
`Besm_Instr` list, because it needs the original `Tac_Val` constant operand, which is gone by
the time the list is built. It is listed here because it is the same *idea* — replace an
expensive form with a cheaper equivalent — and the backend already does one case of it: in
`ADD_PTR`, a power-of-two word scale becomes a single `asn` shift instead of a `b/mul` call
([instr.c](instr.c), the pointer-scaling path).

The general rule: multiply by a power of two becomes a left shift. For `n * 4`:

```
   7 ,xta, 0      ; A = n           7 ,xta, 0
   7 ,xts, =4     ; push n; A = 4
     ,call, b/mul ; A = n * 4    →    ,asn, 62     ; logical left shift by 2 (field = 64 − 2)
   7 ,atx, 3                        7 ,atx, 3
```

`asn` shifts by `field − 64`, so a left shift by *k* uses field `64 − k`. Likewise an
**unsigned** divide or remainder by a power of two becomes `asn` (right shift, field `64 + k`)
or an `aax` mask. **Signed** divide by a power of two is *not* a plain shift — it must round
toward zero — so it stays on `b/div`.

### 5.7 Direct symbolic addressing for globals (to investigate)

Every reference to a module-level global currently costs two instructions: `emit_xta_val`
([emit.c](emit.c)) emits `,utc, name` to put the global's address in the C
register, then a bare `,xta,` to load through it:

```
     ,utc, g       ; C = address of g           →     ,xta, g     ; if the assembler/linker
     ,xta,         ; A = mem[C]                              permits a relocatable symbol here
   7 ,atx, 3                                          7 ,atx, 3
```

If the Dubna single-pass assembler and linker accept a relocatable external symbol directly
in the address field of `xta` / `atx` / `a+x`, the `utc` disappears for the common scalar
load/store/arith forms (the index-register forms used by array indexing and `&global` still
need `utc`). This one is flagged "investigate" because it depends on linker behavior and must
be validated on the simulator before adoption.

### 5.8 Pointer-register reuse (resolved in instruction selection)

An earlier plan proposed a peephole that, on back-to-back dereferences of one pointer, skips
the `ati 1` that reloads index register r1 when r1 still holds that pointer. This is now moot:
word `LOAD`/`STORE` no longer touch r1. A dereference is `<reg> ,wtc, <off>` — which loads
the pointer word's address bits straight into the C address-modifier register — followed by a
bare `,xta,` / `,atx,` reading or writing `mem[C]`:

```
LOAD  *p → d:                 STORE  *p = src:
  <pr> ,wtc, <po>   ; C = p     <src> ,xta,        ; A = src
       ,xta,         ; A=mem[C]  <pr> ,wtc, <po>   ; C = p
  <dr> ,atx, <do>                     ,atx,        ; mem[C] = src
```

Since the C register resets after the one instruction that uses it (every instruction except
`utc`/`wtc` clears C), consecutive dereferences cannot share it, so there is nothing left for a
peephole to reuse. What the peephole *can* do is drop a redundant dereference outright — see
5.9. The one interaction the backend must honour is that a `wtc reg,off` of an auto temp
**reads** that slot — `instr_reads_auto_slot` in [peephole.c](peephole.c)
lists `WTC` so that dead-temp-store elimination (5.2) does not drop the store that materialises
an `ADD_PTR` address the following `wtc` dereferences.

### 5.9 C groups as the unit of analysis

The C address-modifier register is reset to zero after every instruction **except `utc` (022)
and `wtc` (023)**. A C-setter and the instruction after it are therefore one indivisible unit —
a **C group** — and the pass must both analyse and rewrite them together. The backend emits
these shapes ([emit.c](emit.c)):

| Group | Meaning |
|---|---|
| `utc name` + `xta/atx 0,woff` | the global `name`, word `woff` |
| `wtc reg,off` + `xta/atx` | through the pointer in frame slot `(reg, off)` |
| `wtc name` + `xta/atx` | through the global pointer `name` (C = mem[name]) |
| `utc reg,off` + `vtm 14` | the address of a *local* at frame offset `off`, not a memory operand |
| `wtc reg,off` + `vjm`, `wtc name` + `vjm` | an indirect call: `vjm` jumps to `0 + C` |

Taking the address of a *global* used to appear here too, as `utc name` + `vtm 14`. It no
longer does. `vtm` (024) is a Format-2 instruction like `utc`, so its own 15-bit address field
holds a relocatable name, and `M[reg] = offset + C` takes no `M[reg]` contribution — the
whole pair collapses to a single `14 ,vtm, name` at instruction selection
([instr.c](instr.c)), one instruction and one word shorter. The local form
above cannot collapse: its index register `reg` lives in the `utc`'s register field, and
`vtm`'s register field is already spoken for by the destination `M[14]`.

A dereference *through* a global pointer collapsed the same way, and for the same reason:
it used to be `utc name` + `wtc` (C = &name, then C = mem[C]), but `wtc` (023) is Format 2
too, so `wtc name` loads C from `mem[name]` on its own. That is what a call through a
file-scope function pointer now emits, ahead of its bare `13 ,vjm,`.

Two rules follow.

**A consumer's `(reg, addr)` fields do not name a frame slot.** Its effective address is
`addr + M[reg] + C`. The pass models this with a `Loc`, in
[peephole.c](peephole.c):

- `LOC_FRAME(reg, off)` — a plain `xta/atx`, no C involved
- `LOC_GLOBAL(name, woff)` — from the first shape above
- `LOC_DEREF(pointer)` — from the two `wtc` shapes, identified by *where the pointer lives*
  (a frame slot, or a name)
- `LOC_NONE` — an address computation, a nonzero literal, a `vjm`: names nothing, matches nothing

A *zero* literal is the one constant that names a location. Instruction selection reserves no
literal for it and leaves the instruction with an empty address field, because `mem[0]` always
reads as zero; the bare `xta` therefore addresses `mem[M[0] + 0]` and classifies as
`LOC_FRAME(0, 0)`. That is not a slot `frame_lookup` can ever hand out — those carry r6
(`REG_PAR`) or r7 (`REG_AUTO`) — and nothing ever stores to it, so `A ≡ LOC_FRAME(0,0)` holds
exactly when `A == 0`. A bare `xta` that reads `mem[C]` is a group *consumer* and is stepped by
the sweep, so it never reaches `plain_loc` to be confused with one.

Rule 5.1 then compares `Loc`s. `LOC_GLOBAL(g, 0)` and `LOC_GLOBAL(g, 1)` are different words,
so `g.x = 7; return g.y;` keeps its reload; `LOC_DEREF` through slot `(6,0)` and through
`(6,1)` are different pointers, so `*p = x; *q = y; return *p;` keeps its reload too.

**A consumer may never be deleted alone.** Its setter would survive and re-bind C to whatever
instruction fell into the gap. The sweep guarantees this structurally rather than by a guard:
it steps the cursor from a group's setter straight past its consumer, so no rule ever sees a
consumer as its cursor, and a match splices out the whole group — two nodes, or three for a
dereference through a global pointer.

#### Why there is no memory-clobber analysis

"A mirrors location *L*" looks like it should be invalidated by any store that might alias
*L*. It is not, and the reason is a property of the machine: **memory is only ever written
from A** (`atx`, `stx`). A store therefore either writes somewhere other than *L*, or writes
*L* the very value A already holds. Either way A still mirrors *L*. The mirror can only go
stale when **A itself changes**, or when the frame base `M[6]`/`M[7]` moves — and the pass
settles its tracked location at every instruction that does either.

`LOC_DEREF` gets the same guarantee for free. It depends on the pointer's value, but the
pointer lives in memory, so it can only be written from A, by an `atx`/`stx` that settles the
tracked location on the pointer itself (or on `LOC_NONE`) — discarding the `LOC_DEREF` before
it can be matched. A `call`, which may write anything, is already a basic-block boundary.

This is worth stating plainly because the natural design — a table of "a store to a frame slot
kills all dereferences, a store through a pointer kills everything" — is dead code on this
architecture. A machine with a store-immediate or a memory-to-memory move would need it.

### 5.10 I/O address folding

`ext` (033), `mod` (002) and the extracode all name their operand — a device register, a CPU
register, a trap argument — through the **effective address** rather than through a memory
word: `EA = (addr + M[reg] + C) mod 0100000`. A constant address that fits the 12-bit Format-1
field is simply that field, but everything else has to be *put* somewhere the address
calculation reads from, and the only two candidates are an index register and the C register.

Instruction selection ([intrinsics.c](intrinsics.c), `emit_io_op`) picks C,
and reaches it through the stack:

```
   xta <addr>          A = the effective address
   xts <acc>           push it; A = the accumulator operand
15 wtc                 stack mode (V = 0, M = 017): pop it back into C
   ext                 EA = C
```

XTS (003) writes A to `mem[M[15]]`, bumps the pointer and loads the accumulator operand in one
instruction; the `wtc` that follows is in stack mode, so it decrements the pointer and loads C
from the word just pushed. Push and pop are adjacent and balanced, no index register is
disturbed, and both instructions leave ω logical.

That costs exactly what materialising the address into an index register costs. What it buys
is a shape the peephole can fold — and both folds below are impossible on the index-register
form, because there the address arrives in a register whose `(reg, addr)` fields name no
location the pass can reason about.

**(a) Displacement fusion.** A constant addend belongs in the instruction's own address field,
where `EA = addr + C` adds it back for free:

```
6 xta        6 xta            6 xta          6 xta
  a+x =100     xts       ⇒      xts =1         xts
  xts       15 wtc              call b$uadd 15 wtc
15 wtc         ext 64        15 wtc            ext 1
  ext                           ext
```

Two shapes reach the rule because C picks the addition by operand type: a signed one is the
machine's own `a+x`, an unsigned one a call to the runtime helper `b$uadd`, which pops the
pushed left operand and leaves the sum in A. Deleting the `xts`+`call` pair leaves the base in
A with the stack balanced exactly as before. Since `<besm6.h>` declares the address parameter
`unsigned`, the helper form is the one device code actually produces.

The fold is exact even though the C addition is 48-bit and the address field is 15: the `wtc`
keeps only bits 15:1 and EA is formed mod 0100000, and truncation commutes with addition, so
`low15(base + N)` and `low15(base) + N` are the same address. Overflow of the C-level sum
cannot change the outcome. The displacement must fit the short field (≤ `07777`); a larger one
declines the fold rather than truncating.

**(b) Memory-resident address.** When the address was merely loaded out of a frame slot or a
global, the round-trip is waste — `wtc` reads memory itself, so it can address that location
directly:

```
6 xta              xta          utc g            xta
  xts       ⇒    6 wtc            xta      ⇒     wtc g
15 wtc             ext 1          xts            ext 1
  ext 1                        15 wtc
                                  ext 1
```

The `xts` becomes the plain accumulator load it always was, and the `wtc` moves off the stack
pointer onto the location itself — one instruction either way, since WTC is Format 2 and its
own 15-bit address field reaches any global directly (the Format 1 accessors are the ones that
need a `utc` escape). A's previous value is dead across the rewrite: the `xts`-turned-`xta`
redefines it unconditionally before the I/O op, which is itself a basic-block boundary.

The two compose, and the pass already re-sweeps to a fixed point, so (a) exposes the base that
(b) then anchors on. `__besm6_ext(x + 1, w)` goes from six nodes to three:

```
6 xta
  xts =1                     6 xta 1
  call b$uadd        ⇒       6 wtc
6 xts 1                        ext 1
15 wtc
  ext
```

Neither rule can fire on ordinary code: only these three instruction kinds ever carry the
`xts`/`15 wtc`/io trailer the matcher requires. Both need list look-ahead and rewrite in
place, so like 5.5 they live in the sweep rather than in `rule_table`.

### 5.11 ω fixup before a conditional branch

The rewrite that makes 5.4 safe in general rather than by luck.

`uza`/`u1a` do not test a latched flag: the hardware recomputes ω at branch time from the
accumulator and the **ω group** recorded in R (bits 5–3), and the group is written by
whichever instruction last set it. So the same `uza` means four different things:

| group | ω | `uza` branches when |
|---|---|---|
| logical | A ≠ 0 | **A = 0** ← what a C truth test wants |
| additive | A < 0 | A ≥ 0 |
| multiplicative | abs(A) < 0.5 | otherwise |
| none | 1 | never |

Instruction selection is careful about this — `JUMP_IF_ZERO` always loads its condition with
an `xta`, which sets the logical group, immediately before the branch. But 5.1 and 5.2 then
delete that store/reload pair whenever A already holds the value, and what is left exposed is
the group the *producer* left. For `if (x - y)`:

```
   6 ,xta,          ; A = x                 ω group := logical
   6 ,a-x, 1        ; A = x - y             ω group := ADDITIVE
     ,atx, t                                (a store does not touch the group)
     ,xta, t        ; ← 5.1 deletes this reload …
     ,uza, .T1      ; … and the branch now reads A ≥ 0: a *sign* test
```

which compiles `if (x - y)` as `if (x - y >= 0)` — silently wrong for every `x > y`. This was
[BUG.md](tmp/BUG.md), found in v7's `sort(1)`.

The repair is one instruction, inserted only where the tracked group is not already logical:

```
   6 ,xta,
   6 ,a-x, 1
     ,aex,          ; A ^= memory word 0 — A unchanged, ω group := logical
     ,uza, .T1
```

`aex` is the cheapest of the logical ops, and memory word 0 reads as zero architecturally, so
the fixup costs one instruction and changes nothing but the group. (It leaves the R suppress
bits alone too, so it composes with 5.3 — the runtime library writes the same no-op as
`,aox,`; see [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md), "ω mode and the AU mode
register R".)

Making it *conditional* is the whole point, and is what the pass's ω tracking buys: the
common shapes pay nothing, because they already end in the logical group — a surviving
`xta`, a relational helper (5.4, by the exit contract), a compiled function's `b/ret`, an
extracode, a read-address `ext`/`mod`. `omega_after` in
[peephole.c](peephole.c) is the per-opcode table from
[Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) §4 transcribed, so a kind whose group is
not modelled falls to "kept" and, at worst, buys a redundant `aex`.

The rule covers all three groups, not just the additive one that exposed it: `arx` and any
future inlined multiply leave the multiplicative group, under which `uza` would test bit 48.
That is why `__besm6_arx` no longer emits an unconditional correction of its own — it pays
only when its result actually feeds a branch.

Like 5.5 and 5.10 this lives in the sweep rather than in `rule_table`; it is the one rewrite
that *inserts* a node, which no `(cur, st)` predicate can express.

### 5.12 What a new instruction kind owes the pass

Two of the pass's structures are **whitelists**, and a whitelist is silent when it is
incomplete: a kind left out of one does not fail to build, does not fail to assemble, and does
not fail to link. It miscompiles. Whoever adds a `Besm_InstrKind` owes both of these:

- **A kind with a memory operand must be added to `instr_reads_auto_slot`**
  ([peephole.c](peephole.c)). It ends in `default: return false` — "this
  instruction reads no frame slot" — so an omitted kind tells dead-temp-store elimination (5.2)
  that the temporary feeding it is never read, and the store that materialises its operand is
  deleted out from under it. This is why `WTC` is in the list.
- **A kind that clobbers A, R or ω must be a basic-block boundary** (`is_block_boundary`, and
  Section 4). Otherwise the pass keeps believing a stale "A mirrors *L*" or a stale R across
  it, and rules 5.1 and 5.3 rewrite on that belief.

The `<besm6.h>` intrinsics ([Besm6_Intrinsics.md](Besm6_Intrinsics.md)) are the worked example
of both. `BESM_IO_EXT`, `BESM_IO_MOD` and `BESM_IO_EXTRACODE` are boundaries, for the reasons
in Section 4. And one of them is deliberately *not* what it looks like: `BESM_BRANCH_STOP` is
**not** a terminator, so 5.5(b) must not open an unreachable run at it — the BESM-6 halt is
resumable, the operator presses *continue*, and the code after it is live. A `stop` treated as
a terminator would silently delete the rest of the block.

---

## 6. Implementing the pass in this codebase

The pass is a new translation unit, `peephole.c` / `peephole.h`, exposing:

```c
void besm_peephole(Besm_Func *func, const Frame *frame);
```

Structure:

1. **Walk each block's instruction list** with a sliding window — a cursor plus a few
   look-ahead pointers — maintaining the tracked A/R/ω state described in Section 4 and
   resetting it at every label and branch.
2. **Match a rule table.** Each rule is a predicate over the window plus a rewrite action.
   Keeping rules in a table (rather than one tangled function) mirrors how production
   compilers organize peephole passes and keeps each rule independently testable.
3. **Rewrite by splicing the linked list.** Removing a node means relinking its predecessor's
   `next` and freeing the node with `besm_free_instr`
   ([besm_free.c](besm_free.c)), which also frees its heap-owned `name`. Be
   careful never to free a `name` string that another node still points at (the emit helpers
   `xstrdup` their names, so each node owns its own copy — splicing one node never dangles
   another). The tracked location does borrow a node's `name`, but only ever a node that
   precedes the group being deleted, so it cannot dangle either.
4. **Iterate to a fixpoint.** One rewrite can expose another (5.1 enables 5.2, which with 5.3
   enables further `ntr` collapsing), so the pass repeats over the list until a full sweep
   makes no change.

Hook it into `codegen_function` ([codegen.c](codegen.c)) immediately before
`emit_madlen_module`, so emission always sees the optimized list.

### Correctness invariants

- **Never rewrite across a basic-block boundary.** A value in A or a known R/ω is only valid
  within straight-line code (Section 4).
- **Preserve observable behavior.** `CompileAndRun` results must be identical before and after
  (Section 7). The pass changes the instruction sequence, never the computed values.
- **Respect the side effects of the deleted instruction.** Dropping a reload is safe because a
  load has no effect but setting A and ω; dropping a *store* (5.2) is safe only when the slot
  is truly dead.

---

## 7. Verifying rewrites

The backend test suite (`besm-tests`, built from the `*_tests.cpp` files) supports two
complementary checks; both are used for every rule:

- **`CompileToMadlen`** asserts the *shape* of the emitted assembly. These assertions pin
  exact instruction sequences, so they will need updating as each rewrite lands — that is
  expected, and is how a rule's effect is locked in.
- **`CompileAndRun`** runs the program under the Dubna simulator and checks the computed
  result. This is the behavior guard: the value a function returns must not change.

For the rules that depend on subtle machine state — chiefly the compare→branch fusion
(Section 5.4), which assumes `atx` preserves ω and the comparison helpers set ω consistently —
confirm the assumption with **instruction tracing**. Per the project build notes, re-run a
test's job file under the simulator with `-d c`:

```sh
cd build/backend/besm6
dubna -d c SomeTest.dub > SomeTest.trace
```

and inspect the `ACC` and `RAU` (mode register R) values around the rewritten window to
verify the accumulator, mode bits, and ω behave as the rule assumes.

---

## 8. Limitations and ordering

- **Local only.** A peephole window cannot see loop-level or interprocedural opportunities.
  Those belong to the machine-independent `optimize/` passes (constant folding, copy
  propagation, dead-store elimination), which run earlier on the TAC.
- **Runs after instruction selection.** The pass cleans up *selection's* output; it relies on
  selection having produced correct (if verbose) code.
- **Interacts with frame allocation.** Eliminating a dead temporary store (5.2) makes its
  frame slot unnecessary; the frame-slot-reuse task (Phase M) builds on that to shrink the
  prologue's stack extension.
- **Order-sensitive.** Rules enable one another, so the pass iterates to a fixpoint rather
  than running each rule once.

In short, the peephole pass is the BESM-6 backend's final polish: a small, table-driven,
fixpoint sweep over the `Besm_Instr` list that removes the store/reload, mode-register, and
compare/branch residue that one-node-at-a-time instruction selection necessarily leaves
behind.

---

## See also

- [Besm6_Instruction_Set.md](Besm6_Instruction_Set.md) — the instructions referenced above.
- [Besm6_Runtime_Library.md](Besm6_Runtime_Library.md) — the `b/…` helper conventions
  (including ω behavior relevant to Section 5.4).
- [Besm6_Calling_Conventions.md](Besm6_Calling_Conventions.md) — `b/save`/`b/ret`, R = 7
  on entry, the r6/r7 frame registers.
- [Madlen.md](Madlen.md) — the assembler statement format used throughout.
- [TAC_Optimization.md](../../docs/TAC_Optimization.md) — the machine-independent optimizations that run
  earlier and are deliberately *not* duplicated here.
