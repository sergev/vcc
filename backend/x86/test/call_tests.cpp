//
// x86-64 calls: integer arguments in rdi..r9 and the outgoing area, %al before a
// variadic callee, indirect calls through r11, results in rax.
//
#include "x86_test.h"

EXPECT_CODE(CallTwoArgs, R"(pushq %rbp
movq %rsp, %rbp
subq $16, %rsp
movl $1, %edi
movl $2, %esi
call g
movl %eax, -4(%rbp)
movl -4(%rbp), %eax
leave
ret
)",
            "int g(int a, int b); int f(void) { return g(1, 2); }")

// The seventh and eighth arguments go in the outgoing area at the bottom of the frame.
TEST_F(X86Test, StackArguments)
{
    std::string code = Code(CompileToX86(R"(
long g(long a, long b, long c, long d, long e, long k, long x, long y);
long f(void) { return g(1, 2, 3, 4, 5, 6, 7, 8); }
)"));
    EXPECT_NE(std::string::npos, code.find("subq $32, %rsp\nmovl $7, %eax\nmovq %rax, (%rsp)\n"
                                           "movl $8, %eax\nmovq %rax, 8(%rsp)\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("movl $6, %r9d\ncall g\n")) << code;
}

// A constant argument takes the parameter's type; a narrow one is extended to 32 bits.
// A call needs the frame even without slots: rsp is 16-byte aligned only once rbp is
// pushed.
EXPECT_CODE(NarrowArgument, R"(pushq %rbp
movq %rsp, %rbp
movl $-1, %edi
call g
leave
ret
)",
            "void g(signed char c); void f(void) { g(-1); }")

// A variadic callee gets the number of vector registers used in %al.
TEST_F(X86Test, VariadicCallSetsAl)
{
    std::string code = Code(CompileToX86("int g(int n, ...); int f(void) { return g(1, 2); }"));
    EXPECT_NE(std::string::npos, code.find("movl $2, %esi\nxorl %eax, %eax\ncall g\n")) << code;
}

TEST_F(X86Test, FunctionAddress)
{
    std::string code = Code(CompileToX86("int g(void); void *f(void) { return (void *)g; }"));
    EXPECT_NE(std::string::npos, code.find("leaq g(%rip), %rax\nmovq %rax, -8(%rbp)\n")) << code;
}

TEST_F(X86Test, IndirectCall)
{
    std::string code = Code(CompileToX86("int f(int (*p)(int)) { return p(3); }"));
    EXPECT_NE(std::string::npos, code.find("movl $3, %edi\nmovq -8(%rbp), %r11\ncall *%r11\n"))
        << code;
}

TEST_F(X86Test, RunCalls)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
long sum8(long a, long b, long c, long d, long e, long k, long x, long y) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * k + 7 * x + 8 * y;
}
int narrow(signed char c, unsigned short s) { return c + s; }
int apply(int (*p)(signed char, unsigned short), int v) { return p(v, v); }
int main(void) {
    if (sum8(1, 1, 1, 1, 1, 1, 1, -7) != -28)
        return 1;
    if (narrow(-1, 65535) != 65534)
        return 2;
    if (apply(narrow, 258) != 260)
        return 3;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}

// Linked with clang's code both ways: stack arguments and narrow values.
TEST_F(X86Test, RunCallsWithClang)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunWithClang(R"(
long theirs8(long a, long b, long c, long d, long e, long k, long x, signed char y);
short theirs_narrow(void);
long ours8(long a, long b, long c, long d, long e, long k, long x, signed char y) {
    return a + b + c + d + e + k + x * 100 + y;
}
int ours_narrow(unsigned char c) { return c; }
int main(void) {
    if (theirs8(1, 2, 3, 4, 5, 6, 7, -8) != 1 + 2 + 3 + 4 + 5 + 6 + 700 - 8)
        return 1;
    if (theirs_narrow() != -2)
        return 2;
    return 42;
})",
                           R"(
long ours8(long a, long b, long c, long d, long e, long k, long x, signed char y);
int ours_narrow(unsigned char c);
long theirs8(long a, long b, long c, long d, long e, long k, long x, signed char y) {
    return ours8(a, b, c, d, e, k, x, y);
}
short theirs_narrow(void) { return ours_narrow(254) - 256; }
)");
    EXPECT_EQ(42, exit_status);
}
