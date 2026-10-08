# Plan: `defer` and stackless coroutines, for Braam

Status: a plan, no code. It defines two extensions of C — a `defer` statement and
stackless coroutines — measured against what Braam requires of a process and against
how vcc is built, and lays out the work in phases. §9 records the alternatives that
were considered and rejected.

Five rules drive every decision below:

1. **Stackless.** A coroutine lowers to a frame in linear memory plus a function that
   dispatches on a state number. No separate stack, no stack switching, no Asyncify,
   no JSPI.
2. **No hidden allocation.** The compiler never calls an allocator. Frame storage is
   always supplied by the program.
3. **No customization points.** No promise type, no awaiter protocol, no library type
   the compiler must know about. The language provides suspension and delegation;
   schedulers, I/O and cancellation policy are ordinary C. `await` is defined
   entirely in terms of `yield`, so the language has no notion of an I/O operation or
   of readiness: a yielded value is whatever the runtime says it is.
4. **No undefined behaviour in the new features.** Every misuse is a compile-time
   error or a defined trap (§2.4).
5. **Fixed surface.** A handful of keywords, one type constructor, and a short list
   of operations; nothing else.

## 1. What this is for

A Braam process is a wasm module the kernel *steps*. It has no event loop of its own
and nothing in it may block: a program asks the kernel for something with
`sys_async`, returns out of the step, and is re-entered later with the answer. Braam's
own programs are C++20 coroutines, and that is how a `co_await` on a syscall unwinds
the whole native stack and comes back through `_resume`. The facts, all from
`braam-core` (`doc/Concept.md` §2.1, §4.3; `doc/System_Calls.md` §6, §7.3, §11;
`src/proc/rt.cpp`; `src/kernel/sysabi.h`):

- Imports: `env.memory`, `kernel.sys(op, a0, a1, a2) -> i32` (five synchronous ops:
  Exit, GetPid, Now, Stage, Random) and `kernel.sys_async(op, token, ptr, len)`
  (everything else; the reply comes later). The op word is `op | arg << 8`
  (`sys_op`, `sysabi.h:476`).
- Exports, exactly: `_start(argv_ptr, len) -> i32`, `_resume(token, reply_ptr, len)
  -> i32`, `_alloc(n) -> ptr`, `_free(ptr, n)`, `_sig(n)`. A step returns 0 when the
  process has exited (after `sys(Exit, status)`), 1 while a call is outstanding. A
  reply block is `i32 status` then data, placed by the host through `_alloc` and freed
  by the process. The host writes argv (then the environment) through `_alloc` before
  `_start`.
- A custom section `braam` of five `u32`: magic `0x6d617262`, `PROC_ABI` (21 today),
  flags, `initial_pages`, `max_pages` (1600). `tools/stamp.py` appends it after the
  link; `exec` refuses a binary whose ABI is not the kernel's.
- Link: `--import-memory --initial-memory=N --no-entry --stack-first -z
  stack-size=131072 --gc-sections`. No `memory` export. `test/system/abi.mjs` asserts
  all of the above for every binary.
- Several calls may be outstanding (one per task, `PROC_TASKS` = 8); the reply names
  its token. No preemption: a loop that never parks cannot be interrupted, so every
  interpreter on Braam runs in bursts and parks on `Sleep 0` now and then.
- No Asyncify, no JSPI, no stack switching, no tail calls (`Concept.md` §1, §3.3).

`braam-apps/devel/wasm/examples/cat.s` is the whole model in 70 lines of hand-written
wasm: "a loop turned inside out", `_resume` branching on the token to the code after
each call. That is what the compiler must produce from ordinary-looking C.

So the task is: **a C program compiled by vcc must be able to suspend at a syscall
with its state in linear memory, return out of `_start`/`_resume`, and continue
there.** Stackless coroutines give that; `defer` makes early returns and cancellation
safe for the resources such a program holds. Both are front-end features; the wasm
backend needs one improvement (§6.5); the Braam runtime is new (§7).

Out of scope: coroutines or `defer` on the other targets (the lowering is
target-neutral, but it is enabled only where a runtime exists: wasm32), symmetric
transfer, awaiting anything but a coroutine, exceptions, and vcc running on Braam
itself (that would be a port of vcc's own sources with `await` in front of every
blocking call; nothing here prevents it, and §7.3's choice to keep C's names for the
blocking functions is made with it in mind).

## 2. The surface

Two statements, two expressions, one function specifier, one type, nine operations.
Spellings follow C's convention for added keywords: reserved identifiers in the
compiler, short names from a header, so no existing program changes meaning unless it
includes `<coro.h>`.

| In the compiler | In `<coro.h>` | What |
|---|---|---|
| `_Coro(Y)` | `coro(Y)` | function specifier: a coroutine yielding `Y` |
| `_Yield expr;` / `_Yield;` | `yield` | suspend, handing `Y` to the resumer; an expression of type `co_signal` |
| `_Await e` | `await` | run a coroutine to completion, forwarding its suspensions |
| `_Defer stmt` | `defer` | run `stmt` when the enclosing scope is left |
| `_Coro_frame(Y, T)` | `co_frame(Y, T)` | the frame of a coroutine yielding `Y` and returning `T`: an incomplete type, only pointers to it exist |
| `__co_init`, `__co_resume`, `__co_cancel`, `__co_destroy`, `__co_done`, `__co_value`, `__co_result`, `__co_sizeof`, `__co_alignof` | `co_init` … `co_alignof` | the operations, keywords with call syntax like `__builtin_va_class` |

```c
typedef enum { CO_SUSPENDED, CO_DONE } co_status;
typedef enum { CO_CONTINUE, CO_CANCEL } co_signal;
```

### 2.1 Coroutines

```c
coro(int) void range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
        if (yield i == CO_CANCEL)
            return;
}

void print_range(void)
{
    static _Alignas(16) char storage[256];
    co_frame(int, void) *f = co_init(storage, sizeof storage, range, 0, 10);
    while (co_resume(f) == CO_SUSPENDED)
        print_int(co_value(f));
}
```

- `coro(Y) T f(params)` declares a coroutine. It may not be variadic, `inline`, or
  K&R; it does not convert to a function pointer (§2.4 has the indirect form). Its
  prototype and definition must agree on `Y` as on everything else.
- **Frames.** A frame is an object in storage the caller supplies, address-stable for
  its life. `co_init(storage, bytes, f, args...)` builds `f`'s frame at the front of
  `storage`, copies the arguments in, runs no body code, and returns a
  `co_frame(Y, T) *` for `f`'s `Y` and `T`. The rest of the storage, from the frame's
  end to `storage + bytes`, is the **arena** from which the frames of everything `f`
  awaits are taken (§2.2). A block of exactly `co_sizeof(f)` bytes is a frame for a
  coroutine that awaits nothing.
- **Sizes are run-time values**, not constant expressions: `co_sizeof(f)` and
  `co_alignof(f)` read `f`'s descriptor, a word of data the defining unit emits
  (§6.3). This is the price of separate compilation — a program must await the
  libc's coroutines without seeing their bodies — and of laying frames out after the
  optimizer has run. §9 argues it. `_Alignas(16)` and `co_sizeof(f) <= bytes` are the
  caller's to arrange; `co_init` traps otherwise.
- `co_resume(p)` runs to the next suspension or to the return; `co_cancel(p)` does the
  same, delivering `CO_CANCEL` at the suspension point; `co_destroy(p)` does not resume
  user code at all: it runs the `defer`s active at the suspension point, innermost
  first, finishes the frame (§2.3) and returns `CO_DONE`. All three trap on a frame
  that has finished or been destroyed, so a program that may hold one tests
  `co_done(p)` before `co_destroy(p)`. `co_done(p)` is true once the body has
  returned or the frame was destroyed. `co_value(p)` is the last yielded value, type
  `Y`; `co_result(p)` the return value, type `T`.
- Coroutines start suspended; `co_init` runs nothing.

### 2.2 Suspending and delegating

`yield e` stores `e` as the frame's value, suspends, and when resumed evaluates to
`CO_CONTINUE` or `CO_CANCEL` according to how it was resumed. `yield;` in a
`coro(void)`. The result may be ignored; a coroutine that never tests it cannot be
cancelled at that point and runs on to the next test. Cancellation is cooperative and
visible in the source; there is no unwinding, because C has no exceptions and an
unwind path for cancellation would add a second control-flow mechanism.

`await` has two forms:

```c
ssize_t n = await read_exact(fd, p, len);   // arena form: the common one
co_frame(io_req *, ssize_t) *sub = co_init(buf, sizeof buf, read_exact, fd, p, len);
ssize_t n = await sub;                      // explicit-frame form
```

Both mean: resume the sub-coroutine; whenever it suspends, suspend too, forwarding
its yielded value to our own resumer unchanged, and forward the signal we are resumed
with back into it; when it finishes, the expression's value is its result. The
callee's `Y` must be the awaiter's `Y` exactly (`coro(void)` chains are fine), so
every coroutine in one chain speaks the same protocol to the runtime. `await sub` is
exactly

```c
co_signal sig = CO_CONTINUE;
while ((sig == CO_CANCEL ? co_cancel(sub) : co_resume(sub)) == CO_SUSPENDED)
    sig = yield co_value(sub);
co_result(sub)
```

and that equivalence is the specification, not an illustration of it. A worked
example of delegation, where the runtime's request type is the program's own:

```c
typedef struct { int fd; int op; void *buf; size_t len; ssize_t out; } io_req;

coro(io_req *) ssize_t read_exact(int fd, char *p, size_t n)
{
    size_t got = 0;
    while (got < n) {
        io_req r = { .fd = fd, .op = OP_READ, .buf = p + got, .len = n - got };
        if ((yield &r) == CO_CANCEL)
            return -1;
        if (r.out <= 0)
            return got;
        got += r.out;
    }
    return got;
}

coro(io_req *) int read_header(int fd, struct header *h)
{
    ssize_t n = await read_exact(fd, (char *)h, sizeof *h);
    return n == sizeof *h ? 0 : -1;
}
```

`r` is a local whose address is yielded. Because a frame is address-stable, that is
well defined for as long as the coroutine is suspended at that point — precisely the
window in which the scheduler uses the request.

The arena form takes the callee's frame from the *task's* arena — the storage block
of the root frame the awaiter belongs to — as a stack: pushed at the `await`, popped
when it completes. An `await` chain is strictly LIFO, so one bump pointer per task
serves every level, recursion included, and no frame size has to be known at compile
time. A chain that does not fit traps (`CO_TRAP_NO_SPACE`); the root's owner chooses
the block size, and a trap names the coroutine it could not start. Nothing calls an
allocator.

A suspension may appear anywhere an expression may: `x = g() + await f();` is legal.
What is live across it is the compiler's problem (§6.2), not the programmer's. A
suspension inside a `defer` body is an error.

### 2.3 `defer`

```c
int copy(const char *from, const char *to)
{
    int in = await open(from, O_RDONLY);
    if (in < 0)
        return -1;
    defer await close(in);          // not allowed: a defer may not suspend (see below)
    ...
}
```

The rules:

- `defer stmt` schedules `stmt` to run when the lexical scope containing the `defer`
  is left: by falling off its end, `return`, `break`, `continue`, or a `goto` out of
  it. Several in one scope run in reverse order; each scope has its own list. A
  `defer` is activated when control passes it, and `exit`, `longjmp` (which wasm32 has
  not got) and traps run none. The scopes are C11's blocks (§6.8p3, §6.8.4p3,
  §6.8.5p5): a compound statement, a selection or iteration statement as a whole,
  and each of their substatements, braced or not. So `if (x) defer f();` runs `f()`
  at once, since the `if` body ends there, and a `defer` that is the unbraced body
  of a loop runs at the end of each iteration.
- `stmt` may be any statement, a compound one included, but control may not leave it
  except by completing it: no `return`, no `break`/`continue` that would leave it, no
  `goto` out, and **no `yield` or `await` inside it.** A suspension inside cleanup
  would make the cleanup a coroutine of its own: the scope would be half exited and
  the coroutine resumed somewhere inside its own cleanup. So a resource released by a syscall (a Braam file
  descriptor) is not released by `defer close(fd)` but by the program at the point it
  chooses; `defer` serves the memory, locks, counters and buffers that make up the
  rest.
- **A jump may not enter a scope past a `defer` in it.** `goto L` where `L` lies after
  a `defer` of the same scope, and a `case`/`default` label after a `defer` in the
  switch's block, are compile-time errors. This keeps the set of active `defer`s a
  static fact at every point, which is what lets the compiler emit the cleanup
  inline with no run-time bookkeeping, and what makes `co_destroy` possible. A
  `defer` inside a `case` needs braces, which also makes its scope the case rather
  than the whole switch.
- In a coroutine, suspension is not scope exit: the `defer`s stay active across a
  `yield`. They run when the scope is left on resumption, or on `co_destroy`.

Deferred statements see the variables' values at the time they run, as any statement
does.

### 2.4 Defined misuse

Compile-time errors: `yield`/`await` outside a coroutine or inside a `defer`;
`yield expr` in a `coro(void)` and bare `yield` elsewhere; `await` of a different `Y`;
`co_value` on a `co_frame(void, T)`, `co_result` on a `co_frame(Y, void)`; a coroutine
that is variadic, `inline`, or K&R; its name used as a value other than in `co_init`,
`co_sizeof`, `co_alignof` or an arena `await`; a jump past a `defer`; control leaving a
`defer` body; `main` as a coroutine outside Braam mode.

Traps, in every build: `co_init` on storage too small or misaligned
(`CO_TRAP_STORAGE`); resuming a frame that is running (`CO_TRAP_REENTRANT`) or
finished (`CO_TRAP_FINISHED`); `co_value` when the last status was not `CO_SUSPENDED`
(`CO_TRAP_NO_VALUE`); `co_result` before `CO_DONE` or after `co_destroy`
(`CO_TRAP_NOT_DONE`); falling off the end of a non-void coroutine
(`CO_TRAP_NO_RETURN`); an arena `await` that does not fit (`CO_TRAP_NO_SPACE`). A trap
is `__co_trap(code)`: under node it prints the name and exits 255; on Braam it is
`unreachable`, which the kernel reports as a crash.

Not covered: letting storage go out of scope while its frame is suspended; that
leaks whatever the coroutine owned, and the language cannot detect it without
ownership tracking. `co_destroy` is the tool, and a later lint can flag the pattern.

A function pointer to a coroutine (`coro_ptr(Y, T)`, a pointer plus the coroutine's
descriptor, so a scheduler can hold a heterogeneous list of tasks) is deferred to
phase 9: Braam's runtime does not need it (`main` is known), and its init thunk needs
a uniform argument list, which the plan proposes as "one `void *`" there.

## 3. Where each part lives

```
scanner     four keywords, nine builtins, _Coro_frame        (scanner.c keyword table)
parser      specifier, statements, expressions, the type     (decl.c, stmt.c, expr.c)
ast         STMT_DEFER, EXPR_YIELD/AWAIT/CO_*, FUNC_SPEC_CORO, the frame type
semantic    coroutine-ness on the symbol, Y/T types, every compile-time rule
translator  defer as inline cleanup on every exit edge; a coroutine body as an
            ordinary TAC function over an opaque suspension call; co_* as loads,
            stores and calls against a fixed header layout
optimizer   unchanged: the suspension is a call it may not move or drop
coro split  after the optimizer: liveness, frame layout, spills, dispatch, f$init,
            the descriptor                                   (translator/coro.c)
genwasm     one change: a dispatch loop around an irreducible region only, not
            around the whole function                        (structure.c)
runtime     libc/wasm32: co.c (push/pop/trap); libc/wasm32/braam: crt0, syscalls,
            allocator, headers, a fake kernel for node
driver      target wasm32-braam: link line, the braam section
```

The lowering is in shared code and target-neutral; `Target.coroutines` (set for
wasm32) gates it, and `lower -t besm6` of a program with `_Coro` says "coroutines are
not supported on this target". `defer` has no target dependency and is not gated;
BESM-6 output is unchanged because no BESM-6 program uses it.

## 4. Front end

### 4.1 Scanner and parser

- Tokens in `scanner/scanner.h`, rows in the sorted `keywords[]` table
  (`scanner/scanner.c:159-206`, strcmp order, `_` after `Z`), names in
  `token_name`. `cpp` needs nothing: it does not know keywords. It predefines
  `__vcc_coroutines__` for wasm32 so headers can feature-test.
- `_Coro(Y)`: a function specifier with an argument, parsed in
  `parse_declaration_specifiers` beside `_Alignas` (`parser/decl.c:488-537`);
  `parse_function_specifier` (`:1018`) becomes a switch; `FunctionSpec` gains
  `FUNC_SPEC_CORO` with a `Type *yield_type`. Both "is this a declaration" tests
  (`parser/stmt.c:137-140`, `:260-263`) and `decl.c:513` learn the token.
- `_Defer stmt`: `STMT_DEFER{body}` in `parse_statement`.
- `_Yield [expr]`, `_Await expr`: unary-level expressions, `EXPR_YIELD{expr?}` and
  `EXPR_AWAIT{expr}`, in `parse_unary_expression` (`parser/expr.c:656`). `_Await`
  binds like `sizeof`: `await f(x) + 1` is `(await f(x)) + 1`. `if (yield i ==
  CO_CANCEL)` would parse as `yield (i == CO_CANCEL)` under those rules, which is
  never what is meant. Decision: `_Yield` takes an *assignment-expression*
  and binds tighter than `==`, so `yield i == CO_CANCEL` means `(yield i) ==
  CO_CANCEL`; the grammar note records it, and `<coro.h>` examples write the
  parentheses anyway.
- The nine operations: one `EXPR_CO_OP{op, args}` kind with an enum, argument counts
  checked in the parser, like `__builtin_va_class` (`expr.c:708`). `co_init`'s third
  argument and `co_sizeof`'s only one are identifiers naming a coroutine; they are
  parsed as expressions and judged in semantic.
- `_Coro_frame(Y, T)`: a type specifier (`TYPE_SPEC_CORO_FRAME` with two type names)
  in `parse_type_specifier` and `is_type_specifier`; `fuse_type_specifiers` makes it a
  `TYPE_STRUCT` with a canonical tag spelled from `Y` and `T` (`__co_frame(int,void)`),
  never defined, so it is incomplete and two spellings of the same `Y`, `T` are one
  type by the existing tag rule. No new `TypeKind`, so the Type switches in
  ast/semantic/translator are untouched.
- New AST kinds go through the usual files: `ast.asdl`, `ast.h`, `ast_alloc.c`,
  `ast_free.c`, `ast_clone.c`, `ast_compare.c`, `ast_export.c`, `ast_import.c` (the
  `tag > TAG_STMT + STMT_DEFAULT` and `TAG_EXPR + EXPR_VA_CLASS` range checks move),
  `ast_yaml.c`, `ast_print.c`, `ast_graphviz.c`, the `.dot` diagrams; tests in
  `ast/test/clone_tests.cpp` and `parser/test/serialize_tests.cpp`. The binary AST has
  no version word; `parse` and `lower` are rebuilt together, as always.
- `grammar/c11.y`, `c11.l`, `c11.asdl` and `docs/C_Grammar.md` get the extension in a
  marked section.

### 4.2 Semantic

- **Coroutine-ness on the symbol.** `Symbol.u.func` gains `coro` and `yield_type`,
  beside `noret` (`semantic/symtab.h:40-44`); `symtab_add_fun` checks a redeclaration
  agrees. The C function type stays `T f(params)`, so argument checking is unchanged.
- **Context.** A `current_coro` like `current_switch`: set by `typecheck_fn_decl`
  around the body with `Y` and `T`; `yield`/`await` check against it; `return` in a
  coroutine checks `T` as now. `in_defer` depth for the `defer` rules.
- **`defer`.** `typecheck_statement` checks the body with `in_defer` raised;
  `label_loops` pushes a barrier frame so `break`/`continue` inside the body resolve
  only to loops inside it ("break statement not inside loop or switch" otherwise), and
  `return`/`yield`/`await`/`goto`-out are refused. A new pass or an extension of
  `resolve_labels.c` records, per label and per `case`, the scope path and whether a
  `defer` precedes it in its scope, and rejects the jumps §2.3 forbids. `Stmt` has no
  source location, so the message names the label and the function.
- **Types.** `yield e` coerces `e` to `Y` (as `return` does to `T`) and has type
  `co_signal` (an `int` enum declared by the compiler, as `size_t` is by the headers:
  `<coro.h>` declares both enums, and the compiler types `yield` as `int`). `await e`:
  `e` is a `co_frame(Y', T') *` with `Y' == Y`, result `T'`; or a call expression whose
  callee is a coroutine symbol, checked as an ordinary call plus the `Y` rule.
  `co_init(storage, bytes, f, args...)`: `void *`, `size_t`, a coroutine symbol, then
  the arguments checked against `f`'s parameters as a call; result `co_frame(Y, T) *`
  for `f`'s types. `co_resume`/`co_cancel`/`co_destroy` → `co_status` (`int`),
  `co_done` → `int`, `co_value` → `Y`, `co_result` → `T`, `co_sizeof`/`co_alignof` →
  `size_t`, not constant (`is_constant_expression` says no).
- A coroutine symbol decays nowhere else. `main` may be a coroutine only when
  `target_config->braam` is set (§7).
- `eval_const` and `const_convert.c` never see the new expressions: none is a constant
  expression.

Tests: `parser/test/negative_tests.cpp` and `semantic/test/*` with `EXPECT_DEATH`, as
the existing negative tests do; positive parses in YAML.

## 5. Lowering `defer`

In the translator only: cleanup is emitted inline on every edge that leaves a scope,
with no run-time stack, so there is no library dependency, no allocation, no hidden
state, and the optimizer sees all of it. The rule the lowering implements: every
control-flow edge leaving one or more lexical scopes passes through the cleanup of
those scopes, innermost first.

- `TacCtx` (`translator/translate.h:21-34`; it is initialised positionally at
  `translate.c:920`) gains a scope stack: each block of §2.3 pushes an entry holding
  the list of `Stmt *` deferred so far — a compound statement, `for`'s own scope,
  and every substatement of `if`, `switch`, `while`, `do` and `for`, braced or not
  (an unbraced one is a block of its own in C11, so `if (x) defer f();` lowers to
  `f()` right there); `gen_stmt`'s `STMT_DEFER` appends to the top entry and emits
  nothing.
- `emit_scope_exits(ctx, down_to)` lowers, with `gen_stmt`, the deferred statements
  of every scope from the innermost to `down_to`, each scope's in reverse order. It
  is called: at the end of a compound statement for its own scope (only when the end
  is reachable — `stmt_falls_through` already exists); before `RETURN` for every
  scope; before `break`/`continue` for the scopes down to the loop's or switch's
  body; before `goto` for the scopes down to the common ancestor of the `goto` and
  its label. The label's scope path comes from the semantic pass (§4.2), kept on the
  AST like `branch_target_label` is.
- The deferred statement is lowered once per exit edge. That duplicates code in a
  scope with many exits; a later version can emit one cleanup block per scope with a
  "where next" temporary, the way C++ compilers share landing pads. The optimizer
  sees plain code either way, and a `defer` whose scope has one exit costs nothing
  extra.
- Lowering a deferred statement at several points re-walks the same AST subtree;
  `gen_stmt` is already re-entrant (loop bodies are walked once, but string
  constants and temporaries are minted per emission, which is what we want).
- Static locals inside a deferred compound statement: the same `StaticLocalRec` is
  emitted once (it is keyed by the declaration), and every copy of the code names
  it.

Tests: translator YAML goldens (`translator/test/defer_tests.cpp`: fall-off, return,
break, continue, goto out across two scopes, nested LIFO, a loop body's defer per
iteration, a `defer` in a `for` init scope), negative tests for the forbidden jumps,
and wasm32 run tests (`backend/wasm/test/defer_tests.cpp`: the order printed).

## 6. Lowering coroutines

### 6.1 Shape of the result

`coro(Y) T f(A a, B b)` becomes three toplevels in its unit:

```
int   f$resume(void *fp)            the body as a state machine; returns co_status
void  f$init(void *fp, A a, B b)    stores the arguments, state = 0
const struct __co_desc f$co = { size, align, f$resume }       .rodata, global
```

and the frame it runs in, laid out by the compiler (the *header* is a fixed ABI; the
rest is the unit's business):

```
 0  u32   state      0 created; k ≥ 1 suspended at point k; DONE; DESTROYED
 4  u32   flags      bit 0 RUNNING, bit 1 CANCEL, bit 2 DESTROY (the pending signal)
 8  ptr   resume     f$resume, so a co_frame(Y, T) * is self-describing
12  ptr   task       the root frame of this task: whose arena to push on
16  u32   top        arena bump pointer      (meaningful in a root)
20  u32   limit      arena end               (meaningful in a root)
24  Y     value      absent when Y is void; aligned to Y
    T     result     absent when T is void; aligned to T
    A a, B b         the parameters
    ...              locals and temporaries live across a suspension,
                     and every address-taken object of the body
```

Offsets of `value` and `result` depend only on `Y` and `T`, so a holder of a
`co_frame(Y, T) *` (a scheduler, the `await` expansion, `co_value`) reads them with
no knowledge of `f`. Alignment is the maximum over the members, at least 4, clamped
to 16 as the shadow stack's is. The frame has no pointer into itself that the
compiler plants, so a frame may be *moved* while suspended if the program knows
nothing in it was address-taken — but the language promises nothing, and the
`await` arena never moves anything.

`co_resume(p)`, `co_cancel(p)`, `co_destroy(p)` are one runtime routine,
`__co_resume(p, signal)`, inline-able later: it traps on DONE/DESTROYED and on
RUNNING, sets RUNNING and the signal bits, and calls `p->resume(p)` through
`call_indirect`. (Through a `co_frame(Y, T) *` the callee is not statically known; a
direct call when `p` is the result of a visible `co_init` is a later peephole, not a
requirement.) `co_init(storage, bytes, f, args)` is `__co_setup(storage, bytes,
&f$co)` — size and alignment checks, header zeroed, `task = storage`, `top = storage
+ size`, `limit = storage + bytes` — then `f$init(storage, args)`.

### 6.2 The two-stage translation

The optimizer must see a coroutine body as ordinary code, and the frame must be laid
out from *optimized* code, or every temporary the source mentions ends up in it. So
the translator produces a **provisional** function, the optimizer runs, and a **split
pass** finishes the job — LLVM's CoroSplit, scaled to this compiler.

**Stage 1, in the translator** (`translate_fn` with a coroutine symbol):

- The TAC function is `f$resume` with params `(%fp, %a, %b)`: the frame pointer, then
  the user's parameters *as ordinary TAC parameters* — the split pass moves them.
  Its return type is `int`.
- Prologue: nothing yet. The body is lowered by `gen_stmt` as for any function.
- `yield e` → store `e` at `fp + value_off` (an aggregate by `gen_aggregate_copy`),
  then `%s = FUN_CALL __co_suspend(%fp)`, then the destroy branch: `JUMP_IF_ZERO (%s
  == 2) over; <emit_scope_exits for every scope>; store DESTROYED to state; RETURN
  CO_DONE; over:` and the expression's value is `%s` (0 or 1). `__co_suspend` is a
  declared extern `int (void *)` in the provisional function so the verifier is
  content; the optimizer treats it as an opaque call: never dead, kills every memory
  fact, stays where it is. Nothing in `optimize/` changes.
- `await` → the loop of §2.2, in TAC: for the arena form `%sub = FUN_CALL
  __co_push(task, &f$co)` (reads size and align from the descriptor, traps on no
  space) then `FUN_CALL f$init(%sub, args)`; for the explicit form `%sub` is the
  operand. Then `L: %st = FUN_CALL __co_resume(%sub, %sig); JUMP_IF_NOT_ZERO (%st ==
  CO_DONE) done; copy sub->value to fp->value; %sig = __co_suspend(%fp); JUMP_IF
  (%sig == 2) destroy; JUMP L; destroy: __co_resume(%sub, DESTROY); __co_pop; <scope
  exits>; RETURN; done: result from sub->result; __co_pop(task, %sub)` (arena form
  only). The forwarding of CANCEL is `%sig` itself: the next `__co_resume(%sub,
  %sig)` delivers it.
- `return e` → `defer`s (§5), store `e` at `result_off`, state = DONE, clear RUNNING,
  `RETURN CO_DONE`. The end of a void body is the same without the store; the end
  of a non-void body is `FUN_CALL_NORETURN __co_trap(CO_TRAP_NO_RETURN)`.
- `co_*` operations → loads and stores against the header and calls of `__co_*`
  (`libc/wasm32/co.c`, ordinary C compiled by us, ~150 lines). `co_sizeof(f)` is a
  `LOAD` of `f$co`'s first word; the unit gets an `EXTERN f$co` when `f` is not
  defined in it, through `tac_record_extern`, and an `EXTERN f$init` likewise.
- Frame-resident storage: every `ALLOCATE_LOCAL` of the body and every
  `GET_ADDRESS` target stays as it is at this stage; the split pass moves them.

**Stage 2, the split pass** (`translator/coro.c`, called from `translate()` after
`optimize_function`, then `optimize_function` once more and `verify_function`):

1. Liveness over the TAC body, backward, per name — `optimize/dead_store.c` has
   exactly this analysis (`:380-440`); it is factored into `optimize/liveness.c` and
   shared. The set to spill is the union over suspension points of the names live
   after `__co_suspend` (what the resumed code reads), plus every `ALLOCATE_LOCAL`
   name and every `GET_ADDRESS` source (an address may be held across a suspension
   through memory the analysis cannot see; and the shadow stack, where the backend
   would put them, is unwound at every return), plus the user parameters (the only
   channel from `f$init` to the body).
2. Layout: header, parameters in order, then the spilled names by alignment, then
   the aggregates. Record `size` and `align`.
3. Rewrite: a spilled scalar name's every use becomes `LOAD` from `fp + off` into a
   fresh temporary just before the instruction, every definition a `STORE` just
   after; an aggregate's `ALLOCATE_LOCAL` goes and its `GET_ADDRESS` becomes `ADD_PTR
   fp, off`. The second optimizer round forwards the stores to the loads inside a
   block (`cse.c`'s store-to-load forwarding does it for a constant address off a
   parameter) and prunes the dead names.
4. Suspension points: the k-th `__co_suspend` becomes `STORE k → state; flags &=
   ~RUNNING; RETURN CO_SUSPENDED; LABEL %co_k; %s = LOAD flags; %s = %s >> 1` (the
   signal bits, 0/1/2). The dispatch goes in front of the body: `%st = LOAD state;
   JUMP_IF (%st == 1) %co_1; … ` — a compare chain, as `switch` lowers today; a
   `JUMP_TABLE` instruction is a later improvement if coroutines with many points
   turn up. State 0 falls into the body's first instruction; `flags |= RUNNING` is
   the runtime's, before the call.
5. Parameters: dropped from `f$resume`'s list (they are frame members now);
   `f$init(%fp, %a, %b)` is synthesized as a function toplevel storing each at its
   offset, with `state = 0`; the descriptor `f$co` is a `STATIC_CONSTANT` toplevel
   with `{size, align, f$resume}` — a function address in static data is already
   supported (`backend/wasm/data.c:86-93`, a table-index relocation). Both are
   appended to the chain `translate_fn` returns, so `emit_unit_begin` lists their
   `.functype`s.
6. `verify_function` sees ordinary TAC. `tac_verify` gains nothing; `fp` is a `char
   *` and the frame accesses are `ADD_PTR` + `LOAD`/`STORE`.

Why the body is correct across the two optimizer rounds: before the split, no name
crosses a suspension in the optimizer's eyes (the suspension is a call, and a call
returns to the same point); after the split, the only names that cross a `RETURN`
are in memory. `dead_store` would otherwise delete the last store to a local before
a `RETURN`, which is exactly the failure mode the agent's survey flagged
(`dead_store.c:431-436`); step 3 is what prevents it.

### 6.3 What the wasm backend sees

- `f$resume` is a function with one `i32` parameter whose CFG has edges from the
  entry into the middle of loops. Ramsey's translation needs a reducible graph, so
  today's `structure.c` would fall back to the whole-function dispatch skeleton for
  any coroutine with a `yield` inside a loop — correct, and slower than it should be.
  §6.5.
- Frame accesses are `i32.load off`/`i32.store off` on the parameter: the constant
  addends fold into memarg offsets (`peephole.c`); a `local.get 0` precedes each,
  and the stackify pass keeps them off locals.
- `f$resume` itself has no shadow-stack frame (every address-taken object moved to
  the coroutine frame), only the calls' area; it is a leaf in `frame.c`'s terms unless
  it awaits.
- `call_indirect (i32) -> (i32)` for `__co_resume`'s dispatch is an indirect
  `FUN_CALL` the backend already handles; `f$resume` gets a table slot because
  `f$co` takes its address.
- Nothing else. The backend does not know what a coroutine is.

### 6.4 Runtime: `libc/wasm32/co.c`

`__co_setup`, `__co_push`, `__co_pop`, `__co_resume`, `__co_trap`; the header struct
in `libc/wasm32/include/coro.h` under `__co_` names, with the user-facing macros and
enums beside them. Compiled by `wasm32_compile_libc_c`, in `libc.a`, so a program
that uses no coroutine carries nothing (`--gc-sections`). `__co_trap` prints the
trap's name through `putbyte` under node (there is a host to print to) and is
`unreachable` on Braam.

### 6.5 Structured control flow for irreducible regions

`backend/wasm/structure.c` chooses between Ramsey's translation for the whole
function and the dispatch skeleton for the whole function. The coroutine dispatch
makes every loop containing a suspension point multi-entry, so coroutines would
always get the skeleton. The change, which also improves Duff's device and `goto`
into a loop: find the irreducible strongly connected regions (the retreating edges
whose target does not dominate the source, and the SCC each lies in); give each such
region one *dispatch header* — a `loop` with a `br_table` over the region's entry
blocks on a `state` local, as LLVM's `FixIrreducibleControlFlow` does; route every
edge into an entry block through it; then Ramsey's translation runs over the graph
with the region collapsed to that header, and inside the region the blocks that are
not entries keep their structure. The whole-function skeleton stays for
`--no-structure` and as the fallback for a region the new code declines.

Measure on a generator with a loop, a `read_exact`-style awaiter and the Braam `cat`
of §7: code-section bytes and `bench_wasm.sh`, before and after.

## 7. Braam: target, runtime and libc

### 7.1 The target `wasm32-braam`

A second entry in `cc.c`'s target table sharing `vgenwasm`, as `x86_64-linux` shares
`vgenx86`: `lower -t wasm32` (same data model; the semantic descriptor gains
`coroutines = 1` for wasm32 and a `braam` bit the driver sets through a `lower`
flag `--braam`, which allows a coroutine `main`), `cpp -t wasm32-braam` adds
`__braam__`, headers and libraries from `share/vcc/wasm32-braam/`, and the link:

```
wasm-ld --no-entry --import-memory --initial-memory=<pages*65536> --stack-first
        -z stack-size=131072 --gc-sections -o out <lib>/crt0.o objs -lc
```

then the `braam` section, appended by the driver itself (a 40-line LEB128 writer in
`cc/cc.c`: magic, `BRAAM_PROC_ABI`, 0, the same page count, 1600) — `stamp.py`'s own
argument for a post-link step is that only the linker's caller knows the page count,
and here that is `vcc`. `--initial-pages=N` on the command line overrides the
default (whatever `BRAAM_BIN_INITIAL_PAGES` is in braam-core's
`cmake/BraamProgram.cmake` at the time). The exports come from `.export_name`
directives in `crt0.S`, the imports from `.import_module`/`.import_name` as
`console.s` does now. `BRAAM_PROC_ABI` is one constant in `libc/wasm32/braam/sysabi.h`,
with the op numbers (`BRAAM_SYS_WRITE 16`, …) transcribed from `sysabi.h`; a ctest
compares both against `../Braam/braam-core/src/kernel/sysabi.h` when that tree is
present, so a drift is a failing test rather than a stale binary.

### 7.2 The process runtime: `libc/wasm32/braam/crt0.c`

Written in C, with coroutines, as `src/proc/rt.cpp` is written in C++ with them:

```c
typedef struct braam_call {         /* one syscall, from the request to the answer */
    unsigned op, token;
    const void *ptr; unsigned len;  /* the request */
    void *reply; unsigned reply_len;/* the block the host placed; status is its first word */
} braam_call;

coro(braam_call *) int __braam_root(int argc, char **argv)
{
    int status = await main(argc, argv);
    await fflush(stdout);           /* what the program left buffered, then exit */
    return status;
}

int _start(unsigned argv_ptr, unsigned len)       /* .export_name _start */
{
    heap_init(); build argc/argv from the blob (it stays: argv points into it);
    root = co_init(task_storage, sizeof task_storage, __braam_root, argc, argv);
    return step();
}

static int step(void)
{
    if (co_resume(root) == CO_DONE) { sys(BRAAM_SYS_EXIT, co_result(root), 0, 0); return 0; }
    braam_call *c = co_value(root);
    c->token = ++token;  pending = c;
    sys_async(c->op, c->token, (unsigned)c->ptr, c->len);
    return 1;
}

int _resume(unsigned token, unsigned reply, unsigned len)
{
    if (!pending || pending->token != token) { free((void *)reply); return exited ? 0 : 1; }
    pending->reply = (void *)reply;  pending->reply_len = len;  pending = 0;
    return step();
}
```

- `task_storage` is a static block (64 KiB to start; `__braam_task_bytes` a weak
  size the program may define) holding the root frame and the arena for everything
  `main` awaits. A trap on `CO_TRAP_NO_SPACE` names the coroutine.
- `_alloc`/`_free` are `malloc`/`free`. The present `libc/wasm32/malloc.c` is a bump
  allocator; Braam frees every reply block, so `libc/wasm32/braam/malloc.c` is a
  first-fit free list over `memory.grow`, ~120 lines (the other targets keep theirs).
- `_sig(n)` records a bit; `sig_catch`/`sig_take` as in `rt.h`.
- Several tasks (`PROC_TASKS`): a table of roots and pending calls, `braam_spawn(f,
  arg)` creating a second root in its own static block, `_resume` searching the
  table by token. Phase 8; `chat` is the only Braam program that needs it.
- `exit(n)` from inside the program: there is no unwinding, so, as in Braam's own
  compat layer (`doc/Compat.md`: "C `exit()` and `abort()` trap"), `exit` records the
  status with `sys(Exit)` and traps; the kernel reports a crash. A program ends by
  returning from `main`. Documented, with the reason.
- The step cannot be interrupted: a compute loop with no `await` in it never sees
  `^C`. `braam_yield()` — `await sleep_ms(0)` — is the program's way to park, as
  every interpreter on Braam does once per burst.

### 7.3 The libc for Braam

Group A of Braam's own classification (`braam-apps/CLAUDE.md`, "Three groups") is
what `libc/common` already is: `mem*`, `str*`, `ctype`, the printf engine, `malloc`.
Group B blocks, and here it keeps C's names and signatures with a coroutine
specifier, so a port is `await` in front of the calls and `coro(braam_call *)` on
the functions that make them, and nothing else changes:

```c
coro(braam_call *) ssize_t read(int fd, void *buf, size_t n);
coro(braam_call *) ssize_t write(int fd, const void *buf, size_t n);
coro(braam_call *) int open(const char *path, int flags);
coro(braam_call *) int close(int fd);
coro(braam_call *) int sleep_ms(unsigned ms);
coro(braam_call *) int fflush(FILE *);
coro(braam_call *) int fgetc(FILE *);   coro(braam_call *) char *fgets(char *, int, FILE *);
coro(braam_call *) int stat(const char *, struct stat *);   …
```

All of them are `await braam_sys(op, payload, len)` — the one primitive, which
yields its `braam_call` (a local whose address is yielded, as `read_exact` does in
§2.2) and, resumed, reads the reply's
status and data and frees the block — plus a copy into the caller's buffer.

`printf` and friends cannot be coroutines (variadic): they format into the stdout
buffer, as Braam's `b_printf` and C4's driver do. The buffer is written out by
`await fflush(stdout)`, by any libc coroutine that reads (`fgetc` on stdin flushes
stdout first, as a line-buffered terminal does), and by `__braam_root` at the end.
A full buffer grows (`realloc`) rather than blocks.

Headers in `libc/wasm32/braam/include/`, first on the search path:
`braam.h` (`braam_call`, `braam_sys`, the op numbers, `braam_spawn`, `braam_yield`,
signals), `unistd.h`, `fcntl.h`, `sys/stat.h`, a `stdio.h` whose blocking half is
coroutines and whose formatting half is `libc/common`'s, `stdlib.h` with `exit`'s
note. `coro.h` is shared with plain wasm32 (`libc/wasm32/include/coro.h`).

### 7.4 Running without Braam: `libc/wasm32/braam/run.mjs`

A fake kernel for node, so the unit tests need no Braam checkout: it instantiates
the module with `env.memory` of the stamp's page counts, puts argv through `_alloc`,
calls `_start`, and serves `kernel.sys` (Exit, GetPid, Now, Random) and
`kernel.sys_async` for Write (fd 1 and 2 to stdout and stderr), Read (fd 0 from
stdin, in 512-byte chunks), Open/Close/Read/Write/Stat on files under the current
directory, Sleep (`setTimeout`), and Poll with one descriptor. Each reply is
delivered through `_resume` from a later macrotask — never from inside
`sys_async` — so a program that forgets to return from the step is caught. It
checks the import and export lists and the `braam` section exactly as
`test/system/abi.mjs` does, prints `[exit N]` on stderr, and returns 255 on a trap.
About 200 lines.

### 7.5 Running on Braam

An optional ctest, on when `-DBRAAM_CORE=<path to a built braam-core>` is given:
plants `hello`, `cat`, `wc` built by `vcc -t wasm32-braam` into a session of the
SDK's harness (`test/system/harness.mjs`: `store.files.set("/bin/<name>", bytes)`,
`submit("<name> ...")`, read the screen), the way `braam-apps/devel/c4/test/run.mjs`
does, and runs `test/system/abi.mjs` over them. Skipped otherwise, like every run
test without its tools. A `braam_add_package`-style recipe for a `.zip` is a
documentation item, not code here.

## 8. Phases

Each phase ends green on `ctest -j8 -R 'wasm|translat|parser|semantic|ast'` (the
whole suite after any shared-code change), with a commit. Per step, only the tests of
the part touched. Goldens of the wasm backend stay under `NaiveSelection()`.

1. **`defer`.** Scanner, parser, AST (the full file list of §4.1), semantic rules
   (§4.2's `defer` items), translator scope stack and `emit_scope_exits`, YAML and run
   tests. The warm-up through every AST file, and useful on its own.
2. **Coroutine front end.** `_Coro(Y)` on symbols, `_Coro_frame(Y, T)`, `_Yield`,
   `_Await`, the nine operations: parsed, type-checked, serialized, printed; every
   compile-time rule of §2.4 with a negative test; the translator says "coroutines:
   not yet" so nothing links. `Target.coroutines` for wasm32.
3. **Generators.** Stage 1 for `yield`, `return`, `co_init`/`co_resume`/`co_value`/
   `co_result`/`co_done`/`co_sizeof`; `optimize/liveness.c` factored out of
   `dead_store.c`; the split pass with spills, dispatch, `f$init` and `f$co`;
   `libc/wasm32/co.c`; `<coro.h>`. Tests: translator YAML of the state machine
   (`translator/test/coro_tests.cpp`), wasm32 run tests under node
   (`backend/wasm/test/coro_tests.cpp`: `range`, a Fibonacci generator, a coroutine
   with a struct local whose address is yielded, two frames of one coroutine
   interleaved, `co_sizeof` across two units — `CompileAndRunWithClang`'s two-unit
   plumbing with our own second unit — and each trap).
4. **Delegation.** `await` in both forms, the arena (`__co_push`/`__co_pop`),
   `co_cancel` and forwarding, `co_destroy` with `defer`, `yield` nested in
   expressions. Tests: `read_exact`/`read_header` of §2.2 against a
   scheduler written in the test program, a recursive coroutine (tree walk), a
   three-level chain cancelled at the bottom, `co_destroy` running two scopes'
   defers, an arena that overflows.
5. **Backend quality.** §6.5's regional dispatch in `structure.c`; flow goldens for
   Duff's device and `goto` into a loop change from the skeleton to the regional
   form; coroutine goldens; sizes measured.
6. **Braam target and runtime.** `wasm32-braam` in `cc.c`, `cpp`, CMake and the
   install; `crt0.c`, the allocator, `braam_sys`, `read`/`write`/`open`/`close`/
   `sleep_ms`/`fflush`, the headers, `run.mjs`; the ABI constants and the drift test.
   Tests (`backend/wasm/test/braam_tests.cpp`, a fixture over the fake kernel): hello
   through `printf` and the exit flush; `cat` (stdin to stdout, the `cat.s`
   program in C); `wc`; a sleep; the import/export/section check; `exit()`'s
   behaviour; a program that returns a status.
7. **On Braam.** §7.5's optional system test; `stat`, `fgets`, `getchar`, `stdio.h`
   completed; a worked example in `docs/` built both ways.
8. **Signals and tasks.** `_sig`, `sig_catch`/`sig_take`, `Err(Intr)` on a read;
   `braam_spawn` and the pending table; `braam_yield`.
9. **Later, as needed.** `coro_ptr(Y, T)` with a `void *` init thunk; a direct call
   in `__co_resume` when the callee is visible; a `JUMP_TABLE` for wide dispatches;
   shared cleanup blocks for `defer`; a lint for a suspended frame going out of
   scope.
10. **Docs.** `docs/Coroutines.md` (the language: §2 of this plan as a manual, the
    frame ABI, the traps), `docs/Braam.md` (the target, the runtime, porting a
    program, running one by hand), a section in `docs/Wasm_Backend.md` for §6.5,
    `CLAUDE.md`, `README.md`, `docs/Technical_Reference.md`, `cc/README.md`,
    `cpp/README.md`, `docs/C_Grammar.md`.

## 9. Alternatives considered

| Alternative | Chosen | Reason |
|---|---|---|
| `coro_frame(f)`, a complete object type per coroutine, and `co_sizeof(f)` an integer constant expression | `co_frame(Y, T)`, incomplete; storage supplied with its size; `co_sizeof` a run-time load | The libc's coroutines are awaited from units that never see their bodies; and the frame is laid out after the optimizer, which is what keeps it small. A fixed-size object type would fix the layout at the declaration. |
| Frames never nested: every `await` on a frame the awaiter declared as a local | Also `await f(args)`: the callee's frame from the task's arena, LIFO | Without compile-time sizes a sub-frame cannot be a local. An await chain is a stack, so a bump pointer per task costs nothing and permits recursion and separate compilation. Storage is still the program's: the root's block. |
| Cancellation only through `co_cancel`, cleanup entirely the coroutine's | `co_destroy` as well | It runs the `defer`s active at the suspension point and nothing else, so a scheduler can drop a suspended task without the task's cooperation, and a resource registered with `defer` is released exactly once however the scope ends: by completion, return, cancellation or destruction. |
| `coro_ptr(Y, T)` in the first version | Phase 9 | Braam needs none; the init thunk's argument list is the unresolved part. |
| `yield` at the precedence of a unary operator, with parentheses required around `yield e == …` | `_Yield` takes an assignment-expression and binds above `==` | So `if (yield i == CO_CANCEL)` means what it looks like. |
| The whole frame layout part of the ABI | The header is ABI; the rest is the defining unit's | Only the header crosses units. The rest may change with the optimizer. |
| A trap for resuming an uninitialised frame | None | A frame comes only from `co_init`; storage that was never initialised is garbage the language cannot recognise. The reentrancy and finished checks stay. |
| Eager start (the body runs to its first suspension inside `co_init`) | Lazy: `co_init` runs nothing | Otherwise the point at which side effects happen depends on the call site rather than the source. |
| A `defer` whose run-time "active" flag a jump may skip, so `goto` and `case` into its scope are allowed | Such jumps are compile-time errors | The active set stays a static fact, cleanup is inline code, and `co_destroy` knows from the state number alone what to run. The flag remains possible later without breaking programs. |
| `defer` running at a suspension (releasing a lock across `await`) | Suspension is not scope exit | A suspended scope is alive; a resource held across `await` stays held, and the programmer who does not want that closes the scope before the `await`, which keeps lifetimes visible in the source. |
| Symmetric transfer, awaiting arbitrary "awaitables", task groups, allocator hooks | None | Library concerns, or specification weight for little gain; a `select` is a scheduler that resumes several frames. |

## 10. Risks and open questions

- **Code size of the dispatch.** A compare chain per resume and a `LOAD`/`STORE`
  per frame access. The second optimizer round and the memarg folding should keep a
  generator within 1.5× of the hand-written `cat.s` shape; §6.5 is the lever if not.
- **The `defer`-past-`case` rule** may surprise; the alternative (a run-time active
  flag per `defer` that a jump may skip) is implementable later without changing
  programs that compile today.
- **`exit()` as a trap** is Braam's own limitation. If it matters, the runtime can
  make `exit` set a sticky status and have every libc coroutine return an error
  afterwards, so the program unwinds by its own returns — the pattern Braam's `vi`
  port uses.
- **`main(void)` in Braam mode.** Phase 6 requires `coro(braam_call *) int main(int,
  char **)`; a wrapper for the `(void)` form is small if wanted.
- **`PROC_ABI` tracking.** The drift test fails loudly, but only where braam-core is
  checked out beside this tree.
- **Deep chains.** An `await` depth of *n* costs *n* resume calls per suspension;
  Braam's C++ pays the same. No tail calls on Braam, so nothing
  here may rely on them either (we emit none).
- **The optimizer's second round** on code with many frame loads: `cse.c` kills all
  loads at every `STORE` through a pointer, so two frame members in one block cost a
  reload each. Acceptable; a "same base, different constant offset, cannot alias"
  rule in `alias.c` is a later refinement.
- **Liveness granularity.** The spill set is per name over the whole body, not per
  suspension point: a local live across one `yield` is in the frame everywhere. Locals
  that live across no suspension stay out of the frame; finer placement is not needed for correctness.

## 11. Verification

- `ctest -j8` after each phase, the output tee'd to a scratch file; the full suite
  after phases 1, 2, 5 and 6 (shared code). BESM-6 output unchanged.
- By hand, plain wasm32: `build/cc/cc -t wasm32 gen.c -o gen.wasm && node
  libc/wasm32/run.mjs gen.wasm`.
- By hand, Braam without Braam: `build/cc/cc -t wasm32-braam cat.c -o cat &&
  node libc/wasm32/braam/run.mjs cat < input`.
- By hand, on Braam: `node test/run.mjs --kernel build/kernel.wasm
  build/web/rootfs.zip $VCC/cat` from braam-core asserts the ABI; the harness session
  of §7.5 runs it.
- `wasm-objdump -x cat` shows the three imports, the five exports, no `memory`
  export, and one `braam` section; `wasm-validate` passes.
