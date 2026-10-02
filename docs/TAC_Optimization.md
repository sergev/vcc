# TAC-Level Optimization

This article describes the four machine-independent optimization passes we plan to implement on the TAC intermediate representation: **constant folding**, **unreachable code elimination**, **copy propagation**, and **dead store elimination**. The approach closely follows Chapter 19 of [Nora Sandler, *Writing a C Compiler*](https://nostarch.com/writing-c-compiler), adapted to the types and conventions of this codebase.

## Why optimize at TAC level?

The TAC lowering phase (`translator/`) is deliberately written for correctness, not speed. It introduces a fresh temporary variable for almost every subexpression and emits copy instructions liberally to keep each lowering rule simple. The result is correct but bloated: many temporaries are used exactly once, many copies are immediately overwritten, and every function body ends with a backstop `Return` that is unreachable when the source code already has a `return` statement.

Optimizing at TAC level is the right place to fix this because:

- TAC is already fully typed and fully lowered — we do not need to worry about C syntax or scoping rules.
- TAC is machine-independent — optimizations written here benefit every backend (RISC-V, BESM-6, and future targets).
- TAC instructions have a simple, uniform structure that makes optimization algorithms easy to express.

Optimizations that transform one function at a time, without knowledge of the rest of the program, are called **intraprocedural** optimizations. The four passes described here are all intraprocedural. Each `TAC_TOPLEVEL_FUNCTION` is processed independently; static variables, function calls, and pointer aliasing are handled conservatively.

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
- **Meet (join at merge points):** intersection — a copy is reaching only if it holds on every incoming path.
- **Transfer function for a single instruction:**
  - **Gen:** if the instruction is `Copy(src, dst)`, add `(src, dst)` to the set.
  - **Kill:** remove every pair `(s, d)` from the set where `s == dst` or `d == dst` (overwriting `dst` invalidates any copy that mentioned it as either operand).

The analysis iterates over the CFG (in forward order) until the reaching-copies sets stop changing. Each block's output is computed from its input by applying the transfer function instruction by instruction.

Once the analysis converges, each use of a variable `x` is replaced by `src` if every reaching copy `(src, x)` agrees on the same `src` at that point.

### Conservatism around aliased variables

Two categories of variables must be treated conservatively:

1. **Observable variables** — anything with static storage duration (file-scope globals, `extern`s, local `static`s). A `FunCall` instruction may call any function, which may read or modify such a variable. At every `FunCall`, all copies involving observable variables are killed from the reaching set.

2. **Address-taken variables**. Any variable that appears as the `src` of a `GetAddress` instruction may be modified through the resulting pointer. At every `Store` or `FunCall`, copies involving such variables are killed.

The optimizer classifies a name *locally*, without consulting the rest of the program: in TAC a local and a global are both bare names, but a name is **observable** exactly when it is neither a temporary (`%0`, `%1`, … — always compiler-generated and private) nor one of the function's parameters or automatic locals. The translator records those names on the function toplevel (`Tac_TopLevel.function.locals`); the no-shadowing rule makes the classification unambiguous program-wide. See `optimize/alias.c`.

### Self-copies

After substitution, some `Copy(x, x)` instructions may appear (the source and destination are the same variable). These are no-ops and are removed immediately.

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

## The optimization pipeline

No single pass is sufficient on its own. The passes form a **virtuous cycle**:

- Constant folding produces constants that copy propagation can substitute into expressions, which constant folding can then evaluate again.
- Constant folding turns conditional jumps into unconditional ones, creating unreachable blocks that unreachable code elimination can remove.
- Copy propagation eliminates the variable in a copy's destination, turning the copy into a dead store that dead store elimination can remove.
- Dead store elimination removes instructions, which may make previously reachable blocks empty, which unreachable code elimination can then clean up.

Because the passes amplify each other, the optimizer runs them in a loop until the instruction list stabilizes.

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

        if flags.copy_propagation:
            cfg = propagate_copies(cfg)

        if flags.dead_store_elim:
            cfg = eliminate_dead_stores(cfg)

        new_body = flatten_cfg(cfg)             // rejoin into flat list

        if new_body == body or new_body is empty:
            return new_body                     // fixed point reached

        body = new_body
```

Equality of instruction lists is tested with `tac_compare_instruction` (declared in `tac/tac.h`). An empty body after optimization is also a termination condition: if the optimizer removes everything, there is nothing left to iterate over.

### Pass ordering

Within one iteration, constant folding runs first on the flat list because it is the only pass that does not need a CFG. The remaining three passes operate on the CFG representation and run in the order shown: unreachable code elimination, copy propagation, dead store elimination. This ordering ensures that each pass can take advantage of what the previous pass produced within the same iteration.

### Types

No pass creates or renames a variable, and copy propagation substitutes only across
a `COPY`, which the translator emits only between types of the same size, so every
operand keeps its width. After the loop, `optimize_prune_locals` drops from the
function's `locals` every name the body no longer mentions, so `params` + `locals`
stay exactly the typed symbols of the optimized body.

### Command-line control

By default all four passes are enabled. Individual passes can be disabled for debugging, except constant folding.
For each pass, a separate CLI option exists in the `lower` binary.
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
- `tac_compare_instruction` — fixed-point check (declared in `tac/tac.h`).
- `xalloc` / `xfree` — memory for CFG data structures.
- `libutil/string_map` — map from variable name to copy-set entry or liveness bit, used in the dataflow analyses.

### Integration

The optimizer is called from `translator/main.c` after `translate()` returns the TAC for one top-level function and before the binary export step. No changes to the TAC binary format are needed; the optimizer is a pure transformation on the in-memory `Tac_Instruction` list.

### Testing

A new test binary `optimizer-tests` (from `optimizer/optimizer_tests.cpp`) covers:
- Each pass in isolation, with hand-crafted input/output instruction lists.
- The combined pipeline on representative C snippets lowered by the full `parse` → `lower` chain.
- Regression cases: empty function body, function with no optimization opportunities, function that folds to a constant.
