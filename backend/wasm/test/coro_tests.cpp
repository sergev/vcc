//
// Coroutines on wasm32 alone: the dispatch as a br_table (the run tests every target
// shares are backend/common/test/coro/coro_run_tests.cpp).
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// A dispatch of more than two suspension points is a jump table.  Here one
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
    SKIP_IF_NO_WASM32_TOOLS();
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
