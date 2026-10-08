//
// wasm32 frames: a name whose address is taken, an ALLOCATE_LOCAL object or an
// aggregate lives in a slot of a frame on the shadow stack (__stack_pointer), allocated
// on entry and released before each return; every other one is a wasm local.
//
#include <gtest/gtest.h>

#include "wasm_test.h"

// A parameter whose address is taken is stored into its slot by the prologue; the
// result is read from the slot before the epilogue releases the frame.
EXPECT_CODE(AddressOfParam,
            "global.get __stack_pointer\ni32.const 16\ni32.sub\nlocal.tee 2\n"
            "global.set __stack_pointer\nlocal.get 2\nlocal.get 0\ni32.store 0\n"
            "local.get 2\nlocal.set 1\nlocal.get 1\ni32.const 7\ni32.store 0\n"
            "local.get 2\ni32.load 0\nlocal.get 2\ni32.const 16\ni32.add\n"
            "global.set __stack_pointer\nreturn\nend_function\n",
            "int f(int a) { int *p = &a; *p = 7; return a; }")

// Scalars only: no frame.
TEST_F(WasmTest, NoFrame)
{
    std::string s = Code(CompileToWasm("int f(int a, int b) { int c = a * b; return c + 1; }"));
    EXPECT_EQ(s.find("__stack_pointer"), std::string::npos) << s;
}

// The slots are aligned to their types, and the frame to 16 bytes.
TEST_F(WasmTest, FrameLayout)
{
    std::string s = Code(CompileToWasm(R"(
        int f(void)
        {
            char c[3];
            double d;
            int i;
            char *pc = c;
            double *pd = &d;
            int *pi = &i;
            *pc = 1;
            *pd = 2;
            *pi = 3;
            return c[0] + i;
        }
    )"));
    EXPECT_EQ(s.find("global.get __stack_pointer\ni32.const 32\ni32.sub\n"), 0u) << s;
    EXPECT_NE(s.find("f64.store 8\n"), std::string::npos) << s;  // d
    EXPECT_NE(s.find("i32.store 16\n"), std::string::npos) << s; // i
}

// Run: frames nest and are released, through recursion and an early return.
TEST_F(WasmTest, RunRecursiveFrames)
{
    SKIP_IF_NO_WASM32_TOOLS();
    EXPECT_EQ("", CompileAndRunWasm(R"(
        int sum(int n)
        {
            int a[100];
            for (int i = 0; i < 100; i++)
                a[i] = n;
            if (n == 0)
                return 0;
            int s = sum(n - 1);
            return s + a[99];
        }
        int depth(int *p, int n) { int x = n; if (n) return depth(&x, n - 1) + *p; return *p; }
        int main(void)
        {
            int base = 0;
            if (sum(200) != 20100) return 1;
            if (depth(&base, 10) != 55) return 2;
            return 7;
        }
    )"));
    EXPECT_EQ(7, exit_status);
}
