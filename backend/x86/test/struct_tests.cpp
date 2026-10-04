//
// x86-64 structs: member access at offsets, whole copies through r11 (a loop past 64
// bytes), arguments whole on the stack and results through the address in rdi.
//
#include "x86_test.h"

#define EXPECT_HAS(name, expected, src)                                    \
    TEST_F(X86Test, name)                                                  \
    {                                                                      \
        std::string code = Code(CompileToX86(src));                        \
        EXPECT_NE(std::string::npos, code.find(expected)) << code;         \
    }

// A whole copy (here of an argument) moves the widest pieces the alignment allows,
// padding included.
EXPECT_HAS(CopyPieces, "movq -16(%rbp), %r11\nmovq %r11, (%rsp)\nmovq -8(%rbp), %r11\n"
                       "movq %r11, 8(%rsp)\nmovl $1, %edi\ncall g\n",
           "struct s { long a; int b; }; int g(int n, struct s x);"
           "int f(void) { struct s x = { 1, 2 }; return g(1, x); }")
EXPECT_HAS(CopyBytes, "movb -3(%rbp), %r11b\nmovb %r11b, (%rsp)\nmovb -2(%rbp), %r11b\n"
                      "movb %r11b, 1(%rsp)\nmovb -1(%rbp), %r11b\nmovb %r11b, 2(%rsp)\n",
           "struct s { char c[3]; }; int g(struct s x);"
           "int f(void) { struct s x = { \"ab\" }; return g(x); }")

// Past 64 bytes a loop of quadwords, then the rest.
EXPECT_HAS(CopyLoop, "leaq (%rsp), %rax\nleaq -84(%rbp), %r10\nmovl $9, %ecx\n"
                     "movq (%r10), %r11\nmovq %r11, (%rax)\naddq $8, %r10\naddq $8, %rax\n"
                     "subq $1, %rcx\njne .Lx0\nmovl (%r10), %r11d\nmovl %r11d, (%rax)\n"
                     "call g\n",
           "struct s { int a[19]; }; int g(struct s x); int f(struct s *p) { return g(*p); }")

// A member is a load or store at its offset in the slot.
EXPECT_HAS(MemberAccess, "movl $5, -8(%rbp)\nmovl -8(%rbp), %eax\n",
           "struct s { long a; int b; }; int f(void) { struct s x; x.b = 5; return x.b; }")

// The result is written through the address that came in rdi, which goes back in rax;
// the parameters start at rsi.
EXPECT_CODE(StructResultCallee, R"(pushq %rbp
movq %rsp, %rbp
subq $32, %rsp
movq %rdi, -8(%rbp)
movq %rsi, -16(%rbp)
movq -16(%rbp), %rax
movq %rax, -32(%rbp)
movq $2, -24(%rbp)
movq -8(%rbp), %r10
movq -32(%rbp), %r11
movq %r11, (%r10)
movq -24(%rbp), %r11
movq %r11, 8(%r10)
movq -8(%rbp), %rax
leave
ret
)",
            "struct s { long a, b; }; struct s f(long a) { struct s x = { a, 2 }; return x; }")
// The caller passes the destination's address.
EXPECT_HAS(StructResultCaller, "movl $7, %esi\nleaq -32(%rbp), %rdi\ncall g\n",
           "struct s { long a, b; }; struct s g(long a);"
           "long f(void) { struct s x = g(7); return x.b; }")

TEST_F(X86Test, RunStructs)
{
    SKIP_IF_NO_X86_TOOLS();
    CompileAndRunX86(R"(
struct small { char c; short s; };
struct big { long v[10]; double d; };
struct big make(long base) {
    struct big b;
    for (int i = 0; i < 10; i++)
        b.v[i] = base + i;
    b.d = 0.5;
    return b;
}
long sum(struct big b, struct small s, int k) {
    long t = 0;
    for (int i = 0; i < 10; i++)
        t += b.v[i];
    return t + s.c + s.s + k + (long)(b.d * 2);
}
int main(void) {
    struct big b = make(100);
    struct big copy = b;
    struct small s = { -3, 300 };
    struct big *p = &copy;
    p->v[9] = 1000;
    if (b.v[9] != 109 || copy.v[9] != 1000 || copy.v[0] != 100)
        return 1;
    if (sum(b, s, 5) != 1045 + 297 + 5 + 1)
        return 2;
    return 42;
})");
    EXPECT_EQ(42, exit_status);
}
