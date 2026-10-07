//
// MMIX programs, and the runtime itself, on mmix.
//
#include <gtest/gtest-spi.h>

#include "mmix_test.h"

// main's result is mmix's exit status: $255 of the final trap, which crt0 sets.
TEST_F(MmixTest, RunReturn200)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix("int main(void) { return 200; }"));
    EXPECT_EQ(200, exit_status);
}

// The status is the low byte of the int.
TEST_F(MmixTest, RunReturnWideConstant)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("", CompileAndRunMmix("int main(void) { return 0x12345678; }"));
    EXPECT_EQ(0x78, exit_status);
}

TEST_F(MmixTest, RunBookStatus)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("-7\n", CompileAndRunBook("int main(void) { return -7; }"));
    EXPECT_EQ(249, exit_status);
}

TEST_F(MmixTest, RunBookStatusMax)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("2147483647\n", CompileAndRunBook("int main(void) { return 2147483647; }"));
}

TEST_F(MmixTest, RunBookStatusMin)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("-2147483648\n",
              CompileAndRunBook("int main(void) { return -2147483647 - 1; }"));
}

// GCC's main leaves its int result unextended; crt0 extends it before printing.
TEST_F(MmixTest, RunGccMainOnOurRuntime)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string src = "int main(void) { return -7; }";
    EXPECT_EQ("-7\n", Run("", "crt0-status.o", &src, { "-O1" }, ".gcc"));
    EXPECT_EQ(249, exit_status);
}

// A halt that is not our exit fails the run, though its status (0 here, as for a jump
// into zeroed memory, which holds trap 0,0,0) could pass for a result: there is no
// "[exit N]" report.
TEST_F(MmixTest, RunHaltWithoutReportFails)
{
    SKIP_IF_NO_MMIX_TOOLS();
    ::testing::TestPartResultArray results;
    std::string out;
    {
        ::testing::ScopedFakeTestPartResultReporter intercept(
            ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD,
            &results);
        out = RunAssembly(R"(    .text
    .global main
main:
    setl    $255, 0
    trap    0, 0, 0
)");
    }
    EXPECT_EQ("ERROR", out);
    ASSERT_EQ(1, results.size());
    EXPECT_NE(std::string::npos,
              std::string(results.GetTestPartResult(0).message()).find("did not stop itself"))
        << results.GetTestPartResult(0).message();
}

// Hand-written assembly prints through putbyte, and returns a status.
TEST_F(MmixTest, RuntimePutbyte)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("Hi\n", RunAssembly(R"(    .text
    .global main
main:
    get     $0, rJ
    setl    $2, 72
    pushj   $1, putbyte
    setl    $2, 105
    pushj   $1, putbyte
    setl    $2, 10
    pushj   $1, putbyte
    put     rJ, $0
    setl    $0, 42
    pop     1, 0
)"));
    EXPECT_EQ(42, exit_status);
}

// setjmp/longjmp from libc.a (newlib's): a jump out of nested frames, longjmp(env, 0)
// arriving as 1, and a second setjmp on the same buffer.
static const char setjmp_program[] = R"(
#include <setjmp.h>
static jmp_buf env;
static int depth;
__attribute__((noinline)) static void dive(int n, int val)
{
    depth = n;
    if (n == 5)
        longjmp(env, val);
    dive(n + 1, val);
}
int main(void)
{
    volatile int round = 0;
    int r = setjmp(env);
    round++;
    if (round == 1) {
        if (r != 0)
            return 1;
        dive(0, 7);
    }
    if (round == 2) {
        if (r != 7 || depth != 5)
            return 2;
        dive(0, 0);
    }
    if (round == 3 && r != 1)
        return 3;
    return round == 3 ? 42 : 4;
}
)";

TEST_F(MmixTest, RunSetjmpLongjmp)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string src = setjmp_program;
    src.replace(src.find("__attribute__((noinline)) "), 26, "");
    EXPECT_EQ("", CompileAndRunMmix(src));
    EXPECT_EQ(42, exit_status);
}

// The same from GCC's code, which keeps values in registers, with our <setjmp.h>.
TEST_F(MmixTest, RunSetjmpLongjmpGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string src = setjmp_program;
    EXPECT_EQ("", Run("", "crt0.o", &src,
                      { "-O2", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I", TEST_MODEL_INCLUDE_DIR,
                        "-I", TEST_COMMON_INCLUDE_DIR },
                      ".gcc"));
    EXPECT_EQ(42, exit_status);
}

// Across both: recursion 5000 deep alternating between our code and GCC's, which spills
// the register ring, unwound by a longjmp to a setjmp of either side; values live in the
// setjmp caller's registers (GCC's) or frame (ours) survive.
TEST_F(MmixTest, RunSetjmpLongjmpAcrossGcc)
{
    SKIP_IF_NO_MMIX_TOOLS();
    std::string gcc = R"(
#include <setjmp.h>
extern jmp_buf env;
long our_dive(long n, int val);
long gcc_dive(long n, int val)
{
    if (n == 0)
        longjmp(env, val);
    return our_dive(n - 1, val) + n;
}
int gcc_catch(long n, int val)
{
    long keep = n * 3 + val;
    int r = setjmp(env);
    if (r == 0) {
        our_dive(n, val);
        return -1;
    }
    return keep == n * 3 + val ? r : -2;
}
)";
    std::string ours = CompileToMmix(R"(
#include <setjmp.h>
jmp_buf env;
long gcc_dive(long n, int val);
int gcc_catch(long n, int val);
long our_dive(long n, int val)
{
    if (n == 0)
        longjmp(env, val);
    return gcc_dive(n - 1, val) + 1;
}
int main(void)
{
    long keep = 12345;
    int r = setjmp(env);
    if (r == 0)
        gcc_dive(5000, 9);
    if (r != 9 || keep != 12345)
        return 1;
    if (gcc_catch(5000, 0) != 1)
        return 2;
    if (gcc_catch(3, 77) != 77)
        return 3;
    return 42;
}
)");
    EXPECT_EQ("", Run(ours, "crt0.o", &gcc,
                      { "-O2", "-nostdinc", "-I", TEST_INCLUDE_DIR, "-I", TEST_MODEL_INCLUDE_DIR,
                        "-I", TEST_COMMON_INCLUDE_DIR },
                      ".gcc"));
    EXPECT_EQ(42, exit_status);
}

// An unsigned value converted to int of the same width is sign-extended again, also when
// copy propagation has left the shift reading the unsigned temporary: (int)0x80000000 >> 5.
TEST_F(MmixTest, RunUnsignedToIntThenShift)
{
    SKIP_IF_NO_MMIX_TOOLS();
    EXPECT_EQ("-67108864\n-67108864\n0\n", CompileAndRunBook(R"(
#include <stdio.h>
int cvt(unsigned u) { return (int)(u << 3) >> 5; }
int same(unsigned u) { int i = (int)u; return i / 32; }
int main(void) {
    printf("%d\n", cvt(0x10000000u));
    printf("%d\n", same(0x80000000u));
    return 0;
})"));
}
