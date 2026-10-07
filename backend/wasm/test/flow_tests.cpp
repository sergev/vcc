//
// wasm32 control flow, stage 1: the dispatch skeleton (structure.c).  A forward jump
// leaves the block that ends just before its target; a backward one goes through the
// state and the loop's br_table.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// Straight-line code has no skeleton at all.
EXPECT_CODE(NoBlocks, "local.get 0\nreturn\nend_function\n", "int f(int a) { return a; }")

// Only forward jumps: nested blocks, the entry innermost, no loop.  The jump from
// block 0 to block 2 leaves B_2, at depth 1.
EXPECT_CODE(ForwardOnly,
            "block\nblock\nlocal.get 0\ni32.eqz\nbr_if 1\nend_block\ni32.const 1\nreturn\n"
            "end_block\ni32.const 2\nreturn\nend_function\n",
            "int f(int a) { if (a) return 1; return 2; }")

// A loop: the backward jump sets the state and restarts the loop.
TEST_F(WasmTest, BackwardJump)
{
    NaiveSelection();
    std::string s = Code(
        CompileToWasm("int f(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }"));
    EXPECT_EQ(s.find("loop\nblock\nblock\nblock\nlocal.get 7\nbr_table {0, 1, 2, 0}\n"
                     "end_block\n"),
              0u)
        << s;
    EXPECT_NE(s.find("i32.const 1\nlocal.set 7\nlocal.get 6\nbr_if 1\nend_block\n"),
              std::string::npos)
        << s;
    EXPECT_NE(s.find("end_loop\nunreachable\nend_function\n"), std::string::npos) << s;
}

// Run: loops of each kind, break and continue, a switch, and goto backward and forward.
TEST_F(WasmTest, RunLoops)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        int main(void)
        {
            int s = 0, i = 0;
            while (i < 10) { if (i == 7) break; s += i++; }          // 21
            do { s++; } while (s < 25);                               // 25
            for (int j = 0; j < 10; j++) { if (j % 2) continue; s += j; } // 45
            switch (s) { case 44: s = 0; break; case 45: s += 5; default: s++; } // 51
        again:
            if (s < 60) { s += 3; goto again; }                       // 60
            goto out;
            s = 0;
        out:
            return s;
        }
    )"));
    EXPECT_EQ(60, exit_status);
}

// Run: recursion, and nested loops whose inner one jumps back from deep inside.
TEST_F(WasmTest, RunNestedLoopsAndRecursion)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
        int main(void)
        {
            int count = 0;
            for (int i = 0; i < 10; i++)
                for (int j = 0; j < i; j++)
                    if ((i + j) % 3 == 0)
                        count++;
            return fib(10) + count; // 55 + 15
        }
    )"));
    EXPECT_EQ(70, exit_status);
}
