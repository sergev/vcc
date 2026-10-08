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
