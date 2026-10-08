//
// wasm32 rewrites of the finished code (peephole.c, locals.c), golden under the default
// pipeline: values left on the operand stack, tees, dead values, tests folded into
// comparisons, offsets folded into accesses, stores merged, branches and constructs
// removed, and locals shared.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// A golden test of the default pipeline.
#define EXPECT_OPT(name, expected, src)                \
    TEST_F(WasmTest, name)                             \
    {                                                  \
        EXPECT_EQ(expected, Code(CompileToWasm(src))); \
    }

// Each operand stays on the stack; the members' offsets go into the loads.
EXPECT_OPT(StackifyExpression,
           "local.get 0\ni32.load 0\nlocal.get 1\ni32.load 0\ni32.mul\n"
           "local.get 0\ni32.load 4\nlocal.get 1\ni32.load 4\ni32.mul\ni32.add\nend_function\n",
           R"(
    struct P { int x, y; };
    int dot(struct P *p, struct P *q) { return p->x * q->x + p->y * q->y; }
)")

// A loaded value waits on the stack below the store that follows: nothing moves
// across the store, the load still comes first.
EXPECT_OPT(StackifyBelowStore,
           "local.get 0\ni32.load 0\nlocal.get 0\ni32.const 5\ni32.store 0\ni32.const 5\n"
           "i32.add\nend_function\n",
           "int f(int *p) { int a = *p; *p = 5; return a + *p; }")

// A global read before a call stays before it.
EXPECT_OPT(StackifyBeforeCall, "i32.const 0\ni32.load g\ncall h\ni32.add\nend_function\n", R"(
    int g;
    int h(void);
    int f(void) { int a = g; int b = h(); return a + b; }
)")

// Volatile reads stay in order, each through its own instruction.
EXPECT_OPT(VolatileInOrder,
           "local.get 0\ni32.load 0\nlocal.set 1\nlocal.get 0\ni32.load 0\nlocal.get 1\n"
           "i32.sub\nend_function\n",
           "int f(volatile int *p) { int a = *p; int b = *p; return b - a; }")

// A value set and read at once, then read again: local.tee.
EXPECT_OPT(Tee, "local.get 0\ni32.const 3\ni32.mul\nlocal.tee 0\nlocal.get 0\ni32.mul\nend_function\n",
           "int f(int x) { int y = x * 3; return y * y; }")

// A result nobody reads is dropped.
EXPECT_OPT(DropResult, "call h\ndrop\ni32.const 0\nend_function\n",
           "int h(void); int f(void) { h(); return 0; }")

// An index times a power of two is a shift; == 0 is eqz.
EXPECT_OPT(ShiftAndEqz, "local.get 0\ni64.const 3\ni64.shl\ni64.eqz\ni64.extend_i32_s\nend_function\n",
           "long long f(long long a) { return a * 8 == 0; }")

// A constant index goes into the store's offset.
EXPECT_OPT(StoreOffset, "local.get 0\nlocal.get 1\ni32.store 12\nend_function\n",
           "void f(int *p, int v) { p[3] = v; }")

// Constants stored side by side are one store, aligned as the bytes were, as nothing
// is known of p.
EXPECT_OPT(MergeStores, "local.get 0\ni32.const 67305985\ni32.store 0:p2align=0\nend_function\n",
           "void f(char *p) { p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 4; }")

// Through the frame, aligned to 16, the members of a structure become one i64 store.
TEST_F(WasmTest, MergeStoresInFrame)
{
    std::string s = Code(CompileToWasm(R"(
        struct S { char a, b; short c; int d; };
        int f(void) { struct S s = { 1, 2, 3, 4 }; struct S *p = &s; return p->a + p->d; }
    )"));
    EXPECT_NE(s.find("local.get 0\ni64.const 17180066305\ni64.store 0\n"), std::string::npos) << s;
}

// Copies vanish when their locals share one: swapping is no code at all.
EXPECT_OPT(CoalesceCopies, "local.get 1\nlocal.get 0\ni32.sub\nend_function\n",
           "int f(int a, int b) { int t = a; a = b; b = t; return a - b; }")

// A loop: the exit test is a br_if before it, the back edge a br_if at its end; the
// pointer walks in the parameter a; the final return is the end of the function.
EXPECT_OPT(OptimizedLoop,
           "block\ni32.const 0\nlocal.set 3\nlocal.get 0\nlocal.get 1\ni32.const 2\ni32.shl\n"
           "i32.add\nlocal.set 2\nlocal.get 1\ni32.const 0\ni32.le_s\nbr_if 0\nloop\n"
           "local.get 3\nlocal.get 0\ni32.load 0\ni32.add\nlocal.set 3\nlocal.get 0\n"
           "i32.const 4\ni32.add\nlocal.tee 0\nlocal.get 2\ni32.lt_s\nbr_if 0\nend_loop\n"
           "end_block\nlocal.get 3\nend_function\n",
           "int f(int *a, int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }")

// The ternary's merge block and the branches to it are gone; the result reuses n.
EXPECT_OPT(OptimizedRecursion,
           "local.get 0\ni32.const 2\ni32.ge_s\nif\nlocal.get 0\ni32.const 1\ni32.sub\n"
           "call fib\nlocal.get 0\ni32.const 2\ni32.sub\ncall fib\ni32.add\nlocal.set 0\n"
           "end_if\nlocal.get 0\nend_function\n",
           "int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }")

// Each switch off alone: no rewrites leaves the selection's locals.
TEST_F(WasmTest, NoStackify)
{
    wasm_stackify = false;
    std::string s = Code(CompileToWasm("int f(int a, int b) { return a * b + 1; }"));
    EXPECT_NE(s.find("local.set"), std::string::npos) << s;
}

// Run: merged stores read back by bytes, values kept across stores and calls, and
// volatile accesses, with the rewrites on.
TEST_F(WasmTest, RunRewrites)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        struct S { char a, b; short c; int d; long long e; };
        static int calls;
        static int bump(int x) { calls += x; return calls; }
        static int keep(int *p) { int a = *p; *p = a * 2; return a + *p; }
        int main(void)
        {
            struct S s = { 1, -2, 300, -4, 5 };
            unsigned char *b = (unsigned char *)&s;
            if (b[0] != 1 || b[1] != 254 || b[2] != 44 || b[3] != 1 || b[4] != 252)
                return 1;
            if (s.a + s.b + s.c + s.d + s.e != 300)
                return 2;
            int v = 7;
            if (keep(&v) != 21 || v != 14)
                return 3;
            int g = calls;
            if (g + bump(3) + bump(4) != 10)
                return 4;
            volatile int w = 1;
            int x = w;
            w = 2;
            if (w - x != 1)
                return 5;
            char c[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
            long long sum = 0;
            for (int i = 0; i < 8; i++)
                sum = sum * 10 + c[i];
            return sum == 12345678 ? 0 : 6;
        }
    )"));
    EXPECT_EQ(0, exit_status);
}
