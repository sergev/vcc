//
// alloca (<alloca.h>, docs/Plan.md) run on every target but BESM-6: the memory lives
// until the function returns, on wasm32's shadow stack or the arena of
// libc/common/costack.c, which a target gives up as its backend learns the builtins.
// Through each backend's coroutine fixture CoroTest (test/coro_test.h).  The programs
// print nothing that depends on the target's sizes, and stay within the 1 KiB arena of
// a 16-bit size_t.
//
#include <gtest/gtest.h>

#include <cstring>

#include "coro_test.h"

class AllocaTest : public CoroTest {};

// Memory taken in a loop, given back at the return: a thousand calls would not fit
// otherwise.
TEST_F(AllocaTest, AllocaLoop)
{
    EXPECT_EQ("sum 90 calls 1000\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <stdio.h>

static int sum(void)
{
    int total = 0;
    for (int i = 0; i < 10; i++) {
        char *p = alloca(16);
        for (int j = 0; j < 16; j++)
            p[j] = (char)i;
        total += p[0] + p[15];
    }
    return total;
}

int main(void)
{
    int s = 0, n = 0;
    for (; n < 1000; n++)
        s = sum();
    printf("sum %d calls %d\n", s, n);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Each level's memory survives the calls below it, which take and give back their own.
TEST_F(AllocaTest, AllocaRecursion)
{
    EXPECT_EQ("abcde\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <stdio.h>
#include <string.h>

static void down(char *out, int n)
{
    if (n == 0)
        return;
    char *p = alloca(n + 1);
    memset(p, 'a' + n - 1, n);
    p[n] = 0;
    down(out, n - 1);
    for (int i = 0; i < n; i++)
        if (p[i] != 'a' + n - 1)
            return;
    strncat(out, p, 1);
}

int main(void)
{
    char out[8] = "";
    down(out, 5);
    printf("%s\n", out);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// Odd sizes, zero among them: every result aligned for any object, none overlapping.
TEST_F(AllocaTest, AllocaAlignment)
{
    EXPECT_EQ("aligned 1 apart 1\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    static const int sizes[] = { 1, 3, 0, 5, 7, 2 };
    char *p[6];
    int aligned = 1, apart = 1;
    for (int i = 0; i < 6; i++) {
        p[i] = alloca(sizes[i]);
        if ((uintptr_t)p[i] % _Alignof(max_align_t) != 0)
            aligned = 0;
        for (int j = 0; j < sizes[i]; j++)
            p[i][j] = (char)i;
    }
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < sizes[i]; j++)
            if (p[i][j] != i)
                apart = 0;
    printf("aligned %d apart %d\n", aligned, apart);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// An early return of a value read from the memory, from inside nested blocks, after a
// deferred statement that reads it too.
TEST_F(AllocaTest, AllocaDeferReturn)
{
    EXPECT_EQ("defer 7\ndefer 7\nfirst 12 then 30\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <coro.h>
#include <stdio.h>

static int f(int early)
{
    int *v = alloca(3 * sizeof(int));
    v[0] = 5;
    v[1] = 7;
    v[2] = 18;
    defer printf("defer %d\n", v[1]);
    if (early) {
        for (int i = 0; i < 3; i++)
            if (v[i] == 7)
                return v[0] + v[i];
    }
    return v[1] + v[2] + v[0];
}

int main(void)
{
    int a = f(1);
    int b = f(0);
    printf("first %d then %d\n", a, b);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// A call with arguments on the stack after the alloca: neither overwrites the other.
TEST_F(AllocaTest, AllocaStackArguments)
{
    EXPECT_EQ("78 abcdefghijkl\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <stdio.h>
#include <string.h>

static int sum12(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j,
                 int k, int l)
{
    return a + b + c + d + e + f + g + h + i + j + k + l;
}

int main(void)
{
    char *p = alloca(13);
    strcpy(p, "abcdefghijkl");
    int s = sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    char *q = alloca(4);
    strcpy(q, "xyz");
    printf("%d %s\n", sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) == s ? s : -1, p);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// alloca beside co_alloca: the function's memory outlives the block of the frame.
TEST_F(AllocaTest, AllocaBesideCoAlloca)
{
    EXPECT_EQ("3 4 5 6 | 3 4 5 6 | 18\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <coro.h>
#include <stdio.h>

coro(int) void range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
        yield i;
}

static int run(void)
{
    int *seen = alloca(4 * sizeof(int));
    int n     = 0;
    {
        co_frame(int, void) *f = co_alloca(range, 0, 3, 7);
        while (co_resume(f) == CO_SUSPENDED) {
            seen[n++] = co_value(f);
            printf("%d ", co_value(f));
        }
    }
    printf("|");
    int total = 0;
    for (int i = 0; i < n; i++) {
        printf(" %d", seen[i]);
        total += seen[i];
    }
    return total;
}

int main(void)
{
    int t = run();
    printf(" | %d\n", t);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}

// On the arena, running out traps.  (On a stack it is a stack overflow.)
TEST_F(AllocaTest, AllocaArenaOverflow)
{
    if (target_config->stack_alloca)
        GTEST_SKIP() << "alloca on the stack";
    EXPECT_EQ("coroutine trap: CO_TRAP_NO_SPACE: co_alloca or alloca\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <stdio.h>

int main(void)
{
    for (;;)
        if (!alloca(256))
            break;
    puts("not reached");
    return 0;
}
)"));
    EXPECT_EQ(255, exit_status);
}

// longjmp out of a function that took memory gives it back with the stack: twenty
// thousand times 200 bytes would not fit otherwise.  Not on the arena, which only a
// return gives back, nor where the runtime has no setjmp: wasm32, and the bare-metal
// AArch64, ARM32 and RISC-V (aarch64-darwin has the system's).
TEST_F(AllocaTest, AllocaLongjmp)
{
    static const char *const no_setjmp[] = { "wasm32", "aarch64", "arm32", "riscv32", "riscv64" };
    if (!target_config->stack_alloca)
        GTEST_SKIP() << "alloca on the arena";
    for (const char *name : no_setjmp)
        if (strcmp(target_config->name, name) == 0)
            GTEST_SKIP() << "no setjmp in the runtime";
    EXPECT_EQ("20000\n", CompileAndRunCoro(R"(
#include <alloca.h>
#include <setjmp.h>
#include <stdio.h>

static jmp_buf env;

static void deep(int n)
{
    char *p = alloca(200);
    p[0]   = 1;
    p[199] = (char)n;
    if (p[0] + p[199] == 1 + (char)n)
        longjmp(env, 1);
}

int main(void)
{
    int count = 0;
    for (int i = 0; i < 20000; i++) {
        volatile int jumped = 0;
        if (setjmp(env) == 0)
            deep(i);
        else
            jumped = 1;
        count += jumped;
    }
    printf("%d\n", count);
    return 0;
}
)"));
    EXPECT_EQ(0, exit_status);
}
