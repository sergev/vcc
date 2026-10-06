//
// x86-64 floating point in SSE: arithmetic with an operand from memory or .rodata,
// NaN-correct comparisons and branches, conversions, and calls.
//
#include "x86_test.h"

#define EXPECT_HAS(name, expected, src)                                    \
    TEST_F(X86Test, name)                                                  \
    {                                                                      \
        NaiveSelection();                                                  \
        std::string code = Code(CompileToX86(src));                        \
        EXPECT_NE(std::string::npos, code.find(expected)) << code;         \
    }

// A parameter comes in xmm0, the result goes back in xmm0; the second operand of an
// operation comes from memory.
EXPECT_CODE(AddDoubles, R"(pushq %rbp
movq %rsp, %rbp
subq $32, %rsp
movsd %xmm0, -8(%rbp)
movsd %xmm1, -16(%rbp)
movsd -8(%rbp), %xmm14
addsd -16(%rbp), %xmm14
movsd %xmm14, -24(%rbp)
movsd -24(%rbp), %xmm0
leave
ret
)",
            "double f(double a, double b) { return a + b; }")
EXPECT_HAS(MultiplyFloats, "movss -4(%rbp), %xmm14\nmulss -8(%rbp), %xmm14\n",
           "float f(float a, float b) { return a * b; }")

// A constant operand comes from .rodata.
TEST_F(X86Test, ConstantOperand)
{
    NaiveSelection();
    std::string s = CompileToX86("double f(double a) { return a / 3.0; }");
    EXPECT_NE(std::string::npos, Code(s).find("divsd .LC0(%rip), %xmm14\n")) << s;
    EXPECT_NE(std::string::npos,
              s.find("    .section .rodata\n    .p2align 3\n.LC0:\n    .quad   0x4008000000000000\n"))
        << s;
}

// Negation flips the sign bit with a 16-byte mask, as xorps reads it.
TEST_F(X86Test, NegateFloat)
{
    NaiveSelection();
    std::string s = CompileToX86("float f(float a) { return -a; }");
    EXPECT_NE(std::string::npos, Code(s).find("xorps .LC0(%rip), %xmm14\n")) << s;
    EXPECT_NE(std::string::npos,
              s.find(".p2align 4\n.LC0:\n    .quad   0x0000000080000000\n    .quad   0x0000000000000000\n"))
        << s;
}

// `<` swaps the operands to use `a`, which is false for a NaN; `==` also needs the
// parity flag clear, `!=` takes it set.
EXPECT_HAS(LessThan, "movsd -16(%rbp), %xmm14\nucomisd -8(%rbp), %xmm14\nseta %al\n",
           "int f(double a, double b) { return a < b; }")
EXPECT_HAS(GreaterOrEqual, "movss -4(%rbp), %xmm14\nucomiss -8(%rbp), %xmm14\nsetae %al\n",
           "int f(float a, float b) { return a >= b; }")
EXPECT_HAS(Equal, "sete %al\nsetnp %r11b\nandb %r11b, %al\nmovzbl %al, %eax\n",
           "int f(double a, double b) { return a == b; }")
EXPECT_HAS(NotEqual, "setne %al\nsetp %r11b\norb %r11b, %al\n",
           "int f(double a, double b) { return a != b; }")

// A branch on a double compares it with zero; a NaN counts as nonzero.
EXPECT_HAS(BranchIfZero, "xorps %xmm15, %xmm15\nucomisd %xmm15, %xmm14\njp .Lx0\nje .L",
           "int f(double a) { if (a) return 1; return 2; }")

EXPECT_HAS(IntToDouble, "movl -4(%rbp), %eax\ncvtsi2sdl %eax, %xmm14\n",
           "double f(int a) { return a; }")
// An unsigned int converts as a 64-bit value.
EXPECT_HAS(UintToFloat, "movl -4(%rbp), %eax\ncvtsi2ssq %rax, %xmm14\n",
           "float f(unsigned a) { return a; }")
EXPECT_HAS(DoubleToLong, "cvttsd2si %xmm14, %rax\n", "long f(double a) { return a; }")
EXPECT_HAS(DoubleToUint, "cvttsd2si %xmm14, %rax\nmovl %eax, ",
           "unsigned f(double a) { return a; }")
EXPECT_HAS(FloatToDouble, "cvtss2sd -4(%rbp), %xmm14\n", "double f(float a) { return a; }")

// FP arguments take xmm0-xmm7 apart from the integer ones; a variadic callee gets
// their number in %al.
TEST_F(X86Test, MixedArguments)
{
    std::string code = Code(CompileToX86(R"(
int printf(const char *fmt, ...);
void f(void) { printf("%d %f %d", 1, 2.5, 3); }
)"));
    EXPECT_NE(std::string::npos, code.find("movl $1, %esi\nmovsd .LC0(%rip), %xmm0\nmovl $3, %edx\n"
                                           "movl $1, %eax\ncall printf\n"))
        << code;
}

TEST_F(X86Test, RunFloatingPoint)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
double scale(double x, float f, int n, double y, double a, double b, double c, double d,
             double e, double g, double h) {
    return x * f + n + y + a + b + c + d + e + g + h;
}
int main(void) {
    double zero = 0.0;
    double nan = zero / zero;
    double big = 1e19;
    unsigned long ubig = 18000000000000000000ul;
    if (scale(2.0, 1.5f, 1, 1, 1, 1, 1, 1, 1, 1, 1) != 12.0)
        return 1;
    if (nan == nan || !(nan != nan) || nan < 1.0 || nan >= 1.0 || !nan)
        return 2;
    if ((unsigned long)big != 10000000000000000000ul || (double)ubig != 1.8e19)
        return 3;
    if ((int)-2.7 != -2 || (unsigned)3e9 != 3000000000u || (float)(unsigned)4000000000u != 4e9f)
        return 4;
    if (-zero != 0.0 || 1.0 / -zero > 0)
        return 5;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}

// Linked with clang's code both ways: FP arguments past the eighth on the stack.
TEST_F(X86Test, RunFloatingPointWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    SKIP_IF_NO_X86_CLANG();
    CompileAndRunWithClang(R"(
double theirs(double a, double b, double c, double d, double e, double k, double g,
              double h, double i, float j, long n);
double ours(double a, double b, double c, double d, double e, double k, double g,
            double h, double i, float j, long n) {
    return a + b + c + d + e + k + g + h + i * 10 + j * 100 + n * 1000;
}
int main(void) {
    return theirs(1, 1, 1, 1, 1, 1, 1, 1, 2, 0.5f, 7) == 7078.0 ? 42 : 1;
})",
                           R"(
double ours(double a, double b, double c, double d, double e, double k, double g,
            double h, double i, float j, long n);
double theirs(double a, double b, double c, double d, double e, double k, double g,
              double h, double i, float j, long n) {
    return ours(a, b, c, d, e, k, g, h, i, j, n);
}
)");
    EXPECT_EQ(42, exit_status);
}
