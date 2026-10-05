# TAC-Level Optimization

This article describes the five machine-independent optimization passes on the TAC intermediate representation: **constant folding**, **unreachable code elimination**, **common-subexpression elimination**, **copy propagation**, and **dead store elimination**. Except for common-subexpression elimination, the approach closely follows Chapter 19 of [Nora Sandler, *Writing a C Compiler*](https://nostarch.com/writing-c-compiler), adapted to the types and conventions of this codebase.

## Why optimize at TAC level?

The TAC lowering phase (`translator/`) is deliberately written for correctness, not speed. It introduces a fresh temporary variable for almost every subexpression and emits copy instructions liberally to keep each lowering rule simple. The result is correct but bloated: many temporaries are used exactly once, many copies are immediately overwritten, and every function body ends with a backstop `Return` that is unreachable when the source code already has a `return` statement.

Optimizing at TAC level is the right place to fix this because:

- TAC is already fully typed and fully lowered — we do not need to worry about C syntax or scoping rules.
- TAC is machine-independent — optimizations written here benefit every backend (RISC-V, BESM-6, and future targets).
- TAC instructions have a simple, uniform structure that makes optimization algorithms easy to express.

Optimizations that transform one function at a time, without knowledge of the rest of the program, are called **intraprocedural** optimizations. The five passes described here are all intraprocedural. Each `TAC_TOPLEVEL_FUNCTION` is processed independently; static variables, function calls, and pointer aliasing are handled conservatively.

The canonical source of truth for TAC node types is [tac/tacky.asdl](../tac/tacky.asdl). The C representation is in [tac/tac.h](../tac/tac.h).

## TAC IR quick recap

A `Tac_Program` holds a linked list of `Tac_TopLevel` nodes. We transform only `TAC_TOPLEVEL_FUNCTION` entries; static variables and static constants pass through unchanged.

A function's code is the linked list `u.function.body` — a sequence of `Tac_Instruction` nodes, each carrying a `Tac_InstructionKind` discriminator and a union of operands.

Operands are `Tac_Val` nodes of two kinds:

```asdl
Val = Constant(Const val)
    | Var(identifier name)
```

A `Constant` embeds a `Tac_Const` with one of eleven scalar kinds: `ConstInt`, `ConstLong`, `ConstLongLong`, `ConstUInt`, `ConstULong`, `ConstULongLong`, `ConstFloat`, `ConstDouble`, `ConstLongDouble`, `ConstChar`, `ConstUChar`.

A `Var` holds a string name. The translator generates temporaries as `%0`, `%1`, `%2`, … using `new_temp()` in `translator/translate.c`. Named variables correspond to C source identifiers.

Control flow is explicit in the instruction stream:

```asdl
Instruction = Jump(identifier target)
            | JumpIfZero(Val condition, identifier target)
            | JumpIfNotZero(Val condition, identifier target)
            | Label(identifier name)
            | Return(Val? src)
            | ...
```

In the examples below, we use a shorthand notation close to the ASDL. `x = a + b` means `Binary(Add, Var("a"), Var("b"), Var("x"))`; `x = 3` means `Copy(Constant(ConstInt(3)), Var("x"))`; `Target:` means `Label("Target")`.

## Constant folding

Constant folding evaluates expressions whose operands are all constants at compile time, replacing the instruction with a simpler one.

### Arithmetic and logical instructions

A `Binary` instruction with two constant operands is replaced by a `Copy` of the folded result:

```
%0 = 6 / 2          →    %0 = 3
%1 = 5 * 4          →    %1 = 20
%2 = a == a         →    %2 = 1
```

A `Unary` instruction with a constant operand is similarly folded:

```
%0 = -3             →    %0 = -3   (already a constant; emitted as Copy)
%1 = !0             →    %1 = 1
%2 = ~0xFF          →    %2 = ...  (bitwise complement, type-dependent)
```

Integer folding must respect the width of the result type encoded in `Tac_ConstKind`; overflow wraps at the C type's boundary, matching C's defined behavior for unsigned arithmetic and the implementation-defined wrapping our target uses for signed types.

Floating-point folding applies the same rule for `ConstFloat`, `ConstDouble`, and `ConstLongDouble`. A long double constant is binary128 bits, folded in software binary128 (`libutil/float128.c`, the same code as the RISC-V runtime), or as a double on a target whose long double is one; the host's own `long double` is never used.

### Type conversion instructions

When the source of a conversion instruction is a constant, the result is a new constant of the target kind, and the instruction becomes a `Copy`:

```
SignExtend(Constant(ConstInt(3)), dst)    →   Copy(Constant(ConstLong(3)), dst)
Truncate(Constant(ConstLong(256)), dst)  →   Copy(Constant(ConstInt(0)), dst)
IntToDouble(Constant(ConstInt(2)), dst)  →   Copy(Constant(ConstDouble(2.0)), dst)
```

This covers all fourteen conversion instruction kinds: `SignExtend`, `Truncate`, `ZeroExtend`, and the twelve `*To*` floating-point conversions.

### Conditional jumps

A conditional jump with a constant condition is converted to an unconditional jump or deleted entirely:

```
JumpIfZero(Constant(0), Target)     →   Jump(Target)     // always taken
JumpIfZero(Constant(nonzero), T)    →   (deleted)         // never taken
JumpIfNotZero(Constant(0), Target)  →   (deleted)         // never taken
JumpIfNotZero(Constant(k≠0), T)    →   Jump(T)           // always taken
```

Turning a conditional jump into an unconditional one, or removing it entirely, directly creates opportunities for the next pass to eliminate the now-unreachable code.

### Implementation note

Constant folding walks the flat `Tac_Instruction` linked list once. It is the only pass that does not require a control-flow graph (CFG). New `Tac_Instruction` and `Tac_Val` nodes are allocated with `tac_new_instruction`, `tac_new_val`, and `tac_new_const`; replaced nodes are freed with `tac_free_instruction`.

## Control-flow graphs

The three remaining passes reason about which paths through a function can reach a given instruction. A flat instruction list does not make this explicit; a **control-flow graph** (CFG) does.

### Basic blocks

A **basic block** is a maximal sequence of instructions such that:
- Execution enters only at the first instruction (no label in the interior).
- Execution leaves only at the last instruction (no jump or return in the interior).

Every label starts a new basic block. Every jump, conditional jump, and return ends the current basic block.

### CFG structure

The CFG has:
- An **Entry** pseudo-node that has an edge to the block containing the first instruction.
- An **Exit** pseudo-node that receives edges from every `Return` instruction.
- One node per basic block, with edges to successor blocks.

A `Jump(target)` adds an edge from the current block to the block whose first instruction is `Label(target)`. A `JumpIfZero(cond, target)` adds two edges: one to the target block (if the condition is zero) and one to the immediately following block (fall-through, if the condition is nonzero). A `Return` adds an edge to Exit.

### Building and flattening

Building the CFG is a single linear scan of the instruction list. Flattening it back into a list concatenates the instruction sequences of all reachable blocks in order (typically the original linear order, or reverse-post-order for analyses that need a specific traversal).

## Unreachable code elimination

After constant folding may have turned some conditional jumps into unconditional ones, some blocks may have become unreachable. Unreachable code elimination removes them.

### The algorithm

Starting from the Entry node, perform a depth-first (or breadth-first) traversal of the CFG, marking every reachable block. Any unmarked block is unreachable and is removed entirely.

### Cleanup after removal

After removing unreachable blocks, two cleanup steps tighten the code further.

**Useless jumps.** A `Jump(target)` where `target` is the label of the immediately following block is a no-op. Once unreachable blocks are gone, some jumps that previously skipped over removed blocks now fall into this category. They are deleted.

**Unused labels.** A `Label(name)` that is not the target of any remaining jump (or the entry label of the function) serves no purpose at the TAC level. It is removed. Labels do not produce machine instructions, so removing them does not affect code size or speed — but it makes the instruction list easier to read and debug.

### The backstop Return

The translator appends `Return(NULL)` to the end of every function as a backstop in case the source code is missing a `return` statement. When the function body already ends with an explicit `return`, this backstop is unreachable. Unreachable code elimination removes it automatically, without any special-case logic in the translator.

This illustrates a broader principle: generating slightly redundant code and cleaning it up in an optimization pass is often simpler than generating perfectly minimal code directly.

## Copy propagation

When the instruction `Copy(src, dst)` appears, later uses of `dst` can often be replaced by `src`. This is called **copy propagation**; propagating a constant is the special case sometimes called **constant propagation**.

### A simple example

```
%0 = 3
Return(%0)
```

Since `%0` holds the value `3` at the `Return`, we can substitute:

```
%0 = 3
Return(3)
```

The assignment to `%0` is now a dead store (see below). After dead store elimination removes it, the function reduces to a single `Return(3)`.

### The safety problem

Substituting freely is only safe when we know the value of `dst` has not been changed on any path that reaches the use. Consider:

```
%0 = 4
JumpIfZero(flag, Else)
%0 = 3
Else:
Return(%0)
```

When `Return(%0)` executes, `%0` is either `3` or `4`, depending on `flag`. We cannot substitute either constant. The copy `%0 = 4` does not *reach* the `Return` on all paths.

### Reaching-copies analysis

We determine which copies are safe by computing **reaching copies** — a forward dataflow analysis on the CFG.

The lattice element for each program point is a set of `(src, dst)` pairs representing copies that are valid on *every* path reaching that point.

- **Initial value:** the reaching-copies set at Entry is empty.
- **Meet (join at merge points):** intersection — a copy is reaching only if it holds on every incoming path. A predecessor not visited yet is left out of the meet, standing for every copy (an optimistic start, as in CSE), and a block none of whose predecessors has been visited waits for one; a reachable empty block passes its in-set on. So a copy made ahead of a loop reaches into it, where an empty start would lose it at the loop head, the back edge's out-set being unknown when the head is first met. BESM-6, which opts out of the loop optimizations, keeps the empty start, and its output with it.
- **Transfer function for a single instruction:**
  - **Gen:** if the instruction is `Copy(src, dst)`, add `(src, dst)` to the set.
  - **Kill:** remove every pair `(s, d)` from the set where `s == dst` or `d == dst` (overwriting `dst` invalidates any copy that mentioned it as either operand).

The analysis iterates over the CFG (in forward order) until the reaching-copies sets stop changing. Each block's output is computed from its input by applying the transfer function instruction by instruction.

Once the analysis converges, each use of a variable `x` is replaced by `src` if every reaching copy `(src, x)` agrees on the same `src` at that point.

A copy is not propagated across a change of the value class (an integer copied into a pointer, on a 32-bit target, is a cast), nor between pointers to different scalar types. A `Store` is as wide as its pointer's pointee, so in

```c
double d;
unsigned long *w = (unsigned long *)&d;
*w = 0;
```

forwarding `&d` into the store would make it a store of a `double`: an integer constant in an FP register, or 8 bytes written where 4 were meant.

### Conservatism around aliased variables

Two categories of variables must be treated conservatively:

1. **Observable variables** — anything with static storage duration (file-scope globals, `extern`s, local `static`s). A `FunCall` instruction may call any function, which may read or modify such a variable. At every `FunCall`, all copies involving observable variables are killed from the reaching set. So are they at every `Store`: a pointer may point at a global whose address was taken in another function, which the local classification cannot see.

2. **Address-taken variables**. Any variable that appears as the `src` of a `GetAddress` instruction may be modified through the resulting pointer. At every `Store` or `FunCall`, copies involving such variables are killed.

The optimizer classifies a name *locally*, without consulting the rest of the program: in TAC a local and a global are both bare names, but a name is **observable** exactly when it is neither a temporary (`%0`, `%1`, … — always compiler-generated and private) nor one of the function's parameters or automatic locals. The translator records those names on the function toplevel (`Tac_TopLevel.function.locals`); the no-shadowing rule makes the classification unambiguous program-wide. See `optimize/alias.c`.

### Self-copies

After substitution, some `Copy(x, x)` instructions may appear (the source and destination are the same variable). These are no-ops and are removed immediately.

## Common-subexpression elimination

Common-subexpression elimination (CSE) removes a computation whose value is already held in a variable. The pass is in `optimize/cse.c`.

### A simple example

```
%0 = x * y
%1 = x * y
%2 = %0 + %1
```

On every path to the second multiply, `x * y` has been computed into `%0`, and neither `x`, `y` nor `%0` has changed since. The multiply becomes a copy, `%1 = %0`. Copy propagation then rewrites `%2 = %0 + %0`, and dead store elimination removes the copy. CSE runs just before copy propagation in each iteration, so all three steps happen in the same iteration. The pass introduces no new variables. A copy always goes from an existing holder, so the function's typed `locals` stay exactly the names of its body.

The redundancy is common in lowered TAC: `a[i] = a[i] + 1` computes the element address `ADD_PTR(a, i)` twice, once for the load and once for the store.

### Available-expressions analysis

CSE is a forward dataflow analysis on the CFG, with the same structure as reaching copies:

- **Lattice element:** a set of facts "expression E is held in variable h", keyed by E, holding on every path to the program point.
- **Initial value:** empty at entry.
- **Meet:** intersection. A fact survives only if every predecessor has it with the *same* holder. A predecessor that has not been visited yet is left out of the meet (an optimistic start). This keeps an expression computed before a loop available inside the loop. Copy propagation does the same, except on BESM-6.
- **Transfer function for a single instruction:**
  - **Kill:** defining a variable v removes every fact whose holder is v or whose expression reads v.
  - **Gen:** a candidate `h = E` adds (E → h), unless E reads h itself (`x = x + 1`). If E already has a holder, the old holder is kept: the rewrite turns this instruction into a copy of it, so it still holds E, and keeping it lets the fact agree around a loop's back edge.

The rewrite replays each block from its in-set. A candidate `d = E` with (E → h) available becomes `d = h`, provided d is private and has the same type as h. When d is h itself, the recomputation is deleted.

As in dead store elimination, a block that is still reachable but has been emptied by an earlier pass takes part as an identity node. Left unvisited, an empty block on the way into a loop would let a fact from the back edge look available on entry.

### Candidates and expression keys

A candidate is a pure computation: the result depends only on the operands, and there is no side effect. The candidates are `Binary`, `Unary`, every conversion, `AddPtr`, `PtrDiff`, and `GetAddress` (with its byte and decay forms) of a static object. An address does not depend on the object's value, so it is not killed when the object is assigned. The address of a frame slot is *not* a candidate. It takes one instruction to recompute from the stack or frame pointer, and holding it would only add register pressure. Volatile instructions are never touched. Division is a candidate: CSE only removes an evaluation that an identical one dominates, so it never introduces a trap. So are the memory reads: a `Load` (or `LoadByte`) through a pointer, keyed by the pointer, and a `CopyFromOffset` member read of a named aggregate, keyed by the aggregate and the offset; see below for when they are killed.

An expression's key is its instruction kind and operator, its immediate field (the `AddPtr` scale, or the destination kind of the integer conversions), and its operands. A variable is spelled by its name. A constant is spelled by its kind and exact bits, so `-0.0` and `0.0` stay apart, and so do an `int` 1 and a `long` 1. The operands of commutative operators are sorted, so `a + b` and `b + a` share a key. The destination type is not part of the key; instead, a fact is used only for a destination of the same type as its holder. This is what keeps `Truncate(x)` to `char` apart from `Truncate(x)` to `short`.

### Conservatism around aliased variables

A holder is always private: a temporary, a parameter or an automatic local. Expressions may read globals and address-taken variables, so facts that mention them are killed:

- at every `FunCall`, since the callee may write them;
- at every `Store`, since the pointer may point at any of them, including a global whose address another function took;
- at every `CopyToOffset`, for the aggregate it writes.

A read through a pointer may see any memory, so every `Load` fact is killed by anything that may write memory a pointer reaches: a `Store`, a `FunCall`, or a write — a `Copy`, a computation, a `CopyToOffset` — to a global or address-taken variable. There is no type-based alias analysis: the code this compiler builds (the v7 Unix sources among it) puns types freely. A member read of a named aggregate needs no extra rule. It reads the aggregate's name, so a write to the aggregate kills it, and so does a store or call when the aggregate is global or address-taken.

### Store-to-load forwarding

After `Store(v, p)`, a `Load` through `p` reads `v` until something writes memory. The store first kills every load fact, as any store does, and then adds the fact `Load p → v`, so `*p = x; return *p;` returns `x` without reading memory, while `*p = 1; *q = 2; x = *p` forwards nothing: `q` may be `p`. The stored value must be a private variable or a constant. A constant holder is rewritten into a `Copy` of that constant, and only for a destination of the constant's own type. A byte store (`StoreByte`) truncates, so it gives no fact.

## Dead store elimination

An instruction is a **dead store** if it assigns a value to a variable that is never subsequently read before the variable's value is overwritten again or the function exits. Dead stores can be removed safely because they have no observable effect on the program.

### A simple example

```
%0 = a + b
%0 = 2
Return(%0)
```

The first instruction's result is immediately overwritten by the second. The value of `a + b` is never used, so `%0 = a + b` is a dead store and can be removed.

### Liveness analysis

We determine which stores are dead by computing **variable liveness** — a backward dataflow analysis on the CFG.

A variable is **live** at a program point if there exists a path from that point to a use of the variable before any intervening redefinition.

The lattice element for each program point is a set of live variable names.

- **Initial value:** the live set at Exit is empty (or, for conservative correctness, the set of all observable and address-taken variables, since they may be observed after the function returns).
- **Meet (join at merge points):** union — a variable is live if it is live on any outgoing path.
- **Transfer function for a single instruction, applied backward:**
  - Remove `def(i)` from the live set (the instruction's destination is no longer live *before* the instruction if it was defined here).
  - Add `use(i)` to the live set (any operands read by the instruction are live *before* it).

An instruction is a dead store when its destination variable is not in the live set *after* the instruction.

A subtlety: an earlier pass (unreachable code elimination's jump/label cleanup) can leave a block that is still *reachable* but now has no instructions. The liveness fixpoint must treat such an empty block as an **identity node** — its in-set equals its out-set (the meet of its successors) — so liveness flows through it to its predecessors. Skipping empty blocks would strand their in-sets empty and let a predecessor wrongly drop a store that is live past the gap.

### Which instruction kinds are removable

Not every instruction with a destination variable can be removed when the destination is dead. Instructions with side effects must be kept.

**Removable** (pure computation — safe to delete when dst is dead):
- `Copy`, `Binary`, `Unary`
- All fourteen conversion instructions (`SignExtend`, `Truncate`, `ZeroExtend`, `IntToDouble`, …)
- `GetAddress` (when the resulting pointer is unused)
- `Load`, `AddPtr`, `CopyFromOffset`

**Not removable** (has side effects or is control flow):
- `Store` — writes through a pointer; the write is observable.
- `FunCall` — may have arbitrary side effects; removing it would change program behavior.
- `Jump`, `JumpIfZero`, `JumpIfNotZero`, `Label`, `Return` — control flow.
- `CopyToOffset` — writes a field of a struct; the write may be observable through a pointer.

### Conservatism around aliased variables

Observable variables (globals, `extern`s, local `static`s) and address-taken variables must be treated as live at Exit (they may be read by the caller or by another function), so a store to one is never dead. At every `FunCall`, they must be treated as potentially redefined (the callee might write them), which restores their liveness. Observability is determined per function: a name is observable when it is neither a temporary nor one of the function's parameters or automatic locals (see `optimize/alias.c`).

## Loop optimizations

Three transformations work on loops. They are on for every target but BESM-6, which opts out (`Target.no_loop_opt`) so that its code stays as it was.

### Loop rotation

The translator lowers a `while` or `for` loop tested at its bottom, behind a copy of the test at its top as a guard:

```
    if (!cond) goto end            // the guard
top:
    body
continue:
    update
    if (cond) goto top             // the test
end:
```

The loop body runs with one conditional jump per iteration instead of a conditional jump out and an unconditional one back. The condition is lowered twice from the AST. When the guard is always true, as in `for (i = 0; i < 10; i++)`, constant folding drops it. Rotation is an option of the translator, `OptFlags.loop_rotate` (`--no-loop-rotate`).

The guard of `for (i = 0; i < n; ...)` compares a constant with a variable once copy propagation has forwarded the 0. Constant folding mirrors such a comparison (`0 < n` → `n > 0`) so that the constant is second, where the code generators take an immediate.

### Induction variables

In

```c
for (j = 0; j < n; j++)  ... v[j] ... v[j + 1] ...
```

every iteration computes `v + j*s` for each subscript. Strength reduction (`optimize/ivsr.c`) gives each such address a pointer `q`, kept equal to `v + j*s` at every point of the loop: set ahead of the loop, stepped by `c*s` right after `j` steps by `c`. A subscript then reads `q` (`v[j]`) or `q` plus a constant (`v[j + 1]`).

- **Loops** are the natural loops of the CFG: a back edge `b → h` where `h` dominates `b`, and the blocks that reach `b` without passing `h`. The **preheader** is the one predecessor of `h` outside the loop. A rotated loop has one, the guard. A loop without exactly one is left alone.
- **A basic induction variable** `j` is a private, not address-taken integer with one definition in the loop: `j = j ± c`, or `j = t` where `t = j ± c` is the one definition of `t` and dominates the copy. (CSE often leaves the second form, with `t` computed in the header for the subscript and copied at the bottom.)
- **A reduced address** is `ADD_PTR(v, x, s)`, with `v` private, not address-taken and not defined in the loop, and `x` equal to `j` plus a constant at that point: `j` itself, `t = j ± c`, or `sign_extend` of either (the index of a 64-bit target). The extension is allowed only for a signed `j`, whose overflow is undefined, so that the extension of `j + c` is the extension of `j` plus `c`. When `j` steps between the computation of `x` and its use, the step is taken back from the constant. "Between" is decided by dominance and by reachability within one iteration.
- **Linear-function test replacement.** A loop test `x op bound`, with `x = j + c` and `bound` invariant, becomes `q + c*s op v + bound*s`, with the end pointer formed in the preheader. This is valid only while the pointers do not wrap. They do not when one of `q`'s addresses is formed every iteration (its block dominates every latch), since forming it is undefined otherwise, and the end pointer is then at most one past the last. A test carries over only against the direction of the step (`<` or `<=` for an increasing `j`, `>` or `>=` for a decreasing one), and `!=` or `==` in either direction.
- **An index `inv - j`**, with `inv` invariant, is reduced the same way: its pointer is kept at `v + (inv - j)*s` and steps the other way. In a bubble sort the inner bound `n - 1 - i` is such an index of the outer loop, so the inner loop's end pointer steps down by one element per outer pass. A test of `inv - j` carries over as a test of `j` does. A test of `j` against `inv` itself becomes the mirrored test of the pointer against `v` (`j < inv` is `inv - j > 0`), for a step of one only, which cannot pass `inv` by.
- **The address of a global**, `t = &g`, moves to the preheader when the loop indexes `t` and `t` has this one definition. An array's address is taken where it is first subscripted, which is inside the loop when nothing ahead of it does, and `g[j]` then has no invariant base.
- **The induction variable goes** when nothing reads it but its own step: nothing else in the loop, and nothing after the loop ahead of another definition. (Two loops that each declare `int i` share the name, so the question is one of liveness, not of the whole function.) Its definitions ahead of the loop are then dead stores.
- **A pointer is stepped in place.** For `v[j]` and `v[j + 1]`, CSE and copy propagation leave `t = q + 1` formed for the second subscript and `q = t` at the bottom, with `q` still read in between (a store to `v[j]`), so no register can hold both. When `t = q + c` is the one definition of `t`, dominates the copy, and the copy runs every iteration, the pass steps `q` itself where `t` was formed, reads `t` as `q`, and reads `q` between the two as `q - c`. That is done only where the reads in between are addresses of loads and stores (or `ADD_PTR`s with a constant), where the offset folds into the addressing mode, and only when no exit taken in between leads to a read of `q`.
- `p + 0`, for a pointer `p` of the destination's type, becomes a copy, since a reduced pointer often starts at `v + 0*s`. This is not done for an aggregate base, whose copy would copy the aggregate. Likewise `x ± 0` for an integer `x`, the start `inv - 0` of a pointer on `inv - j`. (Constant folding has no algebraic identities, and adding them there would change BESM-6 code.)

The pass runs only at the fixed point of the scalar passes, and the loop goes on when it changes something (see below). A loop bound becomes invariant only once CSE and copy propagation have found its one computation. In `for (j = 0; j < n - 1 - i; j++)` the guard and the bottom test each compute `n - 1 - i`, and it takes several rounds to make the bottom one a copy of the guard's.

The new pointers are typed temporaries added to the function's locals. The pass is idempotent: a reduced `ADD_PTR` reads `q`, which the loop defines, so it is no candidate the next time. It is controlled by `OptFlags.ivsr` (`--no-ivsr`).

On the MSP430 bubble sort, rotation and this pass together take the inner loop from 21 cycles to 10, GCC's figure: `mov @r8+, r11`, `mov @r8, r14`, the compare and branch, and the test against the end pointer.

## The optimization pipeline

No single pass is sufficient on its own. The passes form a **virtuous cycle**:

- Constant folding produces constants that copy propagation can substitute into expressions, which constant folding can then evaluate again.
- Constant folding turns conditional jumps into unconditional ones, creating unreachable blocks that unreachable code elimination can remove.
- Common-subexpression elimination turns a recomputation into a copy, which copy propagation forwards and dead store elimination removes.
- Copy propagation eliminates the variable in a copy's destination, turning the copy into a dead store that dead store elimination can remove.
- Dead store elimination removes instructions, which may make previously reachable blocks empty, which unreachable code elimination can then clean up.

Because the passes amplify each other, the optimizer runs them in a loop until the instruction list stabilizes.

The passes rewrite the list in place, so a round's result cannot be compared with its input directly. `optimize_function` spells the list as YAML before a round and compares the spelling after it. That costs little next to the passes themselves. Every pass only removes or simplifies, so the loop always reaches a fixed point; it is capped at `OPT_MAX_ROUNDS` (64) rounds anyway, and a Debug build stops with a fatal error if the cap is reached, since that would mean two passes undo each other.

### Pseudocode

```
optimize(body, flags):
    if body is empty:
        return body

    loop:
        if flags.constant_folding:
            body = constant_fold(body)          // operates on flat list

        cfg = build_cfg(body)                   // split into basic blocks

        if flags.unreachable_code_elim:
            cfg = eliminate_unreachable(cfg)

        if flags.cse:
            cfg = eliminate_common_subexpressions(cfg)

        if flags.copy_propagation:
            cfg = propagate_copies(cfg)

        if flags.dead_store_elim:
            cfg = eliminate_dead_stores(cfg)

        new_body = flatten_cfg(cfg)             // rejoin into flat list

        if new_body spells as body did and flags.ivsr:
            if reduce_induction_variables(new_body) changed it:
                body = new_body; continue       // the scalar passes again

        if new_body spells as body did, or new_body is empty:
            return new_body                     // fixed point reached

        body = new_body
```

Equality is tested on the YAML spelling of the list (`tac_export_yaml_instruction_list`). An empty body after optimization is also a termination condition: if the optimizer removes everything, there is nothing left to iterate over.

### Pass ordering

Within one iteration, constant folding runs first on the flat list because it is the only pass that does not need a CFG. The remaining four passes operate on the CFG representation and run in the order shown: unreachable code elimination, common-subexpression elimination, copy propagation, dead store elimination. This ordering ensures that each pass can take advantage of what the previous pass produced within the same iteration. In particular, the copies that CSE leaves behind are forwarded and removed in the same iteration.

### Types

Only strength reduction creates variables (typed temporaries, added to the
function's `locals`), and copy propagation substitutes only across
a `COPY`, which the translator emits only between types of the same size, so every
operand keeps its width. The copies that CSE makes are between two names of the same
type. After the loop, `optimize_prune_locals` drops from the
function's `locals` every name the body no longer mentions, so `params` + `locals`
stay exactly the typed symbols of the optimized body.

### Command-line control

By default all five passes are enabled. Individual passes can be disabled for debugging, except constant folding.
For each pass, a separate CLI option exists in the `lower` binary: `--no-unreachable`, `--no-cse`, `--no-copy-prop`,
`--no-dead-store`, `--no-ivsr` and `--no-loop-rotate` (the translator's); `--opt-debug` traces the passes. `--opt-max-iter N` stops after N rounds (0, the
default, runs to a fixed point); the `VCC_OPT_MAX_ITER` environment variable sets it for a whole build, and the
backend test fixtures read it as well, which is how a miscompile is bisected to the round that introduces it.
The constant folding is always enabled, to simplify the subsequent code generation.

## Implementation plan

The optimizer lives in a new top-level directory `optimizer/`:

| File | Contents |
|------|----------|
| `optimizer.h` | Public API — `optimize_function(body, flags)` |
| `optimize.c` | Pipeline loop, fixed-point check |
| `const_fold.c` | Constant folding pass |
| `cfg.h`, `cfg.c` | CFG construction and flattening |
| `unreachable.c` | Unreachable code elimination |
| `dataflow.h`, `dataflow.c` | Helpers shared by the forward dataflow passes |
| `cse.c` | Available-expressions analysis and rewrite |
| `copy_prop.c` | Reaching-copies analysis and substitution |
| `dead_store.c` | Liveness analysis and dead store removal |

The pipeline entry point:

```c
// Returns an optimized copy of body. Caller frees the result.
Tac_Instruction *optimize_function(Tac_Instruction *body, OptFlags flags);
```

### Existing utilities to reuse

- `tac_new_instruction`, `tac_new_val`, `tac_new_const` — allocate replacement nodes.
- `tac_free_instruction` — free removed nodes.
- `tac_export_yaml_instruction_list` — the spelling the fixed-point check compares.
- `xalloc` / `xfree` — memory for CFG data structures.
- `libutil/string_map` — map from variable name to copy-set entry or liveness bit, used in the dataflow analyses.

### Integration

The optimizer is called from `translator/main.c` after `translate()` returns the TAC for one top-level function and before the binary export step. No changes to the TAC binary format are needed; the optimizer is a pure transformation on the in-memory `Tac_Instruction` list.

### Testing

A new test binary `optimizer-tests` (from `optimizer/optimizer_tests.cpp`) covers:
- Each pass in isolation, with hand-crafted input/output instruction lists.
- The combined pipeline on representative C snippets lowered by the full `parse` → `lower` chain.
- Regression cases: empty function body, function with no optimization opportunities, function that folds to a constant.
