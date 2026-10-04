//
// x86-64 register allocation: values in argument registers unless live across a call
// (or a divide or variable shift, which write rdx or rcx), rbx and r12-r15 pushed and
// popped, no callee-saved xmm, parameters and arguments moved as if at once, the
// two-operand forms, and integers kept in the canonical form of their type.
//
#include "x86_test.h"

// A loop's values stay in argument registers, the pointer and count where they arrive;
// nothing goes through the frame.
TEST_F(X86Test, LoopInRegisters)
{
    std::string code = Code(CompileToX86(R"(
long sum(long *p, int n)
{
    long s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
)"));
    EXPECT_NE(std::string::npos, code.find(R"(movslq %ecx, %rdx
leaq (%rdi,%rdx,8), %rdx
movq (%rdx), %rdx
addq %rdx, %r8
addl $1, %ecx
)")) << code;
    EXPECT_EQ(std::string::npos, code.find("(%rbp)")) << code;
    EXPECT_EQ(std::string::npos, code.find("pushq")) << code;
}

// A value live across a call takes rbx, pushed after rbp into the first slot and
// popped before it; the 16-byte alignment counts the push.
TEST_F(X86Test, CalleeSavedAcrossCall)
{
    std::string code = Code(CompileToX86(R"(
int g(int);
int keep(int a, int b) { int x = g(a); return x + b; }
)"));
    EXPECT_EQ(R"(pushq %rbp
movq %rsp, %rbp
pushq %rbx
subq $8, %rsp
movl %edi, %edi
movl %esi, %ebx
call g
movl %eax, %edi
addl %ebx, %edi
movl %edi, %eax
addq $8, %rsp
popq %rbx
popq %rbp
ret
)",
              code);
}

// Doubles in xmm registers, computed in place, with no frame at all.
TEST_F(X86Test, DoublesInRegisters)
{
    EXPECT_EQ("mulsd %xmm1, %xmm0\naddsd %xmm2, %xmm0\nret\n",
              Code(CompileToX86("double dot(double a, double b, double c) { return a * b + c; }")));
}

// No xmm register is callee-saved: a double live across a call keeps its slot, and the
// one not live across it stays in a register.
TEST_F(X86Test, DoubleAcrossCallInSlot)
{
    std::string code = Code(CompileToX86(R"(
double h(double);
double across(double a, double b) { double x = h(a); return x + b; }
)"));
    EXPECT_EQ(R"(pushq %rbp
movq %rsp, %rbp
subq $16, %rsp
movsd %xmm1, -8(%rbp)
call h
addsd -8(%rbp), %xmm0
leave
ret
)",
              code);
}

// Arguments trading registers: a cycle, broken through rax.
TEST_F(X86Test, ArgumentsMovedAtOnce)
{
    std::string code = Code(CompileToX86(R"(
int g2(int, int);
int swap(int a, int b) { return g2(b, a); }
)"));
    EXPECT_NE(std::string::npos, code.find("movq %rsi, %rax\nmovl %edi, %esi\nmovl %eax, %edi\ncall g2\n"))
        << code;
}

// A narrow parameter is extended again on arrival, and a narrow result computed in a
// register is extended again in place.
TEST_F(X86Test, NarrowValuesCanonical)
{
    std::string code = Code(CompileToX86(R"(
signed char narrow(signed char c, int k) { signed char d = c + k; return d; }
)"));
    EXPECT_EQ(std::string::npos, code.find("(%rbp)")) << code;
    EXPECT_EQ(0u, code.find("movsbl %dil, %edi\n")) << code;
    EXPECT_NE(std::string::npos, code.find("addl %esi, %edi\nmovsbl %dil, %edi\nmovl %edi, %eax\n"))
        << code;
}

// A divisor that arrived in rdx is moved out before cltd overwrites it, and a value
// live across the divide is kept out of the argument registers.
TEST_F(X86Test, DivideAvoidsRdx)
{
    std::string code = Code(CompileToX86("int divc(int a, int b, int c) { return a / c + b; }"));
    EXPECT_NE(std::string::npos, code.find(R"(movl %esi, %ebx
movl %edi, %eax
movl %edx, %r10d
cltd
idivl %r10d
movl %eax, %edi
addl %ebx, %edi
)")) << code;
}

// A value that arrived in rcx is shifted outside it; the count goes in %cl; a value
// live across the shift is kept out of the argument registers.
TEST_F(X86Test, ShiftAvoidsRcx)
{
    std::string code = Code(CompileToX86(R"(
long shx(int a, int b, int c, long x, int n) { return (x << n) + a; }
)"));
    EXPECT_NE(std::string::npos, code.find("movl %edi, %ebx\n")) << code;
    EXPECT_NE(std::string::npos, code.find("movq %rcx, %rsi\nmovl %r8d, %ecx\nshlq %cl, %rsi\n"))
        << code;
}

// d = a - d: the destination is the second operand, so the result is computed in the
// first operand's register, which is dead after.
TEST_F(X86Test, DestinationIsSecondOperand)
{
    EXPECT_EQ("movl %edi, %edi\nmovl %esi, %esi\nsubl %esi, %edi\nmovl %edi, %eax\nret\n",
              Code(CompileToX86("int rsub(int a, int b) { b = a - b; return b; }")));
}

// A parameter on the stack is loaded into its register.
TEST_F(X86Test, StackParameterInRegister)
{
    std::string code = Code(CompileToX86(R"(
long seventh(long a, long b, long c, long d, long e, long f, long g) { return g * a; }
)"));
    EXPECT_NE(std::string::npos, code.find("movq 16(%rbp), %rbx\nimulq %rbx, %rdi\n")) << code;
}

// Every two-operand operator with its operands and destination in every arrangement
// of registers: d = a op b, a = a op b, b = a op b, and a = b op a, against the
// host's results.  The long variant has the divisor or count in rdx, the dividend
// or shifted value in rcx.
TEST_F(X86Test, RunTwoOperandAliasing)
{
    SKIP_IF_NO_X86_TOOLS();
    std::string src = R"(
#include <stdio.h>

#define OPS(X) X(add, +) X(sub, -) X(mul, *) X(and, &) X(or, |) X(xor, ^) X(div, /) \
               X(rem, %) X(shl, <<) X(shr, >>)
#define DEFINE(name, op)                                                 \
    int name##_d(int a, int b) { int d = a op b; return d; }            \
    int name##_a(int a, int b) { a = a op b; return a; }                \
    int name##_b(int a, int b) { b = a op b; return b; }                \
    int name##_r(int a, int b) { a = b op a; return a; }                \
    long name##_l(long z, long y, long b, long a) { return (a op b) + z + y; }
OPS(DEFINE)

int main(void)
{
    int a = 1000, b = 7;
#define PRINT(name, op)                                                            \
    printf("%s %d %d %d %d %ld\n", #name, name##_d(a, b), name##_a(a, b), name##_b(a, b), \
           name##_r(b + 2, 3), name##_l(1, 2, 5, 3000));
    OPS(PRINT)
    return 0;
}
)";
    std::string expected;
    char line[128];
    int a = 1000, b = 7;
#define EXPECT_LINE(name, op)                                                               \
    snprintf(line, sizeof line, "%s %d %d %d %d %ld\n", #name, a op b, a op b, a op b,       \
             3 op (b + 2), (3000L op 5L) + 3);                                              \
    expected += line;
    EXPECT_LINE(add, +)
    EXPECT_LINE(sub, -)
    EXPECT_LINE(mul, *)
    EXPECT_LINE(and, &)
    EXPECT_LINE(or, |)
    EXPECT_LINE(xor, ^)
    EXPECT_LINE(div, /)
    EXPECT_LINE(rem, %)
    EXPECT_LINE(shl, <<)
    EXPECT_LINE(shr, >>)
#undef EXPECT_LINE
    EXPECT_EQ(expected, CompileAndRunX86(src));
    EXPECT_EQ(0, exit_status);
}

// Values live across calls in all five callee-saved registers and in slots past them,
// and doubles across a call in their slots.
TEST_F(X86Test, RunCalleeSaved)
{
    SKIP_IF_NO_X86_TOOLS();
    EXPECT_EQ("28 21.5\n", CompileAndRunX86(R"(
#include <stdio.h>

int id(int x) { return x; }
double half(double x) { return x / 2; }

int main(void)
{
    int a = id(1), b = id(2), c = id(3), d = id(4), e = id(5), f = id(6), g = id(7);
    double x = half(3), y = half(40);
    int s = a + b + c + d + e + f + g;
    printf("%d %g\n", id(s), x + y);
    return 0;
}
)"));
}
