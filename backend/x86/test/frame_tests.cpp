//
// x86-64 frame: slots below rbp, the prologue and epilogue, large frames.
//
#include "x86_test.h"

// A local in a slot: a constant stored straight into it, then loaded for the return.
TEST_F(X86Test, LocalSlot)
{
    DisableOptimization();
    EXPECT_EQ(R"(pushq %rbp
movq %rsp, %rbp
subq $16, %rsp
movl $5, -4(%rbp)
movl -4(%rbp), %eax
leave
ret
)",
              Code(CompileToX86("int main(void) { int a = 5; return a; }")));
}

// Each width stores as itself and loads extended by its signedness.
TEST_F(X86Test, SlotWidths)
{
    DisableOptimization();
    std::string code = Code(CompileToX86(R"(
long f(void) {
    signed char c = -1; unsigned char u = 255;
    short s = -2; unsigned short us = 2;
    long l = 7; long m = l; c = c; u = u; s = s;
    us = us;
    return m;
})"));
    for (const char *s : { "movb %al, -1(%rbp)", "movsbl -1(%rbp), %eax", "movzbl", "movw %ax",
                           "movswl", "movzwl", "movb $255", "movq $7", "movq %rax" })
        EXPECT_NE(std::string::npos, code.find(s)) << s << " in\n" << code;
}

// A 64-bit constant beyond 32 bits is not an immediate: it goes through a register.
TEST_F(X86Test, WideConstantStore)
{
    DisableOptimization();
    std::string code = Code(CompileToX86("long f(void) { long a = 0x123456789L; return a; }"));
    EXPECT_NE(std::string::npos, code.find("movabsq $4886718345, %rax\nmovq %rax, -8(%rbp)\n"))
        << code;
}

// Displacements take 32 bits: a large frame needs no address arithmetic.
TEST_F(X86Test, LargeFrame)
{
    DisableOptimization();
    std::string src = "long f(void) {\n";
    for (int i = 0; i < 600; i++)
        src += "    long v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    src += "    return v599;\n}\n";
    std::string code = Code(CompileToX86(src.c_str()));
    // 600 locals and the 600 temporaries that carry their initializers.
    EXPECT_NE(std::string::npos, code.find("subq $9600, %rsp\n")) << code;
    EXPECT_NE(std::string::npos, code.find("movq $599, -9600(%rbp)\n")) << code;
    EXPECT_NE(std::string::npos, code.find("movq -9592(%rbp), %rax\nleave\nret\n")) << code;
}

TEST_F(X86Test, RunLocals)
{
    SKIP_IF_NO_X86_TOOLS();
    DisableOptimization();
    CompileAndRunX86(R"(
int main(void) {
    signed char c = -3;
    unsigned short s = 60000;
    long big = 0x123456789L;
    int r = c;
    big = big;
    s = s;
    r = r;
    return r;
})");
    EXPECT_EQ(253, exit_status);
}
