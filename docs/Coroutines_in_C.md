# Coroutines and `defer` in C

vcc extends C with two features:

- **`defer`** runs a cleanup statement when a block is left, whichever way it is left.
- **Coroutines** are functions that can stop in the middle, return to their caller,
  and later continue from where they stopped.

This is a tutorial. It assumes you know ordinary C and nothing else.

> **Status: planned.** None of this is implemented yet. The design and the work are
> in [backend/wasm/Plan.md](../backend/wasm/Plan.md). `defer` will work on every
> target. Coroutines will work on wasm32 only, and they exist for programs that run
> on [Braam](#8-coroutines-on-braam).

## Contents

1. [`defer`](#1-defer)
2. [A first coroutine](#2-a-first-coroutine)
3. [Frames and storage](#3-frames-and-storage)
4. [The operations](#4-the-operations)
5. [`await`: one coroutine calling another](#5-await-one-coroutine-calling-another)
6. [Stopping a coroutine early](#6-stopping-a-coroutine-early)
7. [Rules, errors and traps](#7-rules-errors-and-traps)
8. [Coroutines on Braam](#8-coroutines-on-braam)
9. [Quick reference](#9-quick-reference)

---

## 1. `defer`

### The problem

A function that acquires several things must release them on every way out:

```c
int work(void)
{
    char *a = malloc(100);
    if (!a)
        return -1;
    char *b = malloc(200);
    if (!b) {
        free(a);                /* don't forget */
        return -1;
    }
    if (step(a, b) < 0) {
        free(b);                /* don't forget either */
        free(a);
        return -1;
    }
    free(b);
    free(a);
    return 0;
}
```

Every new exit has to repeat the cleanup, and forgetting it once is a leak.

### The solution

```c
int work(void)
{
    char *a = malloc(100);
    if (!a)
        return -1;
    defer free(a);

    char *b = malloc(200);
    if (!b)
        return -1;              /* frees a */
    defer free(b);

    if (step(a, b) < 0)
        return -1;              /* frees b, then a */
    return 0;                   /* frees b, then a */
}
```

`defer S;` means: **when control leaves the block that contains this `defer`, run
`S`.** You write the cleanup once, next to the acquisition.

### The rules

**It belongs to the enclosing block.** The `{ }` that contains the `defer` is its
scope. In a loop body, the deferred statement runs at the end of every iteration:

```c
for (int i = 0; i < n; i++) {
    char *line = read_line(i);
    defer free(line);           /* runs at the end of each iteration */
    process(line);
}
```

A `defer` written as the body of an `if` without braces belongs to that body. It runs
straight away, because the body ends at once:

```c
if (x)
    defer puts("A");            /* prints A immediately: the body ends here */
```

**Every way out counts.** Falling off the end of the block, `return`, `break`,
`continue`, and a `goto` to a label outside the block all run it.

**Last in, first out.** Several `defer`s in one block run in reverse order:

```c
{
    defer puts("1");
    defer puts("2");
    defer puts("3");
}                               /* prints 3, 2, 1 */
```

That is the right order for resources that depend on each other: lock `a`, then
`b`; unlock `b`, then `a`.

**Nested blocks run inner first:**

```c
{
    defer puts("A");
    {
        defer puts("B");
    }                           /* prints B */
    defer puts("C");
}                               /* prints C, then A */
```

**Only what was reached.** A `defer` takes effect when control passes it. If a
`return` comes before the `defer`, nothing is run for it:

```c
char *p = malloc(10);
if (!p)
    return -1;                  /* the defer below was not reached: nothing runs */
defer free(p);
```

**Variables are read when the deferred statement runs**, not when the `defer` is
reached:

```c
{
    int x = 1;
    defer printf("%d\n", x);
    x = 2;
}                               /* prints 2 */
```

**A block of cleanup is allowed:**

```c
defer {
    unlock(m);
    log_unlock(m);
}
```

**What does not run it:** `exit()`, `abort()`, a trap, or `longjmp`. These do not
leave blocks in the ordinary way.

### What a deferred statement may not do

Control must leave a deferred statement only by reaching its end. So these are
compile-time errors inside a `defer`:

- `return`;
- `break` or `continue` that would leave the deferred statement (a loop entirely
  inside it may use them);
- `goto` to a label outside it;
- `yield` and `await` (section 5).

### Jumps into a block past a `defer`

You may not jump *into* a block to a point after one of its `defer`s. If you could,
the compiler could not tell whether that `defer` was reached.

```c
    goto inside;                /* error: jumps past the defer */
    {
        defer puts("x");
    inside:
        ...
    }
```

The same applies to `switch`: a `case` label that comes after a `defer` in the
switch's block is an error. Give the `case` its own block instead:

```c
switch (c) {
case 1: {
    char *p = malloc(10);
    defer free(p);              /* scope is this case only */
    use(p);
    break;                      /* frees p */
}
case 2:
    ...
}
```

### The cost

None at run time. The compiler copies the deferred statements onto each exit path.
Nothing is allocated and no list is kept while the program runs.

---

## 2. A first coroutine

An ordinary function runs from start to finish in one go. A **coroutine** can stop
in the middle with `yield`, hand a value to whoever is running it, and continue from
the same point the next time it is resumed. All its local variables keep their
values in between.

```c
#include <coro.h>
#include <stdio.h>

coro(int) void count_to(int n)          /* yields ints, returns nothing */
{
    for (int i = 1; i <= n; i++)
        yield i;
}

int main(void)
{
    static _Alignas(16) char storage[256];
    co_frame(int, void) *f = co_init(storage, sizeof storage, count_to, 3);

    while (co_resume(f) == CO_SUSPENDED)
        printf("%d\n", co_value(f));    /* prints 1, 2, 3 */
    return 0;
}
```

Step by step:

1. `coro(int) void count_to(int n)` declares a coroutine. The `int` in `coro(int)` is
   the type of the values it yields. `void` is the type it returns at the end, as
   with any function.
2. `co_init(storage, sizeof storage, count_to, 3)` prepares a run of `count_to(3)` in
   `storage`. **No code of `count_to` runs yet.**
3. `co_resume(f)` runs the coroutine until it reaches a `yield` or finishes. It
   returns `CO_SUSPENDED` if it stopped at a `yield`, or `CO_DONE` if it finished.
4. `co_value(f)` is the value of the last `yield`.
5. The loop ends when `co_resume` returns `CO_DONE`.

You write the loop inside the coroutine as a plain loop. The caller sees a sequence
of values.

`#include <coro.h>` gives the short names `coro`, `yield`, `await`, `defer`,
`co_frame` and `co_init` through `co_alignof`, and declares `co_status` and
`co_signal`. Without the header the long spellings still work: `_Coro`, `_Yield`,
`_Await`, `_Defer`, `_Coro_frame`, `__co_init` and so on. So a program that already
uses a variable named `yield` or `defer` is not affected unless it includes
`<coro.h>`.

### A coroutine that yields nothing

`coro(void)` suspends without a value. Its `yield` is written alone:

```c
coro(void) void blink(void)
{
    for (;;) {
        led_on();
        yield;
        led_off();
        yield;
    }
}
```

Each `co_resume` runs one half-period. `co_value` cannot be used on such a
coroutine.

### A coroutine that returns a value

`return` works as usual. The caller reads the result with `co_result` after
`co_resume` has returned `CO_DONE`:

```c
coro(int) long sum_up(int n)            /* yields each term, returns the total */
{
    long s = 0;
    for (int i = 1; i <= n; i++) {
        s += i;
        yield i;
    }
    return s;
}

    ...
    while (co_resume(f) == CO_SUSPENDED)
        ;                               /* ignore the terms */
    long total = co_result(f);          /* 15 for n = 5 */
```

A coroutine whose return type is not `void` must end with a `return`. Running off
its end is a trap (section 7).

### What may be a coroutine

Any function except:

- a variadic one (`...`);
- an `inline` one;
- one with an old-style (K&R) parameter list.

A coroutine is not an ordinary function. You cannot call it as `count_to(3)`, and
you cannot take its address as a function pointer. You use its name only in
`co_init`, `co_sizeof`, `co_alignof` and `await` (section 5).

A coroutine may call ordinary functions as usual. An ordinary function may run a
coroutine with `co_init` and `co_resume`, but it cannot `yield` or `await` itself.

---

## 3. Frames and storage

Everything a coroutine must remember while it is stopped is kept in its **frame**:

- its arguments;
- the local variables still needed after a `yield`;
- where to continue;
- the last value yielded and, at the end, the result.

**You supply the memory for the frame.** The compiler never allocates any. You give
`co_init` a block of memory and its size:

```c
static _Alignas(16) char storage[256];
co_frame(int, void) *f = co_init(storage, sizeof storage, count_to, 3);
```

The block may be static, a local array, or memory from `malloc`. It must be:

- **large enough**: at least `co_sizeof(count_to)` bytes;
- **aligned** to `co_alignof(count_to)`, which is never more than 16, so
  `_Alignas(16)` is always enough.

If either is wrong, `co_init` traps.

`co_sizeof` and `co_alignof` are known only when the program runs, not when it is
compiled. So you cannot write `char storage[co_sizeof(count_to)]`. Pick a size with
room to spare, or check it:

```c
if (co_sizeof(count_to) > sizeof storage)
    fatal("storage too small");
```

They are run-time values for two reasons. The compiler decides what goes into the
frame only after optimizing the coroutine, which keeps frames small. And you can run
a coroutine from a library without seeing its source.

### The frame pointer

`co_init` returns a `co_frame(Y, T) *`, where `Y` is the yield type and `T` the return
type. You use this pointer with every other operation. `co_frame(Y, T)` is an
incomplete type: you can only have pointers to it, never an object of it.

The pointer type mentions only `Y` and `T`, not which coroutine it is. So one
function can drive any coroutine that yields `int` and returns `void`.

### Rules for the storage

- **Do not move or copy** a frame while it is in use. Its address must stay the same
  from `co_init` to the end.
- **Keep the storage alive** while the frame is in use. If a local array goes out of
  scope while its coroutine is stopped in the middle, anything the coroutine was
  holding is lost. Finish the coroutine, or use `co_destroy` (section 6), first.
- **Each run needs its own frame.** You can run the same coroutine several times at
  once, each in its own storage. The runs do not affect each other:

```c
static _Alignas(16) char s1[256], s2[256];
co_frame(int, void) *a = co_init(s1, sizeof s1, count_to, 2);
co_frame(int, void) *b = co_init(s2, sizeof s2, count_to, 2);
co_resume(a); co_resume(b);             /* a at 1, b at 1 */
co_resume(a);                           /* a at 2, b still at 1 */
```

### Locals and pointers

Local variables of a coroutine live in the frame, and the frame does not move. So a
pointer to a local stays valid while the coroutine is stopped. A coroutine can even
yield the address of its own local; section 5 has an example. The pointer stops
being valid when the coroutine leaves that variable's block.

The compiler decides which locals must live in the frame. You need not think about
it: a value computed before a `yield` and used after it is always still there.

---

## 4. The operations

All of them take the frame pointer `f` from `co_init`, except `co_init`, `co_sizeof`
and `co_alignof`.

| Operation | What it does |
|---|---|
| `co_init(mem, size, g, args...)` | Prepares a run of coroutine `g` with these arguments in `mem`. Runs no code of `g`. Returns the frame pointer. |
| `co_resume(f)` | Runs until the next `yield` (returns `CO_SUSPENDED`) or until the coroutine finishes (returns `CO_DONE`). |
| `co_cancel(f)` | Like `co_resume`, but the `yield` the coroutine is stopped at returns `CO_CANCEL`, asking it to stop (section 6). |
| `co_destroy(f)` | Ends a stopped coroutine without running any more of its code, except its pending `defer`s. Returns `CO_DONE`. |
| `co_done(f)` | True once the coroutine has finished or been destroyed. |
| `co_value(f)` | The value of the last `yield`. Valid only after `co_resume` or `co_cancel` returned `CO_SUSPENDED`. |
| `co_result(f)` | The value the coroutine returned. Valid only after it finished with `CO_DONE`, not after `co_destroy`. |
| `co_sizeof(g)` | Bytes of memory a frame of `g` needs. |
| `co_alignof(g)` | Alignment a frame of `g` needs. At most 16. |

The types:

```c
typedef enum { CO_SUSPENDED, CO_DONE } co_status;      /* what co_resume returns */
typedef enum { CO_CONTINUE, CO_CANCEL } co_signal;     /* what yield returns */
```

`co_init` checks the arguments against the coroutine's parameters as an ordinary
call does, and converts them the same way.

These operations look like function calls but are built into the compiler. You
cannot take their address.

---

## 5. `await`: one coroutine calling another

A coroutine often needs to call another coroutine and wait for its result. `await`
does that:

```c
coro(int) void walk(struct node *t)      /* yields the values of a tree, in order */
{
    if (!t)
        return;
    await walk(t->left);
    yield t->value;
    await walk(t->right);
}
```

`await g(args)` runs coroutine `g` to its end. **Each time `g` yields, the coroutine
doing the `await` yields the same value to its own caller.** When `g` returns, the
`await` expression has `g`'s result as its value. To the caller of `walk`, it looks
as if the values came from `walk` itself.

The example also shows that a coroutine may `await` itself recursively.

### Where the sub-coroutine's frame comes from

You gave `co_init` a block of memory. The front of it holds the frame of the
coroutine you started. **The rest of the block is spare room**, and every `await`
inside that run takes the frame of the coroutine it calls from there. The frame is
given back when that coroutine finishes. `await`s always finish in reverse order of
starting, so this is a simple stack.

So the size you pass to `co_init` must cover the deepest chain of `await`s: for `walk`
on a tree of depth 20, about 21 frames of `walk`. If the room runs out, the program
traps and names the coroutine that did not fit. Make the block bigger.

A coroutine that awaits nothing needs only `co_sizeof` of itself.

### The second form: awaiting a frame you made

You can also make the sub-coroutine's frame yourself and `await` the pointer:

```c
static _Alignas(16) char buf[512];
co_frame(int, void) *sub = co_init(buf, sizeof buf, walk, root);
await sub;
```

Both forms mean the same. The first is shorter and needs no extra storage.

### Yield types must match

An `await` passes the sub-coroutine's yielded values straight up, so its yield type
must be **exactly** the yield type of the coroutine that awaits it. A `coro(int)` can
await only `coro(int)` coroutines. A `coro(void)` can await only `coro(void)` ones.
The return types may differ: the result is the value of the `await` expression.

To use a coroutine with a different yield type, run it by hand with `co_init` and
`co_resume`. Any function or coroutine may do that.

### `await` and `yield` inside expressions

Both are expressions and may appear inside larger ones:

```c
total = total + await read_number();
if ((yield x) == CO_CANCEL) ...
```

`await` binds as tightly as `sizeof`: `await f(x) + 1` means `(await f(x)) + 1`.
`yield` binds tighter than `==`: `yield i == CO_CANCEL` means `(yield i) ==
CO_CANCEL`. Write the parentheses anyway; the code is clearer with them.

### What `await` is, exactly

`await sub` behaves exactly like this loop:

```c
co_signal sig = CO_CONTINUE;
while ((sig == CO_CANCEL ? co_cancel(sub) : co_resume(sub)) == CO_SUSPENDED)
    sig = yield co_value(sub);
/* the value of the await is co_result(sub) */
```

So nothing in the language knows about input, output or scheduling. A yielded value
means whatever the program running the outermost coroutine decides it means.

### An example: requests to a scheduler

A coroutine can yield a *request*, and whoever is running it carries the request out
and resumes it with the answer:

```c
typedef struct {
    int fd;
    void *buf;
    size_t len;
    long out;                           /* filled in by the scheduler */
} io_req;

coro(io_req *) long read_exact(int fd, char *p, size_t n)
{
    size_t got = 0;
    while (got < n) {
        io_req r = { .fd = fd, .buf = p + got, .len = n - got };
        if ((yield &r) == CO_CANCEL)
            return -1;
        if (r.out <= 0)
            return got;                 /* end of input, or an error */
        got += r.out;
    }
    return got;
}

coro(io_req *) int read_header(int fd, struct header *h)
{
    long n = await read_exact(fd, (char *)h, sizeof *h);
    return n == sizeof *h ? 0 : -1;
}
```

The scheduler is ordinary C:

```c
static _Alignas(16) char mem[4096];
co_frame(io_req *, int) *t = co_init(mem, sizeof mem, read_header, fd, &hdr);

while (co_resume(t) == CO_SUSPENDED) {
    io_req *r = co_value(t);
    r->out = do_read(r->fd, r->buf, r->len);
}
int status = co_result(t);
```

`read_exact` yields the address of its local `r`. That is safe: `r` stays in place
while `read_exact` is stopped, and that is exactly when the scheduler uses it.

### Cost

An `await` chain *n* coroutines deep costs *n* resumes for each value that travels up
it. Keep chains reasonably short in hot code.

---

## 6. Stopping a coroutine early

Sometimes the caller no longer wants the rest: the user pressed `^C`, or a search
found what it needed. There are two ways to stop a coroutine before it finishes.

### `co_cancel`: ask it to stop

`yield` returns a `co_signal`. It is `CO_CONTINUE` after `co_resume`, and
`CO_CANCEL` after `co_cancel`. The coroutine decides what to do:

```c
coro(int) void numbers(void)
{
    for (int i = 0; ; i++)
        if ((yield i) == CO_CANCEL)
            return;                     /* clean up and finish */
}

    ...
    co_cancel(f);                       /* numbers returns; co_cancel returns CO_DONE */
```

Cancelling is a request, not a command. A coroutine that ignores the result of
`yield` carries on running until its next `yield`, and `co_cancel` then returns
`CO_SUSPENDED`. A coroutine that tests the signal at every `yield` can always be
cancelled. Nothing is thrown or unwound: the coroutine leaves by its own `return`,
so its `defer`s run as on any `return`.

`await` passes the cancel down: if the outer coroutine is cancelled while it awaits,
the `yield` the inner coroutine is stopped at returns `CO_CANCEL`.

### `co_destroy`: end it now

`co_destroy(f)` runs none of the coroutine's ordinary code. It runs only the
`defer`s that are active at the `yield` where it stopped, innermost first, and marks
the frame finished. If the coroutine is inside an `await`, the inner coroutine's
`defer`s run first.

```c
coro(int) void reader(void)
{
    char *buf = malloc(4096);
    defer free(buf);
    for (;;)
        yield fill(buf);
}

    ...
    co_resume(f);
    co_resume(f);
    co_destroy(f);                      /* frees buf; nothing else of reader runs */
```

After `co_destroy`, `co_done(f)` is true and `co_result(f)` is not available. Do not
call `co_destroy` on a frame that has already finished: check `co_done(f)` first.

### `defer` across `yield`

A `yield` does not leave any block. The `defer`s of a stopped coroutine stay pending.
They run when the coroutine later leaves their block, or at `co_destroy`:

```c
coro(void) void task(void)
{
    lock(&a);
    defer unlock(&a);
    {
        lock(&b);
        defer unlock(&b);
        yield;                          /* a and b both still locked */
    }                                   /* unlock(&b) */
    yield;                              /* a still locked */
}                                       /* unlock(&a) */
```

So a resource held across a `yield` stays held while the coroutine is stopped. To
release it before stopping, close its block before the `yield`.

A deferred statement may not contain `yield` or `await`. Cleanup always runs to its
end without stopping.

---

## 7. Rules, errors and traps

### Compile-time errors

- `yield` or `await` outside a coroutine, or inside a deferred statement.
- `yield expr;` in a `coro(void)`, or a bare `yield;` in any other coroutine.
- `await` of a coroutine whose yield type is not exactly yours.
- `co_value` on a `coro(void)` frame; `co_result` on a frame whose return type is
  `void`.
- A coroutine that is variadic, `inline`, or has a K&R parameter list.
- A coroutine name used other than in `co_init`, `co_sizeof`, `co_alignof` or
  `await`: called directly, assigned, or converted to a function pointer.
- A jump into a block past one of its `defer`s, including a `case` label.
- `return`, `goto`, or a `break`/`continue` that leaves a deferred statement.
- Coroutines on a target other than wasm32.

### Traps

These stop the program with a message naming the trap, in every build:

| Trap | Cause |
|---|---|
| `CO_TRAP_STORAGE` | `co_init` given memory that is too small or badly aligned |
| `CO_TRAP_REENTRANT` | a coroutine resumes its own frame, directly or through others |
| `CO_TRAP_FINISHED` | `co_resume`, `co_cancel` or `co_destroy` on a finished frame |
| `CO_TRAP_NO_VALUE` | `co_value` when the last resume did not return `CO_SUSPENDED` |
| `CO_TRAP_NOT_DONE` | `co_result` before the coroutine finished, or after `co_destroy` |
| `CO_TRAP_NO_RETURN` | a coroutine with a non-`void` return type ran off its end |
| `CO_TRAP_NO_SPACE` | an `await` found no room left in the block given to `co_init` |

### What the language does not catch

- Memory given to `co_init` that goes away while the coroutine is stopped. Finish or
  `co_destroy` the coroutine first.
- Copying or moving a frame's memory.
- Two threads resuming one frame. (wasm32 has no threads.)

Not yet available: pointers to coroutines, for a scheduler that keeps a list of
different coroutines. They are planned for later.

---

## 8. Coroutines on Braam

Braam is an operating system that runs in a browser tab. A Braam program may never
wait inside a call. When it needs input, it sends a request to the kernel, returns,
and is started again later with the answer. Coroutines let you write such a program
as ordinary C. Each blocking call is an `await`. The C runtime, not your code,
returns to the kernel and continues where you stopped.

Compile for Braam with `vcc -t wasm32-braam`. The output is a program Braam can
install and run.

### `main`

`main` is a coroutine that yields Braam requests:

```c
#include <coro.h>
#include <stdio.h>
#include <unistd.h>

coro(braam_call *) int main(int argc, char **argv)
{
    char buf[512];
    ssize_t n;
    while ((n = await read(0, buf, sizeof buf)) > 0)
        if (await write(1, buf, n) < 0)
            return 1;
    return n < 0;
}
```

That is `cat`. On Braam `main` must have this form: a `coro(braam_call *)` coroutine
taking `argc` and `argv`. The runtime builds `main`'s frame in a static block. Every `await`
inside `main` takes its frame from that block (section 5). If a program needs a
bigger block, it defines its size:

```c
const unsigned __braam_task_bytes = 256 * 1024;
```

### Blocking calls are coroutines

Every function that may have to wait keeps its usual C name and arguments. It is
declared as `coro(braam_call *)`, so you put `await` in front of the call:

| Ordinary C | On Braam |
|---|---|
| `n = read(fd, buf, len);` | `n = await read(fd, buf, len);` |
| `n = write(fd, buf, len);` | `n = await write(fd, buf, len);` |
| `fd = open(path, O_RDONLY);` | `fd = await open(path, O_RDONLY);` |
| `close(fd);` | `await close(fd);` |
| `c = fgetc(f);` | `c = await fgetc(f);` |
| `fgets(s, n, f);` | `await fgets(s, n, f);` |
| `stat(path, &st);` | `await stat(path, &st);` |
| `fflush(stdout);` | `await fflush(stdout);` |
| `sleep(1);` | `await sleep_ms(1000);` |

A function of yours that calls any of these must itself be a
`coro(braam_call *)` coroutine, and its callers must `await` it, up to `main`.
Functions that never wait (`strlen`, `malloc`, `memcpy`, `qsort`, `snprintf`, …) are
ordinary and are called as usual.

### Output

`printf`, `puts` and `putchar` do not wait: they add to the output buffer. The buffer
is written out when you `await fflush(stdout)`, when a read from standard input
starts, and when `main` returns. So a prompt appears before the program reads the
answer, and nothing is lost at the end.

### Ending the program

Return from `main`. The return value is the exit status.

`exit()` cannot return through the coroutines that are running. On Braam it records
the status and stops the program as a crash. Return from `main` instead.

### Letting `^C` in

The kernel can deliver a signal only while the program waits for something. A long
computation that never `await`s cannot be interrupted. Call `await braam_yield();`
from time to time inside such a loop. It gives the kernel a turn.

### Cleanup on Braam

Use `defer` for memory, buffers and counters. A file descriptor is closed by `await
close(fd)`, which cannot be in a `defer`, because a deferred statement may not
`await`. Close descriptors yourself at the point you choose.

---

## 9. Quick reference

```c
#include <coro.h>

coro(Y) T g(params) { ... }             /* declare a coroutine: yields Y, returns T */

yield e;                                 /* stop, hand e to the caller (Y not void) */
yield;                                   /* stop (Y is void) */
co_signal s = (yield e);                 /* CO_CONTINUE or CO_CANCEL */

T r = await g(args);                     /* run g to its end, passing its yields up */
T r = await f;                           /* the same, on a frame made with co_init */

defer statement;                         /* run statement when this block is left */

static _Alignas(16) char mem[N];
co_frame(Y, T) *f = co_init(mem, sizeof mem, g, args);

co_status st = co_resume(f);             /* CO_SUSPENDED or CO_DONE */
co_status st = co_cancel(f);             /* resume, yield returns CO_CANCEL */
co_destroy(f);                           /* run pending defers, finish */
int done = co_done(f);
Y v = co_value(f);                       /* after CO_SUSPENDED */
T r = co_result(f);                      /* after CO_DONE */
size_t n = co_sizeof(g), a = co_alignof(g);
```

| Short name | Long name |
|---|---|
| `coro(Y)` | `_Coro(Y)` |
| `yield` | `_Yield` |
| `await` | `_Await` |
| `defer` | `_Defer` |
| `co_frame(Y, T)` | `_Coro_frame(Y, T)` |
| `co_init` … `co_alignof` | `__co_init` … `__co_alignof` |
