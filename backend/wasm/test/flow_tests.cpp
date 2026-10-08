//
// wasm32 control flow (structure.c): blocks, loops and ifs by Ramsey's translation for
// a reducible graph; for an irreducible one, a dispatch for each region with several
// entries, and Ramsey's translation around it; the dispatch skeleton under
// --no-regional for an irreducible graph, and for every function under --no-structure.
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

// A jump into a loop makes the graph irreducible: a dispatch for that region alone, a
// br_table at the loop's head over its two entries; the code before it is structured,
// each way in setting the state.  The jump back sets it too; the fall-through from the
// first entry into the second is a forward jump inside the region and needs none.
TEST_F(WasmTest, IrreducibleRegion)
{
    NaiveSelection();
    EXPECT_EQ("block\ni32.const 0\nlocal.set 1\nlocal.get 0\ni32.eqz\nif\ni32.const 0\n"
              "local.set 5\nbr 1\nelse\ni32.const 1\nlocal.set 5\nbr 1\nend_if\nend_block\n"
              "loop\nblock\nblock\nlocal.get 5\nbr_table {0, 1, 0}\nend_block\n"
              "local.get 1\ni32.const 3\ni32.add\nlocal.set 2\nlocal.get 2\nlocal.set 1\n"
              "br 0\nend_block\n"
              "local.get 1\ni32.const 5\ni32.add\nlocal.set 3\nlocal.get 3\nlocal.set 1\n"
              "local.get 3\ni32.const 100\ni32.lt_s\nlocal.set 4\ni32.const 0\nlocal.set 5\n"
              "local.get 4\nbr_if 0\nlocal.get 3\nreturn\nend_loop\nunreachable\nend_function\n",
              Code(CompileToWasm(irreducible_src)));
}

// Under --no-regional, the whole function is the dispatch skeleton.
TEST_F(WasmTest, IrreducibleNoRegional)
{
    NaiveSelection();
    wasm_regional = false;
    std::string s = Code(CompileToWasm(irreducible_src));
    EXPECT_EQ(s.find("loop\nblock\nblock\nblock\nblock\nblock\n"), 0u) << s;
}

static const char duff_src[] = R"(
void duff(char *to, const char *from, int count)
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
)";

// Duff's device: the switch is structured, and the loop it jumps into is one region of
// eight entries, a br_table at its head.  Each case runs into the next by leaving its
// block (a br 0 the peephole pass drops), and only the jump back, the one br_if, goes
// through the dispatch; the loop is left to the function's end.
TEST_F(WasmTest, DuffRegion)
{
    NaiveSelection();
    std::string s = Code(CompileToWasm(duff_src));
    size_t loop   = s.find("loop\n");
    ASSERT_NE(std::string::npos, loop) << s;
    EXPECT_EQ(s.find("block\n"), 0u) << s;
    size_t table = s.find("br_table {0, 1, 2, 3, 4, 5, 6, 7, 0}\n");
    ASSERT_NE(std::string::npos, table) << s;
    EXPECT_GT(table, loop) << s;
    EXPECT_EQ(s.find("br_table", table + 1), std::string::npos) << s;
    std::string body = s.substr(table);
    size_t br_ifs = 0, brs = 0;
    for (size_t at = body.find("\nbr_if "); at != std::string::npos; at = body.find("\nbr_if ", at + 1))
        br_ifs++;
    for (size_t at = body.find("\nbr 0\n"); at != std::string::npos; at = body.find("\nbr 0\n", at + 1))
        brs++;
    EXPECT_EQ(1u, br_ifs) << body;
    EXPECT_EQ(7u, brs) << body;
    EXPECT_NE(std::string::npos, body.find("br_if 0\nbr 1\nend_loop\n")) << body; // back, or out
}

// A generator whose loop does not suspend at every iteration: the resume enters the loop
// in the middle, a region of two entries; the dispatch on the coroutine's state before
// it stays an if chain.
TEST_F(WasmTest, CoroutineRegion)
{
    NaiveSelection();
    std::string s = CompileToWasm(R"(
#include <coro.h>
coro(int) void evens(int lo, int hi)
{
    for (int i = lo; i < hi; i++) {
        if (i % 2)
            continue;
        if (yield i == CO_CANCEL)
            return;
    }
}
)");
    std::string r = s.substr(s.find("evens$resume:"));
    r             = Code(r.substr(0, r.find("end_function") + 13));
    EXPECT_EQ(r.find("block\n"), 0u) << r;
    size_t table = r.find("br_table {0, 1, 0}\n");
    EXPECT_NE(std::string::npos, table) << r;
    EXPECT_EQ(r.find("br_table", table + 1), std::string::npos) << r;
}

// Run: irreducible graphs, by goto into a loop and Duff's device, a dispatch per region
// and, under --no-regional, the whole-function skeleton.
static const char irreducible_main[] = R"(
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
)";

TEST_F(WasmTest, RunIrreducible)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(std::string(irreducible_src) + duff_src + irreducible_main));
    EXPECT_EQ(0, exit_status);
}

TEST_F(WasmTest, RunIrreducibleNoRegional)
{
    SKIP_IF_NO_WASM32_TOOLS();
    wasm_regional = false;
    EXPECT_EQ("", CompileAndRunWasm(std::string(irreducible_src) + duff_src + irreducible_main));
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

// A function of n labels with random gotos among them, entered by a switch: irreducible
// graphs of every shape, regions nested in loops among them.  Deterministic: its own
// generator.
static std::string RandomGotos(unsigned seed)
{
    auto next = [&seed](unsigned m) {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) % m;
    };
    unsigned n      = 3 + next(10);
    std::string src = "unsigned f(unsigned x)\n{\n    unsigned steps = 0;\n";
    src += "    switch (x % " + std::to_string(n) + ") {\n";
    for (unsigned i = 0; i < n; i++)
        if (next(10) < 6)
            src += "    case " + std::to_string(i) + ": goto L" + std::to_string(i) + ";\n";
    src += "    default: goto L0;\n    }\n";
    for (unsigned i = 0; i < n; i++) {
        std::string l = std::to_string(i), p = std::to_string(next(n)), q = std::to_string(next(n));
        src += "L" + l + ":\n    x = x * 1103515245u + " + std::to_string(1 + next(99)) + ";\n";
        src += "    if (++steps > 200) return x ^ " + l + ";\n";
        unsigned kind = next(10);
        if (kind < 3)
            src += "    goto L" + p + ";\n";
        else if (kind < 5)
            src += "    if ((x >> 7) % 3 == 0) goto L" + p + ";\n";
        else
            src += "    if ((x >> 9) & 1) goto L" + p + "; else goto L" + q + ";\n";
    }
    src += "    return x;\n}\n";
    src += "int main(void) { unsigned s = 0; for (unsigned i = 0; i < 20; i++) s += f(i * 7919u);"
           " printf(\"%u\\n\", s); return 0; }\n";
    return "#include <stdio.h>\n" + src;
}

// Run: random irreducible graphs, the same output as the dispatch skeleton gives.
TEST_F(WasmTest, RunRandomGotos)
{
    SKIP_IF_NO_WASM32_TOOLS();
    for (unsigned seed = 1; seed <= 12; seed++) {
        std::string src = RandomGotos(seed);
        wasm_structure  = false;
        std::string ref = CompileAndRunWasm(src);
        NextUnit();
        wasm_structure  = true;
        std::string out = CompileAndRunWasm(src);
        NextUnit();
        EXPECT_FALSE(ref.empty()) << src;
        EXPECT_EQ(ref, out) << src;
    }
}

// Run: a region whose inner loop (a, e2) the dispatch node enters as well as e1 does, so
// redirecting the jumps from outside and back is not enough, and every jump to an entry
// goes through the dispatch.
TEST_F(WasmTest, RunIrreducibleNested)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("3415479615\n", CompileAndRunWasm(R"(
#include <stdio.h>
unsigned g(unsigned x, int k)
{
    unsigned s = 0;
    if (k & 1)
        goto e1;
    goto e2;
e1:
    s += 1;
a:
    s = s * 3 + x;
    if (s > 100000)
        return s;
    if (s & 1)
        goto e1;
e2:
    s += 2;
    goto a;
}
int main(void)
{
    unsigned t = 0;
    for (int k = 0; k < 6; k++)
        t = t * 7 + g(k * 13, k);
    printf("%u\n", t);
    return 0;
}
)"));
}
