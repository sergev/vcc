# Plan: `defer` and stackless coroutines, for Braam

Status: phases C1–C9 are built — `defer`, the coroutines' front end, generators,
delegation (`await` in both forms, the arena, `co_alloca` in functions and
coroutines, every operation), a dispatch per irreducible region in the wasm backend,
and the target `wasm32-braam` with its runtime, `stdio.h` on files and stdin, a fake
kernel for node, a system test on Braam itself, signals, tasks and `poll`, and
`coro_ptr`, the jump-table dispatch, shared cleanups and the lint; §8 lists what
remains, the docs. The document defines two extensions of C — a `defer`
statement and stackless coroutines — measured against what Braam requires of a process
and against how vcc is built, and lays out the work in phases. §9 records the
alternatives that were considered and rejected.

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

Two statements, two expressions, one function specifier, one type, ten operations.
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
| `__co_init`, `__co_alloca`, `__co_resume`, `__co_cancel`, `__co_destroy`, `__co_done`, `__co_value`, `__co_result`, `__co_sizeof`, `__co_alignof` | `co_init` … `co_alignof` | the operations, keywords with call syntax like `__builtin_va_class` |

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
    co_frame(int, void) *f = co_alloca(range, 0, 0, 10);
    while (co_resume(f) == CO_SUSPENDED)
        print_int(co_value(f));
}
```

- `coro(Y) T f(params)` declares a coroutine. It may not be variadic, `inline`,
  `_Noreturn` or `main`, and needs a prototype; it does not convert to a function
  pointer (§2.4 has the indirect form). Its prototype and definition must agree on `Y`
  as on everything else.
- **Frames.** A frame is an object in storage the caller supplies, address-stable for
  its life. `co_init(storage, bytes, f, args...)` builds `f`'s frame at the front of
  `storage`, copies the arguments in, runs no body code, and returns a
  `co_frame(Y, T) *` for `f`'s `Y` and `T`. The rest of the storage, from the frame's
  end to `storage + bytes`, is the **arena** from which the frames of everything `f`
  awaits are taken (§2.2). A block of exactly `co_sizeof(f)` bytes is a frame for a
  coroutine that awaits nothing.
- **Sizes are known when the program is linked.** The unit defining `f` emits a
  descriptor `f$co`, two words holding the frame's size and alignment (§6.1);
  `co_sizeof(f)` and `co_alignof(f)` load them. They are not integer constant
  expressions — no array size, `case` label or `_Static_assert` may use them —
  because the frame is laid out after the optimizer has run, in the unit that defines
  `f`, and a program must await the libc's coroutines without seeing their bodies. §9
  argues it. (Absolute symbols `f$size`/`f$align`, which would cost no load, were the
  first design; clang's wasm assembler cannot write one, §10.) With `co_init`, `_Alignas(16)`
  and `co_sizeof(f) <= bytes` are the caller's to arrange; `co_init` traps otherwise.
- **`co_alloca(f, extra, args...)`** is the way to give a frame its own size: it
  builds `f`'s frame, followed by `extra` bytes of arena for what `f` awaits, in
  memory that lives **until the end of the enclosing block**, and returns the
  `co_frame(Y, T) *`. In an ordinary function the memory comes off the shadow stack;
  in a coroutine, from the task's arena (§2.2), since a coroutine's shadow stack is
  unwound at every suspension and the arena is not. The new frame is the root of a
  task of its own, its `extra` bytes its own arena. At the end of the block, on every
  exit edge and on `co_destroy` of the enclosing coroutine, an implicit cleanup runs
  — `if (!co_done(p)) co_destroy(p);`, then the memory is given back — ordered as if
  the `co_alloca` were a `defer` at its own position (§2.3). So a sub-coroutine
  started with `co_alloca` never outlives its block with its `defer`s unrun, a
  loop body's `co_alloca` is released every iteration, and the allocations nest
  with the blocks, LIFO.
- `co_resume(p)` runs to the next suspension or to the return; `co_cancel(p)` does the
  same, delivering `CO_CANCEL` at the suspension point; `co_destroy(p)` does not resume
  user code at all: it runs the `defer`s active at the suspension point, innermost
  first, finishes the frame (§2.3) and returns `CO_DONE`. All three trap on a frame
  that has finished or been destroyed, so a program that may hold one tests
  `co_done(p)` before `co_destroy(p)`. `co_done(p)` is true once the body has
  returned or the frame was destroyed. `co_value(p)` is the last yielded value, type
  `Y`; `co_result(p)` the return value, type `T`.
- Coroutines start suspended; `co_init` and `co_alloca` run nothing.

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
allocator. A `co_alloca` inside a coroutine pushes on the same stack and is popped at
the end of its block, so arena `await`s and `co_alloca`s nest in one LIFO order; the
frame it builds is the root of a new task whose arena is the `extra` it was given.

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
  than the whole switch. A `co_alloca` is a `defer` for this purpose: its release
  (§2.1) is ordered with the `defer`s of its block, and no jump may enter the block
  past it.
- In a coroutine, suspension is not scope exit: the `defer`s stay active across a
  `yield`. They run when the scope is left on resumption, or on `co_destroy`.

Deferred statements see the variables' values at the time they run, as any statement
does.

### 2.4 Defined misuse

Compile-time errors: `yield`/`await` outside a coroutine or inside a `defer`;
`yield expr` in a `coro(void)` and bare `yield` elsewhere; `await` of a different `Y`;
`co_value` on a `co_frame(void, T)`, `co_result` on a `co_frame(Y, void)`; a coroutine
that is variadic, `inline`, `_Noreturn`, or without a prototype; `_Coro` on anything but
a function; a yield type (or a `co_frame`'s `Y` or `T`) that is an array or a function;
its name used as a value other than in `co_init`, `co_alloca`, `co_sizeof`, `co_alignof`
or an arena `await`; a jump past a `defer` or a `co_alloca`; a `co_alloca` in the head
of a loop, which runs more than once per entry of the block; control leaving a `defer`
body; a non-void coroutine that may fall off its end (as for any function); `main` as a
coroutine outside Braam mode; any of it on a target without coroutines.

Traps, in every build: `co_init` on storage too small or misaligned
(`CO_TRAP_STORAGE`); resuming a frame that is running (`CO_TRAP_REENTRANT`) or
finished (`CO_TRAP_FINISHED`); `co_value` when the last status was not `CO_SUSPENDED`
(`CO_TRAP_NO_VALUE`); `co_result` before `CO_DONE` or after `co_destroy`
(`CO_TRAP_NOT_DONE`); an arena `await`, or a `co_alloca` in a coroutine, that does
not fit (`CO_TRAP_NO_SPACE`); a `co_alloca` in a function past the end of the shadow
stack (the ordinary out-of-bounds trap). A trap
prints `coroutine trap: <name>` and exits with status 255 under node; on Braam it
will be `unreachable`, which the kernel reports as a crash.

Not covered: letting storage given to `co_init` go out of scope while its frame is
suspended; that leaks whatever the coroutine owned, and the language cannot detect it
without ownership tracking. `co_destroy` is the tool. A `co_alloca` frame cannot be
left behind: its block's end destroys it.

**The lint** (built in C9; `semantic/coroutines.c`) flags the two plain cases. Each is
reported as `warning: <function>: …` on stderr, and compilation goes on. Storage
counts as automatic when it is a local array, decayed, or `&x` of a local; a local
pointer's value is memory from elsewhere.
- **The frame outlives its storage.** The storage given to `co_init` is automatic, and
  the frame is stored where it outlives it: a global, a static, a variable of an
  outer block, through a pointer or into a member. Returning it counts too.
- **The frame may be left suspended.** Such a frame was resumed by a statement that
  drops the status (`co_resume(p);` or `co_cancel(p);`). Nothing in the block
  destroys it, asks `co_done`, reads `co_result` or awaits it.

**`coro_ptr(Y, T)`** (built in C9) points to a coroutine that takes `(void)` or
`(void *)`, so a scheduler can hold a list of coroutines of different code, and a
table can name them.
- **The type** is `_Coro_ptr(Y, T)`, `coro_ptr` in `<coro.h>`: a pointer to a struct
  tagged `__co_desc`. Like a frame type's struct it is never defined, and two are the
  same when their Y and T are.
- **Conversion.** The name of such a coroutine, used as a value, converts to its
  `coro_ptr`, in a static initializer too. The value is the address of its descriptor
  `f$co`, which then holds four words: size, alignment, init and resume. For `(void)`
  the init is a thunk `f$initp` that takes the `void *` and ignores it, so every
  descriptor's init has the type `void (char *, void *)`.
- **The operations.** `co_init`, `co_alloca`, `co_sizeof`, `co_alignof` and an arena
  `await p(arg)` take a `coro_ptr` where they take a coroutine's name, with zero or
  one argument, converted to `void *`. A `coro_ptr` cannot be called otherwise.
- **The "init thunk's argument list"** that kept this phase open is "one `void *`":
  a coroutine with other parameters has no `coro_ptr`, and the error says so.

## 3. Where each part lives

```
scanner     four keywords, ten builtins, _Coro_frame         (scanner.c keyword table)
parser      specifier, statements, expressions, the type     (decl.c, stmt.c, expr.c)
ast         STMT_DEFER, EXPR_YIELD/AWAIT/CO_*, FUNC_SPEC_CORO, the frame type
semantic    coroutine-ness on the symbol, Y/T types, every compile-time rule
translator  defer as inline cleanup on every exit edge; a coroutine body as an
            ordinary TAC function over an opaque suspension call; co_* as loads,
            stores and calls against a fixed header layout
optimizer   unchanged: the suspension is a call it may not move or drop
coro split  after the optimizer: liveness, frame layout, spills, dispatch, f$init,
            the descriptor f$co                              (translator/coro.c)
genwasm     __builtin_stack_save/restore/alloca for co_alloca (call.c); a
            dispatch node per irreducible region, not a skeleton around the
            whole function                                   (structure.c)
runtime     libc/wasm32: co.c (setup, resume, arena push/pop, checks, traps);
            libc/wasm32/braam: crt0, syscalls,
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
  `EXPR_AWAIT{expr}`, in `parse_unary_expression`. `_Await` takes a
  cast-expression: `await f(x) + 1` is `(await f(x)) + 1`. `_Yield` takes a
  *relational-expression*, so it binds tighter than `==`: `yield i == CO_CANCEL` means
  `(yield i) == CO_CANCEL`, and `yield a + b` yields the sum. It has no operand when
  the next token cannot begin an expression.
- The ten operations: one `EXPR_CO_OP{op, args}` kind with an enum `CoOp`, argument
  counts checked in the parser. `co_init`'s third
  argument, `co_alloca`'s first and `co_sizeof`'s only one are identifiers naming a
  coroutine; they are parsed as expressions and judged in semantic.
- `_Coro_frame(Y, T)`: a type specifier in `parse_type_specifier` and
  `is_type_specifier`, which builds a `TYPE_STRUCT` tagged `__co_frame` carrying `Y`
  and `T` in `struct_t.frame_yield`/`frame_result`; it is never defined, so it is
  incomplete. `compatible_type` compares two frame types by `Y` and `T` (a typedef'd
  spelling is the same type), not by the tag. No new `TypeKind`, so the Type switches
  in ast/semantic/translator are untouched.
- New AST kinds go through the usual files: `ast.asdl`, `ast.h`, `ast_alloc.c`,
  `ast_free.c`, `ast_clone.c`, `ast_compare.c`, `ast_export.c`, `ast_import.c` (the
  `tag > TAG_STMT + STMT_DEFAULT` and `TAG_EXPR + EXPR_VA_CLASS` range checks move),
  `ast_yaml.c`, `ast_print.c`, `ast_graphviz.c`, the `.dot` diagrams; tests in
  `ast/test/clone_tests.cpp` and `parser/test/serialize_tests.cpp`. The binary AST has
  no version word; `parse` and `lower` are rebuilt together, as always.
- `grammar/c11.y`, `c11.l`, `c11.asdl` and `docs/C_Grammar.md` get the extension in a
  marked section.

### 4.2 Semantic

In `semantic/coroutines.c`, but for the jumps:

- **Coroutine-ness on the symbol.** `Symbol.u.func` has `coro` and `yield_type`,
  beside `noret`; a redeclaration must agree on both. The C function type stays
  `T f(params)`, so argument checking is unchanged (`typecheck_call_args`, shared by
  calls, `co_init`, `co_alloca` and the arena `await`).
- **Context.** The yield type of the coroutine being checked, set by
  `typecheck_fn_decl` around the body; the depth of deferred statements and of loop
  heads, kept by `statements.c`.
- **Types.** `yield e` coerces `e` to `Y` (as `return` does to `T`) and has type
  `int` (`co_signal`). `await e`: a call whose callee is a coroutine symbol (the
  arena form), checked as a call, its value `T`; or a `co_frame(Y', T') *` (the
  explicit form), its value `T'`; either way `Y' == Y`. `co_init(storage, bytes, f,
  args...)`: `void *`, `size_t`, a coroutine, then the arguments as a call;
  `co_alloca(f, extra, args...)` likewise; both give `co_frame(Y, T) *` for `f`.
  `co_resume`/`co_cancel`/`co_destroy`/`co_done` → `int`, `co_value` → `Y`,
  `co_result` → `T`, `co_sizeof`/`co_alignof` → `size_t`, not constant. A coroutine
  named by an operation has its function type on the `EXPR_VAR`, as a callee has.
- **Jumps.** `semantic/defer.c` counts each `co_alloca` as a `defer` registered after
  the declaration or expression statement holding it, or, in the head of an `if` or
  `switch`, before the body, so a `goto` or `case` past one is the same error.
  `co_alloca` may not be in a loop's head, which runs more than once per entry of the
  block.
- A coroutine symbol decays nowhere else. `main` may not be a coroutine; Braam mode
  will allow it (§7).
- `Target.coroutines` is set for wasm32 alone; elsewhere a coroutine, a `yield`, an
  `await` or an operation is "coroutines are not supported on target …". `cpp -t
  wasm32` predefines `__vcc_coroutines__`, and `<coro.h>` has the short names and the
  two enums.
- `eval_const` and `const_convert.c` never see the new expressions: none is a constant
  expression.

Tests: `parser/test/negative_tests.cpp` (argument counts), `parser/test/serialize_tests.cpp`
(a round trip), `semantic/test/coro_tests.cpp` (each rule), and
`translator/test/coro_tests.cpp`.

## 5. Lowering `defer`

In the translator only: cleanup is emitted inline on every edge that leaves a scope,
with no run-time stack, so there is no library dependency, no allocation, no hidden
state, and the optimizer sees all of it. The rule the lowering implements: every
control-flow edge leaving one or more lexical scopes passes through the cleanup of
those scopes, innermost first.

- `TacCtx` (`translator/translate.h:21-34`; it is initialised positionally at
  `translate.c:920`) gains a scope stack: each block of §2.3 pushes an entry holding
  the list of **exit actions** registered so far — a deferred `Stmt *`, or a
  `co_alloca` release (§6.1) — for a compound statement, `for`'s own scope,
  and every substatement of `if`, `switch`, `while`, `do` and `for`, braced or not
  (an unbraced one is a block of its own in C11, so `if (x) defer f();` lowers to
  `f()` right there); `gen_stmt`'s `STMT_DEFER` appends to the top entry and emits
  nothing, and a `co_alloca` appends its release after emitting the allocation.
- `emit_scope_exits(ctx, down_to)` lowers the exit actions — a deferred statement
  with `gen_stmt`, a release as its few instructions — of every scope from the innermost to `down_to`, each scope's in reverse order. It
  is called: at the end of a compound statement for its own scope (only when the end
  is reachable — `stmt_falls_through` already exists); before `RETURN` for every
  scope; before `break`/`continue` for the scopes down to the loop's or switch's
  body; before `goto` for the scopes down to the common ancestor of the `goto` and
  its label. The label's scope path comes from the semantic pass (§4.2), kept on the
  AST like `branch_target_label` is.
- The deferred statement is lowered once per exit edge. That duplicates code in a
  scope with many exits, so since C9 (`translator/stmt.c`) an exit whose cleanup is
  large shares one copy per block, the way C++ compilers share landing pads:
  - **The test.** A size estimate, three per statement in the deferred statements and
    twelve per `co_alloca` release, at least 12. A coroutine's destroy paths, one per
    suspension point, share from 6.
  - **The chain.** The block's actions are lowered once more at its end, after a jump,
    with an entry label in front of each. Then come tests of a "where next" variable
    for the exits that end there (a label, a return of the one return variable, a
    coroutine's end), then a jump into the chain of the next block out.
  - **The exit** sets the variable and jumps into the chain of the innermost block it
    leaves, at the action it has reached.
  - **Why the chain is static.** An exit from inside a block always goes on into the
    same place outside it: an enclosing block's actions cannot change while the inner
    block is open.
  - **The fall-through** keeps its own copy, so the common path pays nothing.
  - **Off** on BESM-6, whose code must not change, and with `lower --no-shared-cleanup`.
    A `goto` back over a defer of the common block keeps its own copy, and so does a
    return of an aggregate or a `long double`.
  - **Measured.** A test program with large cleanups is 10% smaller; programs whose
    cleanups are one call, the Braam libc for one, do not change.
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

Built in phases C3 and C4 (`translator/coro.c`, `libc/wasm32/co.c`).

`coro(Y) T f(A a, B b)` becomes two functions and a descriptor in its unit, global for a
global `f`, local for a `static` one:

```
int    f$resume(char *fp)              the body as a state machine; returns co_status
void   f$init(char *fp, A a, B b)      stores the arguments in the frame
size_t f$co[2] = { size, align }       the frame's size and alignment
```

and the frame it runs in, laid out by the compiler (the *header* is a fixed ABI; the
rest is the unit's business):

```
 0  u32   state      0 created; k ≥ 1 suspended at point k; DONE (0xfffffffe);
                     DESTROYED (0xffffffff)
 4  u32   flags      bit 0 RUNNING; bits 1-2 the signal of this resumption:
                     0 continue, 1 cancel, 2 destroy
 8  ptr   resume     f$resume, so a co_frame(Y, T) * is self-describing
12  ptr   task       the root frame of this task: whose arena to push on
16  ptr   top        arena bump pointer      (meaningful in a root)
20  ptr   limit      arena end               (meaningful in a root)
24  Y     value      absent when Y is void; aligned to Y
    T     result     absent when T is void; aligned to T
    A a, B b         the parameters
    ...              names live across a suspension, and every object in memory
                     (address taken, aggregate, long double), by decreasing alignment
```

Offsets of `value` and `result` depend only on `Y` and `T` (`coro_layout`), so a holder
of a `co_frame(Y, T) *` (a scheduler, the `await` expansion, `co_value`) reads them with
no knowledge of `f`. Alignment is the maximum over the members, at least 4, clamped
to 16 as the shadow stack's is. The frame has no pointer into itself that the
compiler plants, so a frame may be *moved* while suspended if the program knows
nothing in it was address-taken — but the language promises nothing, and the
`await` arena never moves anything.

The runtime routines (`libc/wasm32/co.c`, ordinary C compiled by us; named `__coro_*`
because `__co_*` are the operations' keywords):

- `__coro_setup(storage, bytes, desc, resume, parent)`: checks `storage` against
  `desc`'s alignment and `bytes` against its size (`CO_TRAP_STORAGE`), clears the
  header, sets `resume`, `top = storage + size`, `limit = storage + bytes`, and `task =
  storage` for a root (`parent` null) or `parent->task` for an arena `await`'s frame.
  `co_init(storage, bytes, f, args)` is that with `&f$co`, `&f$resume` and no parent,
  then `f$init(storage, args)`.
- `__coro_resume(p, signal)`: `co_resume`, `co_cancel`, `co_destroy` with signals 0, 1,
  2. Traps on DONE/DESTROYED (`CO_TRAP_FINISHED`) and on RUNNING
  (`CO_TRAP_REENTRANT`); a destroy of a frame never started marks it DESTROYED, there
  being no `defer` to run; otherwise sets `flags = RUNNING | signal << 1`, calls
  `p->resume(p)` through `call_indirect`, clears `flags` and returns the status. An
  arena `await` of a named coroutine does not call it (C9): it sets `flags` itself and
  calls `g$resume` directly. Its frame is private to the await, which never resumes it
  once it is done or while it runs, and destroys it only after it has suspended, so the
  checks cannot fail.
- `__coro_done(p)`: the state is DONE or DESTROYED.
- `__coro_value(p, off)` and `__coro_result(p, off)`: check the state
  (`CO_TRAP_NO_VALUE` unless suspended; `CO_TRAP_NOT_DONE` unless DONE) and return
  `p + off`, which the caller reads as `Y` or `T` at the offset it computed.
- `__coro_push(fp, bytes, align, name)`: `bytes` aligned to `align` off the arena of
  `fp->task`, bumping its `top`; `CO_TRAP_NO_SPACE: name` when they do not fit, `name`
  the coroutine being started (a string constant at each site). `__coro_pop(fp, p)`
  sets `fp->task->top` back to `p`.

`co_alloca(f, extra, args)` gets its memory one of two ways, then does what `co_init`
does on it with `bytes = n = (f$co[0] + extra + 15) & -16`:

- **In an ordinary function**: `%sp = __builtin_stack_save()`, `%p =
  __builtin_alloca(n)`. The release, run at the end of the block (§5), is `if (%p) {
  if (!__coro_done(%p)) __coro_resume(%p, 2); __builtin_stack_restore(%sp); %p = 0; }`.
  The three builtins are expanded inline by the backend (§6.3).
- **In a coroutine**: `%p = __coro_push(fp, n, 16, "f")`, from the arena of the task
  the coroutine belongs to. The release is the same with `__coro_pop(fp, %p)` in place
  of the restore. The coroutine's own `co_destroy` reaches the release because
  it is one of the scope exits active at the suspension point.

Either way `__coro_setup` makes `%p` the root of its own task, with the `extra` bytes
as its arena. A `co_alloca` in an operand of `&&`, `||` or `?:` may be skipped while
its block runs on, so `%p` is set to null at the block's entry (an instruction the
translator inserts after the block's first point, `tac_scope_entry`) and the release
tests it; the optimizer drops the test where the allocation always runs. One inside a
`sizeof` is never evaluated and registers nothing (`semantic/defer.c` does not count it
either).

### 6.2 The two-stage translation

The optimizer must see a coroutine body as ordinary code, and the frame must be laid
out from *optimized* code, or every temporary the source mentions ends up in it. So
the translator produces a **provisional** function, the optimizer runs, and a **split
pass** finishes the job — LLVM's CoroSplit, scaled to this compiler.

**Stage 1, in the translator** (`translate_fn` with a coroutine symbol; `coro.c`):

- The TAC function is `f$resume` with params `(%.fp, %a, %b)`: the frame pointer, then
  the user's parameters *as ordinary TAC parameters* — the split pass moves them.
  Its return type is `int`, and a structure result never goes through sret.
- The body is lowered by `gen_stmt` as for any function.
- `yield e` → store `e` at `fp + value_off` (an aggregate by `gen_aggregate_copy`),
  then `%s = FUN_CALL __coro_suspend(%fp)`, then the destroy branch: `JUMP_IF_ZERO (%s
  == 2) over; <the exit actions of every open block>; state = DESTROYED; RETURN 1;
  over:`, and the expression's value is `%s` (0 or 1). `__coro_suspend` is declared by
  an EXTERN, so the verifier is content; the optimizer treats it as an opaque call:
  never dead, kills every memory fact, stays where it is. Nothing in `optimize/`
  changes. The EXTERN goes once the split has replaced every call.
- `return e` → store `e` at `result_off` first, then the exit actions (§5), then
  state = DONE and `RETURN 1` (CO_DONE). The end of a void body is the same without
  the store; the end of a non-void body is unreachable (semantic rejects one that is
  not).
- `await` (`gen_await`) → the loop of §2.2, in TAC: for the arena form `%sub =
  FUN_CALL __coro_push(fp, g$co[0], g$co[1], "g")`, `__coro_setup(%sub, g$co[0], &g$co,
  &g$resume, fp)` (the sub-frame joins the awaiter's task) and `g$init(%sub, args)`;
  for the explicit form `%sub` is a copy of the operand, evaluated once. Then `%sig = 0;
  L: %st = __coro_resume(%sub, %sig); JUMP_IF_NOT_ZERO %st done; copy sub->value to
  fp->value; %s = __coro_suspend(fp); if (%s == 2) { __coro_resume(%sub, 2);
  __coro_pop(fp, %sub); <every block's exit actions>; state = DESTROYED; RETURN 1 }
  %sig = %s; JUMP L; done: the result from sub->result; __coro_pop(fp, %sub)` (the
  pops in the arena form only: an explicit frame is the program's, and destroying the
  awaiter leaves it suspended, as the equivalence of §2.2 says). The forwarding of
  CANCEL is `%sig` itself: the next `__coro_resume(%sub, %sig)` delivers it. `%sub` and
  `%sig` live across the suspension, so the split puts them in the frame.
- `co_*` operations → calls of the runtime (§6.1). `co_sizeof(g)` loads `g$co[0]`,
  `co_alignof(g)` `g$co[1]`. A unit names `g$co`, `g$init` and `g$resume` through
  EXTERNs with TAC types (`tac_record_extern_tac`), since they have no symbol; a name
  the unit defines itself goes with the unit's externs (`note_own_type`), for the
  verifier and against a second EXTERN.
- `co_alloca` → §6.1's sequence for a function; the release goes on the block's exit
  actions (`EXIT_CO_RELEASE`, §5, its `sp` null in a coroutine).

**Stage 2, the split pass** (`coro_split`, called from `translate()` after
`optimize_function`, then `optimize_function` once more and the verifier):

1. Liveness over the TAC body (`optimize/liveness.c`, factored out of
   `dead_store.c`). The names to move into the frame are those live just after a
   `__coro_suspend` but for its own result (a call's result is killed there), every
   `ALLOCATE_LOCAL` name and every `GET_ADDRESS` source (an address may be held across
   a suspension through memory the analysis cannot see; and the shadow stack, where
   the backend would put them, is unwound at every return), and the user parameters
   (the only channel from `f$init` to the body).
2. Layout: after the value and the result, the parameters in order, then the rest by
   decreasing alignment. The size, rounded to the alignment, and the alignment go into
   `f$co`.
3. Rewrite: a moved scalar's every use becomes `LOAD` from `fp + off` into a fresh
   temporary (`%co.N`) just before the instruction, every definition a `STORE` just
   after; an `ALLOCATE_LOCAL` goes, `GET_ADDRESS` becomes `ADD_PTR fp, off`, and
   `COPY_TO_OFFSET`/`COPY_FROM_OFFSET` a `STORE`/`LOAD` at the member's address. An
   object in memory used whole (a structure argument, a call's structure result) goes
   through a shadow copy in the shadow-stack frame, copied in before the use and out
   after the definition. The second optimizer round forwards the stores to the loads
   inside a block and prunes the dead names.
4. Suspension points: the k-th `__coro_suspend` becomes `STORE k → state; RETURN 0;
   LABEL %co.resumek; %s = LOAD flags >> 1` (the signal). The dispatch goes in front of
   the body: `%st = LOAD state; JUMP_IF (%st == k) %co.resumek; …` — a compare chain,
   as `switch` lowers today. From three suspension points on (C9) it is a
   `JUMP_TABLE` instead. That instruction is `JumpTable(Val index, identifier*
   targets, identifier default_target)`, made only by this pass: `%co.start` for 0
   and the default, `%co.resumek` for k. The optimizer and the shared CFG
   (`backend/common/flow.c`, whose blocks now hold any number of successors) take it,
   and a constant index folds to a `JUMP`. The wasm backend makes it a `br_table`
   (§6.3), and BESM-6 and MSP430 reject it. Measured on the Braam programs, it saves
   0.4% of the code, and 1% on the files with the most suspension points. State 0
   falls into the body's first instruction; RUNNING and the signal are the runtime's.
5. Parameters: dropped from `f$resume`'s list; `f$init(%.fp, %a, %b)` is made as a
   function toplevel storing each at its offset (an aggregate in chunks), and `f$co`
   as a static variable; both follow `f$resume` in the chain `translate()` returns.
   Nothing new reaches the backend: the TAC format is unchanged.
6. The verifier sees ordinary TAC: `fp` is a `char *` and the frame accesses are
   `ADD_PTR` + `LOAD`/`STORE`.

Why the body is correct across the two optimizer rounds: before the split, no name
crosses a suspension in the optimizer's eyes (the suspension is a call, and a call
returns to the same point); after the split, the only names that cross a `RETURN`
are in memory. `dead_store` would otherwise delete the last store to a local before
a `RETURN` (exit blocks are seeded only with observable and address-taken names);
step 3 is what prevents it.

A loop whose every iteration suspends needs no back edge in `f$resume`: each pass
returns at the `yield` and comes back through the dispatch, so a generator's state
machine is acyclic and Ramsey's translation structures it as is.

### 6.3 What the wasm backend sees

- `f$resume` is a function with one `i32` parameter. Where a loop runs part of its
  iterations without suspending, the dispatch enters it in the middle, and that loop
  gets a dispatch of its own (§6.5); the rest stays structured.
- Frame accesses are `i32.load off`/`i32.store off` on the parameter: the constant
  addends fold into memarg offsets (`peephole.c`) where the address is not shared.
- **A `JUMP_TABLE` dispatch** (C9) becomes a `br_table` in `structure.c`. Every target
  of a jump table counts as a merge node, as a dispatch node's entries do, so each has
  a block to leave. A target the regional dispatch reroutes through a dispatch node
  must set the state on the way, which a `br_table` entry cannot. That entry leaves a
  block of its own, a trampoline, after which the state is set and the jump made.
  Under the skeleton (`--no-structure`) the table is a chain of compares and `br_if`s.
- `call_indirect (i32) -> (i32)` for `__coro_resume`'s dispatch is an indirect call
  in the runtime; `f$resume` gets a table slot because `co_init` and `co_alloca` pass
  its address.
- `co_alloca`'s three builtins are expanded in `call.c` the way `gen_va_start` expands
  `__va_start`: `__builtin_stack_save()` is `global.get __stack_pointer`;
  `__builtin_stack_restore(p)` is `global.set __stack_pointer`; `__builtin_alloca(n)`
  is `global.get __stack_pointer`, `n`, `i32.sub`, `i32.const -16`, `i32.and`,
  `global.set __stack_pointer`, and its value `__stack_pointer` again. They take no
  calls' area and no `.functype`. A function that allocates gets a frame (16 bytes)
  even with no slots: its epilogue resets `__stack_pointer` from the frame pointer.
- Nothing else. The backend does not know what a coroutine is.

### 6.4 Runtime: `libc/wasm32/co.c`

§6.1 lists the routines. Compiled by `wasm32_compile_libc_c`, in `libc.a`, so a program
that uses no coroutine carries nothing. A trap prints `coroutine trap: <name>` through
`putbyte` and exits with status 255; on Braam it will be `unreachable`. The header is
private to `co.c` and the translator (`coro.c`'s offsets); `<coro.h>` has the
user-facing macros and enums only.

### 6.5 Structured control flow for irreducible regions

Built in phase C5 (`backend/wasm/structure.c`; [docs/Wasm_Backend.md](../../docs/Wasm_Backend.md)
has the description). The coroutine dispatch makes a loop that does not suspend at
every iteration multi-entry, and that made the whole function the dispatch skeleton.
Now, only when Ramsey's analysis finds the graph irreducible, `structure.c` works on a
copy of it, as LLVM's `FixIrreducibleControlFlow` does:

- Tarjan's strongly connected regions of the reachable blocks; inside a region with
  one entry (a loop), the same again with its header's incoming edges set aside.
- A region with several entries gets a **dispatch node**, `local.get state;
  br_table` over its entries. The jumps into an entry from the rest of the region,
  and those inside it that a depth-first walk from the dispatch node finds going
  back, set `state` and go to the dispatch node instead. A forward jump inside the
  region stays, so Duff's cases still run into each other. Then the region, now a
  loop headed by the dispatch node, is looked at in turn.
- When that is not enough (an inner region entered by the dispatch node as well as
  from inside, 58 of 300 random `goto` graphs), every jump to an entry is redirected,
  and the work starts again. Should that fail too (a region entered at the function's
  start), the whole-function skeleton remains; it also stays for `--no-structure`,
  and `--no-regional` asks for it in place of the dispatch nodes.
- Ramsey's translation runs over that graph unchanged, each entry of a dispatch node
  counting as a merge node; one `state` local serves every dispatch node. A jump that
  goes through one sets the state just before it, a `br_if` setting it ahead of its
  test: the other way reads no state, or sets its own.

Code-section bytes (genwasm with all rewrites; before = the C4 build, the same as
`--no-regional`):

| Program | Before | After |
|---|---|---|
| a generator whose loop does not always suspend | 243 | 215 |
| Duff's device and a `goto` into a loop | 484 | 470 |
| the nested region of `flow_tests.cpp` (every jump redirected) | 243 | 224 |
| `read_exact`/`read_header`, a `cat` loop (every iteration suspends: acyclic) | 583, 366 | 583, 366 |
| the 607 book programs (`bench_wasm.sh`; all reducible) | 137384 | 137384 |

The random `goto` graphs of `flow_tests.cpp` (and 300 more, by hand) give the
skeleton's output. They also found a hang in CSE, whose fixpoint did not converge on
such graphs; it is fixed (docs/TAC_Optimization.md, available expressions).

## 7. Braam: target, runtime and libc

### 7.1 The target `wasm32-braam`

Built in phase C6. A second entry in `cc.c`'s target table sharing `vgenwasm`, as
`x86_64-linux` shares `vgenx86`:

- **`lower -t wasm32-braam`** is an alias of wasm32's data model (`semantic/target.c`)
  whose descriptor carries `braam = 1` (a copy of wasm32's with the bit set), in place
  of the `--braam` flag first planned: the driver passes the target name to every pass
  anyway. On it `main` must be `coro(braam_call *) int main(int, char **)`, and a plain
  one is an error (`semantic/coroutines.c`); elsewhere `main` still cannot be a
  coroutine.
- **`cpp -t wasm32-braam`** predefines wasm32's macros and `__braam__`.
- **Headers and libraries** from `share/vcc/wasm32-braam/`: `libc/wasm32/braam/include`
  first, then wasm32's, ILP32's and the common ones, the first of a name winning.
  CMake stages the same tree in `build/share/vcc/wasm32-braam`, where `build/cc/cc`
  finds it, so the tests run the driver end to end.
- **The link:**

  ```
  wasm-ld --no-entry --import-memory --stack-first -z stack-size=131072 --gc-sections
          --initial-memory=<pages*65536> -o out -L<lib> <lib>/crt0.o objs -lc
  ```

  then the `braam` section, appended by the driver (`braam_stamp` in `cc/cc.c`, the
  work of braam-core's `tools/stamp.py`): magic, `PROC_ABI` 21, flags 0, the page
  count, 1600; an earlier `braam` section is dropped. `--initial-pages=N` (1 to 1600)
  sets the count, 4 by default as `BRAAM_BIN_INITIAL_PAGES` is in braam-core's
  `cmake/BraamProgram.cmake`.
- **The exports** come from `.export_name` directives and the imports from
  `.import_module`/`.import_name` in `libc/wasm32/braam/exports.s`, which is
  `crt0.o`: vcc's C has no way to spell either.
- **The ABI numbers** (`BRAAM_PROC_ABI`, the op numbers, `BRAAM_O_*`, `BRAAM_CHUNK`)
  are in `libc/wasm32/braam/include/braam.h`, and Braam's errors (each `Error` plus 32)
  in its `errno.h`. The ctest `braam-abi` (`scripts/check_braam_abi.py`) compares
  them with braam-core's `src/kernel/sysabi.h` and `result.h`, the enums' implicit
  values counted, when that tree is at `../../Braam/braam-core` or `-DBRAAM_CORE`
  names it; a drift is a failing test rather than a stale binary.

### 7.2 The process runtime: `libc/wasm32/braam/rt.c`

Written in C, with coroutines, as `src/proc/rt.cpp` is written in C++ with them:

```c
static coro(braam_call *) int braam_root(int argc, char **argv)
{
    int status = await main(argc, argv);
    await fflush(NULL);             /* what the program left buffered, then exit */
    return status;
}

static void run(int k)              /* task k to its next suspension */
{
    if (co_resume(tasks[k].frame) == CO_DONE) {
        if (k == 0) {                   /* the root: the process exits */
            exited = 1;
            braam_sys_sync(BRAAM_SYS_EXIT, co_result(tasks[0].frame), 0, 0);
        } else {
            tasks[k].frame = NULL;      /* the slot is free again */
        }
        return;
    }
    braam_call *c = co_value(tasks[k].frame);
    c->token = ++token;  tasks[k].call = c;
    __braam_sys_async(c->op, c->token, c->ptr, c->len);
}
```

Built in C6 with one task, the root, and in C8 with a table of `BRAAM_TASKS` (8, as
braam-core's `PROC_TASKS`): a frame and the call it waits on per task, task 0 the
root.

- `_start(argv, len)` (`__braam_start`) copies argv out of the host's blob (u32 argc,
  then u32 length and the bytes of each word), each word with a NUL, sets up the root
  with `co_init` on a block of `__braam_task_bytes` (the root frame and the arena of
  everything `main` awaits; 64 KiB unless the program defines the name: the default is
  a `libc.a` member of its own, `taskbytes.c`, which the linker then leaves out, since
  vcc has no weak symbols) from `malloc`, and runs it.
  `_resume(token, reply, len)` gives the block to the task whose call has that
  token and runs that task, else frees it (an answer to a call whose frame was
  destroyed). Either returns 0 once the root has returned, 1 otherwise.
- `braam_sys` (`sys.c`) yields a `braam_call` that is a local of its frame, so the
  pointer stays good while it waits; resumed, it reads the status, copies the data and
  frees the block. A `defer` (`__braam_forget`) tells the runtime when the frame goes
  before the answer comes: the task's slot is freed, so the table never points into a
  dead frame. That makes `co_destroy` of a waiting task safe, and it is why a task may
  suspend only in `braam_sys`, whose `defer` this is.
- `_alloc`/`_free` are `malloc`/`free`; `libc/wasm32/braam/malloc.c` is a first fit
  over an address-ordered free list with neighbours merged and `memory.grow` when
  nothing fits, since Braam frees every reply block (plain wasm32 keeps its bump
  allocator).
- **Tasks** (C8). `braam_spawn(frame)` takes a `braam_task *`
  (`co_frame(braam_call *, int)`) from `co_init`, puts it in a free slot, runs it to
  its first suspension, as `proc_spawn` does, and returns the slot, or 0 when the
  table is full. Several tasks may park in one step, each with its call outstanding.
  A finished task leaves the table, and its frame keeps the result.
- **Signals** (C8). `_sig(n)` records a bit, as braam-core's does. `sig_catch(sig,
  on)` is Sys::SigAct with a shadow of the mask, as `io.cpp`'s is.
  `sig_take`/`sig_pending` read and clear the bits. The kernel does the rest: a
  caught signal abandons the interruptible calls (Read, KeyRead, Sleep, Wait, ClipRead,
  Poll), which answer `Err(Intr)`, so `read` and the rest return -1 with `EINTR`. An
  uncaught one cancels the process, status 130. `<signal.h>` for Braam has the numbers
  and no `signal()`/`raise()`, since there are no handlers. `poll` (`<poll.h>`) is
  Sys::Poll.
- `exit(n)` (`exports.s`) does `sys(Exit, n)` and traps, as Braam's own compat layer
  does (`doc/Compat.md`: "C `exit()` and `abort()` trap"): nothing unwinds, so neither
  the coroutines' defers nor the stdio buffers run, and the kernel reports a crash. A
  program ends by returning from `main`. A coroutine trap (`co.c`) goes through
  `exit` too, its message left in the stdout buffer.
- The step cannot be interrupted: a compute loop with no `await` in it never sees
  `^C`. `braam_yield()` (C8) is `await sleep_ms(0)`, the program's way to park, as
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

Built in C6: `braam_sys`, `read`, `write` (all of the bytes, in chunks), `open` (the
flags Sys::Open's bits, so `O_RDONLY` is 1), `close`, `sleep_ms` and `fflush`; on an
error they return -1 and set `errno`. A `read` of standard input flushes standard
output first, so a prompt is out before the answer is read. `printf`, `puts`, `putchar`, `fprintf`,
`vfprintf`, `vprintf`, `fputs` and `fputc` fill a stream's buffer: `putbyte` writes to
the stream of the `fprintf` running, stdout otherwise.

Built in C7, the rest of `stdio.h`:

- **Input.** Each stream has an input buffer of `BRAAM_CHUNK` bytes, plus one byte
  in front of it for `ungetc`. `fgetc`, `getc`, `getchar`, `fgets` and `fread` take
  from it and refill it with `read`. Like `read`, a refill on stdin flushes stdout
  first.
- **Opening and closing.** `fopen` reads the mode as Braam's compat layer does, and
  a stream it makes joins the list that `fflush(NULL)` and the runtime's last flush
  walk. `fclose` writes the buffer out, closes the descriptor and frees the buffers;
  for a stream from `fopen` it frees the `FILE` too.
- **Positioning.** `fseek` writes out pending output, moves the descriptor back past
  what was read ahead and not taken, and drops the input buffer. `ftell` and
  `rewind` go through it.
- **Output.** `fwrite`, `putc` and `perror` fill the buffer, as `fputc` does.
- **The `C` rule.** Switching a stream between input and output needs an `fflush` or
  `fseek` between, as C requires.
- **File and directory calls.** `stat`, `lstat` and `fstat` (`<sys/stat.h>`) fill a
  `struct stat` as Braam's `cstat.cpp` does: the mode from the kind, `st_ino` an
  FNV-1a hash of the path, and the time in seconds. `<unistd.h>` gains `lseek`
  (Sys::Seek), `unlink`, `rmdir` and `remove` (all three Sys::Remove), `chdir`,
  `getcwd` and `getpid`; `mkdir` and `rename` are here too. `<sys/types.h>` gives
  `off_t` and the other types 64 bits, as Braam's replies carry them.
- **Not built:** `scanf`, `tmpfile`, `setvbuf`, and Sys::List (directory reading).

Headers in `libc/wasm32/braam/include/`, first on the search path: `braam.h`
(`braam_call`, `braam_sys`, `braam_sys_sync`, `sleep_ms`, `braam_now`, the ABI
numbers), `unistd.h`, `fcntl.h`, `errno.h` (C's three and Braam's errors), a `stdio.h`
whose blocking half is coroutines and whose formatting half is `libc/common`'s, and
`stdlib.h` with `exit`'s note. `strerror` knows Braam's errors. `coro.h` is shared with
plain wasm32.

### 7.4 Running without Braam: `libc/wasm32/braam/run.mjs`

Built in C6. A fake kernel for node, so the unit tests need no Braam checkout: `node
run.mjs prog [args...]`. It checks the module as braam-core's `test/system/abi.mjs`
does (the imports a subset of `env.memory`, `kernel.sys`, `kernel.sys_async`; exactly
the five exports; one `braam` section with the magic, `PROC_ABI` and 1600 pages), makes
`env.memory` of the section's page counts, writes argv (the program's name first)
through `_alloc` and calls `_start`. It serves `kernel.sys` (Exit, GetPid, Now,
Random) and `kernel.sys_async` for Write (fd 1 and 2 to stdout and stderr), Read (fd 0
from stdin, in 512-byte chunks), Open, Close, Read, Write, Seek and FStat on files
(node has no `lseek`, so it keeps each descriptor's position itself), Stat, Remove,
MkDir, Chdir and Rename on paths, and Sleep (`setTimeout`); anything else is
`Err(Unsupported)`. Each reply goes through `_resume`
from a later macrotask, never from inside `sys_async`, so a program that forgets to
return from the step is caught. It prints `[exit N]` on stderr and exits with N; a trap
is a crash, status 255, with what Sys::Exit said if anything (as after `exit()`); a
process that waits while nothing is coming, 254.

Since C8 it also serves SigAct and Poll, and delivers signals. A byte 0x03 on
standard input is a `^C`, as a terminal makes one: the read that reaches it parks,
and SIG_INT finds it parked. Node's own SIGINT and SIGTERM are passed on. A caught
signal goes to `_sig`, and the parked calls (a Read of stdin, a Sleep, a Poll) answer
`Err(Intr)`; an uncaught one ends the run with status 130. Poll answers a file and
a redirected stdin as ready and a terminal by what is buffered.

### 7.5 Running on Braam

Built in C7. The ctest `braam-system` (`backend/wasm/test/braam_system.mjs`) is
defined when `BRAAM_CORE` is a built checkout, one with `build/kernel.wasm` and
`build/web/rootfs.zip`. By default `BRAAM_CORE` is `../../Braam/braam-core`;
`-DBRAAM_CORE=` overrides it. Otherwise the test is left out, like every run test
without its tools. The test:

- builds `hello`, `cat`, `wc` and the worked example `docs/examples/notes.c` with the
  in-tree driver;
- runs braam-core's `test/system/abi.mjs` over them;
- plants them in `/bin` of a session of the harness (`test/system/harness.mjs`), the
  way `braam-apps/devel/c4/test/run.mjs` does;
- types command lines at the shell, the output redirected to a file and the status
  echoed to another:
  - hello's arguments and status;
  - `cat` and `wc` on a file and through `<`;
  - a missing file;
  - `notes`' whole session, `notes ask` reading the terminal up to `^D`;
  - `^C` (C8): caught by a read of the terminal while a second task sleeps, both
    interrupted; not caught, status 130; caught by a loop that parks in
    `braam_yield`. That loop keeps the kernel busy, so the test runs a fixed number
    of ticks instead of `run()`, which waits until the kernel is idle.

Every program ran on the first try on the real kernel. The fake kernel and Braam
gave the same output for the same programs, but for the pid.
[docs/Braam_Example.md](../../docs/Braam_Example.md) walks through the example both
ways. A `braam_add_package`-style recipe for a `.zip` stays a documentation item.

## 8. Phases

Each phase ends green on `ctest -j8 -R 'wasm|translat|parser|semantic|ast'` (the
whole suite after any shared-code change), with a commit. Per step, only the tests of
the part touched. Goldens of the wasm backend stay under `NaiveSelection()`.

- **C10. Docs.** `docs/Coroutines_in_C.md` (the tutorial, written ahead of the code)
    brought up to date with what was built, and the frame ABI added, `docs/Braam.md` (the target, the runtime, porting a
    program, running one by hand), a section in `docs/Wasm_Backend.md` for §6.5,
    `CLAUDE.md`, `README.md`, `docs/Technical_Reference.md`, `cc/README.md`,
    `cpp/README.md`, `docs/C_Grammar.md`.

## 9. Alternatives considered

| Alternative | Chosen | Reason |
|---|---|---|
| `coro_frame(f)`, a complete object type per coroutine, and `co_sizeof(f)` an integer constant expression | `co_frame(Y, T)`, incomplete; `co_sizeof` a load from the descriptor `f$co` the defining unit emits | The libc's coroutines are awaited from units that never see their bodies; and the frame is laid out after the optimizer, which is what keeps it small. A fixed-size object type would fix the layout at the declaration. An absolute symbol would cost no load, but clang's wasm assembler cannot write one (§10). |
| A restricted VLA, `char buf[co_sizeof(f)]`, to put a frame in a local of its own size | `co_alloca(f, extra, args)` | A link-time constant is not an array size, so this would need run-time-sized arrays in the type system and in `sizeof`. `co_alloca` gives the same memory with none of that, and adds the cleanup a plain array cannot have. |
| `co_alloca` scoped to the function, as C's `alloca`, and banned in coroutines (whose shadow stack unwinds at each suspension) | Scoped to the block, usable everywhere: shadow stack in a function, the task's arena in a coroutine | A coroutine that starts coroutines needs it; a function-scoped allocation would grow without bound in a loop; and the end of a block is where an unfinished sub-coroutine can be destroyed, which closes the "storage goes away while suspended" hole for these frames. |
| Frames never nested: every `await` on a frame the awaiter declared as a local | Also `await f(args)`: the callee's frame from the task's arena, LIFO | Without compile-time sizes a sub-frame cannot be a local. An await chain is a stack, so a bump pointer per task costs nothing and permits recursion and separate compilation. Storage is still the program's: the root's block. |
| Cancellation only through `co_cancel`, cleanup entirely the coroutine's | `co_destroy` as well | It runs the `defer`s active at the suspension point and nothing else, so a scheduler can drop a suspended task without the task's cooperation, and a resource registered with `defer` is released exactly once however the scope ends: by completion, return, cancellation or destruction. |
| `coro_ptr(Y, T)` in the first version | Phase C9, for coroutines that take `(void)` or `(void *)` | Braam needs none. The init thunk's argument list was the open part, and the answer is one `void *`, through which a program passes whatever it likes. A thunk for any parameter list would need the arguments packed by the caller in the coroutine's own layout. |
| A direct `co_resume` when the frame comes from a visible `co_init` | Only an arena `await` calls `g$resume` directly | An await knows its callee and owns its frame, so its checks cannot fail. A `co_resume` would need the frame traced through variables, and its checks are what catch a misused frame. |
| `yield` at the precedence of a unary operator, with parentheses required around `yield e == …` | `_Yield` takes an assignment-expression and binds above `==` | So `if (yield i == CO_CANCEL)` means what it looks like. |
| The whole frame layout part of the ABI | The header is ABI; the rest is the defining unit's | Only the header crosses units. The rest may change with the optimizer. |
| A trap for resuming an uninitialised frame | None | A frame comes only from `co_init` or `co_alloca`; storage that was never initialised is garbage the language cannot recognise. The reentrancy and finished checks stay. |
| Eager start (the body runs to its first suspension inside `co_init`) | Lazy: `co_init` and `co_alloca` run nothing | Otherwise the point at which side effects happen depends on the call site rather than the source. |
| A `defer` whose run-time "active" flag a jump may skip, so `goto` and `case` into its scope are allowed | Such jumps are compile-time errors | The active set stays a static fact, cleanup is inline code, and `co_destroy` knows from the state number alone what to run. The flag remains possible later without breaking programs. |
| `defer` running at a suspension (releasing a lock across `await`) | Suspension is not scope exit | A suspended scope is alive; a resource held across `await` stays held, and the programmer who does not want that closes the scope before the `await`, which keeps lifetimes visible in the source. |
| Symmetric transfer, awaiting arbitrary "awaitables", task groups, allocator hooks | None | Library concerns, or specification weight for little gain; a `select` is a scheduler that resumes several frames. |

## 10. Risks and open questions

- **Absolute symbols in the toolchain** (settled in C3). The first design made
  `co_sizeof`/`co_alignof` absolute symbols `f$size`/`f$align` from `.set`, costing no
  load. clang 23's wasm assembler stops on one ("absolute addressing not
  supported"), so the fallback was taken: the descriptor `f$co`, one load each.
- **Code size of the dispatch.** A compare chain per resume (a `br_table` from three
  suspension points, C9) and a `LOAD`/`STORE` per frame access. The second optimizer round and the memarg folding should keep a
  generator within 1.5× of the hand-written `cat.s` shape; §6.5 is the lever if not.
- **The `defer`-past-`case` rule** may surprise; the alternative (a run-time active
  flag per `defer` that a jump may skip) is implementable later without changing
  programs that compile today.
- **Suspending `defer`.** A deferred statement may not suspend, so `defer await
  close(fd)` and `defer await fflush(f)` are errors — and on Braam a descriptor and a
  buffered stream are the resources a program most wants released (memory matters
  less: the kernel drops the instance at exit). Braam's C++ has the same limit, a
  destructor cannot `co_await`. Lifting the rule is backward-compatible, since it only
  turns a compile-time error into accepted code, so it waits for evidence from real
  programs on the Braam runtime (phases C6–C7; `notes` closes its files by hand). What it would take: on ordinary exits
  nothing new — the cleanup is code in the coroutine body and the split pass handles
  its suspension like any other; on `co_destroy`, destruction becomes an unwind that
  can wait — `co_destroy` may return `CO_SUSPENDED`, and whoever runs the coroutine
  resumes it until `CO_DONE`; the end-of-block destroy of a `co_alloca` frame forwards
  the suspensions in a coroutine, as `await` does, and must trap in an ordinary
  function, which has nobody to forward them to — a run-time trap, since the
  sub-coroutine's body may be in another unit.
- **`exit()` as a trap** is Braam's own limitation. If it matters, the runtime can
  make `exit` set a sticky status and have every libc coroutine return an error
  afterwards, so the program unwinds by its own returns — the pattern Braam's `vi`
  port uses.
- **`main(void)` in Braam mode.** `main` must be `coro(braam_call *) int main(int,
  char **)`; a wrapper for the `(void)` form, or for a plain `main` that never blocks,
  is small if wanted.
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
  after phases C5 and C6 (shared code). BESM-6 output unchanged.
- By hand, plain wasm32: `build/cc/cc -t wasm32 gen.c -o gen.wasm && node
  libc/wasm32/run.mjs gen.wasm`.
- By hand, Braam without Braam: `build/cc/cc -t wasm32-braam cat.c -o cat &&
  node libc/wasm32/braam/run.mjs cat < input`.
- On Braam: the ctest `braam-system` (§7.5); by hand, `fimport` the binary in a
  Braam tab and run `/import/<name>` (docs/Braam_Example.md §4).
- `wasm-objdump -x cat` shows the three imports, the five exports, no `memory`
  export, and one `braam` section; `wasm-validate` passes.
