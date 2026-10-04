//
// x86-64 integer operations: two-operand forms through rax, divides in rdx:rax,
// shifts by %cl, comparisons through setcc.
//
#include "x86_test.h"

// A test of one translation unit: its code contains `expected`.
#define EXPECT_HAS(name, expected, src)                                         \
    TEST_F(X86Test, name)                                                    \
    {                                                                           \
        std::string code = Code(CompileToX86(src));                             \
        EXPECT_NE(std::string::npos, code.find(expected)) << code;              \
    }

// Parameters are stored to slots; the second operand comes straight from memory.
EXPECT_CODE(AddFromMemory, R"(pushq %rbp
movq %rsp, %rbp
subq $16, %rsp
movl %edi, -4(%rbp)
movl %esi, -8(%rbp)
movl -4(%rbp), %eax
addl -8(%rbp), %eax
movl %eax, -12(%rbp)
movl -12(%rbp), %eax
leave
ret
)",
            "int f(int a, int b) { return a + b; }")
EXPECT_HAS(SubImmediate, "movq -8(%rbp), %rax\nsubq $5, %rax\n",
           "long f(long a) { return a - 5; }")
// A 64-bit constant beyond 32 bits is no immediate: it goes through r10.
EXPECT_HAS(WideImmediate, "movabsq $4886718345, %r10\nandq %r10, %rax\n",
           "long f(long a) { return a & 0x123456789L; }")
// A narrow operand is loaded extended by its own type.
EXPECT_HAS(NarrowParameter, "movb %dil, -1(%rbp)\n", "int f(signed char c) { return c; }")
EXPECT_HAS(NarrowOperand, "movzbl -1(%rbp), %eax\n",
           "unsigned f(unsigned char c, unsigned u) { return c | u; }")
// Parameters past the sixth are read from the caller's frame.
EXPECT_HAS(StackParameter, "movl 16(%rbp), %eax\naddl 24(%rbp), %eax\n",
           "int f(int a, int b, int c, int d, int e, int k, int g, int h) { return g + h; }")

EXPECT_HAS(Multiply, "movl -4(%rbp), %eax\nimull -8(%rbp), %eax\n",
           "int f(int a, int b) { return a * b; }")
// An immediate factor takes the three-operand form.
EXPECT_HAS(MultiplyImmediate, "imulq $10, -8(%rbp), %rax\n", "long f(long a) { return a * 10; }")

// Signed: cltd/cqto and idiv; unsigned: rdx cleared and div.  The quotient is in rax,
// the remainder in rdx; an immediate divisor goes through r10.
EXPECT_HAS(Divide, "movl -4(%rbp), %eax\ncltd\nidivl -8(%rbp)\nmovl %eax, ",
           "int f(int a, int b) { return a / b; }")
EXPECT_HAS(RemainderImmediate, "movl $3, %r10d\ncqto\nidivq %r10\nmovq %rdx, ",
           "long f(long a) { return a % 3; }")
EXPECT_HAS(RemainderUnsigned, "xorl %edx, %edx\ndivl -8(%rbp)\nmovl %edx, ",
           "unsigned f(unsigned a, unsigned b) { return a % b; }")

// By an immediate, or by %cl; sar or shr by signedness.
EXPECT_HAS(ShiftArithmetic, "sarl $2, %eax\n", "int f(int a) { return a >> 2; }")
EXPECT_HAS(ShiftByCl, "movl -8(%rbp), %ecx\nshrl %cl, %eax\n",
           "unsigned f(unsigned a, int n) { return a >> n; }")
EXPECT_HAS(ShiftLong, "shlq $40, %rax\n", "long f(long a) { return a << 40; }")

// cmp, setcc and movzbl: signed and unsigned conditions.
EXPECT_HAS(CompareSigned, "movl -4(%rbp), %eax\ncmpl -8(%rbp), %eax\nsetl %al\nmovzbl %al, %eax\n",
           "int f(int a, int b) { return a < b; }")
EXPECT_HAS(CompareUnsigned, "setae %al\n", "int f(unsigned a, unsigned b) { return a >= b; }")
EXPECT_HAS(CompareImmediate, "cmpq $7, %rax\nsetne %al\n", "int f(long a) { return a != 7; }")

EXPECT_HAS(Negate, "negl %eax\n", "int f(int a) { return -a; }")
EXPECT_HAS(Complement, "notq %rax\n", "long f(long a) { return ~a; }")
EXPECT_HAS(Not, "testl %eax, %eax\nsete %al\nmovzbl %al, %eax\n", "int f(int a) { return !a; }")

// Width conversions: sign extension to 64 bits, zero extension a plain movl.
EXPECT_HAS(SignExtendInt, "movslq -4(%rbp), %rax\n", "long f(int a) { return a; }")
EXPECT_HAS(ZeroExtendInt, "movl -4(%rbp), %eax\nmovq %rax, ",
           "unsigned long f(unsigned a) { return a; }")
EXPECT_HAS(SignExtendChar, "movsbq -1(%rbp), %rax\n", "long f(signed char c) { return c; }")
EXPECT_HAS(Truncate, "movq -8(%rbp), %rax\nmovb %al, ", "char f(long a) { return a; }")

// Results checked on the machine: each check adds one, without a branch.
TEST_F(X86Test, RunArithmetic)
{
    SKIP_IF_NO_X86_TOOLS();
    DisableOptimization();
    CompileAndRunX86(R"(
int main(void) {
    int a = -7, b = 2;
    long big = 0x100000000L, n = 3;
    unsigned u = 0xfffffff0u;
    int r = (a / b == -3) + (a % b == -1);
    r = r + (u / 16 == 0x0fffffffu) + (u % 7 == 0xfffffff0u % 7);
    r = r + ((big >> 32) == 1) + ((big * 3) / 3 == big) + ((-big >> 1) == -0x80000000L);
    r = r + ((u >> 28) == 15) + ((a >> 1) == -4) + ((a << 3) == -56);
    r = r + (a < b) + !(u < 1u) + (-1L < 0L) + !(u >= 0xfffffff1u);
    r = r + ((a ^ b) == -5) + ((a & 0xff) == 0xf9) + ((a | 1) == -7) + (~a == 6) + (-a == 7);
    r = r + ((big << n) == 0x800000000L) + ((big - 1) == 0xffffffffL);
    return r;
})");
    EXPECT_EQ(21, exit_status);
}
