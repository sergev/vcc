//
// x86-64 control flow: labels, jumps, and a zero test before a conditional jump.
//
#include "x86_test.h"

EXPECT_CODE(IfElse, R"(pushq %rbp
movq %rsp, %rbp
subq $16, %rsp
movl %edi, -4(%rbp)
movl -4(%rbp), %eax
testl %eax, %eax
je .L0
movl $1, %eax
leave
ret
movl $2, %eax
leave
ret
)",
            "int f(int a) { if (a) return 1; else return 2; }")

// A 64-bit condition is tested at its width; a loop jumps back.
TEST_F(X86Test, WhileLoop)
{
    std::string code = Code(CompileToX86(R"(
long f(long n) {
    long s = 0;
    while (n) {
        s = s + n;
        n = n - 1;
    }
    return s;
})"));
    EXPECT_NE(std::string::npos, code.find("testq %rax, %rax\nje .L")) << code;
    EXPECT_NE(std::string::npos, code.find("jmp .L")) << code;
}

TEST_F(X86Test, RunLoops)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
int main(void) {
    int s = 0;
    for (int i = 0; i < 10; i = i + 1) {
        if (i == 3)
            continue;
        if (i == 8)
            break;
        s = s + i;
    }
    int k = 0;
    do
        k = k + 2;
    while (k < 7);
    return s * 8 + k;   // (0+1+2+4+5+6+7) * 8 + 8
})");
    EXPECT_EQ(208, exit_status);
}
