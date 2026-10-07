//
// wasm32 calls: arguments in their parameters' types, a dropped result, and _Noreturn.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// Each argument in its parameter's type (the constant 2 converted in TAC); the result
// goes to a temporary, which a void function falls off the end with.
EXPECT_CODE(CallArguments, "i32.const 1\ni64.const 2\ncall g\nlocal.set 0\nend_function\n",
            "int g(int a, long long b); void f(void) { g(1, 2); }")

// A _Noreturn call ends in unreachable.
TEST_F(WasmTest, NoreturnCall)
{
    NaiveSelection();
    std::string s =
        Code(CompileToWasm("_Noreturn void exit(int); int f(int a) { if (a) exit(3); return a; }"));
    EXPECT_NE(s.find("i32.const 3\ncall exit\nunreachable\n"), std::string::npos) << s;
}

// Every function the unit defines or calls is declared up front.
TEST_F(WasmTest, DeclaresCallees)
{
    std::string s = CompileToWasm(R"(
        double h(float x, long long y);
        static int s(void) { return 1; }
        int f(void) { h(1.0f, 2); return s(); }
    )");
    size_t first  = s.find("\t.section");
    EXPECT_LT(s.find("\t.functype\th (f32, i64) -> (f64)\n"), first) << s;
    EXPECT_LT(s.find("\t.functype\ts () -> (i32)\n"), first) << s;
    EXPECT_NE(s.find("\t.type\ts,@function\ns:\n"), std::string::npos) << s;
    EXPECT_EQ(s.find("\t.globl\ts\n"), std::string::npos) << s;
}

// Run: many arguments of mixed widths, and exit() from deep inside.
TEST_F(WasmTest, RunCalls)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        _Noreturn void exit(int);
        long long mix(char a, short b, int c, long long d, unsigned char e, int f, int g,
                      int h, int i, long long j)
        {
            return a + b + c + d + e + f + g + h + i + j;
        }
        void deep(int n) { if (n == 0) exit(mix(-1, -2, 3, 1LL << 33, 255, 6, 7, 8, 9, 10) >> 33); deep(n - 1); }
        int main(void) { deep(5); return 1; }
    )"));
    EXPECT_EQ(1, exit_status); // (1 << 33) + 295 >> 33
}
