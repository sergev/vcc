//
// x86-64 frame: slots below rbp or from rsp, the red zone, the prologue and epilogue,
// large frames.
//
#include "x86_test.h"

// A local in a slot: a constant stored straight into it, then loaded for the return.
TEST_F(X86Test, LocalSlot)
{
    NaiveSelection();
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
    NaiveSelection();
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
    NaiveSelection();
    DisableOptimization();
    std::string code = Code(CompileToX86("long f(void) { long a = 0x123456789L; return a; }"));
    EXPECT_NE(std::string::npos, code.find("movabsq $4886718345, %rax\nmovq %rax, -8(%rbp)\n"))
        << code;
}

// Displacements take 32 bits: a large frame needs no address arithmetic.
TEST_F(X86Test, LargeFrame)
{
    NaiveSelection();
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

// A leaf keeps its slots in the red zone below rsp, with no prologue at all; the
// array is 16-byte aligned 24 bytes below the return address.
TEST_F(X86Test, LeafRedZone)
{
    x86_peephole     = false;
    std::string code = Code(CompileToX86(R"(
int pick(int i) { int a[4]; a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; return a[i & 3]; }
)"));
    EXPECT_NE(std::string::npos, code.find("leaq -24(%rsp), %rsi\n")) << code;
    EXPECT_EQ(std::string::npos, code.find("subq")) << code;
    EXPECT_EQ(std::string::npos, code.find("%rbp")) << code;
}

// A leaf whose slots do not fit the 128 bytes of the red zone reserves them.
TEST_F(X86Test, LeafPastRedZone)
{
    x86_peephole     = false;
    std::string code = Code(CompileToX86(R"(
int pick(int i) { char a[128]; a[0] = 1; a[i & 127] = 2; return a[0]; }
)"));
    EXPECT_EQ(0u, code.find("subq $136, %rsp\n")) << code;
    EXPECT_NE(std::string::npos, code.find("leaq (%rsp), %rsi\n")) << code;
    EXPECT_NE(std::string::npos, code.find("addq $136, %rsp\nret\n")) << code;
}

// With --frame-pointer, rbp is pushed and set, and slots are addressed from it.
TEST_F(X86Test, FramePointerOption)
{
    x86_peephole      = false;
    x86_frame_pointer = true;
    std::string code  = Code(CompileToX86(R"(
double h(double);
double across(double a, double b) { double x = h(a); return x + b; }
)"));
    EXPECT_EQ(0u, code.find("pushq %rbp\nmovq %rsp, %rbp\nsubq $16, %rsp\nmovsd %xmm1, -8(%rbp)\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("leave\nret\n")) << code;
}

// Without a frame pointer rbp is the sixth callee-saved register: pushed after rbx and
// r12-r15, and rsp kept 16-byte aligned by what is reserved below them.
TEST_F(X86Test, RbpAllocated)
{
    x86_peephole     = false;
    std::string code = Code(CompileToX86(R"(
int g(int);
int six(void)
{
    int a = g(1), b = g(2), c = g(3), d = g(4), e = g(5), f = g(6);
    g(0);
    return a + b + c + d + e + f;
}
)"));
    EXPECT_EQ(0u, code.find("pushq %rbx\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
                            "pushq %rbp\nsubq $8, %rsp\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find("addq $8, %rsp\npopq %rbp\npopq %r15\npopq %r14\n"
                                           "popq %r13\npopq %r12\npopq %rbx\nret\n"))
        << code;
    EXPECT_NE(std::string::npos, code.find(", %ebp\n")) << code;
}

// alloca: the frame from rbp, rsp lowered by the size rounded to 16, and the epilogue
// that finds rsp from rbp (leave, with no register saved).  A leaf is no exception.
TEST_F(X86Test, AllocaLeaf)
{
    EXPECT_EQ(R"(pushq %rbp
movq %rsp, %rbp
movslq %edi, %rax
addq $15, %rax
andq $-16, %rax
subq %rax, %rsp
subl $1, %edi
movslq %edi, %rdi
leaq (%rsp,%rdi,1), %rdi
movb $7, (%rdi)
movsbl (%rdi), %edi
movsbl %dil, %edi
movsbl %dil, %edi
movl %edi, %eax
leave
ret
)",
              Code(CompileToX86(R"(
void *__builtin_alloca(unsigned long);
int f(int n)
{
    char *p = __builtin_alloca(n);
    p[n - 1] = 7;
    return p[n - 1];
}
)")));
}

// The memory starts above the outgoing area, rounded to 16, which the frame reserves
// apart from the slots; the saved registers are found from rbp.
TEST_F(X86Test, AllocaAboveOutgoing)
{
    std::string code = Code(CompileToX86(R"(
void *__builtin_alloca(unsigned long);
long g(long, long, long, long, long, long, long, long);
long f(long n, long k)
{
    long *p = __builtin_alloca(n * sizeof(long));
    p[0] = k;
    return g(1, 2, 3, 4, 5, 6, p[0], k) + p[0];
}
)"));
    EXPECT_EQ(0u, code.find("pushq %rbp\nmovq %rsp, %rbp\npushq %rbx\nsubq $24, %rsp\n")) << code;
    EXPECT_NE(std::string::npos, code.find("subq %rax, %rsp\nleaq 16(%rsp), %rax\n")) << code;
    EXPECT_NE(std::string::npos, code.find("movq %rsi, (%rsp)\nmovq %rsi, 8(%rsp)\n")) << code;
    EXPECT_NE(std::string::npos, code.find("leaq -8(%rbp), %rsp\npopq %rbx\npopq %rbp\nret\n"))
        << code;
}

// In a function that calls alloca rbp is the frame pointer, never allocated.
TEST_F(X86Test, AllocaKeepsRbp)
{
    std::string code = Code(CompileToX86(R"(
void *__builtin_alloca(unsigned long);
int g(int);
int six(int n)
{
    char *p = __builtin_alloca(n);
    int a = g(1), b = g(2), c = g(3), d = g(4), e = g(5), f = g(6);
    p[0] = (char)g(0);
    return a + b + c + d + e + f + p[0];
}
)"));
    EXPECT_EQ(0u, code.find("pushq %rbp\nmovq %rsp, %rbp\n")) << code;
    EXPECT_EQ(std::string::npos, code.find("%ebp")) << code;
    EXPECT_NE(std::string::npos, code.find("popq %rbp\nret\n")) << code;
}

// rsp is 16-byte aligned at every call from a frame addressed from rsp, with 0, 1 or 6
// registers pushed: clang's code finds its 16-byte aligned local aligned.
TEST_F(X86Test, RunRspFrameAlignment)
{
    SKIP_IF_NO_X86_TOOLS();
    SKIP_IF_NO_X86_CLANG();
    std::string ours   = R"(
int misalign(void);
int g(int x) { return x; }

int none(void) { int a[3]; a[0] = misalign(); a[1] = a[0]; a[2] = a[1]; return a[2]; }
int one(int x) { int m = misalign(); return m + g(x) - x; }
int six(void)
{
    int a = g(1), b = g(2), c = g(3), d = g(4), e = g(5), f = g(6);
    return misalign() + a + b + c + d + e + f - 21;
}
int leaf(int i) { int a[8]; for (int k = 0; k < 8; k++) a[k] = k * k; return a[i & 7]; }

int main(void)
{
    return misalign() * 1000 + none() * 100 + one(5) * 10 + six() + (leaf(3) != 9);
}
)";
    std::string theirs = R"(
int misalign(void)
{
    volatile long x __attribute__((aligned(16))) = 0;
    unsigned long a = (unsigned long)&x;
    __asm__ volatile("" : "+r"(a)); // so that clang cannot assume the alignment
    return (int)(a & 15) + (int)x;
}
)";
    CompileAndRunWithClang(ours, theirs);
    EXPECT_EQ(0, exit_status);
}
