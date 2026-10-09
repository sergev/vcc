//
// defer, run under node (docs/Coroutines_in_C.md, section 1): the deferred
// statements on every way out of their block, in order.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// Fall-off, return, nested blocks and the unbraced body of an if.
TEST_F(WasmTest, DeferOrder)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("B\nA\nC\nf\n1\nB\nA\nf\n20\nnow\nend\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

static int f(int x)
{
    defer puts("f");
    {
        defer puts("A");
        defer puts("B");
        if (x)
            return x * 10;
    }
    defer puts("C");
    return 1;
}

int main(void)
{
    printf("%d\n", f(0));
    printf("%d\n", f(2));
    if (1)
        defer puts("now");
    puts("end");
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// break and continue leave the loop body; a goto leaves blocks, and jumps back over
// a defer of its own block, which runs.
TEST_F(WasmTest, DeferJumps)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("body 0\niter 0\niter 1\niter 2\nonce\nleft 0\nleft 1\nleft 2\nn=3 back=2\n",
              CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

int main(void)
{
    for (int i = 0; i < 5; i++) {
        defer printf("iter %d\n", i);
        if (i == 1)
            continue;
        if (i == 2)
            break;
        printf("body %d\n", i);
    }
    while (1) {
        defer puts("once");
        break;
    }
    int n = 0;
again:
    {
        defer printf("left %d\n", n++);
        if (n < 2)
            goto again;
    }
    int back = 0;
loop:
    if (back < 2) {
        defer back++;
        goto loop;
    }
    printf("n=%d back=%d\n", n, back);
    return 0;
}
)"));
}

// The value returned is taken before the defers run, a scalar and a structure alike.
TEST_F(WasmTest, DeferReturnValue)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
#include <coro.h>

struct S { int a, b, c, d; };

static int scalar(void) { int r = 1; defer r = 2; return r; }
static struct S aggregate(void) { struct S s = { 1, 2, 3, 4 }; defer s.a = 9; return s; }
static int through(int *p) { defer *p = 7; return *p; }

int main(void)
{
    int v = 3;
    struct S s = aggregate();
    if (scalar() != 1) return 1;
    if (s.a != 1 || s.d != 4) return 2;
    if (through(&v) != 3 || v != 7) return 3;
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// A deferred statement with a loop, a switch, a label and a defer of its own, lowered
// once on each of three exits, each copy with labels of its own.
TEST_F(WasmTest, DeferComplexBody)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("0 1 2 two | in\n0 1 2 two | in\n0 1 two | in\n", CompileAndRunWasm(R"(
#include <coro.h>
#include <stdio.h>

static void f(int x)
{
    int n = 3;
    defer {
        defer puts("in");
        for (int i = 0; i < 10; i++) {
            if (i == n)
                break;
            printf("%d ", i);
        }
        switch (n) {
        case 2:
        case 3:
            printf("two ");
            break;
        default:
            printf("other ");
        }
        goto done;
    done:
        printf("| ");
    }
    if (x == 1)
        return;
    if (x == 2)
        n = 2;
}

int main(void)
{
    f(0);
    f(1);
    f(2);
    return 0;
}
)"));
}

// Large cleanups shared by the exits that leave them: break, continue, a
// goto out of two blocks, returns of a value from three depths, each with a chain of
// several statements in blocks inside one another; and a coroutine destroyed at each of
// its suspension points.  The same output with every exit lowering its own copy.
static const char *const shared_src = R"(
#include <coro.h>
#include <stdio.h>

static int trace[64], n;
static void note(int x) { trace[n++] = x; }

static int returns(int k)
{
    int total = 0;
    defer { note(1); note(2); note(3); note(4); }
    {
        defer { note(10); note(11); note(12); note(13); }
        if (k == 0)
            return total + 100;
        for (int i = 0; i < 3; i++) {
            defer { note(20 + i); note(30 + i); note(40); note(41); }
            if (i == k)
                return total + i;
            if (i == 1 && k == 9)
                continue;
            if (i == 2 && k == 8)
                break;
            total += 1000;
        }
        if (k == 7)
            goto out;
        total += 5;
    }
    note(99);
out:
    return total;
}

static coro(int) void gen(void)
{
    defer { note(50); note(51); note(52); note(53); }
    for (int i = 0; i < 4; i++) {
        defer { note(60 + i); note(70); note(71); note(72); }
        yield i;
    }
}

int main(void)
{
    int ks[] = { 0, 1, 2, 7, 8, 9, 5 };
    for (int j = 0; j < 7; j++) {
        n = 0;
        int r = returns(ks[j]);
        printf("%d:%d:", ks[j], r);
        for (int i = 0; i < n; i++)
            printf(" %d", trace[i]);
        printf("\n");
    }
    for (int stop = 0; stop <= 4; stop++) {
        n = 0;
        static char storage[1024];
        co_frame(int, void) *g = co_init(storage, sizeof storage, gen);
        for (int i = 0; i < stop; i++)
            co_resume(g);
        if (!co_done(g))
            co_destroy(g);
        printf("destroy after %d:", stop);
        for (int i = 0; i < n; i++)
            printf(" %d", trace[i]);
        printf("\n");
    }
    return 0;
}
)";

TEST_F(WasmTest, DeferShared)
{
    SKIP_IF_NO_WASM32_TOOLS();
    const char *want =
        "0:100: 10 11 12 13 1 2 3 4\n"
        "1:1001: 20 30 40 41 21 31 40 41 10 11 12 13 1 2 3 4\n"
        "2:2002: 20 30 40 41 21 31 40 41 22 32 40 41 10 11 12 13 1 2 3 4\n"
        "7:3000: 20 30 40 41 21 31 40 41 22 32 40 41 10 11 12 13 1 2 3 4\n"
        "8:2005: 20 30 40 41 21 31 40 41 22 32 40 41 10 11 12 13 99 1 2 3 4\n"
        "9:2005: 20 30 40 41 21 31 40 41 22 32 40 41 10 11 12 13 99 1 2 3 4\n"
        "5:3005: 20 30 40 41 21 31 40 41 22 32 40 41 10 11 12 13 99 1 2 3 4\n"
        "destroy after 0:\n"
        "destroy after 1: 60 70 71 72 50 51 52 53\n"
        "destroy after 2: 60 70 71 72 61 70 71 72 50 51 52 53\n"
        "destroy after 3: 60 70 71 72 61 70 71 72 62 70 71 72 50 51 52 53\n"
        "destroy after 4: 60 70 71 72 61 70 71 72 62 70 71 72 63 70 71 72 50 51 52 53\n";
    EXPECT_EQ(want, CompileAndRunWasm(shared_src));
    NextUnit();
    translate_shared_cleanup = false;
    EXPECT_EQ(want, CompileAndRunWasm(shared_src));
}
