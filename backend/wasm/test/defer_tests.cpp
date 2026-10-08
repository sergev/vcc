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
