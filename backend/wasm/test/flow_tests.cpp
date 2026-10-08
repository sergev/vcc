//
// wasm32 control flow (structure.c): blocks, loops and ifs by Ramsey's translation for
// a reducible graph; the dispatch skeleton for an irreducible one, and for every
// function under --no-structure.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// Straight-line code has no structure at all.
EXPECT_CODE(NoBlocks, "local.get 0\nreturn\nend_function\n", "int f(int a) { return a; }")

// A conditional jump is an if whose arms are its two ways.
EXPECT_CODE(IfElse,
            "local.get 0\ni32.eqz\nif\ni32.const 2\nreturn\nelse\ni32.const 1\nreturn\n"
            "end_if\nunreachable\nend_function\n",
            "int f(int a) { if (a) return 1; return 2; }")

// A merge node follows a block, which a jump to it leaves.
TEST_F(WasmTest, MergeNode)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm("int f(int a) { int r = 3; if (a) r = a * 2; return r + 1; }"));
    EXPECT_EQ(s.find("block\n"), 0u) << s;
    EXPECT_NE(s.find("if\n"), std::string::npos) << s;
    EXPECT_NE(s.find("br 1\n"), std::string::npos) << s;
    EXPECT_NE(s.find("end_block\n"), std::string::npos) << s;
    EXPECT_EQ(s.find("br_table"), std::string::npos) << s;
}

// A loop: the backward jump continues it; leaving it goes to the merge node after.
TEST_F(WasmTest, Loop)
{
    NaiveSelection();
    std::string s = Code(
        CompileToWasm("int f(int n) { int s = 0; for (int i = 0; i < n; i++) s += i; return s; }"));
    EXPECT_NE(s.find("loop\n"), std::string::npos) << s;
    EXPECT_NE(s.find("end_loop\n"), std::string::npos) << s;
    EXPECT_EQ(s.find("br_table"), std::string::npos) << s;
    EXPECT_EQ(s.find("local.set 7"), std::string::npos) << s; // no state
}

// The dispatch skeleton, under --no-structure: only forward jumps make nested blocks,
// the entry innermost; the jump from block 0 to block 2 leaves B_2, at depth 1.
TEST_F(WasmTest, DispatchForward)
{
    NaiveSelection();
    wasm_structure = false;
    EXPECT_EQ("block\nblock\nlocal.get 0\ni32.eqz\nbr_if 1\nend_block\ni32.const 1\nreturn\n"
              "end_block\ni32.const 2\nreturn\nend_function\n",
              Code(CompileToWasm("int f(int a) { if (a) return 1; return 2; }")));
}

// The dispatch skeleton's loop: a backward jump sets the state and restarts it.
TEST_F(WasmTest, DispatchBackward)
{
    NaiveSelection();
    wasm_structure = false;
    std::string s  = Code(
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

static const char irreducible_src[] = R"(
int f(int a)
{
    int s = 0;
    if (a)
        goto inside;
    while (s < 100) {
        s += 3;
    inside:
        s += 5;
    }
    return s;
}
)";

// A jump into a loop makes the graph irreducible: the dispatch skeleton.
TEST_F(WasmTest, IrreducibleFallsBack)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm(irreducible_src));
    EXPECT_NE(s.find("br_table"), std::string::npos) << s;
}

// Run: irreducible graphs, by goto into a loop and Duff's device.
TEST_F(WasmTest, RunIrreducible)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(std::string(irreducible_src) + R"(
        static void duff(char *to, const char *from, int count)
        {
            int n = (count + 7) / 8;
            switch (count % 8) {
            case 0: do { *to++ = *from++;
            case 7:      *to++ = *from++;
            case 6:      *to++ = *from++;
            case 5:      *to++ = *from++;
            case 4:      *to++ = *from++;
            case 3:      *to++ = *from++;
            case 2:      *to++ = *from++;
            case 1:      *to++ = *from++;
                    } while (--n > 0);
            }
        }
        int main(void)
        {
            char src[20] = "abcdefghijklmnopqrs", dst[20] = { 0 };
            duff(dst, src, 13);
            if (dst[12] != 'm' || dst[13] != 0 || dst[0] != 'a')
                return 1;
            if (f(0) != 104 || f(1) != 101)
                return 2;
            return 0;
        }
    )"));
    EXPECT_EQ(0, exit_status);
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

// Run: the same loops and switch under --no-structure.
TEST_F(WasmTest, RunDispatchLoops)
{
    SKIP_IF_NO_WASM32_TOOLS();
    wasm_structure = false;
    EXPECT_EQ("", CompileAndRunWasm(R"(
        int main(void)
        {
            int s = 0;
            for (int i = 0; i < 10; i++) { if (i % 3 == 0) continue; s += i; } // 27
            switch (s) { case 27: s += 3; break; default: s = 0; }           // 30
            return s;
        }
    )"));
    EXPECT_EQ(30, exit_status);
}
