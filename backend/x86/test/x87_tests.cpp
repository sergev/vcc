//
// x86-64 long double on the x87: values in 16-byte slots, loaded with fldt and stored
// with fstpt around each operation, the x87 stack empty in between.
//
#include "x86_test.h"

#define EXPECT_HAS(name, expected, src)                                    \
    TEST_F(X86Test, name)                                                  \
    {                                                                      \
        NaiveSelection();                                                  \
        std::string code = Code(CompileToX86(src));                        \
        EXPECT_NE(std::string::npos, code.find(expected)) << code;         \
    }

// Parameters are on the stack, 16-byte aligned; the result goes back in st(0).
EXPECT_CODE(SubtractLongDoubles, R"(pushq %rbp
movq %rsp, %rbp
subq $16, %rsp
fldt 16(%rbp)
fldt 32(%rbp)
fsubrp %st, %st(1)
fstpt -16(%rbp)
fldt -16(%rbp)
leave
ret
)",
            "long double f(long double a, long double b) { return a - b; }")
EXPECT_HAS(DivideLongDoubles, "fldt 16(%rbp)\nfldt 32(%rbp)\nfdivrp %st, %st(1)\n",
           "long double f(long double a, long double b) { return a / b; }")

// Zero and one have their own loads; another constant is a 10-byte .rodata literal.
TEST_F(X86Test, LongDoubleConstants)
{
    std::string s = CompileToX86(
        "long double f(long double a) { return a * 0.1L + 1.0L + (a == 0.0L); }");
    std::string code = Code(s);
    EXPECT_NE(std::string::npos, code.find("fldt .LC0(%rip)\nfmulp %st, %st(1)\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fld1\nfaddp %st, %st(1)\n")) << code;
    EXPECT_NE(std::string::npos, code.find("fldz\n")) << code;
    EXPECT_NE(std::string::npos,
              s.find(".LC0:\n    .quad   0xcccccccccccccccd\n    .quad   0x0000000000003ffb\n"))
        << s;
}

// `<` loads the right operand last, so that st(0) is the left one of fucomip; both
// are popped.
EXPECT_HAS(LongDoubleLessThan, "fldt 16(%rbp)\nfldt 32(%rbp)\nfucomip %st(1), %st\nfstp %st\nseta %al\n",
           "int f(long double a, long double b) { return a < b; }")

// The truncating store switches the control word around fistpq.
EXPECT_HAS(LongDoubleToInt,
           "fldt 16(%rbp)\nfnstcw -24(%rbp)\nmovzwl -24(%rbp), %eax\norl $3072, %eax\n"
           "movw %ax, -22(%rbp)\nfldcw -22(%rbp)\nfistpq -32(%rbp)\nfldcw -24(%rbp)\n"
           "movq -32(%rbp), %rax\n",
           "int f(long double a) { return a; }")
EXPECT_HAS(LongDoubleFromDouble, "fldl -8(%rbp)\nfstpt ", "long double f(double a) { return a; }")
EXPECT_HAS(LongDoubleToFloat, "fstps ", "float f(long double a) { return a; }")

// An argument is copied into a 16-byte aligned slot of the outgoing area, after an
// 8-byte one; the result is stored from st(0).
TEST_F(X86Test, LongDoubleCall)
{
    x86_frame_pointer = true;
    std::string code = Code(CompileToX86(R"(
long double g(long a, long b, long c, long d, long e, long k, long x, long double y);
void f(long double a) { g(1, 2, 3, 4, 5, 6, 7, a); }
)"));
    EXPECT_NE(std::string::npos, code.find("movl $7, %eax\nmovq %rax, (%rsp)\n"
                                           "movq 16(%rbp), %rax\nmovq %rax, 16(%rsp)\n"
                                           "movq 24(%rbp), %rax\nmovq %rax, 24(%rsp)\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("call g\nfstpt -16(%rbp)\n")) << code;
}

TEST_F(X86Test, RunLongDouble)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
long double third(long double x) { return x / 3; }
int main(void) {
    long double one = 1, zero = 0;
    long double nan = zero / zero;
    // 64 bits of significand: 1 + 2^-63 differs from 1, 1 + 2^-64 does not.
    long double eps = 1.0L / 9223372036854775808.0L;
    if (one + eps == one || one + eps / 2 != one)
        return 1;
    if (nan == nan || !(nan != nan) || nan < one || nan >= one || !nan)
        return 2;
    if ((long)(third(10) * 3) != 10 || (int)-2.7L != -2 || (long)third(-7) != -2)
        return 3;
    unsigned long big = 18000000000000000000ul;
    if ((unsigned long)(long double)big != big || (long double)big != 1.8e19L)
        return 4;
    if ((double)third(1) != 1.0 / 3 || (float)third(2) != 2.0f / 3)
        return 5;
    if (-zero != 0 || one / -zero > 0 || (long double)-5 != -5.0L || (long double)3u != 3)
        return 6;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}

// Linked with clang's code both ways: long double arguments on the stack between
// others (an 8-byte one in between, so the 16-byte alignment matters), results in
// st(0).
TEST_F(X86Test, RunLongDoubleWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunWithClang(R"(
long double theirs(int a, long double x, double d, long double y, long b, long c, long e,
                   long k, long m, long n, long double z);
long double ours(int a, long double x, double d, long double y, long b, long c, long e,
                 long k, long m, long n, long double z) {
    return a + x * 10 + d * 100 + y * 1000 + b * 10000 + n * 100000 + z * 1000000;
}
int main(void) {
    long double r = theirs(1, 2, 3, 4, 5, 0, 0, 0, 0, 6, 7);
    if (r != 7654321)
        return 1;
    theirs(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    return theirs(1, 2, 3, 4, 5, 0, 0, 0, 0, 6, 7) == 7654321 ? 42 : 2;
})",
                           R"(
long double ours(int a, long double x, double d, long double y, long b, long c, long e,
                 long k, long m, long n, long double z);
long double theirs(int a, long double x, double d, long double y, long b, long c, long e,
                   long k, long m, long n, long double z) {
    return ours(a, x, d, y, b, c, e, k, m, n, z);
}
)");
    EXPECT_EQ(42, exit_status);
}
