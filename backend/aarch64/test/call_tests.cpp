//
// AArch64 calls and parameters: registers, stack arguments, results, and interop with
// clang in both directions.
//
#include "aarch64_test.h"

// Parameters are stored from their registers into slots; arguments are loaded into
// x0-x7, the result taken from w0.
TEST_F(Aarch64Test, CallAndParameters)
{
    std::string code = Code(CompileToAarch64(R"(
int add(int a, long b) { return a + b; }
int main(void) { return add(1, 2); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(str w0, [x29, #-4]
str x1, [x29, #-16]
)")) << code;
    EXPECT_NE(std::string::npos, code.find(R"(mov w0, #1
mov x1, #2
bl add
)")) << code;
}

// The ninth integer argument goes on the stack, in an 8-byte slot; the callee reads it
// above its frame record.
TEST_F(Aarch64Test, StackArguments)
{
    std::string code = Code(CompileToAarch64(R"(
int tenth(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) { return i + j; }
int main(void) { return tenth(1, 2, 3, 4, 5, 6, 7, 8, 9, 10); }
)"));
    EXPECT_NE(std::string::npos, code.find(R"(mov w9, #9
str w9, [sp]
mov w9, #10
str w9, [sp, #8]
)")) << code;
    EXPECT_NE(std::string::npos, code.find("ldr w9, [x29, #16]\n")) << code;
}

// Floating-point arguments take v0-v7, apart from the integer registers.
TEST_F(Aarch64Test, FloatingPointArguments)
{
    std::string code = Code(CompileToAarch64(R"(
double g(int a, double b, float c, long d);
double f(void) { return g(1, 2.0, 3.0f, 4); }
)"));
    EXPECT_NE(std::string::npos, code.find("fmov d0, x17\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fmov s1, w17\n")) << code;
    EXPECT_NE(std::string::npos, code.find("mov x1, #4\nbl g\n")) << code;
}

TEST_F(Aarch64Test, RunRecursionAndManyArguments)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunAarch64(R"(
int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
long sum(signed char a, int b, long c, unsigned d, short e, long f, int g, long h, int i, long j)
{
    return a + b + c + d + e + f + g + h + i * 1000 + j * 100000;
}
int main(void) {
    long s = sum(-1, 2, 3, 4, -5, 6, 7, 8, 9, 10);
    return (fact(5) == 120) + 2 * (s == 1009024);
})"));
    EXPECT_EQ(3, exit_status);
}

TEST_F(Aarch64Test, RunPutchar)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("Hi\n", CompileAndRunAarch64(R"(
int putchar(int c);
int main(void) { putchar('H'); putchar('i'); putchar(10); return 0; }
)"));
    EXPECT_EQ(0, exit_status);
}

// Our code calls clang's with ten mixed arguments, and clang's calls ours back; narrow
// values cross in both directions with their upper bits left to the receiver.
TEST_F(Aarch64Test, RunInteropWithClang)
{
    SKIP_IF_NO_AARCH64_TOOLS();
    EXPECT_EQ("", CompileAndRunWithClang(R"(
long theirs(signed char a, int b, long c, unsigned char d, short e, long f, int g, long h,
            int i, long j);
signed char narrow(void);
int call_ours(void);
long ours(int a, long b, unsigned short c, int d, int e, int f, int g, int h, long i, int j)
{
    return a + b + c + d + e + f + g + h + i * 1000 + j * 100000;
}
signed char ours_narrow(int x) { return x; }
int main(void) {
    long t = theirs(-1, 2, 3, 200, -5, 6, 7, 8, 9, 10);
    signed char n = narrow();
    return (t == 1009220) + 2 * (n == -2) + 4 * call_ours();
})",
                                         R"(
long ours(int a, long b, unsigned short c, int d, int e, int f, int g, int h, long i, int j);
signed char ours_narrow(int x);
long theirs(signed char a, int b, long c, unsigned char d, short e, long f, int g, long h,
            int i, long j)
{
    return a + b + c + d + e + f + g + h + i * 1000 + j * 100000;
}
signed char narrow(void) { return -2; }
int call_ours(void)
{
    return ours(1, 2, 65535, 4, 5, 6, 7, 8, 9, 10) == 1074568 && ours_narrow(0x17f) == 127;
})"));
    EXPECT_EQ(7, exit_status);
}
