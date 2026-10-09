//
// Coroutines run under node (docs/Coroutines_in_C.md; backend/wasm/Plan.md §6): the
// split pass's state machines, the runtime of libc/wasm32/co.c, and co_alloca's
// shadow-stack builtins.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// The generator of the tutorial, on a frame made by co_alloca.
TEST_F(WasmTest, CoroRange)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("3\n4\n5\n6\ndone 1\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(int) void range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
        if (yield i == CO_CANCEL)
            return;
}

int main(void)
{
    co_frame(int, void) *f = co_alloca(range, 0, 3, 7);
    while (co_resume(f) == CO_SUSPENDED)
        printf("%d\n", co_value(f));
    printf("done %d\n", co_done(f));
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Two frames of one coroutine, interleaved: one in static storage by co_init, one by
// co_alloca; long long values and results.
TEST_F(WasmTest, CoroFibonacciInterleaved)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0 (0) 1 (1) 1 (1) 2 (2) 3 (3) 5 8 13 21 34 -> 10 5\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(long long) int fib(int n)
{
    long long a = 0, b = 1;
    for (int i = 0; i < n; i++) {
        yield a;
        long long t = a + b;
        a = b;
        b = t;
    }
    return n;
}

static _Alignas(16) char storage[256];

int main(void)
{
    co_frame(long long, int) *f = co_init(storage, sizeof storage, fib, 10);
    co_frame(long long, int) *g = co_alloca(fib, 0, 5);
    while (co_resume(f) == CO_SUSPENDED) {
        printf("%lld ", co_value(f));
        if (!co_done(g) && co_resume(g) == CO_SUSPENDED)
            printf("(%lld) ", co_value(g));
    }
    printf("-> %d %d\n", co_result(f), co_result(g));
    return 0;
}
)"));
}

// A local whose address is yielded lives in the frame, so the pointer stays good
// while the coroutine is suspended.
TEST_F(WasmTest, CoroAddressYielded)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0 10 20 30 40 50 \n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(int *) void cells(int n)
{
    struct { int v[4]; } box = { { 0 } };
    for (int i = 0; i < n; i++) {
        box.v[i & 3] = i * 10;
        yield &box.v[i & 3];
    }
}

int main(void)
{
    co_frame(int *, void) *c = co_alloca(cells, 0, 6);
    while (co_resume(c) == CO_SUSPENDED)
        printf("%d ", *co_value(c));
    printf("\n");
    return 0;
}
)"));
}

// Structures yielded, returned and passed; co_destroy of a suspended coroutine runs its
// defer, as does its return.
TEST_F(WasmTest, CoroStructures)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("2 2 103\n3 2 203\n4 2 303\nwalk done\nresult 4 2 303\nwalk done\ndestroy 1 done 1\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

struct pt { int x, y; long long z; };

coro(struct pt) struct pt walk(struct pt from, int steps)
{
    defer printf("walk done\n");
    for (int i = 0; i < steps; i++) {
        from.x++;
        from.z += 100;
        yield from;
    }
    return from;
}

int main(void)
{
    struct pt p = { 1, 2, 3 };
    co_frame(struct pt, struct pt) *w = co_alloca(walk, 0, p, 3);
    while (co_resume(w) == CO_SUSPENDED) {
        struct pt q = co_value(w);
        printf("%d %d %lld\n", q.x, q.y, q.z);
    }
    struct pt r = co_result(w);
    printf("result %d %d %lld\n", r.x, r.y, r.z);

    co_frame(struct pt, struct pt) *w2 = co_alloca(walk, 0, p, 5);
    co_resume(w2);
    int st = co_destroy(w2);
    printf("destroy %d done %d\n", st, co_done(w2));
    return 0;
}
)"));
}

// A co_alloca frame lives until the end of its block, left any way: a loop's body each
// iteration (the shadow stack where it was after), return, goto back, and the end of
// main.  An unfinished coroutine is destroyed there, its defer run; co_cancel delivers
// CO_CANCEL at its yield.
TEST_F(WasmTest, CoroAllocaReleases)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("loop 50 kept 1\n[1 gone]\nfirst over 3: 4\n[7 gone]\n[7 gone]\n[8 cancelled]\n"
              "[8 gone]\ncancel 1\n[9 gone]\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

static int gone;

coro(int) void count(int id, int n)
{
    defer {
        if (id < 100)
            printf("[%d gone]\n", id);
        else
            gone++;
    }
    for (int i = 0; i < n; i++)
        if (yield i == CO_CANCEL) {
            printf("[%d cancelled]\n", id);
            return;
        }
}

static unsigned long depth(void)
{
    volatile int here;
    return (unsigned long)&here;
}

static int first_over(int limit)
{
    co_frame(int, void) *c = co_alloca(count, 0, 1, 100);
    while (co_resume(c) == CO_SUSPENDED)
        if (co_value(c) > limit)
            return co_value(c);
    return -1;
}

int main(void)
{
    unsigned long before = depth();
    for (int k = 0; k < 1000; k++) {
        co_frame(int, void) *c = co_alloca(count, 64, 100 + k, 2);
        co_resume(c);
        if (k == 49)
            break;
        if (k == 48)
            continue;
    }
    printf("loop %d kept %d\n", gone, depth() == before);
    printf("first over 3: %d\n", first_over(3));

    int tries = 0;
again:
    {
        co_frame(int, void) *c = co_alloca(count, 0, 7, 10);
        co_resume(c);
        if (++tries < 2)
            goto again;
    }
    {
        co_frame(int, void) *c = co_alloca(count, 0, 8, 10);
        co_resume(c);
        printf("cancel %d\n", co_cancel(c));
    }
    co_frame(int, void) *c = co_alloca(count, 0, 9, 10);
    co_resume(c);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// yield inside expressions, a switch, nested loops and past a goto; double and long
// double values and a pointer into a local array held across the suspensions; a static
// coroutine.  The same program without coroutines prints the same.
TEST_F(WasmTest, CoroMixed)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0.5 -3 3.25 3 -10 10.25 10 | 7 6.5\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

static int twice(int *p) { return *p * 2; }

static coro(double) long double mix(long double base, int n)
{
    double acc = 0.5;
    int arr[5] = { 1, 2, 3, 4, 5 };
    int *q     = arr;
    long double total = base;
    for (int i = 0; i < n; i++) {
        switch (i % 3) {
        case 0:
            acc = acc * 2 + (yield acc) + twice(q);
            break;
        case 1:
            for (int j = 0; j < 2; j++) {
                if (j == 1)
                    goto skip;
                yield -acc;
            }
        skip:
            q++;
            break;
        default:
            total += *q + (yield acc + 0.25);
        }
    }
    return total;
}

int main(void)
{
    co_frame(double, long double) *m = co_alloca(mix, 0, 1.5L, 7);
    int n = 0;
    while (co_resume(m) == CO_SUSPENDED)
        printf("%g ", co_value(m)), n++;
    printf("| %d %g\n", n, (double)co_result(m));
    return 0;
}
)"));
}

// A coroutine of another unit, known by its prototype: co_sizeof and co_alignof read
// its descriptor, co_alloca uses it.  And one defined after its use in the same unit.
TEST_F(WasmTest, CoroTwoUnits)
{
    SKIP_IF_NO_WASM32_TOOLS();
    std::string squares = CompileToWasm(R"(
#include <coro.h>
coro(int) long squares(int n)
{
    long sum = 0;
    for (int i = 1; i <= n; i++) {
        sum += i * i;
        yield i * i;
    }
    return sum;
}
)");
    NextUnit();
    std::string s_path = ScratchPath("-squares.s"), o_path = ScratchPath("-squares.o");
    FILE *f            = fopen(s_path.c_str(), "w");
    ASSERT_NE(nullptr, f);
    fputs(squares.c_str(), f);
    fclose(f);
    std::vector<std::string> as = Config().assembler;
    as.insert(as.end(), { "-o", o_path, s_path });
    ASSERT_EQ(0, RunTool(as, ScratchPath("-squares.log")));
    Config().extra_libs.push_back(o_path);

    EXPECT_EQ("size 44 align 4\n1 4 9 16 = 30\nhello\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>
coro(int) long squares(int n);
coro(char) void later(void);
int main(void)
{
    printf("size %d align %d\n", (int)co_sizeof(squares), (int)co_alignof(squares));
    co_frame(int, long) *s = co_alloca(squares, 0, 4);
    while (co_resume(s) == CO_SUSPENDED)
        printf("%d ", co_value(s));
    printf("= %ld\n", co_result(s));
    co_frame(char, void) *l = co_alloca(later, 0);
    while (co_resume(l) == CO_SUSPENDED)
        putchar(co_value(l));
    putchar('\n');
    return 0;
}
coro(char) void later(void)
{
    for (const char *p = "hello"; *p; p++)
        yield *p;
}
)"));
}

// Each trap of the runtime: its name, and the program ends with status 255.
static const char *const trap_program = R"(
#include <coro.h>
#include <stdio.h>
co_frame(int, int) *self;
coro(int) int g(int n) { if (n == 9) co_resume(self); yield n; return n; }
char buf[64];
int main(void)
{
    co_frame(int, int) *f;
    TRAP
    puts("not reached");
    return 0;
}
)";

static std::string TrapProgram(const char *body)
{
    std::string s = trap_program;
    s.replace(s.find("TRAP"), 4, body);
    return s;
}

TEST_F(WasmTest, CoroTraps)
{
    SKIP_IF_NO_WASM32_TOOLS();
    struct {
        const char *body, *trap;
    } cases[] = {
        { "f = co_init(buf + 1, 60, g, 1);", "CO_TRAP_STORAGE" },
        { "f = co_init(buf, 8, g, 1);", "CO_TRAP_STORAGE" },
        { "self = f = co_init(buf, sizeof buf, g, 9); co_resume(f);", "CO_TRAP_REENTRANT" },
        { "f = co_alloca(g, 0, 1); co_resume(f); co_resume(f); co_resume(f);",
          "CO_TRAP_FINISHED" },
        { "f = co_alloca(g, 0, 1); co_value(f);", "CO_TRAP_NO_VALUE" },
        { "f = co_alloca(g, 0, 1); co_resume(f); co_result(f);", "CO_TRAP_NOT_DONE" },
        { "f = co_alloca(g, 0, 1); co_destroy(f); co_result(f);", "CO_TRAP_NOT_DONE" },
    };
    for (const auto &c : cases) {
        std::string out = CompileAndRunWasm(TrapProgram(c.body));
        EXPECT_EQ(std::string("coroutine trap: ") + c.trap + "\n", out) << c.body;
        EXPECT_EQ(255, exit_status) << c.body;
        NextUnit();
    }
}

// Delegation: read_exact and read_header of the tutorial, against a scheduler written
// in the program that serves the requests out of a string, a few bytes at a time.
TEST_F(WasmTest, CoroAwaitReadHeader)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("req 0 8\nreq 0 5\nreq 0 2\nheader 0: 1234 abcd\nreq 0 8\nreq 0 5\nreq 0 3\nshort -1\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>
#include <string.h>

typedef struct { int fd; void *buf; size_t len; int out; } io_req;
struct header { char a[4], b[4]; };

coro(io_req *) int read_exact(int fd, char *p, size_t n)
{
    size_t got = 0;
    while (got < n) {
        io_req r = { .fd = fd, .buf = p + got, .len = n - got };
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
    int n = await read_exact(fd, (char *)h, sizeof *h);
    return n == sizeof *h ? 0 : -1;
}

static const char *input;

static void serve(io_req *r)
{
    printf("req %d %d\n", r->fd, (int)r->len);
    size_t n = strlen(input);
    if (n > 3)
        n = 3;
    if (n > r->len)
        n = r->len;
    memcpy(r->buf, input, n);
    input += n;
    r->out = n;
}

static int run(const char *text, struct header *h)
{
    static _Alignas(16) char storage[512];
    input = text;
    co_frame(io_req *, int) *t = co_init(storage, sizeof storage, read_header, 0, h);
    while (co_resume(t) == CO_SUSPENDED)
        serve(co_value(t));
    return co_result(t);
}

int main(void)
{
    struct header h;
    int st = run("1234abcd", &h);
    printf("header %d: %.4s %.4s\n", st, h.a, h.b);
    printf("short %d\n", run("12345", &h));
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// A recursive coroutine: an in-order tree walk, each level a frame on the arena.  The
// arena is given back as each await finishes, so a second walk fits as the first did.
TEST_F(WasmTest, CoroAwaitRecursive)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("1 2 3 4 5 6 7 = 7\n1 2 3 4 5 6 7 = 7\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

struct node { int key; struct node *left, *right; };

coro(int) int walk(struct node *t)
{
    if (!t)
        return 0;
    int n = await walk(t->left);
    yield t->key;
    return n + 1 + await walk(t->right);
}

static struct node n1 = { 1 }, n3 = { 3 }, n5 = { 5 }, n7 = { 7 };
static struct node n2 = { 2, &n1, &n3 }, n6 = { 6, &n5, &n7 };
static struct node n4 = { 4, &n2, &n6 };

int main(void)
{
    static _Alignas(16) char storage[1024];
    for (int k = 0; k < 2; k++) {
        co_frame(int, int) *w = co_init(storage, sizeof storage, walk, &n4);
        while (co_resume(w) == CO_SUSPENDED)
            printf("%d ", co_value(w));
        printf("= %d\n", co_result(w));
    }
    return 0;
}
)"));
}

// A three-level chain cancelled at the bottom: co_cancel of the root reaches the yield
// of the innermost coroutine, and each level's defers run as it returns.
TEST_F(WasmTest, CoroAwaitCancelChain)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0 1 2 [bottom -1] [middle -1] [top -2]\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(int) int bottom(void)
{
    int r = 0;
    defer printf("[bottom %d] ", r);
    for (int i = 0;; i++)
        if (yield i == CO_CANCEL) {
            r = -1;
            return r;
        }
}

coro(int) int middle(void)
{
    int r = await bottom();
    defer printf("[middle %d] ", r);
    return r;
}

coro(int) int top(void)
{
    int r = await middle() - 1;
    defer printf("[top %d]", r);
    return r;
}

int main(void)
{
    co_frame(int, int) *t = co_alloca(top, 256);
    for (int i = 0; i < 3; i++) {
        co_resume(t);
        printf("%d ", co_value(t));
    }
    int st = co_cancel(t);
    printf("\n");
    return st == CO_DONE && co_result(t) == -2 ? 0 : 1;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// co_destroy of a coroutine suspended in an await: the sub-coroutine is destroyed
// first, then the defers of the awaiter's two open scopes, innermost first.  The arena
// is given back, so the next chain fits.  And the explicit form, on a frame by co_init.
TEST_F(WasmTest, CoroAwaitDestroy)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("v 10\n[sub]\n[inner]\n[outer]\ndone 1\nv 10\nv 11\n[sub]\nexplicit 5 7\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(long) int sub(int n)
{
    defer puts("[sub]");
    for (int i = 0; i < n; i++)
        yield 10 + i;
    return n + 3;
}

coro(long) int outer(void)
{
    defer puts("[outer]");
    {
        defer puts("[inner]");
        return await sub(2);
    }
}

coro(long) int explicit(int n)
{
    static _Alignas(16) char storage[128];
    co_frame(long, int) *s = co_init(storage, sizeof storage, sub, n);
    int r = await s;
    return r + n;
}

int main(void)
{
    static _Alignas(16) char storage[256];
    co_frame(long, int) *o = co_init(storage, sizeof storage, outer);
    co_resume(o);
    printf("v %ld\n", co_value(o));
    co_destroy(o);
    printf("done %d\n", co_done(o));

    co_frame(long, int) *e = co_init(storage, sizeof storage, explicit, 2);
    while (co_resume(e) == CO_SUSPENDED)
        printf("v %ld\n", co_value(e));
    printf("explicit 5 %d\n", co_result(e));
    return 0;
}
)"));
}

// A coroutine driving a generator of another yield type through co_alloca: the frame
// comes off the task's arena, and is destroyed and given back at the end of its block,
// the loop's body each iteration.  co_destroy of the root cascades through two levels
// of co_alloca.
TEST_F(WasmTest, CoroAllocaInCoroutine)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("a b c |[letters gone]  a b c |[letters gone]  a b c x\n"
              "[digits 2 gone] [letters gone] [root gone]\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(int) void digits(int n)
{
    defer printf("[digits %d gone] ", n);
    for (int i = 0; i < n; i++)
        yield '0' + i;
}

coro(char) void letters(int n)
{
    defer printf("[letters gone] ");
    for (int i = 0; i < n; i++)
        yield 'a' + i;
    co_frame(int, void) *d = co_alloca(digits, 0, 2);
    co_resume(d);
    yield 'x';
    yield 'y';
}

coro(char) int root(void)
{
    defer printf("[root gone]\n");
    for (int k = 0; k < 2; k++) {
        co_frame(char, void) *l = co_alloca(letters, 64, 3);
        for (int i = 0; i < 3 && co_resume(l) == CO_SUSPENDED; i++)
            yield co_value(l);
        yield '|';
    }
    co_frame(char, void) *l = co_alloca(letters, 64, 3);
    while (co_resume(l) == CO_SUSPENDED)
        yield co_value(l);
    return 0;
}

int main(void)
{
    static _Alignas(16) char storage[96 + 2 * 160];
    co_frame(char, int) *r = co_init(storage, sizeof storage, root);
    for (int i = 0; co_resume(r) == CO_SUSPENDED; i++) {
        printf(i ? " %c" : "%c", co_value(r));
        if (co_value(r) == 'x')
            break;
    }
    printf("\n");
    co_destroy(r);
    return 0;
}
)"));
}

// An arena with room for two frames more than the root: the third await traps, and
// names the coroutine.
TEST_F(WasmTest, CoroArenaOverflow)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("depth 0\ndepth 1\ndepth 2\ncoroutine trap: CO_TRAP_NO_SPACE: deep\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

coro(void) int deep(int n)
{
    printf("depth %d\n", n);
    return n < 1000 ? await deep(n + 1) : n;
}

int main(void)
{
    co_frame(void, int) *d = co_alloca(deep, 2 * co_sizeof(deep), 0);
    co_resume(d);
    puts("not reached");
    return 0;
}
)"));
    EXPECT_EQ(255, exit_status);
}

// await forwards a structure value unchanged and takes a structure result; one whose
// result is a long double, awaited inside an expression.
TEST_F(WasmTest, CoroAwaitStructures)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("(1,10) (2,20) (3,30) (9,9) -> 6 60 | 7.5\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

struct pt { int x; long long y; };

coro(struct pt) struct pt line(int n)
{
    struct pt sum = { 0, 0 };
    for (int i = 1; i <= n; i++) {
        struct pt p = { i, 10 * i };
        yield p;
        sum.x += p.x;
        sum.y += p.y;
    }
    return sum;
}

coro(struct pt) long double outer(void)
{
    struct pt s = await line(3);
    yield (struct pt){ 9, 9 };
    printf("-> %d %lld | ", s.x, s.y);
    return 1.5L + (await line(0)).x + s.x;
}

int main(void)
{
    co_frame(struct pt, long double) *o = co_alloca(outer, 512);
    while (co_resume(o) == CO_SUSPENDED) {
        struct pt p = co_value(o);
        printf("(%d,%lld) ", p.x, p.y);
    }
    printf("%g\n", (double)co_result(o));
    return 0;
}
)"));
}

// coro_ptr (phase C9): a table of coroutines of one type that take (void) or
// (void *), set up, measured and started through the pointers; await of a coro_ptr in a
// coroutine; co_alloca through one; pointers compared.
TEST_F(WasmTest, CoroPtr)
{
    EXPECT_EQ("task 0: header 1 align 4\ntask 1: header 1 align 4\ntask 2: header 1 align 4\n"
              "  0 yields 1\n  1 yields 100\n  2 yields 1\n  0 yields 2\n  1 yields 200\n"
              "  2 yields 2\n  0 yields 3\n  1 returns 7\n  2 returns 21\n  0 returns 30\n"
              "alloca'd: 100, same 1, null 1\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

static coro(int) int count(void *arg)
{
    int n = *(int *)arg;
    for (int i = 1; i <= n; i++)
        yield i;
    return n * 10;
}

static coro(int) int twice(void)
{
    yield 100;
    yield 200;
    return 7;
}

static coro(int) int chain(void *arg)
{
    coro_ptr(int, int) p = count;
    int r = await p(arg);
    return r + 1;
}

coro_ptr(int, int) table[] = { count, twice, chain };

int main(void)
{
    int three = 3, two = 2;
    void *args[] = { &three, 0, &two };
    static char storage[3][1024];
    co_frame(int, int) *f[3];
    for (int i = 0; i < 3; i++) {
        printf("task %d: header %d align %d\n", i, co_sizeof(table[i]) > 24, (int)co_alignof(table[i]));
        f[i] = co_init(storage[i], sizeof storage[i], table[i], args[i]);
    }
    int live = 3;
    while (live) {
        live = 0;
        for (int i = 0; i < 3; i++) {
            if (co_done(f[i]))
                continue;
            if (co_resume(f[i]) == CO_SUSPENDED) {
                printf("  %d yields %d\n", i, co_value(f[i]));
                live++;
            } else {
                printf("  %d returns %d\n", i, co_result(f[i]));
            }
        }
    }
    coro_ptr(int, int) q = twice;
    co_frame(int, int) *g = co_alloca(q, 0);
    co_resume(g);
    printf("alloca'd: %d, same %d, null %d\n", co_value(g), q == table[1], q != 0);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// coro_ptr across units: the descriptors are defined where the coroutines are, and a
// static table in another unit points at them.
TEST_F(WasmTest, CoroPtrTwoUnits)
{
    SKIP_IF_NO_WASM32_TOOLS();
    std::string gens = CompileToWasm(R"(
#include <coro.h>
coro(int) int upto(void *arg)
{
    for (int i = 1; i <= *(int *)arg; i++)
        yield i;
    return -1;
}
coro(int) int once(void)
{
    yield 42;
    return 0;
}
)");
    NextUnit();
    std::string s_path = ScratchPath("-gens.s"), o_path = ScratchPath("-gens.o");
    FILE *f            = fopen(s_path.c_str(), "w");
    ASSERT_NE(nullptr, f);
    fputs(gens.c_str(), f);
    fclose(f);
    std::vector<std::string> as = Config().assembler;
    as.insert(as.end(), { "-o", o_path, s_path });
    ASSERT_EQ(0, RunTool(as, ScratchPath("-gens.log")));
    Config().extra_libs.push_back(o_path);

    EXPECT_EQ("1 2 3 (-1) 42 (0) \n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>
coro(int) int upto(void *arg);
coro(int) int once(void);
static coro_ptr(int, int) table[] = { upto, once };
int main(void)
{
    int three = 3;
    for (int i = 0; i < 2; i++) {
        co_frame(int, int) *p = co_alloca(table[i], 0, &three);
        while (co_resume(p) == CO_SUSPENDED)
            printf("%d ", co_value(p));
        printf("(%d) ", co_result(p));
    }
    printf("\n");
    return 0;
}
)"));
}

// A dispatch of more than two suspension points is a jump table (phase C9).  Here one
// of them is inside a loop that runs some iterations without suspending, so the table
// enters that loop in the middle: an irreducible region, whose dispatch node the
// table's entry goes through (a trampoline sets the state).  The same output with the
// regional dispatch off and under the skeleton.
static const char *const wide_src = R"(
#include <coro.h>
#include <stdio.h>
coro(int) int wide(int n)
{
    yield -1;
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i;
        if (i % 3 == 0)
            yield i;
    }
    yield -2;
    yield sum;
    return sum * 2;
}
int main(void)
{
    co_frame(int, int) *w = co_alloca(wide, 0, 10);
    while (co_resume(w) == CO_SUSPENDED)
        printf("%d ", co_value(w));
    printf("-> %d\n", co_result(w));
    return 0;
}
)";

TEST_F(WasmTest, CoroWideDispatch)
{
    const char *want = "-1 0 3 6 9 -2 45 -> 90\n";
    EXPECT_EQ(want, CompileAndRunWasm(wide_src));
    NextUnit();
    wasm_regional = false;
    EXPECT_EQ(want, CompileAndRunWasm(wide_src));
    NextUnit();
    wasm_regional  = true;
    wasm_structure = false;
    EXPECT_EQ(want, CompileAndRunWasm(wide_src));
}
